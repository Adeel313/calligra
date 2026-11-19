/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "PptExport.h"
#include "PptExportTemplate.h"

#include <KoDocument.h>
#include <KoFilterChain.h>
#include <KoFlake.h>
#include <KoImageData.h>
#include <KoPADocument.h>
#include <KoPAPageBase.h>
#include <KoShape.h>
#include <KoShapeContainer.h>
#include <KoShapeGroup.h>
#include <KoShapeLayer.h>
#include <KoShapePainter.h>
#include <KoTextShapeDataBase.h>
#include <styles/KoCharacterStyle.h>

#include <KPluginFactory>

#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <QFile>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPair>
#include <QRectF>
#include <QSaveFile>
#include <QString>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFormat>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <cstring>

K_PLUGIN_FACTORY_WITH_JSON(PptExportFactory, "calligra_filter_odp2ppt.json", registerPlugin<PptExport>();)

namespace {

// ---------------------------------------------------------------------------
// Binary record tree
// ---------------------------------------------------------------------------

struct Record {
    quint8 ver = 0;
    quint16 inst = 0;
    quint16 type = 0;
    QByteArray payload;
    QList<Record> children;

    bool isContainer() const { return ver == 0x0F; }

    QByteArray serialize() const
    {
        QByteArray body = isContainer() ? serializeList(children) : payload;
        QByteArray out;
        out.reserve(8 + body.size());
        const quint16 vi = quint16((inst << 4) | (ver & 0x0F));
        appendU16(out, vi);
        appendU16(out, type);
        appendU32(out, quint32(body.size()));
        out += body;
        return out;
    }

    static QByteArray serializeList(const QList<Record> &list)
    {
        QByteArray out;
        for (const Record &r : list) out += r.serialize();
        return out;
    }

    static void appendU16(QByteArray &b, quint16 v)
    {
        b.append(char(v & 0xFF));
        b.append(char((v >> 8) & 0xFF));
    }
    static void appendU32(QByteArray &b, quint32 v)
    {
        for (int i = 0; i < 4; ++i) b.append(char((v >> (8 * i)) & 0xFF));
    }
};

static quint16 readU16(const QByteArray &b, int p)
{
    return quint16(quint8(b[p])) | (quint16(quint8(b[p + 1])) << 8);
}
static quint32 readU32(const QByteArray &b, int p)
{
    quint32 v = 0;
    for (int i = 0; i < 4; ++i) v |= quint32(quint8(b[p + i])) << (8 * i);
    return v;
}
static void writeU16(QByteArray &b, int p, quint16 v)
{
    b[p] = char(v & 0xFF);
    b[p + 1] = char((v >> 8) & 0xFF);
}
static void writeU32(QByteArray &b, int p, quint32 v)
{
    for (int i = 0; i < 4; ++i) b[p + i] = char((v >> (8 * i)) & 0xFF);
}
static void appendU16(QByteArray &b, quint16 v)
{
    Record::appendU16(b, v);
}
static void appendU32(QByteArray &b, quint32 v)
{
    Record::appendU32(b, v);
}

static QList<Record> parseRecords(const QByteArray &data, int start, int end)
{
    QList<Record> out;
    int p = start;
    while (p + 8 <= end) {
        const quint16 vi = readU16(data, p);
        const quint16 type = readU16(data, p + 2);
        const quint32 len = readU32(data, p + 4);
        if (p + 8 + qint64(len) > end) break;
        Record r;
        r.ver = quint8(vi & 0x0F);
        r.inst = quint16((vi >> 4) & 0x0FFF);
        r.type = type;
        r.payload = data.mid(p + 8, int(len));
        if (r.isContainer()) r.children = parseRecords(r.payload, 0, r.payload.size());
        out.append(r);
        p += 8 + int(len);
    }
    return out;
}

static Record *findRecord(QList<Record> &list, quint16 type, int inst = -1)
{
    for (Record &r : list) {
        if (r.type == type && (inst < 0 || int(r.inst) == inst)) return &r;
        if (Record *found = findRecord(r.children, type, inst)) return found;
    }
    return nullptr;
}

static QList<Record *> findAllRecords(QList<Record> &list, quint16 type)
{
    QList<Record *> out;
    for (Record &r : list) {
        if (r.type == type) out.append(&r);
        out += findAllRecords(r.children, type);
    }
    return out;
}

static QList<Record *> findAllRecords(Record &record, quint16 type)
{
    QList<Record *> out;
    if (record.type == type) out.append(&record);
    out += findAllRecords(record.children, type);
    return out;
}

// ---------------------------------------------------------------------------
// CFB (OLE2 compound file) writer
// ---------------------------------------------------------------------------

struct CfbStream {
    QString name;
    QByteArray data;
};

static QByteArray buildCfb(const QList<CfbStream> &streams)
{
    constexpr quint32 FREESECT = 0xFFFFFFFFu;
    constexpr quint32 ENDOFCHAIN = 0xFFFFFFFEu;
    constexpr quint32 FATSECT = 0xFFFFFFFDu;
    constexpr quint32 NOSTREAM = 0xFFFFFFFFu;
    constexpr int SECTOR = 512;
    constexpr int MINI = 64;
    constexpr int CUTOFF = 4096;

    // classify streams as mini or regular
    QList<int> miniIdx, regularIdx;
    for (int i = 0; i < streams.size(); ++i) {
        if (streams[i].data.size() < CUTOFF) miniIdx.append(i);
        else regularIdx.append(i);
    }

    // build mini stream data and mini FAT
    QByteArray miniData;
    QHash<int, QList<quint32>> miniChains;
    QList<quint32> miniFat;
    for (int idx : miniIdx) {
        const QByteArray &d = streams[idx].data;
        const int sectors = qMax(1, (d.size() + MINI - 1) / MINI);
        const quint32 start = quint32(miniData.size() / MINI);
        miniChains[idx] = QList<quint32>();
        for (int s = 0; s < sectors; ++s) miniChains[idx].append(start + s);
        miniData += d;
        miniData += QByteArray(sectors * MINI - d.size(), '\0');
    }
    for (int idx : miniIdx) {
        const QList<quint32> &chain = miniChains[idx];
        for (int i = 0; i < chain.size(); ++i) {
            miniFat.append(i < chain.size() - 1 ? chain[i + 1] : ENDOFCHAIN);
        }
    }

    // Reserve enough leading sectors for the FAT.  Each FAT sector holds
    // SECTOR/4 entries; we must reserve them before allocating any data
    // sectors, otherwise writing the FAT would overwrite stream data.
    int reserve = 1; // at least one FAT sector
    for (const CfbStream &s : streams) reserve += qMax(1, (s.data.size() + SECTOR - 1) / SECTOR);
    // mini stream, mini FAT and directory are small; allow generous headroom
    reserve += 16;
    const int nFat = (reserve + SECTOR / 4 - 1) / (SECTOR / 4);

    QList<quint32> fat;
    QList<QByteArray> sectors;
    for (int i = 0; i < nFat; ++i) {
        fat.append(FATSECT);
        sectors.append(QByteArray(SECTOR, '\0'));
    }

    auto alloc = [&](const QByteArray &data) -> QList<quint32> {
        const int n = qMax(1, (data.size() + SECTOR - 1) / SECTOR);
        const quint32 start = quint32(sectors.size());
        for (int i = 0; i < n; ++i) {
            const int off = i * SECTOR;
            sectors.append(data.mid(off, SECTOR).leftJustified(SECTOR, '\0'));
            fat.append(i < n - 1 ? start + i + 1 : ENDOFCHAIN);
        }
        return QList<quint32>() << start; // caller uses first; chain is contiguous
    };

    // mini stream (root) chain
    quint32 miniStart = ENDOFCHAIN;
    if (!miniData.isEmpty()) miniStart = alloc(miniData).first();

    // regular streams
    QHash<int, quint32> regStart;
    for (int idx : regularIdx) regStart[idx] = alloc(streams[idx].data).first();

    // mini FAT sectors
    QByteArray mfb;
    for (quint32 v : miniFat) appendU32(mfb, v);
    const int nMiniFat = qMax(1, (mfb.size() + SECTOR - 1) / SECTOR);
    const quint32 miniFatStart = quint32(sectors.size());
    QList<quint32> miniFatChain;
    for (int i = 0; i < nMiniFat; ++i) {
        miniFatChain.append(miniFatStart + i);
        sectors.append(QByteArray(SECTOR, '\xff'));
        fat.append(i < nMiniFat - 1 ? miniFatStart + i + 1 : ENDOFCHAIN);
    }
    mfb = mfb.leftJustified(nMiniFat * SECTOR, '\xff');
    for (int i = 0; i < nMiniFat; ++i) sectors[miniFatStart + i] = mfb.mid(i * SECTOR, SECTOR);

    // directory entries
    QList<QByteArray> dirEntries;

    auto makeDirEntry = [&](const QString &name, quint8 type, quint32 start, quint32 size) {
        QByteArray e(128, '\0');
        const QByteArray nb(reinterpret_cast<const char *>(name.utf16()), name.size() * 2);
        std::memcpy(e.data(), nb.constData(), qMin(nb.size(), 64));
        writeU16(e, 64, quint16(nb.size() + 2));
        e[66] = type;
        writeU32(e, 68, NOSTREAM);
        writeU32(e, 72, NOSTREAM);
        writeU32(e, 76, NOSTREAM);
        writeU32(e, 116, start);
        writeU32(e, 120, size);
        return e;
    };

    QByteArray root = makeDirEntry(QStringLiteral("Root Entry"), 5, miniStart, quint32(miniData.size()));
    dirEntries.append(root);

    for (int i = 0; i < streams.size(); ++i) {
        const quint32 start = miniIdx.contains(i) ? miniChains[i].first() : regStart.value(i, ENDOFCHAIN);
        dirEntries.append(makeDirEntry(streams[i].name, 2, start, quint32(streams[i].data.size())));
    }

    // sibling links (simple linked list under root)
    if (!streams.isEmpty()) writeU32(dirEntries[0], 76, 1);
    for (int i = 0; i < streams.size(); ++i) {
        if (i + 1 < streams.size()) writeU32(dirEntries[i + 1], 72, i + 2);
    }

    QByteArray db;
    for (const QByteArray &e : dirEntries) db += e;
    const int nDir = qMax(1, (db.size() + SECTOR - 1) / SECTOR);
    db = db.leftJustified(nDir * SECTOR, '\0');
    const quint32 dirStart = quint32(sectors.size());
    for (int i = 0; i < nDir; ++i) {
        sectors.append(db.mid(i * SECTOR, SECTOR));
        fat.append(i < nDir - 1 ? dirStart + i + 1 : ENDOFCHAIN);
    }

    // Write FAT data into the reserved leading sectors.  The FAT entries for
    // those sectors were already marked FATSECT above.  Note that the FAT may
    // have grown while allocating data sectors, so guard against overflow.
    while (fat.size() < nFat * (SECTOR / 4)) fat.append(FREESECT);
    for (int i = 0; i < nFat; ++i) {
        QByteArray fs;
        for (int j = 0; j < SECTOR / 4; ++j) appendU32(fs, fat[i * (SECTOR / 4) + j]);
        sectors[i] = fs;
    }

    // header
    QByteArray h(SECTOR, '\0');
    const unsigned char sig[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    std::memcpy(h.data(), sig, 8);
    writeU16(h, 24, 0x003E);
    writeU16(h, 26, 3);
    writeU16(h, 28, 0xFFFE);
    writeU16(h, 30, 9);
    writeU16(h, 32, 6);
    writeU32(h, 40, 0);
    writeU32(h, 44, quint32(nFat));
    writeU32(h, 48, dirStart);
    writeU32(h, 56, CUTOFF);
    writeU32(h, 60, miniFatStart);
    writeU32(h, 64, quint32(nMiniFat));
    writeU32(h, 68, ENDOFCHAIN);
    writeU32(h, 72, 0);
    for (int i = 0; i < 109; ++i) writeU32(h, 76 + i * 4, i < nFat ? quint32(i) : FREESECT);

    QByteArray out = h;
    for (const QByteArray &s : sectors) out += s;
    return out;
}

// ---------------------------------------------------------------------------
// Slide content extraction
// ---------------------------------------------------------------------------

// Character formatting for one run of text.  Each field is only written to
// the binary CF exception when it is actually present, so unset properties
// fall back to the master/default style on import.
struct CharFmt {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool hasSize = false;
    quint16 size = 0;             // points, 1..4000
    bool hasColor = false;
    quint8 red = 0, green = 0, blue = 0;
    bool hasFont = false;
    QString fontFamily;

    bool operator==(const CharFmt &o) const
    {
        return bold == o.bold && italic == o.italic && underline == o.underline
               && hasSize == o.hasSize && size == o.size
               && hasColor == o.hasColor && red == o.red && green == o.green && blue == o.blue
               && hasFont == o.hasFont && fontFamily == o.fontFamily;
    }
};

// Paragraph formatting.  The alignment values match the legacy PPT
// TextAlignment enum: 0 left, 1 centre, 2 right, 3 justify.
struct ParaFmt {
    int alignment = -1;           // -1 = not set
    int start = 0;                // index into SlideText::text
    int length = 0;               // including trailing CR, if any
};

struct TextSpan {
    int start = 0;                // index into SlideText::text (UTF-16 units)
    int length = 0;
    CharFmt fmt;
};

// A text shape together with the bounding box of the shape that owns it,
// in points (1/72 inch).  PowerPoint's binary format uses master units
// (1/576 inch), so positions are multiplied by 8 when written.
struct SlideText {
    QString text;                 // plain text, paragraphs separated by CR
    QVector<TextSpan> spans;      // character-format runs covering the text
    QVector<ParaFmt> paras;       // one entry per paragraph
    QRectF rect;
};

struct SlideImage {
    QByteArray data;      // PNG or JPEG bytes
    QString suffix;
    QByteArray rgbUid;    // 16-byte identifier shared with the FBSE in the Dgg
    QRectF rect;          // position in points
    int pib = 0;          // 1-based picture index, assigned by registerImages()
};

struct SlideInfo {
    QList<SlideText> texts;
    QList<SlideImage> images;
};

// Images shared by all slides, indexed starting at pib=1.
struct ExportedImage {
    QByteArray data;
    QString suffix;
    QByteArray rgbUid;
};

// Render an arbitrary shape into a PNG so visual content that has no native
// binary-PPT representation (charts, tables, autoshapes, paths, connectors,
// SVG/WMF images, ...) survives the round-trip as a picture instead of being
// dropped entirely.  Rasterising is lossy for editability, but faithful to the
// rendered appearance, which is the priority for a legacy binary format.
static QByteArray rasterizeShape(KoShape *shape)
{
    if (!shape) return QByteArray();
    const QSizeF shapeSize = shape->size();
    if (shapeSize.isEmpty()) return QByteArray();
    const QSize pixelSize(qBound(1, qRound(shapeSize.width() * 2.0), 8192),
                          qBound(1, qRound(shapeSize.height() * 2.0), 8192));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    KoShapePainter painter;
    painter.setShapes({shape});
    painter.paint(image);
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return QByteArray();
    return data;
}

static SlideText extractSlideText(QTextDocument *doc, const QRectF &rect);

static void addImageShape(KoShape *shape, const QByteArray &data, const QString &suffix,
                          QList<SlideImage> &images)
{
    if (data.isEmpty()) return;
    SlideImage img;
    img.data = data;
    img.suffix = suffix;
    img.rgbUid = QCryptographicHash::hash(data, QCryptographicHash::Md5).left(16);
    const QPointF pos = shape->absolutePosition(KoFlake::TopLeftCorner);
    // Rasterised shapes are rendered from their local origin; use their own
    // size/position rather than the (possibly larger) boundingRect.
    img.rect = QRectF(pos, shape->size());
    images.append(img);
}

static void collectShapeContent(KoShape *shape, QList<SlideText> &texts, QList<SlideImage> &images)
{
    if (!shape) return;

    // Native picture payload.
    bool handled = false;
    if (auto *imageData = qobject_cast<KoImageData *>(shape->userData())) {
        QByteArray data;
        QBuffer buffer(&data);
        QString suffix;
        if (buffer.open(QIODevice::WriteOnly) && imageData->saveData(buffer))
            suffix = imageData->suffix().toLower();
        if (!data.isEmpty() && (suffix == QLatin1String("png") || suffix == QLatin1String("jpg")
                                || suffix == QLatin1String("jpeg"))) {
            addImageShape(shape, data,
                          suffix == QLatin1String("png") ? QStringLiteral("png") : QStringLiteral("jpeg"),
                          images);
            handled = true;
        }
        // Unsupported image formats (SVG, WMF, ...) fall through to rasterise.
    }

    // Native text payload.  A text shape with no characters (e.g. an empty
    // placeholder inside a picture frame) contributes nothing and must not be
    // rasterised into a phantom image, so it is handled either way.
    if (auto *data = qobject_cast<KoTextShapeDataBase *>(shape->userData())) {
        handled = true;
        if (QTextDocument *doc = data->document()) {
            const SlideText st = extractSlideText(doc,
                QRectF(shape->absolutePosition(KoFlake::TopLeftCorner), shape->size()));
            if (!st.text.isEmpty()) texts.append(st);
        }
    }

    // Recurse into every container, not just layers/groups.  draw:custom-shape
    // loads as a KoTosContainer (e.g. KoPathShape) whose text lives in a child
    // text shape; skipping these children would silently drop all that text.
    const int textsBefore = texts.size();
    const int imagesBefore = images.size();
    if (auto *container = dynamic_cast<KoShapeContainer *>(shape)) {
        for (KoShape *child : container->shapes())
            collectShapeContent(child, texts, images);
    }
    const bool childContent = texts.size() > textsBefore || images.size() > imagesBefore;

    // Pure organisational containers have no visual representation of their
    // own and must never be rasterised, even when empty.
    if (dynamic_cast<KoShapeLayer *>(shape) || dynamic_cast<KoShapeGroup *>(shape)) return;

    if (handled || childContent) return;

    // A leaf shape with no native representation (chart, table, autoshape,
    // connector, ...) has no editable equivalent in binary PPT.  Rasterise it
    // as a picture so its visual content survives the round-trip.
    addImageShape(shape, rasterizeShape(shape), QStringLiteral("png"), images);
}

static SlideInfo pageContent(KoPAPageBase *page)
{
    SlideInfo info;
    for (KoShape *shape : page->shapes()) collectShapeContent(shape, info.texts, info.images);
    if (info.texts.isEmpty()) {
        SlideText empty;
        empty.rect = QRectF(0, 0, 100, 20);
        info.texts.append(empty);
    }
    return info;
}

static QByteArray toUtf16(const QString &s)
{
    // PowerPoint separates paragraphs with CR (0x0D) or VT (0x0B); the
    // importer splits on those, not on LF.  QTextDocument::toPlainText()
    // uses LF, so translate it here.
    QByteArray out;
    for (int i = 0; i < s.size(); ++i) {
        const ushort ch = s.at(i).unicode();
        const ushort c = (ch == 0x0A) ? 0x0D : ch;
        out.append(char(c & 0xFF));
        out.append(char((c >> 8) & 0xFF));
    }
    return out;
}

static CharFmt charFormatFrom(const QTextCharFormat &fmt, const QFont &defaultFont)
{
    CharFmt cf;
    cf.bold = fmt.fontWeight() >= QFont::DemiBold;
    cf.italic = fmt.fontItalic();
    // ODF keeps underline in Calligra's character properties; applyStyle()
    // does not always lift it into QTextCharFormat::fontUnderline().
    cf.underline = fmt.fontUnderline()
        || (fmt.hasProperty(KoCharacterStyle::UnderlineStyle)
            && fmt.intProperty(KoCharacterStyle::UnderlineStyle) != KoCharacterStyle::NoLineStyle);
    const qreal pt = fmt.fontPointSize() > 0 ? fmt.fontPointSize() : defaultFont.pointSizeF();
    if (pt > 0 && pt <= 4000) {
        cf.hasSize = true;
        cf.size = quint16(qRound(pt));
    }
    const QStringList families = fmt.fontFamilies().toStringList();
    if (!families.isEmpty() && !families.first().isEmpty()) {
        cf.hasFont = true;
        cf.fontFamily = families.first();
    } else if (!defaultFont.families().isEmpty() && !defaultFont.families().first().isEmpty()) {
        cf.hasFont = true;
        cf.fontFamily = defaultFont.families().first();
    }
    // A direct RGB colour is emitted with ColorIndexStruct::index = 0xFE,
    // which the legacy importer reads back verbatim.
    const QColor c = fmt.foreground().color();
    if (c.isValid()) {
        cf.hasColor = true;
        cf.red = quint8(c.red());
        cf.green = quint8(c.green());
        cf.blue = quint8(c.blue());
    }
    return cf;
}

static int alignmentFrom(const QTextBlockFormat &fmt)
{
    // Qt6's AlignHorizontal_Mask also includes AlignAbsolute (0x10); strip it
    // so "right" (AlignRight | AlignAbsolute) maps to the legacy value 2.
    const int a = int(fmt.alignment()) & (Qt::AlignLeft | Qt::AlignRight
                                           | Qt::AlignHCenter | Qt::AlignJustify);
    switch (a) {
    case Qt::AlignHCenter: return 1;
    case Qt::AlignRight:   return 2;
    case Qt::AlignJustify: return 3;
    case Qt::AlignLeft:    return 0;
    default:               return -1;
    }
}

// Extract plain text plus per-run/per-paragraph formatting from a text shape.
static SlideText extractSlideText(QTextDocument *doc, const QRectF &rect)
{
    SlideText st;
    st.rect = rect;
    if (!doc) return st;

    // Build the untrimmed plain text (blocks separated by '\n', as
    // QTextDocument::toPlainText() does) while recording fragment spans and
    // paragraph starts in that same coordinate space.
    QString raw;
    QVector<TextSpan> rawSpans;
    QVector<ParaFmt> rawParas;
    QVector<int> rawParaStarts;
    const QFont defaultFont = doc->defaultFont();

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (!raw.isEmpty()) raw += QLatin1Char('\n');
        rawParaStarts.append(raw.size());
        ParaFmt pf;
        pf.alignment = alignmentFrom(block.blockFormat());
        rawParas.append(pf);
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            const QString ft = frag.text();
            if (ft.isEmpty()) continue;
            QTextCharFormat fmt = block.charFormat();
            fmt.merge(frag.charFormat());
            TextSpan span;
            span.start = raw.size();
            span.length = ft.size();
            span.fmt = charFormatFrom(fmt, defaultFont);
            rawSpans.append(span);
            raw += ft;
        }
    }

    // Trim leading/trailing whitespace, matching the previous
    // toPlainText().trimmed() behaviour, then switch paragraph separators
    // from LF to CR (the importer splits on [\v\r]).
    int first = 0;
    while (first < raw.size() && raw.at(first).isSpace()) ++first;
    int last = raw.size();
    while (last > first && raw.at(last - 1).isSpace()) --last;
    st.text = raw.mid(first, last - first);
    for (int i = 0; i < st.text.size(); ++i) {
        if (st.text.at(i) == QLatin1Char('\n')) st.text[i] = QLatin1Char('\r');
    }

    // Clip spans to the trimmed text.
    for (const TextSpan &rs : rawSpans) {
        const int s = qMax(rs.start, first);
        const int e = qMin(rs.start + rs.length, last);
        if (e <= s) continue;
        TextSpan ts = rs;
        ts.start = s - first;
        ts.length = e - s;
        st.spans.append(ts);
    }
    // Merge adjacent spans with identical formatting.
    QVector<TextSpan> merged;
    for (const TextSpan &ts : st.spans) {
        if (!merged.isEmpty() && merged.last().start + merged.last().length == ts.start
            && merged.last().fmt == ts.fmt) {
            merged.last().length += ts.length;
        } else {
            merged.append(ts);
        }
    }
    st.spans = merged;

    // Recompute paragraph boundaries in the trimmed text and attach the
    // matching alignment from the original block.
    int p = 0;
    for (int i = 0; i < st.text.size();) {
        int j = i;
        while (j < st.text.size() && st.text.at(j) != QLatin1Char('\r')) ++j;
        if (j < st.text.size()) ++j; // include the separator in this run
        const int rawIdx = i + first;
        while (p + 1 < rawParaStarts.size() && rawParaStarts[p + 1] <= rawIdx) ++p;
        ParaFmt pf = rawParas.value(p);
        pf.start = i;
        pf.length = j - i;
        st.paras.append(pf);
        i = j;
    }
    if (st.paras.isEmpty()) {
        ParaFmt pf;
        pf.start = 0;
        pf.length = st.text.size();
        st.paras.append(pf);
    }
    return st;
}

// ---------------------------------------------------------------------------
// Picture helpers
// ---------------------------------------------------------------------------

// Register all images used by the slides.  Returns the global image list;
// each SlideImage's rgbUid is looked up to assign a pib (1-based index).
static QList<ExportedImage> registerImages(QList<SlideInfo> &slides)
{
    QList<ExportedImage> images;
    for (SlideInfo &slide : slides) {
        for (SlideImage &img : slide.images) {
            int idx = -1;
            for (int i = 0; i < images.size(); ++i) {
                if (images[i].rgbUid == img.rgbUid) { idx = i; break; }
            }
            if (idx < 0) {
                ExportedImage ei;
                ei.data = img.data;
                ei.suffix = img.suffix;
                ei.rgbUid = img.rgbUid;
                images.append(ei);
                idx = images.size() - 1;
            }
            img.pib = idx + 1; // pib is 1-based
        }
    }
    return images;
}

// Build an OfficeArtFBSE record for the Dgg's BStoreContainer.
static Record buildFbse(const ExportedImage &img, quint32 offset)
{
    Record fbse;
    fbse.ver = 2;
    fbse.inst = (img.suffix == QLatin1String("png")) ? 5 : 4;
    fbse.type = 0xF007;
    QByteArray p;
    const quint8 blipType = (img.suffix == QLatin1String("png")) ? 5 : 4;
    p.append(char(blipType)); // btWin32
    p.append(char(blipType)); // btMacOS
    p += img.rgbUid;           // rgbUid (16 bytes)
    Record::appendU16(p, 0);   // tag
    Record::appendU32(p, quint32(img.data.size())); // size
    Record::appendU32(p, 1);   // cRef
    Record::appendU32(p, offset); // foDelay = offset in Pictures stream
    p.append(char(0)); // unused1
    p.append(char(0)); // cbName
    p.append(char(0)); // unused2
    p.append(char(0)); // unused3
    fbse.payload = p;
    return fbse;
}

// Create a picture shape (msosptPictureFrame = 0x4B) for an image.
static Record buildPictureShape(const SlideImage &img)
{
    Record sp;
    sp.ver = 0x0F;
    sp.inst = 0;
    sp.type = 0xF004;

    Record fsp;
    fsp.ver = 2;
    fsp.inst = 0x4B;
    fsp.type = 0xF00A;
    fsp.payload = QByteArray(8, '\0'); // spid + flags
    sp.children.append(fsp);

    // FOPT with the pib property (opid 0x0104, encoded in 14+2 bits).
    Record fopt;
    fopt.ver = 3;
    fopt.inst = 1;
    fopt.type = 0xF00B;
    QByteArray foptData;
    foptData.append(char(0x04));                     // opid low byte
    foptData.append(char(0x01));                     // opid high 6 bits, fBid=0, fComplex=0
    Record::appendU32(foptData, quint32(img.pib));   // pib value
    fopt.payload = foptData;
    sp.children.append(fopt);

    // Anchor with the image position (master units = points * 8).
    Record anchor;
    anchor.ver = 0;
    anchor.inst = 0;
    anchor.type = 0xF010;
    QByteArray anchorData;
    const qreal s = 8.0;
    Record::appendU16(anchorData, quint16(img.rect.top() * s));
    Record::appendU16(anchorData, quint16(img.rect.left() * s));
    Record::appendU16(anchorData, quint16(img.rect.right() * s));
    Record::appendU16(anchorData, quint16(img.rect.bottom() * s));
    anchor.payload = anchorData;
    sp.children.append(anchor);

    return sp;
}

// ---------------------------------------------------------------------------
// Document stream construction
// ---------------------------------------------------------------------------

// Build a TextPFException: a 32-bit PFMasks word followed by only the fields
// whose mask bit is set, in the order the legacy parser expects.
static QByteArray buildPFException(const ParaFmt &pf)
{
    quint32 masks = 0;
    if (pf.alignment >= 0 && pf.alignment <= 3) masks |= (1u << 11); // align
    QByteArray out;
    appendU32(out, masks);
    if (masks & (1u << 11)) appendU16(out, quint16(pf.alignment));
    return out;
}

// Build a TextCFException: CFMasks, optional CFStyle, font size and a direct
// RGB colour (ColorIndexStruct::index = 0xFE).
static QByteArray buildCFException(const CharFmt &cf, const QHash<QString, quint16> &fonts)
{
    const bool style = cf.bold || cf.italic || cf.underline;
    const bool font = cf.hasFont && fonts.contains(cf.fontFamily);
    quint32 masks = 0;
    if (cf.bold)      masks |= (1u << 0);
    if (cf.italic)    masks |= (1u << 1);
    if (cf.underline) masks |= (1u << 2);
    if (font)         masks |= (1u << 16); // typeface
    if (cf.hasSize)   masks |= (1u << 17);
    if (cf.hasColor)  masks |= (1u << 18);
    QByteArray out;
    appendU32(out, masks);
    if (style) {
        quint16 st = 0;
        if (cf.bold)      st |= 1;
        if (cf.italic)    st |= 2;
        if (cf.underline) st |= 4;
        appendU16(out, st); // CFStyle: 10 style bits + pp9rt = 0
    }
    if (font) appendU16(out, fonts.value(cf.fontFamily));
    if (cf.hasSize) appendU16(out, cf.size);
    if (cf.hasColor) {
        out.append(char(cf.red));
        out.append(char(cf.green));
        out.append(char(cf.blue));
        out.append(char(0xFE));
    }
    return out;
}

// Rebuild a StyleTextPropAtom payload with real per-paragraph and per-run
// formatting.  The importer reads runs until their cumulative count exceeds
// the TextCharsAtom length, so the final count of each list is padded to
// guarantee that condition (and never reads past the atom).
static QByteArray writeStyleAtomPayload(const SlideText &st, const QHash<QString, quint16> &fonts)
{
    const int textLen = st.text.size();
    QByteArray pf, cf;

    int pfSum = 0;
    for (int i = 0; i < st.paras.size(); ++i) {
        const ParaFmt &p = st.paras.at(i);
        quint32 count = quint32(qMax(p.length, 1));
        if (i == st.paras.size() - 1) count = quint32(qMax(textLen - pfSum + 1, 1));
        appendU32(pf, count);
        appendU16(pf, 0); // indentLevel
        pf += buildPFException(p);
        pfSum += int(count);
    }
    if (st.paras.isEmpty()) {
        appendU32(pf, quint32(qMax(textLen + 1, 1)));
        appendU16(pf, 0);
        pf += buildPFException(ParaFmt());
    }

    // Character runs must cover every character including paragraph
    // separators.  Extending each run to the next span's start keeps the
    // importer's per-run capping from splitting a paragraph mid-way.
    for (int i = 0; i < st.spans.size(); ++i) {
        const TextSpan &sp = st.spans.at(i);
        const int nextStart = (i + 1 < st.spans.size()) ? st.spans.at(i + 1).start : textLen + 1;
        quint32 count = quint32(qMax(nextStart - sp.start, 1));
        appendU32(cf, count);
        cf += buildCFException(sp.fmt, fonts);
    }
    if (st.spans.isEmpty()) {
        appendU32(cf, quint32(qMax(textLen + 1, 1)));
        cf += buildCFException(CharFmt(), fonts);
    }
    return pf + cf;
}

// Minimal style atom for a deliberately empty text shape.
static QByteArray emptyStyleAtomPayload()
{
    QByteArray out;
    appendU32(out, 1); appendU16(out, 0); appendU32(out, 0); // PF run
    appendU32(out, 1); appendU32(out, 0);                   // CF run
    return out;
}

// Build the FontEntityAtom (0xFB7) payload for a font family name.
static QByteArray fontEntityPayload(const QString &family)
{
    const QString name = family.left(31);
    QByteArray out;
    for (int i = 0; i < 32; ++i) {
        const ushort ch = (i < name.size()) ? name.at(i).unicode() : 0;
        out.append(char(ch & 0xFF));
        out.append(char((ch >> 8) & 0xFF));
    }
    out.append(char(0)); out.append(char(0));
    out.append(char(0x06)); out.append(char(0x10));
    return out;
}

// Extend the document's FontCollectionContainer with every font family used
// by the exported text, returning a family -> fontRef map.  The legacy
// importer resolves CF typeface references as indices into this collection.
static QHash<QString, quint16> prepareFontCollection(QList<Record> &doc, const QList<SlideInfo> &slides)
{
    QHash<QString, quint16> map;
    Record *fc = findRecord(doc, 0x07D5);
    if (!fc) return map;

    quint16 next = 0;
    for (const Record &entry : fc->children) {
        if (entry.type != 0xFB7 || entry.payload.size() < 64) continue;
        const QString name = QString::fromUtf16(
            reinterpret_cast<const char16_t *>(entry.payload.constData()), 32)
            .section(QLatin1Char('\0'), 0, 0);
        map.insert(name, entry.inst);
        next = qMax(next, quint16(entry.inst + 1));
    }

    for (const SlideInfo &slide : slides) {
        for (const SlideText &st : slide.texts) {
            for (const TextSpan &sp : st.spans) {
                if (!sp.fmt.hasFont || sp.fmt.fontFamily.isEmpty()) continue;
                if (map.contains(sp.fmt.fontFamily)) continue;
                if (next > 128) continue;
                Record entry;
                entry.ver = 0;
                entry.inst = next;
                entry.type = 0xFB7;
                entry.payload = fontEntityPayload(sp.fmt.fontFamily);
                fc->children.append(entry);
                map.insert(sp.fmt.fontFamily, next++);
            }
        }
    }
    return map;
}

// Patch the SmallRectStruct in a 0xF010 PptOfficeArtClientAnchor record.
// Coordinates are master units (1/576 inch); the input rect is in points.
static void patchShapeAnchor(Record *anchor, const QRectF &rect)
{
    if (!anchor || anchor->payload.size() < 8) return;
    const qreal s = 8.0; // 576 / 72
    const qint16 top = qint16(rect.top() * s);
    const qint16 left = qint16(rect.left() * s);
    const qint16 right = qint16(rect.right() * s);
    const qint16 bottom = qint16(rect.bottom() * s);
    writeU16(anchor->payload, 0, quint16(top));
    writeU16(anchor->payload, 2, quint16(left));
    writeU16(anchor->payload, 4, quint16(right));
    writeU16(anchor->payload, 6, quint16(bottom));
}

// Find the OffArtSpContainer that directly owns a given 0xF00D client-data
// record by walking the record tree.
static Record *findShapeForClientData(Record *root, Record *clientData)
{
    if (root->type == 0xF004) {
        for (Record *cd : findAllRecords(*root, 0xF00D)) {
            if (cd == clientData) return root;
        }
    }
    for (Record &child : root->children) {
        if (Record *found = findShapeForClientData(&child, clientData)) return found;
    }
    return nullptr;
}

static QByteArray buildDocument(const QList<SlideInfo> &slides, const QList<ExportedImage> &images,
                                const QSizeF &slideSize)
{
    QList<Record> doc = parseRecords(QByteArray::fromRawData(
        reinterpret_cast<const char *>(kTemplateDocument), kTemplateDocumentLen), 0, kTemplateDocumentLen);

    // Patch the DocumentAtom (0x03E9) slide size from the ODP page size.
    // PointStruct x/y are signed 32-bit master units (1/576 inch); the ODP
    // size is in points (1/72 inch), so multiply by 8.
    if (Record *atom = findRecord(doc, 0x03E9); atom && atom->payload.size() >= 8) {
        writeU32(atom->payload, 0, quint32(qRound(slideSize.width() * 8.0)));
        writeU32(atom->payload, 4, quint32(qRound(slideSize.height() * 8.0)));
    }

    // Register every font family used by the text so CF typeface references
    // resolve correctly on import.
    const QHash<QString, quint16> fonts = prepareFontCollection(doc, slides);

    // Patch the Dgg's BStoreContainer (0xF001 inst=1) with one FBSE per image.
    if (!images.isEmpty()) {
        Record *bstore = findRecord(doc, 0xF001, 1);
        if (bstore) {
            bstore->inst = quint16(images.size());
            bstore->children.clear();
            for (int i = 0; i < images.size(); ++i) {
                bstore->children.append(buildFbse(images[i], 0));
            }
        }
    }

    // Locate the template slide container and text atoms inside it.
    Record *tplSlide = findRecord(doc, 0x03EE);
    if (!tplSlide) return QByteArray();

    // Locate slide list container (0x0FF0 instance 0)
    Record *slideList = findRecord(doc, 0x0FF0, 0);
    if (!slideList || slideList->children.isEmpty()) return QByteArray();
    const Record tplSlidePersist = slideList->children.first();

    // Replace slide list children with one SlidePersistAtom per output slide.
    slideList->children.clear();
    QList<quint32> slidePersistIds;
    quint32 nextPersist = 6; // 1 doc, 2 master, 3 notes, 4 first slide, 5 handout
    for (int i = 0; i < slides.size(); ++i) {
        Record sp = tplSlidePersist;
        const quint32 pid = (i == 0) ? 4 : nextPersist++;
        slidePersistIds.append(pid);
        if (sp.payload.size() >= 4) writeU32(sp.payload, 0, pid);
        if (sp.payload.size() >= 16) writeU32(sp.payload, 12, 256 + quint32(i));
        slideList->children.append(sp);
    }

    // Locate the template's text shapes (0xF004 containers holding text).
    QList<Record *> templateTextShapes;
    for (Record *cd : findAllRecords(*tplSlide, 0xF00D)) {
        if (findRecord(cd->children, 0x0FA0)) {
            if (Record *shape = findShapeForClientData(tplSlide, cd)) {
                if (!templateTextShapes.contains(shape)) templateTextShapes.append(shape);
            }
        }
    }

    // Build output slide containers by cloning the template.
    QList<QByteArray> slideBlobs;
    for (const SlideInfo &info : slides) {
        Record slide = *tplSlide;

        // Re-find text shapes in the cloned slide (pointers into the clone).
        QList<Record *> textShapes;
        for (Record *cd : findAllRecords(slide, 0xF00D)) {
            if (findRecord(cd->children, 0x0FA0)) {
                if (Record *shape = findShapeForClientData(&slide, cd)) {
                    if (!textShapes.contains(shape)) textShapes.append(shape);
                }
            }
        }

        for (int t = 0; t < info.texts.size(); ++t) {
            const SlideText &st = info.texts.at(t);
            Record *shape = nullptr;
            if (t < textShapes.size()) {
                shape = textShapes[t];
            } else {
                // Clone the last text shape to accommodate additional text boxes.
                if (textShapes.isEmpty()) break;
                Record clone = *textShapes.last();
                // The shape lives inside a 0xF003 group; append the clone there.
                Record *group = nullptr;
                for (Record &r : slide.children) {
                    if (r.type == 0x040C) {
                        for (Record &g : r.children) {
                            if (g.type == 0xF002) {
                                for (Record &sub : g.children) {
                                    if (sub.type == 0xF003) { group = &sub; break; }
                                }
                            }
                        }
                    }
                }
                if (!group) break;
                group->children.append(clone);
                shape = &group->children.last();
            }

            // Patch text, style and anchor inside this shape.
            Record *cd = findRecord(shape->children, 0xF00D);
            if (!cd) continue;
            Record *textAtom = findRecord(cd->children, 0x0FA0);
            Record *styleAtom = findRecord(cd->children, 0x0FA1);
            if (!textAtom || !styleAtom) continue;

            textAtom->payload = toUtf16(st.text);
            styleAtom->payload = writeStyleAtomPayload(st, fonts);
            Record *anchor = findRecord(shape->children, 0xF010);
            patchShapeAnchor(anchor, st.rect);
        }

        // The template slide carries its own sample text.  Clear any text
        // shapes we did not fill above, otherwise that sample text leaks
        // into the exported slide.
        for (int t = info.texts.size(); t < textShapes.size(); ++t) {
            Record *shape = textShapes[t];
            Record *cd = findRecord(shape->children, 0xF00D);
            if (!cd) continue;
            Record *textAtom = findRecord(cd->children, 0x0FA0);
            Record *styleAtom = findRecord(cd->children, 0x0FA1);
            if (!textAtom || !styleAtom) continue;
            textAtom->payload.clear();
            styleAtom->payload = emptyStyleAtomPayload();
        }

        // Add picture shapes for this slide's images into the shape group.
        if (!info.images.isEmpty()) {
            Record *group = nullptr;
            for (Record &r : slide.children) {
                if (r.type == 0x040C) {
                    for (Record &g : r.children) {
                        if (g.type == 0xF002) {
                            for (Record &sub : g.children) {
                                if (sub.type == 0xF003) { group = &sub; break; }
                            }
                        }
                    }
                }
            }
            if (group) {
                for (const SlideImage &img : info.images) {
                    group->children.append(buildPictureShape(img));
                }
            }
        }
        slideBlobs.append(slide.serialize());
    }

    // Assemble top-level stream: document container, master, notes, slides,
    // handout, persist directory, user edit.  We keep the template's static
    // records in order and substitute slides where the template slide was.
    QList<QByteArray> topBlobs;
    for (Record &r : doc) {
        if (r.type == 0x03EE) {
            for (const QByteArray &b : slideBlobs) topBlobs.append(b);
        } else if (r.type == 0x1772 || r.type == 0x0FF5) {
            // rebuilt below
        } else {
            topBlobs.append(r.serialize());
        }
    }

    // Compute offsets of each top-level record.
    QList<quint32> offsets;
    quint32 pos = 0;
    for (const QByteArray &b : topBlobs) {
        offsets.append(pos);
        pos += quint32(b.size());
    }

    // The top-level order is: document, master, notes, slides..., handout.
    // Locate the first slide by its record type (0x3EE).
    QList<quint16> topTypes;
    for (Record &r : doc) {
        if (r.type == 0x03EE) {
            for (int k = 0; k < slideBlobs.size(); ++k) topTypes.append(0x03EE);
        } else if (r.type != 0x1772 && r.type != 0x0FF5) {
            topTypes.append(r.type);
        }
    }
    int firstSlideBlobIndex = topTypes.indexOf(0x03EE);
    if (firstSlideBlobIndex < 0) firstSlideBlobIndex = 3;

    // Persist directory.  The template's persist IDs are:
    //   1=document, 2=master, 3=notes, 4=first slide, 5=handout,
    //   6..=additional slides.  The SlidePersistAtoms above use the same
    //   numbering, so the persist directory must be indexed by ID, not by
    //   the order of records in the stream.
    const int nSlides = slideBlobs.size();
    const int maxPersistId = 5 + nSlides - 1; // 4 + nSlides, with handout at 5
    QList<quint32> persistOffsets(maxPersistId + 1, 0);
    persistOffsets[1] = 0;                                        // document
    persistOffsets[2] = offsets.value(1);                         // master
    persistOffsets[3] = offsets.value(2);                         // notes
    persistOffsets[4] = offsets.value(firstSlideBlobIndex);       // first slide
    persistOffsets[5] = offsets.last();                           // handout
    for (int i = 1; i < nSlides; ++i) {
        persistOffsets[5 + i] = offsets.value(firstSlideBlobIndex + i); // extra slides: 6,7,...
    }

    QByteArray pdPayload;
    const quint32 packed = (1 & 0xFFFFF) | ((quint32(maxPersistId) & 0xFFF) << 20);
    appendU32(pdPayload, packed);
    for (int id = 1; id <= maxPersistId; ++id) appendU32(pdPayload, persistOffsets.at(id));
    QByteArray pd;
    Record::appendU16(pd, 0);
    Record::appendU16(pd, 0x1772);
    Record::appendU32(pd, quint32(pdPayload.size()));
    pd += pdPayload;

    const quint32 pdOffset = pos;
    topBlobs.append(pd);

    // UserEditAtom
    QByteArray uePayload(28, '\0');
    writeU32(uePayload, 0, 256 + quint32(slides.size())); // lastSlideIdRef
    writeU16(uePayload, 4, 0);                            // version
    uePayload[6] = 0;                                     // minorVersion
    uePayload[7] = 3;                                     // majorVersion
    writeU32(uePayload, 8, 0);                            // offsetLastEdit
    writeU32(uePayload, 12, pdOffset);                    // offsetPersistDirectory
    writeU32(uePayload, 16, 1);                           // docPersistIdRef
    writeU32(uePayload, 20, 100 + quint32(slides.size()));// persistIdSeed
    writeU16(uePayload, 24, 1);                           // lastView
    writeU16(uePayload, 26, 0);                           // unused
    QByteArray ue;
    Record::appendU16(ue, 0);
    Record::appendU16(ue, 0x0FF5);
    Record::appendU32(ue, quint32(uePayload.size()));
    ue += uePayload;

    topBlobs.append(ue);

    QByteArray document;
    for (const QByteArray &b : topBlobs) document += b;

    return document;
}

// ---------------------------------------------------------------------------
// Current User stream
// ---------------------------------------------------------------------------

static QByteArray buildCurrentUser(quint32 userEditOffset)
{
    QByteArray cu = QByteArray::fromRawData(
        reinterpret_cast<const char *>(kTemplateCurrentUser), kTemplateCurrentUserLen);
    if (cu.size() >= 20) writeU32(cu, 16, userEditOffset);
    return cu;
}

} // namespace

PptExport::PptExport(QObject *parent, const QVariantList &)
    : KoFilter(parent)
{
}

PptExport::~PptExport() = default;

KoFilter::ConversionStatus PptExport::convert(const QByteArray &from, const QByteArray &to)
{
    if (from != "application/vnd.oasis.opendocument.presentation"
        || to != "application/vnd.ms-powerpoint") {
        return BadMimeType;
    }

    auto *document = qobject_cast<KoPADocument *>(m_chain->inputDocument());
    if (!document || document->pages().isEmpty()) return StupidError;

    QList<SlideInfo> slides;
    for (KoPAPageBase *page : document->pages()) slides.append(pageContent(page));

    const QList<ExportedImage> images = registerImages(slides);
    const QSizeF slideSize = document->pages().first()->size();
    const QByteArray documentStream = buildDocument(slides, images, slideSize);
    if (documentStream.isEmpty()) return StupidError;

    // Build the Pictures stream from the registered images.  The importer
    // matches blips to FBSE entries by their 16-byte rgbUid, so no stream
    // offsets need to be patched in the document.
    QByteArray picturesStream;
    for (const ExportedImage &img : images) {
        const bool isPng = (img.suffix == QLatin1String("png"));
        Record blip;
        blip.ver = 0;
        blip.inst = isPng ? 0x06E0 : 0x046A;
        blip.type = isPng ? 0xF01E : 0xF01D;
        QByteArray payload = img.rgbUid;
        payload.append(char(0));
        payload += img.data;
        blip.payload = payload;
        picturesStream += blip.serialize();
    }

    // Find UserEditAtom offset in the produced stream.
    const QList<Record> top = parseRecords(documentStream, 0, documentStream.size());
    quint32 ueOffset = 0;
    for (const Record &r : top) {
        if (r.type == 0x0FF5) break;
        ueOffset += quint32(r.serialize().size());
    }

    const QByteArray currentUser = buildCurrentUser(ueOffset);

    QList<CfbStream> streams;
    streams.append({QStringLiteral("PowerPoint Document"), documentStream});
    streams.append({QStringLiteral("Current User"), currentUser});
    streams.append({QStringLiteral("Pictures"), picturesStream});
    streams.append({QString(), QByteArray::fromRawData(
        reinterpret_cast<const char *>(kTemplateCompObj), kTemplateCompObjLen)});
    // fix the CompObj stream name (it begins with \x01)
    streams.last().name = QString(QChar(1)) + QStringLiteral("CompObj");

    const QByteArray cfb = buildCfb(streams);

    QSaveFile out(m_chain->outputFile());
    if (!out.open(QIODevice::WriteOnly)) return FileNotFound;
    if (out.write(cfb) != cfb.size()) {
        out.cancelWriting();
        return StupidError;
    }
    if (!out.commit()) return StupidError;

    return OK;
}

#include "PptExport.moc"
