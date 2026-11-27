/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "PptExport.h"
#include "PptExportNotesTemplate.h"
#include "PptExportTemplate.h"
#include "PptTransitionMap.h"
#include "msodraw.h"

#include <KPrNotes.h>
#include <KPrPage.h>
#include <KPrPageApplicationData.h>
#include <KPrPlaceholderShape.h>
#include <KoTosContainer.h>
#include <pageeffects/KPrPageEffect.h>

#include <KoColorBackground.h>
#include <KoDocument.h>
#include <KoEmbeddedDocumentSaver.h>
#include <KoGenStyles.h>
#include <KoFilterChain.h>
#include <KoFlake.h>
#include <KoImageData.h>
#include <KoInlineTextObjectManager.h>
#include <KoInsets.h>
#include <KoPatternBackground.h>
#include <KoPADocument.h>
#include <KoPAMasterPage.h>
#include <KoPAPage.h>
#include <KoPAPageBase.h>
#include <KoBorder.h>
#include <KoPathPoint.h>
#include <KoPathShape.h>
#include <KoShape.h>
#include <KoShapeBackground.h>
#include <KoShapeContainer.h>
#include <KoShapeGroup.h>
#include <KoShapeLayer.h>
#include <KoShapePainter.h>
#include <KoShapePaintingContext.h>
#include <KoShapeStroke.h>
#include <KoShapeSavingContext.h>
#include <KoViewConverter.h>
#include <KoXmlWriter.h>
#include <KoXmlNS.h>
#include <KoTextShapeData.h>
#include <KoTextShapeDataBase.h>
#include <KoTextDocument.h>
#include <KoTextMeta.h>
#include <KoVariable.h>
#include <styles/KoCharacterStyle.h>
#include <styles/KoListLevelProperties.h>
#include <styles/KoListStyle.h>
#include <styles/KoParagraphStyle.h>

#include <KPluginFactory>

#include <QBuffer>
#include <QColor>
#include <QCryptographicHash>
#include <functional>
#include <QFile>
#include <QDomDocument>
#include <QTextStream>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QRectF>
#include <QSaveFile>
#include <QString>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFormat>
#include <QTextList>
#include <QVariant>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

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
static void appendI16(QByteArray &b, qint16 v)
{
    appendU16(b, quint16(v));
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

    // Reserve enough leading sectors for the FAT (and the DIFAT chain when
    // the FAT does not fit in the 109 header slots).  Each FAT sector holds
    // SECTOR/4 entries; we must reserve them before allocating any data
    // sectors, otherwise writing the FAT would overwrite stream data.
    int dataSectors = 0;
    for (const CfbStream &s : streams) dataSectors += qMax(1, (s.data.size() + SECTOR - 1) / SECTOR);
    // mini stream, mini FAT and directory are small; allow generous headroom
    const int overhead = 32;
    int nFat = 1;
    int nDifat = 0;
    for (;;) {
        const int total = 1 /* header */ + dataSectors + overhead + nFat + nDifat;
        const int needFat = (total + SECTOR / 4 - 1) / (SECTOR / 4);
        // A DIFAT sector holds 127 FAT sector numbers and one chain pointer.
        const int needDifat = needFat > 109 ? (needFat - 109 + 126) / 127 : 0;
        if (needFat == nFat && needDifat == nDifat) break;
        nFat = needFat;
        nDifat = needDifat;
    }

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

    // DIFAT sectors hold the FAT sector numbers that do not fit in the 109
    // header slots.  The last entry in each DIFAT sector chains to the next.
    // Without these, any file with more than 109 FAT sectors has an invalid
    // directory and cannot be reopened.
    const quint32 difatStart = nDifat > 0 ? quint32(sectors.size()) : ENDOFCHAIN;
    for (int i = 0; i < nDifat; ++i) {
        QByteArray ds(SECTOR, '\xff'); // unused entries are FREESECT
        for (int j = 0; j < 127; ++j) {
            const int fatIndex = 109 + i * 127 + j;
            writeU32(ds, j * 4, fatIndex < nFat ? quint32(fatIndex) : FREESECT);
        }
        writeU32(ds, 127 * 4, (i + 1 < nDifat) ? (difatStart + i + 1) : ENDOFCHAIN);
        sectors.append(ds);
        fat.append(ENDOFCHAIN);
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
    writeU32(h, 68, difatStart);
    writeU32(h, 72, quint32(nDifat));
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
    // TextCFException::position, in percent.  The legacy field is a signed
    // 16-bit value but Calligra's parser only accepts -100..100.
    bool hasPosition = false;
    qint16 position = 0;

    bool operator==(const CharFmt &o) const
    {
        return bold == o.bold && italic == o.italic && underline == o.underline
               && hasSize == o.hasSize && size == o.size
               && hasColor == o.hasColor && red == o.red && green == o.green && blue == o.blue
               && hasFont == o.hasFont && fontFamily == o.fontFamily
               && hasPosition == o.hasPosition && position == o.position;
    }
};

// Paragraph formatting.  The alignment values match the legacy PPT
// TextAlignment enum: 0 left, 1 centre, 2 right, 3 justify.
struct ParaFmt {
    int alignment = -1;           // -1 = not set
    int start = 0;                // index into SlideText::text
    int length = 0;               // including trailing CR, if any

    // Bullet formatting.  bulletLevel 0 means a plain (non-list) paragraph.
    int bulletLevel = 0;          // 1-based nesting level from the ODF list
    int bulletChar = 0;           // Unicode code point of the bullet glyph
    bool bulletHasFont = false;
    QString bulletFont;
    bool bulletHasColor = false;
    quint8 bulletRed = 0, bulletGreen = 0, bulletBlue = 0;
    bool bulletHasSize = false;
    int bulletSizeValue = 0;      // 25..400 = percent, -1..-4000 = absolute pt

    // Paragraph geometry, in points.  leftMargin is the distance from the
    // shape edge to the text; indent is the legacy "first line" indent that
    // the importer turns into text:space-before for lists.
    bool hasIndent = false;
    qreal leftMargin = 0;
    qreal indent = 0;

    // Spacing.  lineSpacing is a percentage (> 0) or absolute master units
    // (< 0); spaceBefore/After are pt.
    bool hasLineSpacing = false;
    int lineSpacingValue = 0;
    bool hasSpaceBefore = false;
    qreal spaceBefore = 0;
    bool hasSpaceAfter = false;
    qreal spaceAfter = 0;
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
    int stackingOrder = 0;
    QString text;                 // plain text, paragraphs separated by CR
    QVector<TextSpan> spans;      // character-format runs covering the text
    QVector<ParaFmt> paras;       // one entry per paragraph
    QList<int> slideNumberPositions;
    QRectF rect;
    KoInsets margins;
    Qt::Alignment verticalAlignment = Qt::AlignTop;
    bool noWrap = false;          // PPT WrapText == wrNone (auto-grow width)
    bool autoGrow = false;        // PPT fFitShapeToText
    qreal rotation = 0;           // clockwise degrees, as stored by binary PPT
    bool hasFill = false;
    quint8 fillRed = 0, fillGreen = 0, fillBlue = 0;
    // Index into SlideInfo::shapes when this text belongs to a native
    // autoshape.  -1 means an ordinary text box.
    int nativeShape = -1;
};

struct SlideImage {
    int stackingOrder = 0;
    QByteArray data;      // PNG or JPEG bytes
    QString suffix;
    QByteArray rgbUid;    // 16-byte identifier shared with the FBSE in the Dgg
    QRectF rect;          // position in points
    int pib = 0;          // 1-based picture index, assigned by registerImages()
    QByteArray embeddedObject;
    quint32 embeddedObjectId = 0;
    // Background fills must be written before the text boxes so they do not
    // paint over the slide content.
    bool behindText = false;
};

// A preset autoshape or straight line that can be written as a native binary
// PowerPoint shape instead of being rasterised into a picture.
struct SlideShape {
    int stackingOrder = 0;
    quint16 spt = 0;              // MSOSPT preset type
    QList<qint32> adjustments;    // raw OfficeArtFOPTE adjust values
    QRectF rect;                  // position in points
    bool flipH = false;
    bool flipV = false;
    qreal rotation = 0;
    bool hasFill = false;
    quint8 fillRed = 0, fillGreen = 0, fillBlue = 0;
    bool hasLine = false;
    quint8 lineRed = 0, lineGreen = 0, lineBlue = 0;
    quint32 lineWidth = 0;        // master units (1/576 inch)
};

struct SlideInfo {
    QList<SlideText> texts;
    QList<SlideImage> images;
    QList<SlideShape> shapes;
    bool hasNotes = false;
    SlideText notes;
    // Transition fields, populated from KPrPageApplicationData.
    quint8 effectType = 0;      // binary MS-PPT effectType
    quint8 effectDirection = 0; // binary MS-PPT effectDirection
    quint8 speed = 0;           // 0=0.75s, 1=0.5s, 2=0.25s
    quint32 advanceMs = 0;      // auto-advance delay
    bool manualAdvance = true;
    bool autoAdvance = false;
    bool hasTransition = false;
    // Solid slide background colour (only present when explicitly set).
    bool hasBackground = false;
    quint8 bgRed = 0, bgGreen = 0, bgBlue = 0;
};

// Images shared by all slides, indexed starting at pib=1.
struct ExportedImage {
    QByteArray data;
    QString suffix;
    QByteArray rgbUid;
    QRectF rect;          // used for the metafile header of WMF/EMF blips
    quint32 offset = 0;
    quint32 recordSize = 0;
};

// Render an arbitrary shape into a PNG so visual content that has no native
// binary-PPT representation (charts, tables, autoshapes, paths, connectors,
// SVG/WMF images, ...) survives the round-trip as a picture instead of being
// dropped entirely.  Rasterising is lossy for editability, but faithful to the
// rendered appearance, which is the priority for a legacy binary format.
static QByteArray rasterizeShape(KoShape *shape, const QRectF &bounds, bool excludeText = false)
{
    if (!shape) return QByteArray();
    const QSizeF shapeSize = bounds.size();
    if (shapeSize.isEmpty()) return QByteArray();
    const QSize pixelSize(qBound(1, qRound(shapeSize.width() * 2.0), 8192),
                          qBound(1, qRound(shapeSize.height() * 2.0), 8192));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    // A text-bearing autoshape keeps its text as an editable text box.  Hide
    // the text children while painting so the rasterised picture contains
    // only the shape's fill and outline; otherwise the text would be painted
    // twice (once in the picture, once as a text box).
    QList<KoShape *> hidden;
    if (excludeText) {
        std::function<void(KoShape *)> hideText = [&](KoShape *s) {
            if (qobject_cast<KoTextShapeDataBase *>(s->userData()) && s->isVisible()) {
                s->setVisible(false);
                hidden.append(s);
            }
            if (auto *container = dynamic_cast<KoShapeContainer *>(s)) {
                for (KoShape *child : container->shapes()) hideText(child);
            }
        };
        hideText(shape);
    }

    KoShapePainter painter;
    painter.setShapes({shape});
    painter.paint(image);

    for (KoShape *s : hidden) s->setVisible(true);

    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return QByteArray();
    return data;
}

static SlideText extractSlideText(QTextDocument *doc, const QRectF &rect);

static QByteArray formulaMathML(KoShape *shape)
{
    if (shape->shapeId() != QLatin1StringView("FormulaShapeID")) {
        return {};
    }

    QBuffer frame;
    frame.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&frame);
    KoGenStyles styles;
    KoEmbeddedDocumentSaver embedded;
    KoShapeSavingContext context(writer, styles, embedded);
    writer.startElement("frame-wrapper");
    writer.addAttribute("xmlns:draw", KoXmlNS::draw);
    writer.addAttribute("xmlns:math", KoXmlNS::math);
    writer.addAttribute("xmlns:svg", KoXmlNS::svg);
    writer.addAttribute("xmlns:calligra", KoXmlNS::calligra);
    writer.addAttribute("xmlns:presentation", KoXmlNS::presentation);
    shape->saveOdf(context);
    writer.endElement();

    QDomDocument document;
    if (!document.setContent(frame.data())) {
        return {};
    }
    QDomElement math = document.elementsByTagName(QStringLiteral("math:math")).item(0).toElement();
    if (math.isNull()) {
        return {};
    }
    math.setAttribute(QStringLiteral("xmlns:math"), KoXmlNS::math);
    QString xml;
    QTextStream output(&xml);
    math.save(output, -1);
    return xml.toUtf8();
}

static QByteArray buildFormulaObject(const QByteArray &math)
{
    if (math.isEmpty() || math.size() > 64 * 1024) {
        return {};
    }

    // Use an embedded storage object so outside readers still have the
    // standard PowerPoint preview. Stage reads the MathML stream on import.
    Record metadata;
    metadata.type = 0x0FCC;
    metadata.ver = 15;
    Record embed;
    embed.type = 0x0FCD;
    embed.payload.resize(8, '\0');
    metadata.children.append(embed);
    Record ole;
    ole.type = 0x0FC3;
    ole.ver = 1;
    ole.payload.resize(24, '\0');
    writeU32(ole.payload, 0, 1); // DVASPECT_CONTENT
    metadata.children.append(ole);

    Record storage;
    storage.type = 0x1011;
    storage.payload = buildCfb({{QStringLiteral("Calligra MathML"), math}});
    return metadata.serialize() + storage.serialize();
}

static void addImageShape(KoShape *shape, const QByteArray &data, const QString &suffix,
                          QList<SlideImage> &images, const QRectF &rect,
                          bool behindText = false, int stackingOrder = 0)
{
    if (data.isEmpty()) return;
    SlideImage img;
    img.stackingOrder = stackingOrder;
    img.data = data;
    img.suffix = suffix;
    img.rgbUid = QCryptographicHash::hash(data, QCryptographicHash::Md5).left(16);
    img.rect = rect;
    img.behindText = behindText;
    if (shape) {
        img.embeddedObject = QByteArray::fromBase64(shape->additionalAttribute(QStringLiteral("calligra:ppt-embedded-object")).toLatin1(),
                                                    QByteArray::AbortOnBase64DecodingErrors);
        if (img.embeddedObject.isEmpty()) {
            img.embeddedObject = buildFormulaObject(formulaMathML(shape));
        }
    }
    images.append(img);
}

// Preserve a drawing-page fill that cannot be represented by a solid
// background colour.  A bitmap fill is emitted as a full-slide picture placed
// behind the text; other fills are rasterised once into the same picture.
static void addBackgroundFill(KoShapeBackground *background, const QRectF &rect,
                              QList<SlideImage> &images)
{
    if (!background || rect.isEmpty()) return;

    if (auto *pattern = dynamic_cast<KoPatternBackground *>(background);
        pattern && pattern->repeat() == KoPatternBackground::Stretched) {
        if (KoImageData *imageData = pattern->patternData()) {
            QByteArray data;
            QBuffer buffer(&data);
            QString suffix;
            if (buffer.open(QIODevice::WriteOnly) && imageData->saveData(buffer)) {
                suffix = imageData->suffix().toLower();
            }
            if (!data.isEmpty() && (suffix == QLatin1String("png")
                                    || suffix == QLatin1String("jpg")
                                    || suffix == QLatin1String("jpeg"))) {
                addImageShape(nullptr, data,
                              suffix == QLatin1String("png") ? QStringLiteral("png")
                                                             : QStringLiteral("jpeg"),
                              images, rect, true);
                return;
            }
        }
    }

    const QSize pixelSize(qBound(1, qRound(rect.width() * 2.0), 8192),
                          qBound(1, qRound(rect.height() * 2.0), 8192));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.scale(pixelSize.width() / rect.width(), pixelSize.height() / rect.height());
    QPainterPath fillPath;
    fillPath.addRect(QRectF(QPointF(0, 0), rect.size()));
    KoShapePaintingContext context;
    KoViewConverter converter;
    background->paint(painter, converter, context, fillPath);
    QByteArray data;
    QBuffer buffer(&data);
    if (buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG")) {
        addImageShape(nullptr, data, QStringLiteral("png"), images, rect, true);
    }
}

// Map the ODF preset names written by the PPT importer back to the legacy
// MSOSPT values.  Names of the form mso-sptNNN carry the numeric value
// directly, which also covers exotic presets that have no ODF alias here.
static quint16 presetShapeType(const QString &preset)
{
    static const QHash<QString, quint16> types = {
        {QStringLiteral("non-primitive"), msosptNotPrimitive},
        {QStringLiteral("rectangle"), msosptRectangle},
        {QStringLiteral("round-rectangle"), msosptRoundRectangle},
        {QStringLiteral("ellipse"), msosptEllipse},
        {QStringLiteral("diamond"), msosptDiamond},
        {QStringLiteral("isosceles-triangle"), msosptIsocelesTriangle},
        {QStringLiteral("right-triangle"), msosptRightTriangle},
        {QStringLiteral("parallelogram"), msosptParallelogram},
        {QStringLiteral("trapezoid"), msosptTrapezoid},
        {QStringLiteral("hexagon"), msosptHexagon},
        {QStringLiteral("octagon"), msosptOctagon},
        {QStringLiteral("cross"), msosptPlus},
        {QStringLiteral("pentagon"), msosptPentagon},
        {QStringLiteral("home-plate"), msosptHomePlate},
        {QStringLiteral("can"), msosptCan},
        {QStringLiteral("cube"), msosptCube},
        {QStringLiteral("quad-bevel"), msosptBevel},
        {QStringLiteral("chevron"), msosptChevron},
        {QStringLiteral("no-smoking"), msosptNoSmoking},
        {QStringLiteral("smiley"), msosptSmileyFace},
        {QStringLiteral("heart"), msosptHeart},
        {QStringLiteral("lightning"), msosptLightningBolt},
        {QStringLiteral("sun"), msosptSun},
        {QStringLiteral("moon"), msosptMoon},
        {QStringLiteral("left-brace"), msosptLeftBrace},
        {QStringLiteral("right-brace"), msosptRightBrace},
        {QStringLiteral("left-bracket"), msosptLeftBracket},
        {QStringLiteral("right-bracket"), msosptRightBracket},
        {QStringLiteral("brace-pair"), msosptBracePair},
        {QStringLiteral("bracket-pair"), msosptBracketPair},
        {QStringLiteral("right-arrow"), msosptArrow},
        {QStringLiteral("left-arrow"), msosptLeftArrow},
        {QStringLiteral("up-arrow"), msosptUpArrow},
        {QStringLiteral("down-arrow"), msosptDownArrow},
        {QStringLiteral("left-right-arrow"), msosptLeftRightArrow},
        {QStringLiteral("up-down-arrow"), msosptUpDownArrow},
        {QStringLiteral("quad-arrow"), msosptQuadArrow},
        {QStringLiteral("line-callout-1"), msosptBorderCallout1},
        {QStringLiteral("line-callout-2"), msosptBorderCallout2},
        {QStringLiteral("line-callout-3"), msosptBorderCallout3},
        {QStringLiteral("right-arrow-callout"), msosptRightArrowCallout},
        {QStringLiteral("left-arrow-callout"), msosptLeftArrowCallout},
        {QStringLiteral("up-arrow-callout"), msosptUpArrowCallout},
        {QStringLiteral("down-arrow-callout"), msosptDownArrowCallout},
    };
    if (const auto it = types.constFind(preset); it != types.constEnd()) return it.value();
    if (preset.startsWith(QStringLiteral("mso-spt"))) {
        bool ok = false;
        const int value = preset.mid(7).toInt(&ok);
        if (ok && value > 0 && value <= 0xFFFF) return quint16(value);
    }
    return 0;
}

static qreal pptShapeRotation(KoShape *shape)
{
    const qreal rotation = shape->rotation();
    if (!std::isfinite(rotation)) return 0;
    qreal result = -rotation;
    while (result < 0) result += 360;
    while (result >= 360) result -= 360;
    const qreal snapped = qRound(result);
    if (qAbs(result - snapped) < 1e-3) return snapped;
    return result;
}

// Read the solid fill and line of a shape.  Callout shapes keep the actual
// decoration in a child path shape, so walk containers as well.
static void collectShapeStyle(KoShape *shape, SlideShape &out, bool descend = true)
{
    if (!shape) return;
    if (auto *color = dynamic_cast<KoColorBackground *>(shape->background().data())) {
        const QColor c = color->color();
        if (c.isValid() && c.alpha() > 0) {
            out.hasFill = true;
            out.fillRed = quint8(c.red());
            out.fillGreen = quint8(c.green());
            out.fillBlue = quint8(c.blue());
        }
    }
    if (KoBorder *border = shape->border(); border && border->hasBorder()) {
        const auto side = KoBorder::TopBorder;
        if (border->borderStyle(side) != KoBorder::BorderNone) {
            const QColor c = border->borderColor(side);
            out.hasLine = true;
            out.lineRed = quint8(c.red());
            out.lineGreen = quint8(c.green());
            out.lineBlue = quint8(c.blue());
            out.lineWidth = quint32(qMax(0, qRound(border->borderWidth(side) * 12700)));
        }
    }
    // Freeform/path shapes (including ODF draw:line) store their outline as a
    // KoShapeStroke rather than a KoBorder.
    if (auto *stroke = dynamic_cast<KoShapeStroke *>(shape->stroke())) {
        const QColor c = stroke->color();
        if (c.isValid() && c.alpha() > 0 && stroke->lineStyle() != Qt::NoPen) {
            out.hasLine = true;
            out.lineRed = quint8(c.red());
            out.lineGreen = quint8(c.green());
            out.lineBlue = quint8(c.blue());
            out.lineWidth = quint32(qMax(0, qRound(stroke->lineWidth() * 12700)));
        }
    }
    if (descend) {
        if (auto *container = dynamic_cast<KoShapeContainer *>(shape)) {
            for (KoShape *child : container->shapes()) {
                // Text-on-shape children may carry the generic text-shape
                // background, which is not the fill of the custom shape they
                // belong to.  Never let it override the visible decoration.
                if (qobject_cast<KoTextShapeDataBase *>(child->userData())) continue;
                if (!child->isVisible(true)) continue;
                collectShapeStyle(child, out, true);
                if (out.hasFill && out.hasLine) break;
            }
        }
    }
}

// Native autoshapes only carry a solid fill in this exporter.  If any part of
// a shape uses a gradient, hatch, pattern or ODF-specific background, keep the
// old raster path instead of silently simplifying it.
static bool shapeHasUnsupportedBackground(KoShape *shape)
{
    if (!shape) return false;
    const auto background = shape->background();
    if (background && !dynamic_cast<KoColorBackground *>(background.data())) return true;
    if (auto *container = dynamic_cast<KoShapeContainer *>(shape)) {
        for (KoShape *child : container->shapes()) {
            if (shapeHasUnsupportedBackground(child)) return true;
        }
    }
    return false;
}

// A callout is a container whose enhanced-path child carries the preset
// attribute and the (much larger) path bounds.  The ODF frame dimensions live
// on the owning container, so find that container before exporting geometry.
static KoShape *findPresetDescendant(KoShape *shape, int depth = 0)
{
    if (!shape || depth > 8) return nullptr;
    if (dynamic_cast<KoShapeLayer *>(shape) || dynamic_cast<KoShapeGroup *>(shape)) return nullptr;
    if (!shape->additionalAttribute(QStringLiteral("calligra:shape-preset")).isEmpty()) return shape;
    auto *container = dynamic_cast<KoShapeContainer *>(shape);
    if (!container) return nullptr;
    for (KoShape *child : container->shapes()) {
        if (KoShape *found = findPresetDescendant(child, depth + 1)) return found;
    }
    return nullptr;
}

// Return true when a shape has a native binary-PPT equivalent.  The caller
// still recurses into its text children so their formatting survives.
static bool nativeShapeFromShape(KoShape *shape, QList<SlideShape> &shapes)
{
    if (!shape) return false;

    SlideShape native;
    KoShape *presetShape = nullptr;
    QString preset = shape->additionalAttribute(QStringLiteral("calligra:shape-preset"));
    if (preset.isEmpty()) {
        presetShape = findPresetDescendant(shape);
        if (presetShape) preset = presetShape->additionalAttribute(QStringLiteral("calligra:shape-preset"));
    } else {
        presetShape = shape;
    }
    const bool isPreset = !preset.isEmpty();
    if (isPreset) {
        // A freeform path has no preset geometry to carry across.  Leave it
        // to the raster fallback instead of emitting an empty shape.
        if (preset == QLatin1String("non-primitive")) return false;
        native.spt = presetShapeType(preset);
        if (!native.spt) return false;
        // A callout wrapper does not carry the geometry attributes itself;
        // they live on its enhanced-path child.
        const QStringList values = presetShape->additionalAttribute(QStringLiteral("calligra:shape-modifiers"))
                                       .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &value : values) {
            bool ok = false;
            const int adjustment = value.toInt(&ok);
            if (ok) native.adjustments.append(qint32(adjustment));
        }
        native.rect = QRectF(shape->absolutePosition(KoFlake::TopLeftCorner), shape->size());
        native.rotation = pptShapeRotation(shape);
    } else if (shape->shapeId() == QLatin1String("KoPathShape")) {
        auto *path = dynamic_cast<KoPathShape *>(shape);
        if (!path || path->subpathCount() != 1 || path->subpathPointCount(0) != 2) return false;
        KoPathPoint *first = path->pointByIndex(KoPathPointIndex(0, 0));
        KoPathPoint *last = path->pointByIndex(KoPathPointIndex(0, 1));
        if (!first || !last) return false;
        // Two points with active Bezier handles describe a curve, which would
        // be flattened if written as msosptLine.  Leave curves to the raster
        // fallback.
        if (first->activeControlPoint1() || first->activeControlPoint2()
            || last->activeControlPoint1() || last->activeControlPoint2()) {
            return false;
        }
        const QTransform matrix = path->transformation();
        const QPointF p1 = matrix.map(first->point());
        const QPointF p2 = matrix.map(last->point());
        native.spt = msosptLine;
        native.rect = QRectF(qMin(p1.x(), p2.x()), qMin(p1.y(), p2.y()),
                             qAbs(p2.x() - p1.x()), qAbs(p2.y() - p1.y()));
        native.flipH = p2.x() < p1.x();
        native.flipV = p2.y() < p1.y();
    } else {
        return false;
    }

    if (shapeHasUnsupportedBackground(shape)) return false;

    collectShapeStyle(shape, native);
    // Preset shapes are kept even without an explicit fill or outline: they
    // may be invisible layout frames that still need to stay editable.
    if (!isPreset && !native.hasFill && !native.hasLine) return false;
    shapes.append(native);
    return true;
}

static QList<KoShape *> orderedShapes(QList<KoShape *> shapes)
{
    std::stable_sort(shapes.begin(), shapes.end(), KoShape::compareShapeZIndex);
    return shapes;
}

static void collectShapeContent(KoShape *shape, QList<SlideText> &texts, QList<SlideImage> &images,
                                QList<SlideShape> &shapes, int &nextOrder, bool insideNativeShape = false)
{
    if (!shape || !shape->isVisible(true) || shape->shapeId() == QLatin1String(KPrPlaceholderShapeId)) return;
    const int stackingOrder = nextOrder++;

    // Native picture or autoshape payload.  Custom shapes keep their text in
    // child text shapes; mark that text as belonging to this shape so the
    // exporter can put it back inside the native PowerPoint object.
    bool handled = insideNativeShape;
    const int nativeIndex = !insideNativeShape && nativeShapeFromShape(shape, shapes) ? shapes.size() - 1 : -1;
    if (nativeIndex >= 0) {
        handled = true;
        shapes[nativeIndex].stackingOrder = stackingOrder;
    }
    // WMF/EMF files imported from ODP load as VectorShape objects, not image
    // shapes.  The plugin exposes their original bytes as dynamic properties,
    // so emit them as native binary-PPT blips instead of rasterising.  The
    // raster cache is often blank in batch mode, and rasterising would also
    // turn vector text in OLE previews into pixels.
    auto *shapeObject = dynamic_cast<QObject *>(shape);
    const QByteArray vectorData = shapeObject ? shapeObject->property("calligra-vector-data").toByteArray() : QByteArray();
    const QString vectorSuffix = shapeObject ? shapeObject->property("calligra-vector-suffix").toString() : QString();
    if (!vectorData.isEmpty() && (vectorSuffix == QLatin1String("wmf") || vectorSuffix == QLatin1String("emf"))) {
        addImageShape(shape, vectorData, vectorSuffix, images, shape->boundingRect(), false, stackingOrder);
        handled = true;
    }
    if (auto *imageData = qobject_cast<KoImageData *>(shape->userData())) {
        QByteArray data;
        QBuffer buffer(&data);
        QString suffix;
        if (buffer.open(QIODevice::WriteOnly) && imageData->saveData(buffer))
            suffix = imageData->suffix().toLower();
        if (!data.isEmpty() && (suffix == QLatin1String("png") || suffix == QLatin1String("jpg")
                                || suffix == QLatin1String("jpeg") || suffix == QLatin1String("wmf")
                                || suffix == QLatin1String("emf"))) {
            const QString nativeSuffix = (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
                                         ? QStringLiteral("jpeg") : suffix;
            addImageShape(shape, data, nativeSuffix,
                          images, QRectF(shape->absolutePosition(KoFlake::TopLeftCorner), shape->size()), false, stackingOrder);
            handled = true;
        }
        // Unsupported image formats (SVG, ...) fall through to rasterise.
        // WMF/EMF are written as native binary-PPT blips: rasterising them
        // would turn vector text in OLE previews into pixels.
    }

    // Native text payload.  A text shape with no characters (e.g. an empty
    // placeholder inside a picture frame) contributes nothing and must not be
    // rasterised into a phantom image, so it is handled either way.
    if (auto *data = qobject_cast<KoTextShapeDataBase *>(shape->userData())) {
        handled = true;
        if (QTextDocument *doc = data->document()) {
            QRectF rect(shape->absolutePosition(KoFlake::TopLeftCorner), shape->size());
            // ODF stores the unrotated box plus a transform.  PowerPoint stores
            // the box in the shape's own (unrotated) coordinate system, so use
            // the transformed centre and the original size to recover it.
            const qreal shapeRotation = shape->rotation();
            qreal pptRotation = 0;
            if (std::isfinite(shapeRotation) && qAbs(shapeRotation) > 1e-6) {
                pptRotation = -shapeRotation;
                while (pptRotation < 0) pptRotation += 360;
                while (pptRotation >= 360) pptRotation -= 360;
                // The importer writes rotate() with only six significant
                // digits, so an exact 46 degrees comes back as 45.99997.
                // Snap only near-integer values; genuine fractional rotations
                // are left untouched.
                const qreal snapped = qRound(pptRotation);
                if (qAbs(pptRotation - snapped) < 1e-3) pptRotation = snapped;
                const QPointF center = shape->absolutePosition(KoFlake::CenteredPosition);
                // OOXML/ODF store the unrotated extent, but the binary PPT
                // importer turns a rotated anchor back into an ODF frame by
                // swapping its width and height around the centre.  Write the
                // swapped extent here so the reloaded frame keeps the
                // original text layout instead of becoming a 10 mm-wide
                // column that clips its whole heading.
                const qreal w = shape->size().height();
                const qreal h = shape->size().width();
                rect = QRectF(center.x() - w / 2, center.y() - h / 2, w, h);
            }
            SlideText st = extractSlideText(doc, rect);
            st.stackingOrder = stackingOrder;
            st.rotation = pptRotation;
            SlideShape textStyle;
            collectShapeStyle(shape, textStyle, false);
            st.hasFill = textStyle.hasFill;
            st.fillRed = textStyle.fillRed;
            st.fillGreen = textStyle.fillGreen;
            st.fillBlue = textStyle.fillBlue;
            st.margins = data->shapeMargins();
            // ODF text-box insets are stored as fo:padding on KoTextShapeData,
            // not in the generic shapeMargins() insets used by other shapes.
            // PowerPoint stores the same values as text FOPT insets; without
            // this, all text shifts by the original 0.1"/0.05" margin.
            if (auto *textData = qobject_cast<KoTextShapeData *>(data)) {
                st.margins.left = textData->leftPadding();
                st.margins.top = textData->topPadding();
                st.margins.right = textData->rightPadding();
                st.margins.bottom = textData->bottomPadding();
            }
            st.verticalAlignment = data->verticalAlignment();
            // draw:auto-grow-width in ODF comes from the PPT's no-wrap flag.
            // Carry it back so narrow text shapes keep their runs on one line.
            const KoTextShapeDataBase::ResizeMethod resize = data->resizeMethod();
            st.noWrap = resize == KoTextShapeDataBase::AutoGrowWidth
                        || resize == KoTextShapeDataBase::AutoGrowWidthAndHeight;
            st.autoGrow = resize == KoTextShapeDataBase::AutoGrowWidth
                          || resize == KoTextShapeDataBase::AutoGrowHeight
                          || resize == KoTextShapeDataBase::AutoGrowWidthAndHeight;
            if (!st.text.trimmed().isEmpty()) texts.append(st);
        }
    }

    // Recurse into every container, not just layers/groups.  draw:custom-shape
    // loads as a KoTosContainer (e.g. KoPathShape) whose text lives in a child
    // text shape; skipping these children would silently drop all that text.
    const int textsBefore = texts.size();
    const int imagesBefore = images.size();
    if (auto *container = dynamic_cast<KoShapeContainer *>(shape)) {
        for (KoShape *child : orderedShapes(container->shapes()))
            collectShapeContent(child, texts, images, shapes, nextOrder, insideNativeShape || nativeIndex >= 0);
    }
    if (nativeIndex >= 0) {
        for (int i = textsBefore; i < texts.size(); ++i)
            texts[i].nativeShape = nativeIndex;
    }
    const bool childContent = texts.size() > textsBefore || images.size() > imagesBefore;

    // Pure organisational containers have no visual representation of their
    // own and must never be rasterised, even when empty.
    if (dynamic_cast<KoShapeLayer *>(shape) || dynamic_cast<KoShapeGroup *>(shape)) return;

    if (handled) return;

    const QRectF bounds = shape->boundingRect();
    if (childContent) {
        // An autoshape (draw:custom-shape) owns its text through a child text
        // shape.  The text is exported separately above, but the shape's own
        // fill and outline have no native PPT representation: rasterise the
        // decoration with the text hidden so it is not painted twice.
        if (dynamic_cast<KoTosContainer *>(shape)) {
            addImageShape(shape, rasterizeShape(shape, bounds, true), QStringLiteral("png"), images, bounds, false, stackingOrder);
        }
        return;
    }

    // A leaf shape with no native representation (chart, table, autoshape,
    // connector, ...) has no editable equivalent in binary PPT.  Rasterise it
    // as a picture so its visual content survives the round-trip.
    addImageShape(shape, rasterizeShape(shape, bounds), QStringLiteral("png"), images, bounds, false, stackingOrder);
}

static SlideInfo pageContent(KoPAPageBase *page)
{
    SlideInfo info;
    int nextOrder = 0;

    // A solid drawing-page fill in the ODP becomes a KoColorBackground on the
    // page.  Carry it into the legacy PPT background shape's FOPT properties.
    auto *normalPage = dynamic_cast<KoPAPage *>(page);
    auto *master = normalPage ? normalPage->masterPage() : nullptr;
    const auto background = master && normalPage->displayMasterBackground() ? master->background() : page->background();
    if (auto *solid = dynamic_cast<KoColorBackground *>(background.data())) {
        const QColor color = solid->color();
        if (color.isValid() && color.alpha() > 0) {
            info.hasBackground = true;
            info.bgRed = quint8(color.red());
            info.bgGreen = quint8(color.green());
            info.bgBlue = quint8(color.blue());
        }
    } else if (background) {
        // Binary PPT has no portable ODF-style bitmap/gradient/hatch page
        // fill.  Preserve it as a full-slide picture; it is written before
        // the text boxes so the slide content stays visible.
        addBackgroundFill(background.data(), QRectF(QPointF(0, 0), page->size()), info.images);
    }

    if (master && normalPage->displayMasterShapes()) {
        for (KoShape *shape : orderedShapes(master->shapes())) {
            collectShapeContent(shape, info.texts, info.images, info.shapes, nextOrder);
        }
    }
    for (KoShape *shape : orderedShapes(page->shapes())) collectShapeContent(shape, info.texts, info.images, info.shapes, nextOrder);
    if (info.texts.isEmpty()) {
        SlideText empty;
        empty.rect = QRectF(0, 0, 100, 20);
        info.texts.append(empty);
    }

    // Speaker notes live on Stage's KPrPage, not on the generic page.  Reuse
    // the same text extraction as slide shapes so notes formatting survives.
    if (auto *stagePage = dynamic_cast<KPrPage *>(page)) {
        KPrNotes *notes = stagePage->pageNotes();
        KoShape *notesShape = notes ? notes->textShape() : nullptr;
        auto *notesData = notesShape ? qobject_cast<KoTextShapeDataBase *>(notesShape->userData()) : nullptr;
        QTextDocument *notesDoc = notesData ? notesData->document() : nullptr;
        if (notesDoc && !notesDoc->toPlainText().trimmed().isEmpty()) {
            info.hasNotes = true;
            info.notes = extractSlideText(notesDoc, QRectF());
        }

        // Slide transition / advance mode.  Only effects with an exact binary
        // PPT mapping are exported; unmapped effects stay at effectType=Cut.
        auto *data = dynamic_cast<KPrPageApplicationData *>(stagePage->applicationData());
        if (data) {
            const KPrPageTransition &tr = data->pageTransition();
            const KPrPageEffect *effect = data->pageEffect();
            const bool automatic = tr.type() != KPrPageTransition::Manual;
            if (automatic || effect) {
                info.hasTransition = true;
                info.manualAdvance = (tr.type() != KPrPageTransition::Automatic);
                info.autoAdvance = automatic;
                info.advanceMs = automatic ? quint32(qMax(0, tr.milliseconds())) : 0;
                if (effect) {
                    const auto mapped = PptTransition::fromOdf(effect->smilType(),
                                                                effect->smilSubType(),
                                                                effect->reverse());
                    info.effectType = mapped.effectType;
                    info.effectDirection = mapped.direction;
                    info.speed = PptTransition::speedFromSeconds(effect->duration() / 1000.0);
                }
            }
        }
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
    // ODF's style:text-position is imported into QTextCharFormat as a
    // vertical alignment plus an exact percentage property.  Prefer the
    // exact value so a -25% subscript does not become -30% on export.
    if (fmt.hasProperty(KoCharacterStyle::TextPosition)) {
        cf.hasPosition = true;
        cf.position = qint16(qBound(-100,
                                    qRound(fmt.property(KoCharacterStyle::TextPosition).toDouble()),
                                    100));
    } else {
        switch (fmt.verticalAlignment()) {
        case QTextCharFormat::AlignSuperScript:
            cf.hasPosition = true;
            cf.position = 30;
            break;
        case QTextCharFormat::AlignSubScript:
            cf.hasPosition = true;
            cf.position = -30;
            break;
        default:
            break;
        }
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

// Fill in the bullet/indent/spacing part of a ParaFmt from one text block.
// ODF list properties live on the block's QTextList; Calligra stores them via
// KoListLevelProperties, which we read back through its public API.
static void fillParaGeometry(const QTextBlock &block, ParaFmt &pf)
{
    const QTextBlockFormat bf = block.blockFormat();

    // Calligra stores paragraph spacing as custom format properties rather
    // than through QTextBlockFormat's margin accessors.  Read both forms so
    // ordinary QTextDocument blocks continue to work as well.
    auto lengthInPoints = [](const QVariant &value) -> qreal {
        if (!value.isValid()) return 0;
        if (value.userType() == QMetaType::QTextLength) {
            const QTextLength len = value.value<QTextLength>();
            return len.type() == QTextLength::FixedLength ? len.rawValue() : 0;
        }
        return value.toDouble();
    };
    // A zero margin must override the template's inherited spacing.
    pf.hasSpaceBefore = true;
    pf.hasSpaceAfter = true;
    pf.spaceBefore = qMax(0.0, lengthInPoints(bf.property(QTextFormat::BlockTopMargin)));
    pf.spaceAfter = qMax(0.0, lengthInPoints(bf.property(QTextFormat::BlockBottomMargin)));

    if (bf.hasProperty(KoParagraphStyle::PercentLineHeight)) {
        const int percent = qRound(bf.doubleProperty(KoParagraphStyle::PercentLineHeight));
        if (percent > 0) {
            pf.hasLineSpacing = true;
            pf.lineSpacingValue = percent;
        }
    } else if (bf.hasProperty(KoParagraphStyle::FixedLineHeight)) {
        const qreal points = bf.doubleProperty(KoParagraphStyle::FixedLineHeight);
        if (points > 0) {
            pf.hasLineSpacing = true;
            // Negative values encode absolute master units (points * 8).
            pf.lineSpacingValue = -qRound(points * 8);
        }
    } else if (bf.lineHeightType() == QTextBlockFormat::ProportionalHeight && bf.lineHeight() > 0) {
        pf.hasLineSpacing = true;
        pf.lineSpacingValue = bf.lineHeight();
    }

    QTextList *list = block.textList();
    if (!list) {
        // Plain paragraph: the legacy "indent" is the left margin plus the
        // first-line text indent (ODF fo:text-indent).
        const qreal textIndent = bf.textIndent();
        if (bf.leftMargin() > 0 || bf.rightMargin() > 0 || textIndent != 0) {
            pf.hasIndent = true;
            pf.leftMargin = bf.leftMargin();
            pf.indent = bf.leftMargin() + textIndent;
        }
        return;
    }

    const KoListLevelProperties llp = KoListLevelProperties::fromTextList(list);
    const int level = qMax(1, llp.level());
    const KoListStyle::LabelType label = llp.labelType();

    // Character bullets use native PPT paragraph properties. Numbered
    // paragraphs receive a text label during extraction.
    if (label == KoListStyle::BulletCharLabelType && llp.bulletCharacter().unicode() != 0) {
        pf.bulletLevel = level;
        pf.bulletChar = llp.bulletCharacter().unicode();

        const QSharedPointer<KoCharacterStyle> chars = llp.characterProperties();
        // ODF lists put the bullet size in the list-level text properties;
        // relativeBulletSize() always carries a default of 100 even when the
        // ODF used an absolute fo:font-size, so prefer the absolute value.
        if (chars && chars->fontPointSize() > 0 && chars->fontPointSize() <= 4000) {
            // Negative bulletSize encodes an absolute point size on import.
            pf.bulletHasSize = true;
            pf.bulletSizeValue = -qRound(chars->fontPointSize());
        } else {
            const int rel = llp.relativeBulletSize();
            if (rel >= 25 && rel <= 400) {
                pf.bulletHasSize = true;
                pf.bulletSizeValue = rel;
            }
        }
        if (chars) {
            const QString family = chars->fontFamily();
            if (!family.isEmpty()) {
                pf.bulletHasFont = true;
                pf.bulletFont = family;
            }
            const QColor col = chars->foreground().color();
            if (col.isValid()) {
                pf.bulletHasColor = true;
                pf.bulletRed = quint8(col.red());
                pf.bulletGreen = quint8(col.green());
                pf.bulletBlue = quint8(col.blue());
            }
        }

        // The importer computes:  text:space-before = pf.indent
        //                        text:min-label-width = pf.leftMargin - pf.indent
        // KoListLevelProperties stores these as indent() and minimumWidth().
        pf.hasIndent = true;
        pf.indent = llp.indent();
        pf.leftMargin = llp.indent() + llp.minimumWidth() + llp.minimumDistance();
    }
}

// Extract plain text plus per-run/per-paragraph formatting from a text shape.
static SlideText extractSlideText(QTextDocument *doc, const QRectF &rect)
{
    SlideText st;
    st.rect = rect;
    if (!doc) return st;

    // Empty paragraphs occupy space and can align text with pictures.
    // Keep them and carry their character formatting on the paragraph mark.
    QString raw;
    QVector<TextSpan> rawSpans;
    QVector<ParaFmt> rawParas;
    QVector<int> rawParaStarts;
    const QFont defaultFont = doc->defaultFont();
    const auto *inlineObjects = KoTextDocument(doc).inlineTextObjectManager();

    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        rawParaStarts.append(raw.size());
        ParaFmt pf;
        pf.alignment = alignmentFrom(block.blockFormat());
        fillParaGeometry(block, pf);
        if (QTextList *list = block.textList(); list
            && !block.blockFormat().boolProperty(KoParagraphStyle::UnnumberedListItem)
            && !block.blockFormat().boolProperty(KoParagraphStyle::IsListHeader)) {
            const auto level = KoListLevelProperties::fromTextList(list);
            if (level.labelType() == KoListStyle::NumberLabelType) {
                KoOdfNumberDefinition number;
                number.setFormatSpecification(level.numberFormat());
                number.setLetterSynchronization(level.letterSynchronization());
                const int explicitStart = block.blockFormat().intProperty(KoParagraphStyle::ListStartValue);
                const int index = explicitStart > 0 ? explicitStart : qMax(1, level.startValue()) + list->itemNumber(block);
                const QString label = number.formattedNumber(index);
                if (!label.isEmpty()) {
                    const QString prefix = level.listItemPrefix() + label + level.listItemSuffix() + QLatin1Char(' ');
                    TextSpan span;
                    span.start = raw.size();
                    span.length = prefix.size();
                    QTextCharFormat labelFormat = block.charFormat();
                    if (block.begin().fragment().isValid()) {
                        labelFormat.merge(block.begin().fragment().charFormat());
                    }
                    if (const auto style = level.characterProperties()) {
                        style->applyStyle(labelFormat, false);
                    }
                    span.fmt = charFormatFrom(labelFormat, defaultFont);
                    rawSpans.append(span);
                    raw += prefix;
                    pf.hasIndent = true;
                    pf.indent = level.indent();
                    pf.leftMargin = level.indent() + level.minimumWidth() + level.minimumDistance();
                }
            }
        }
        rawParas.append(pf);
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment frag = it.fragment();
            QString ft = frag.text();
            if (ft.isEmpty()) continue;
            QTextCharFormat fmt = block.charFormat();
            fmt.merge(frag.charFormat());
            if (inlineObjects && ft == QChar::ObjectReplacementCharacter) {
                const auto *object = inlineObjects->inlineTextObject(frag.charFormat());
                if (object && object->property("isCurrentPageNumber").toBool()) {
                    st.slideNumberPositions.append(raw.size());
                    ft = QStringLiteral("*");
                } else if (dynamic_cast<const KoTextMeta *>(object)) {
                    continue;
                } else if (const auto *variable = qobject_cast<const KoVariable *>(object)) {
                    ft = variable->value();
                }
            }
            ft.replace(QChar::LineSeparator, QLatin1Char('\v'));
            TextSpan span;
            span.start = raw.size();
            span.length = ft.size();
            span.fmt = charFormatFrom(fmt, defaultFont);
            rawSpans.append(span);
            raw += ft;
        }
        if (block.next().isValid()) {
            if (block.length() == 1) {
                TextSpan span;
                span.start = raw.size();
                span.length = 1;
                span.fmt = charFormatFrom(block.charFormat(), defaultFont);
                rawSpans.append(span);
            }
            raw += QLatin1Char('\r');
        }
    }

    st.text = raw;
    st.spans = rawSpans;
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

    for (qsizetype i = 0; i < rawParas.size(); ++i) {
        ParaFmt pf = rawParas.at(i);
        pf.start = rawParaStarts.at(i);
        const auto end = i + 1 < rawParaStarts.size() ? rawParaStarts.at(i + 1) : st.text.size();
        pf.length = end - pf.start;
        st.paras.append(pf);
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
                ei.rect = img.rect;
                images.append(ei);
                idx = images.size() - 1;
            }
            img.pib = idx + 1; // pib is 1-based
        }
    }
    return images;
}

// MSOBLIPTYPE values (distinct from the BLIP record instance signatures).
static quint8 blipTypeForSuffix(const QString &suffix)
{
    if (suffix == QLatin1String("wmf")) return 3;
    if (suffix == QLatin1String("emf")) return 2;
    if (suffix == QLatin1String("png")) return 6;
    return 5; // jpeg
}

// Build an OfficeArtFBSE record for the Dgg's BStoreContainer.
static Record buildFbse(const ExportedImage &img)
{
    Record fbse;
    fbse.ver = 2;
    const quint8 blipType = blipTypeForSuffix(img.suffix);
    fbse.inst = blipType;
    fbse.type = 0xF007;
    QByteArray p;
    p.append(char(blipType)); // btWin32
    p.append(char(blipType)); // btMacOS
    p += img.rgbUid;           // rgbUid (16 bytes)
    Record::appendU16(p, 0);   // tag
    Record::appendU32(p, img.recordSize); // size of the complete BLIP record
    Record::appendU32(p, 1);   // cRef
    Record::appendU32(p, img.offset); // foDelay = offset in Pictures stream
    p.append(char(0)); // unused1
    p.append(char(0)); // cbName
    p.append(char(0)); // unused2
    p.append(char(0)); // unused3
    fbse.payload = p;
    return fbse;
}

static QRect metafileBounds(const ExportedImage &img)
{
    const QByteArray &data = img.data;
    if (img.suffix == QLatin1String("emf") && data.size() >= 44 && readU32(data, 0) == 1
        && readU32(data, 40) == 0x464D4520) {
        const qint32 left = qint32(readU32(data, 8));
        const qint32 top = qint32(readU32(data, 12));
        const qint64 width = qint64(qint32(readU32(data, 16))) - left;
        const qint64 height = qint64(qint32(readU32(data, 20))) - top;
        if (width > 0 && height > 0 && width <= std::numeric_limits<int>::max()
            && height <= std::numeric_limits<int>::max()) {
            return QRect(left, top, int(width), int(height));
        }
    }
    if (img.suffix == QLatin1String("wmf") && data.size() >= 18) {
        const bool placeable = readU32(data, 0) == 0x9AC6CDD7;
        if (placeable && data.size() >= 40) {
            const qint16 left = qint16(readU16(data, 6));
            const qint16 top = qint16(readU16(data, 8));
            return QRect(left, top, qint16(readU16(data, 10)) - left, qint16(readU16(data, 12)) - top);
        }
        const int headerSize = readU16(data, 2) * 2;
        QPoint origin;
        for (qsizetype pos = qMax(18, headerSize); pos <= data.size() - 6;) {
            const quint32 words = readU32(data, pos);
            if (words < 3 || words > quint32((data.size() - pos) / 2)) break;
            const quint16 function = readU16(data, pos + 4);
            if (words >= 5) {
                const qint16 y = qint16(readU16(data, pos + 6));
                const qint16 x = qint16(readU16(data, pos + 8));
                if (function == 0x020B) origin = QPoint(x, y); // META_SETWINDOWORG
                if (function == 0x020C) return QRect(origin, QSize(x, y)).normalized(); // META_SETWINDOWEXT
            }
            pos += words * 2;
        }
    }
    return QRect(0, 0, qMax(1, qRound(img.rect.width())), qMax(1, qRound(img.rect.height())));
}

static QByteArray buildPicturesStream(QList<ExportedImage> &images)
{
    QByteArray picturesStream;
    for (ExportedImage &img : images) {
        Record blip;
        blip.ver = 0;
        QByteArray payload = img.rgbUid; // rgbUid1
        const bool isWmf = img.suffix == QLatin1String("wmf");
        const bool isEmf = img.suffix == QLatin1String("emf");
        if (isWmf || isEmf) {
            // OfficeArtMetafileHeader: cbSize, rcBounds, ptSize, cbsave,
            // compression (0 = DEFLATE), filter. Keeping the metafile
            // native preserves vector text in OLE object previews; rasterising
            // it would turn that text into pixels.
            // EMF is 0x3D4/0xF01A, WMF is 0x216/0xF01B.
            blip.inst = isWmf ? 0x0216 : 0x03D4;
            blip.type = isWmf ? 0xF01B : 0xF01A;
            // qCompress prepends a Qt-specific size word; OfficeArt stores
            // only the RFC 1950 stream. LibreOffice expects compressed BLIPs.
            const QByteArray compressed = qCompress(img.data).mid(4);
            QByteArray mh;
            mh.resize(34);
            const QRect bounds = metafileBounds(img);
            writeU32(mh, 0, quint32(img.data.size())); // cbSize
            writeU32(mh, 4, quint32(bounds.x()));      // rcBounds.left, in metafile coordinates
            writeU32(mh, 8, quint32(bounds.y()));      // rcBounds.top
            writeU32(mh, 12, quint32(bounds.x() + bounds.width())); // rcBounds.right (exclusive)
            writeU32(mh, 16, quint32(bounds.y() + bounds.height())); // rcBounds.bottom (exclusive)
            writeU32(mh, 20, quint32(qMax(1, qRound(img.rect.width() * 12700)))); // ptSize.x in EMUs
            writeU32(mh, 24, quint32(qMax(1, qRound(img.rect.height() * 12700)))); // ptSize.y in EMUs
            writeU32(mh, 28, quint32(compressed.size())); // cbSave
            mh[32] = char(0);                          // compression: DEFLATE
            mh[33] = char(0xFE);                       // filter: must be 0xFE
            payload += mh;
            payload += compressed;
        } else {
            const bool isPng = (img.suffix == QLatin1String("png"));
            blip.inst = isPng ? 0x06E0 : 0x046A;
            blip.type = isPng ? 0xF01E : 0xF01D;
            payload.append(char(0));
            payload += img.data;
        }
        blip.payload = payload;
        img.offset = quint32(picturesStream.size());
        const QByteArray record = blip.serialize();
        img.recordSize = quint32(record.size());
        picturesStream += record;
    }
    return picturesStream;
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
    if (img.embeddedObjectId) {
        writeU32(fsp.payload, 4, 0x0A10); // fOleShape, fHaveAnchor, fHaveSpt
    }
    sp.children.append(fsp);

    // FOPT with the pib property (opid 0x0104, encoded in 14+2 bits).
    Record fopt;
    fopt.ver = 3;
    fopt.inst = 1;
    fopt.type = 0xF00B;
    QByteArray foptData;
    foptData.append(char(0x04));                     // opid low byte
    foptData.append(char(0x41));                     // pib has the fBid flag
    Record::appendU32(foptData, quint32(img.pib));   // pib value
    if (img.embeddedObjectId) {
        appendU16(foptData, 0x010B); // pictureId points to the external object
        appendU32(foptData, img.embeddedObjectId);
        ++fopt.inst;
    }
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
    if (img.embeddedObjectId) {
        Record objectRef;
        objectRef.type = 0x0BC1;
        appendU32(objectRef.payload, img.embeddedObjectId);
        Record clientData;
        clientData.ver = 0x0F;
        clientData.type = 0xF011;
        clientData.children.append(objectRef);
        sp.children.append(clientData);
    }

    return sp;
}

// ---------------------------------------------------------------------------
// Document stream construction
// ---------------------------------------------------------------------------

// Build a TextPFException: a 32-bit PFMasks word followed by only the fields
// whose mask bit is set, in the exact order parseTextPFException() reads them.
// PFMasks bit positions (from MSO::parsePFMasks):
//   0 hasBullet, 1 bulletHasFont, 2 bulletHasColor, 3 bulletHasSize,
//   4 bulletFont, 5 bulletColor, 6 bulletSize, 7 bulletChar,
//   8 leftMargin, 10 indent, 11 align, 12 lineSpacing,
//   13 spaceBefore, 14 spaceAfter.
static QByteArray buildPFException(const ParaFmt &pf, const QHash<QString, quint16> &fonts)
{
    
    const bool hasBullet = pf.bulletLevel > 0 && pf.bulletChar != 0;
    const bool bulletFont = hasBullet && pf.bulletHasFont && fonts.contains(pf.bulletFont);
    const bool bulletColor = hasBullet && pf.bulletHasColor;
    const bool bulletSize = hasBullet && pf.bulletHasSize;
    // A plain paragraph with an explicit indent must use a non-zero level: the
    // importer ignores the indent at level 0.  Explicitly switch bullets off
    // in that case so the master's default body bullet is not inherited.
    const bool suppressBullet = !hasBullet && pf.hasIndent;
    const bool bulletFlags = hasBullet || suppressBullet || bulletFont || bulletColor || bulletSize;

    quint32 masks = 0;
    if (hasBullet || suppressBullet) masks |= (1u << 0);  // hasBullet
    if (bulletFont)   masks |= (1u << 1) | (1u << 4);  // bulletHasFont + bulletFont
    if (bulletColor)  masks |= (1u << 2) | (1u << 5);  // bulletHasColor + bulletColor
    if (bulletSize)   masks |= (1u << 3) | (1u << 6);  // bulletHasSize + bulletSize
    if (hasBullet)    masks |= (1u << 7);  // bulletChar
    if (pf.hasIndent) masks |= (1u << 8) | (1u << 10); // leftMargin + indent
    if (pf.alignment >= 0 && pf.alignment <= 3) masks |= (1u << 11);
    if (pf.hasLineSpacing) masks |= (1u << 12);
    if (pf.hasSpaceBefore) masks |= (1u << 13);
    if (pf.hasSpaceAfter) masks |= (1u << 14);

    QByteArray out;
    appendU32(out, masks);

    if (bulletFlags) {
        quint16 flags = 0;
        if (hasBullet)   flags |= 1;
        if (bulletFont)  flags |= 2;
        if (bulletColor) flags |= 4;
        if (bulletSize)  flags |= 8;
        appendU16(out, flags);
    }
    if (masks & (1u << 7))  appendI16(out, qint16(pf.bulletChar));
    if (masks & (1u << 4))  appendU16(out, fonts.value(pf.bulletFont));
    if (masks & (1u << 6))  appendI16(out, qint16(qBound(-4000, pf.bulletSizeValue, 400)));
    if (masks & (1u << 5)) {
        out.append(char(pf.bulletRed));
        out.append(char(pf.bulletGreen));
        out.append(char(pf.bulletBlue));
        out.append(char(0xFE)); // direct RGB
    }
    if (masks & (1u << 11)) appendU16(out, quint16(pf.alignment));

    // Positive spacing values are percentages of the line height; negative
    // values are absolute master units.  Use the absolute form for ODF pt.
    if (masks & (1u << 12)) appendI16(out, qint16(qBound(-32768, pf.lineSpacingValue, 32767)));
    if (masks & (1u << 13)) appendI16(out, qint16(-qBound(0, qRound(pf.spaceBefore * 8), 32767)));
    if (masks & (1u << 14)) appendI16(out, qint16(-qBound(0, qRound(pf.spaceAfter * 8), 32767)));
    if (masks & (1u << 8))
        appendU16(out, quint16(qBound(0, qRound(pf.leftMargin * 8), 65535)));
    if (masks & (1u << 10))
        appendU16(out, quint16(qBound(0, qRound(pf.indent * 8), 65535)));
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
    if (cf.hasPosition) masks |= (1u << 19);
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
    if (cf.hasPosition) appendU16(out, quint16(cf.position));
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
        // The legacy level is 0-based; ODF list levels are 1-based.  Plain
        // indented paragraphs also need a non-zero level (see buildPFException).
        appendU16(pf, quint16(p.hasIndent ? qMax(1, p.bulletLevel - 1) : qMax(0, p.bulletLevel - 1)));
        pf += buildPFException(p, fonts);
        pfSum += int(count);
    }
    if (st.paras.isEmpty()) {
        appendU32(pf, quint32(qMax(textLen + 1, 1)));
        appendU16(pf, 0);
        pf += buildPFException(ParaFmt(), fonts);
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

    // Collect every family used by text runs and by bullet glyphs.  Registering
    // bullet fonts here lets the importer resolve PF bulletFontRef indices.
    QStringList families;
    for (const SlideInfo &slide : slides) {
        for (const SlideText &st : slide.texts) {
            for (const TextSpan &sp : st.spans) {
                if (sp.fmt.hasFont && !sp.fmt.fontFamily.isEmpty())
                    families.append(sp.fmt.fontFamily);
            }
            for (const ParaFmt &p : st.paras) {
                if (p.bulletHasFont && !p.bulletFont.isEmpty())
                    families.append(p.bulletFont);
            }
        }
    }
    for (const QString &family : families) {
        if (map.contains(family) || next > 128) continue;
        Record entry;
        entry.ver = 0;
        entry.inst = next;
        entry.type = 0xFB7;
        entry.payload = fontEntityPayload(family);
        fc->children.append(entry);
        map.insert(family, next++);
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

// Encode a legacy OfficeArtFOPTE FixedPoint rotation.  The integral part is
// the signed high word and the fractional part is the low word (1/65536 deg).
static quint32 fixedPointFromDegrees(qreal degrees)
{
    qint16 whole = qint16(qFloor(degrees));
    quint16 fraction = quint16(qRound((degrees - whole) * 65536.0));
    if (fraction == 0x10000) {
        ++whole;
        fraction = 0;
    }
    return (quint32(quint16(whole)) << 16) | fraction;
}

static quint32 shapeFopt(Record *shape, quint16 opid)
{
    if (!shape) return 0;
    const Record *fopt = findRecord(shape->children, 0xF00B);
    if (!fopt) return 0;
    const QByteArray &pl = fopt->payload;
    for (int off = 0; off + 6 <= pl.size(); off += 6) {
        if ((readU16(pl, off) & 0x3FFF) == opid) return readU32(pl, off + 2);
    }
    return 0;
}

// Set a simple OfficeArtFOPTE property, keeping the record's entries ordered
// by opid as readers expect.
static void setShapeFopt(Record *shape, quint16 opid, quint32 value)
{
    if (!shape) return;
    Record *fopt = findRecord(shape->children, 0xF00B);
    if (!fopt) return;

    QVector<QPair<quint16, quint32>> entries;
    const QByteArray &pl = fopt->payload;
    for (int off = 0; off + 6 <= pl.size(); off += 6) {
        const quint16 op = readU16(pl, off);
        if ((op & 0x3FFF) == opid) continue;
        entries.append(qMakePair(op, readU32(pl, off + 2)));
    }
    entries.append(qMakePair(opid, value));
    std::sort(entries.begin(), entries.end(),
              [](const QPair<quint16, quint32> &a, const QPair<quint16, quint32> &b) {
                  return (a.first & 0x3FFF) < (b.first & 0x3FFF);
              });

    QByteArray out;
    for (const auto &entry : entries) {
        appendU16(out, entry.first);
        appendU32(out, entry.second);
    }
    fopt->payload = out;
    fopt->inst = quint16(entries.size());
}

// Clone the template's text-bearing shape container and turn it into a real
// preset autoshape.  The template already carries the client text records, so
// text placed in the shape remains as editable as text in a text box.
static Record buildNativeShape(const SlideShape &native, const Record &tpl, quint32 spid)
{
    Record sp = tpl;

    if (Record *fsp = findRecord(sp.children, 0xF00A)) {
        fsp->inst = native.spt;
        fsp->payload = QByteArray(8, '\0');
        writeU32(fsp->payload, 0, spid);
        // fHaveAnchor | fHaveSpt, plus mirroring for diagonal lines.
        quint32 flags = 0x0200 | 0x0800;
        if (native.flipH) flags |= 0x0040;
        if (native.flipV) flags |= 0x0080;
        writeU32(fsp->payload, 4, flags);
    }

    setShapeFopt(&sp, 0x0180, native.hasFill ? 0u : 9u); // FillStyle::fillType
    setShapeFopt(&sp, 0x01BF, (1u << 20) | (native.hasFill ? (1u << 4) : 0u));
    if (native.hasFill) {
        setShapeFopt(&sp, 0x0181, quint32((quint32(native.fillBlue) << 16)
                                          | (quint32(native.fillGreen) << 8)
                                          | native.fillRed));
    }
    setShapeFopt(&sp, 0x01CD, 0); // LineStyle::lineStyle = solid
    setShapeFopt(&sp, 0x01C0, quint32((quint32(native.lineBlue) << 16)
                                      | (quint32(native.lineGreen) << 8)
                                      | native.lineRed));
    setShapeFopt(&sp, 0x01CB, native.lineWidth);
    setShapeFopt(&sp, 0x01FF, (1u << 19) | (native.hasLine ? (1u << 3) : 0u));
    for (int i = 0; i < native.adjustments.size() && i < 8; ++i) {
        setShapeFopt(&sp, quint16(0x0147 + i), quint32(native.adjustments.at(i)));
    }
    setShapeFopt(&sp, 0x0004, fixedPointFromDegrees(native.rotation));
    patchShapeAnchor(findRecord(sp.children, 0xF010), native.rect);

    // The template's sample text must not leak into an empty autoshape.
    if (Record *cd = findRecord(sp.children, 0xF00D)) {
        if (Record *textAtom = findRecord(cd->children, 0x0FA0)) textAtom->payload.clear();
        if (Record *styleAtom = findRecord(cd->children, 0x0FA1)) styleAtom->payload = emptyStyleAtomPayload();
    }
    return sp;
}

// The tiny.ppt template's placeholder text shapes carry a green fill and a
// dark-green outline.  Those are template decorations, not ODF properties, so
// every exported text box inherited them. Explicitly disable the fill and
// line: a background fill still paints the slide colour over background images.
static void neutralizeTextShapeStyle(Record *shape)
{
    if (!shape) return;
    setShapeFopt(shape, 0x0180, 9); // FillStyle::fillType
    const quint32 fillBits = shapeFopt(shape, 0x01BF);
    setShapeFopt(shape, 0x01BF, (fillBits | (1u << 20)) & ~(1u << 4));
    const quint32 lineBits = shapeFopt(shape, 0x01FF);
    setShapeFopt(shape, 0x01FF,
                 (lineBits | (1u << 19)) & ~(1u << 3)); // fUsefLine set, fLine clear
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

// The template's TextRulerAtom (0x0FA6) and DefaultRulerAtom (0x0FAB) can
// override the left margin and indent stored in a TextPFException (the
// importer prefers ruler values when present).  Set those ruler values to -1
// (0xFFFF) so the importer ignores them and uses the per-paragraph values we
// write.  The payload layout is left untouched so all offsets stay valid.
static void neutralizeTextRulerMargins(Record &r)
{
    for (Record &child : r.children) neutralizeTextRulerMargins(child);
    if ((r.type != 0x0FA6 && r.type != 0x0FAB) || r.payload.size() < 4) return;

    const quint16 flags = readU16(r.payload, 0);
    int p = 4; // flags + reserved2
    if (flags & (1u << 1)) p += 2; // cLevels
    if (flags & (1u << 0)) p += 2; // defaultTabSize
    if (flags & (1u << 2)) {       // tabStops: count followed by count entries
        if (p + 2 > r.payload.size()) return;
        const quint16 count = readU16(r.payload, p);
        p += 2 + int(count) * 4;
    }
    for (int level = 0; level < 5; ++level) {
        if (flags & (1u << (3 + level))) { // leftMarginN
            if (p + 2 > r.payload.size()) return;
            writeU16(r.payload, p, 0xFFFF);
            p += 2;
        }
        if (flags & (1u << (8 + level))) { // indentN
            if (p + 2 > r.payload.size()) return;
            writeU16(r.payload, p, 0xFFFF);
            p += 2;
        }
    }
}

// A per-slide NotesContainer is built from a real PowerPoint-generated
// template.  The only per-slide fields are the NotesAtom's slideIdRef and the
// notes body shape's TextContainer (TextCharsAtom + StyleTextPropAtom).  Shape
// IDs are made unique across slides so the drawing object table stays valid.
static Record buildNotesContainer(const SlideText &notes, int slideIndex,
                                  const QHash<QString, quint16> &fonts)
{
    QList<Record> list = parseRecords(QByteArray::fromRawData(
        reinterpret_cast<const char *>(kNotesContainerTemplate), kNotesContainerTemplateLen), 0,
        kNotesContainerTemplateLen);
    Record container = list.isEmpty() ? Record() : list.first();

    // NotesAtom: the id that SlideAtom::notesIdRef must point at.
    if (Record *atom = findRecord(container.children, 0x03F1); atom && atom->payload.size() >= 4) {
        writeU32(atom->payload, 0, 256 + quint32(slideIndex));
    }

    // Give every shape in the notes drawing a unique spid.  The F00A header's
    // inst field encodes the shape type (msospt) and must be left untouched.
    int spid = 40000 + slideIndex * 8;
    for (Record *fsp : findAllRecords(container, 0xF00A)) {
        if (fsp->payload.size() >= 4) writeU32(fsp->payload, 0, quint32(spid));
        ++spid;
    }

    // Find the notes body shape's client textbox (0xF00D containing 0x0F9F)
    // and inject the actual notes text and format runs.
    for (Record *cd : findAllRecords(container, 0xF00D)) {
        if (!findRecord(cd->children, 0x0F9F)) continue;
        Record textAtom;
        textAtom.ver = 0;
        textAtom.type = 0x0FA0;
        textAtom.payload = toUtf16(notes.text);
        Record styleAtom;
        styleAtom.ver = 0;
        styleAtom.type = 0x0FA1;
        styleAtom.payload = writeStyleAtomPayload(notes, fonts);
        cd->children.append(textAtom);
        cd->children.append(styleAtom);
        break;
    }
    return container;
}

// Patch a cloned slide's background shape to a solid colour.  In binary PPT
// the slide background is a dedicated OfficeArtSpContainer (0xF004) held
// directly under the Drawing's OfficeArtDgContainer (0xF002), alongside the
// 0xF003 group shape.  Its OfficeArtFOPT (0xF00B) stores fill properties as a
// flat list of 6-byte FOPTE entries in ascending opid order; the number of
// entries lives in the record header's inst field.  Adding FillType (0x0180,
// msofillSolid) and FillColor (0x0181) makes the importer emit draw:fill /
// draw:fill-color on the drawing-page style.
static void setSlideBackground(Record &slide, quint8 r, quint8 g, quint8 b)
{
    QList<Record *> drawings = findAllRecords(slide, 0xF002);
    if (drawings.isEmpty()) return;

    Record *bgShape = nullptr;
    for (Record &child : drawings.first()->children) {
        if (child.type == 0xF004) { bgShape = &child; break; }
    }
    if (!bgShape) return;
    Record *fopt = findRecord(bgShape->children, 0xF00B);
    if (!fopt) return;

    QList<QPair<quint16, quint32>> entries;
    QByteArray &pl = fopt->payload;
    for (int off = 0; off + 6 <= pl.size(); off += 6) {
        const quint16 op = readU16(pl, off);
        const quint16 opid = op & 0x3FFF;
        if (opid != 0x0180 && opid != 0x0181)
            entries.append(qMakePair(op, readU32(pl, off + 2)));
    }
    entries.append(qMakePair(quint16(0x0180), quint32(0))); // msofillSolid
    const quint32 color = quint32(r) | (quint32(g) << 8) | (quint32(b) << 16);
    entries.append(qMakePair(quint16(0x0181), color));

    std::sort(entries.begin(), entries.end(),
              [](const QPair<quint16, quint32> &a, const QPair<quint16, quint32> &b) {
                  return (a.first & 0x3FFF) < (b.first & 0x3FFF);
              });

    pl.clear();
    for (const auto &entry : entries) {
        appendU16(pl, entry.first);
        appendU32(pl, entry.second);
    }
    fopt->inst = quint16(entries.size());
}

// Reassign document-local object and persist IDs while retaining the OLE
// payload bytes and the application's original embedding metadata.
static QList<QByteArray> addEmbeddedObjects(QList<Record> &document, QList<SlideInfo> &slides, int persistBase)
{
    QList<QByteArray> storages;
    if (document.isEmpty() || document.first().children.isEmpty()) {
        return storages;
    }

    Record objects;
    objects.ver = 0x0F;
    objects.type = 0x0409;
    Record seed;
    seed.type = 0x040A;
    objects.children.append(seed);
    for (SlideInfo &slide : slides) {
        for (SlideImage &image : slide.images) {
            const QByteArray &original = image.embeddedObject;
            if (original.size() < 16 || readU16(original, 0) != 0x000F || readU16(original, 2) != 0x0FCC) {
                continue;
            }
            const quint32 metadataSize = readU32(original, 4);
            if (metadataSize > original.size() - 16 || metadataSize > std::numeric_limits<int>::max() - 8) {
                continue;
            }
            const int storageOffset = int(metadataSize) + 8;
            if (readU16(original, storageOffset + 2) != 0x1011
                || readU32(original, storageOffset + 4) != original.size() - storageOffset - 8) {
                continue;
            }

            // The embed container contains only atoms. Reject nested records
            // before parsing data supplied by an ODP package.
            int end = 8;
            int objectAtom = -1;
            while (end <= storageOffset - 8) {
                const quint32 size = readU32(original, end + 4);
                if ((readU16(original, end) & 0x000F) == 0x000F || size > quint32(storageOffset - end - 8)) {
                    break;
                }
                if (readU16(original, end + 2) == 0x0FC3 && size == 24) {
                    objectAtom = end;
                }
                end += 8 + int(size);
            }
            if (end != storageOffset || objectAtom < 0) {
                continue;
            }

            QByteArray metadata = original.left(storageOffset);
            image.embeddedObjectId = quint32(storages.size()) + 1;
            writeU32(metadata, objectAtom + 16, image.embeddedObjectId);
            writeU32(metadata, objectAtom + 24, persistBase + quint32(storages.size()));
            objects.children.append(parseRecords(metadata, 0, metadata.size()).constFirst());
            storages.append(original.mid(storageOffset));
        }
    }
    if (!storages.isEmpty()) {
        appendU32(objects.children.first().payload, quint32(storages.size()));
        document.first().children.insert(1, objects);
    }
    return storages;
}

static QByteArray buildDocument(QList<SlideInfo> &slides, const QList<ExportedImage> &images,
                                const QSizeF &slideSize)
{
    QList<Record> doc = parseRecords(QByteArray::fromRawData(
        reinterpret_cast<const char *>(kTemplateDocument), kTemplateDocumentLen), 0, kTemplateDocumentLen);

    for (Record &r : doc) neutralizeTextRulerMargins(r);

    const int embeddedPersistBase = 5 + slides.size() + std::count_if(slides.cbegin(), slides.cend(), [](const SlideInfo &slide) {
        return slide.hasNotes;
    });
    const QList<QByteArray> embeddedStorages = addEmbeddedObjects(doc, slides, embeddedPersistBase);

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
                bstore->children.append(buildFbse(images[i]));
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

    // Locate the shape tree (0xF003) inside a slide container.  Text shapes
    // are addressed by index into its children list rather than by Record*:
    // appending cloned shapes reallocates that QList and invalidates any
    // pointers held across the append, which previously produced empty
    // 0xF004 records that make the file unreadable.
    auto findShapeGroup = [](Record &slide) -> Record * {
        Record *dg = findRecord(slide.children, 0x040C);
        if (!dg) return nullptr;
        Record *sp = findRecord(dg->children, 0xF002);
        if (!sp) return nullptr;
        return findRecord(sp->children, 0xF003);
    };

    // Build output slide containers by cloning the template.
    QList<QByteArray> slideBlobs;
    for (int si = 0; si < slides.size(); ++si) {
        const SlideInfo &info = slides.at(si);
        Record slide = *tplSlide;

        // Link the slide to its notes container (0 when there are no notes).
        // Also clear fMasterBackground when the ODP defines its own solid
        // background; the importer otherwise inherits the master's fill and
        // ignores the slide's background shape entirely.
        if (Record *sa = findRecord(slide.children, 0x03EF); sa && sa->payload.size() >= 20) {
            writeU32(sa->payload, 16, info.hasNotes ? (256 + quint32(si)) : 0);
            if (info.hasBackground)
                sa->payload[20] = char(quint8(sa->payload[20]) & ~0x04);
        }

        // Patch the slide's background shape with the ODP solid fill.
        if (info.hasBackground)
            setSlideBackground(slide, info.bgRed, info.bgGreen, info.bgBlue);

        // Patch the template's SlideShowSlideInfoAtom (0x03F9) with the
        // transition, advance mode and speed from the ODP page.
        if (Record *si = findRecord(slide.children, 0x03F9); si && si->payload.size() >= 13) {
            QByteArray &p = si->payload;
            writeU32(p, 0, info.advanceMs);
            p[8] = char(info.effectDirection);
            p[9] = char(info.effectType);
            p[10] = char(info.manualAdvance ? 1 : 0); // fManualAdvance
            p[11] = char(info.autoAdvance ? 4 : 0);   // fAutoAdvance
            p[12] = char(info.speed);
        }

        Record *group = findShapeGroup(slide);
        QHash<int, int> stackingOrders;

        // Locate the template's text shapes as indices into group->children.
        // Indices stay valid when clones are appended to that same list.
        QList<int> textShapeIndices;
        if (group) {
            for (Record *cd : findAllRecords(slide, 0xF00D)) {
                if (!findRecord(cd->children, 0x0FA0)) continue;
                Record *shape = findShapeForClientData(&slide, cd);
                if (!shape) continue;
                for (int i = 0; i < group->children.size(); ++i) {
                    if (&group->children[i] == shape) {
                        if (!textShapeIndices.contains(i)) textShapeIndices.append(i);
                        break;
                    }
                }
            }
        }
        const int templateShapeCount = textShapeIndices.size();
        const int cloneSource = textShapeIndices.isEmpty() ? -1 : textShapeIndices.last();

        // Write preset autoshapes as native PowerPoint objects.  They are
        // cloned from the template text shape so their client text records
        // can hold the shape's own text without a second text box.
        QList<int> nativeShapeIndices;
        if (group && cloneSource >= 0) {
            for (int i = 0; i < info.shapes.size(); ++i) {
                group->children.append(buildNativeShape(
                    info.shapes.at(i), group->children.at(cloneSource),
                    quint32(1000 + si * 200 + i)));
                nativeShapeIndices.append(group->children.size() - 1);
                stackingOrders.insert(nativeShapeIndices.last(), info.shapes.at(i).stackingOrder);
            }
        }

        int plainTextCount = 0;
        for (int t = 0; t < info.texts.size(); ++t) {
            const SlideText &st = info.texts.at(t);
            int index = -1;
            bool isNative = false;
            if (st.nativeShape >= 0 && st.nativeShape < nativeShapeIndices.size()) {
                index = nativeShapeIndices.at(st.nativeShape);
                isNative = true;
            } else if (plainTextCount < templateShapeCount) {
                index = textShapeIndices.at(plainTextCount);
                ++plainTextCount;
            } else if (group && cloneSource >= 0) {
                // Clone the last template text shape for extra text boxes.
                Record clone = group->children.at(cloneSource);
                group->children.append(clone);
                index = group->children.size() - 1;
                ++plainTextCount;
            }
            if (index < 0) break;
            if (!isNative) stackingOrders.insert(index, st.stackingOrder);

            Record &shape = group->children[index];
            Record *cd = findRecord(shape.children, 0xF00D);
            if (!cd) continue;
            Record *textAtom = findRecord(cd->children, 0x0FA0);
            Record *styleAtom = findRecord(cd->children, 0x0FA1);
            if (!textAtom || !styleAtom) continue;

            textAtom->payload = toUtf16(st.text);
            styleAtom->payload = writeStyleAtomPayload(st, fonts);
            auto fieldIndex = styleAtom - cd->children.data() + 1;
            cd->children.removeIf([](const Record &record) { return record.type == 0x0FD8; });
            for (const int position : st.slideNumberPositions) {
                Record slideNumber;
                slideNumber.type = 0x0FD8;
                appendU32(slideNumber.payload, quint32(position));
                cd->children.insert(fieldIndex++, slideNumber);
            }
            if (!isNative) {
                patchShapeAnchor(findRecord(shape.children, 0xF010), st.rect);
                neutralizeTextShapeStyle(&shape);
                if (st.hasFill) {
                    const quint32 fillBits = shapeFopt(&shape, 0x01BF);
                    setShapeFopt(&shape, 0x01BF, fillBits | (1u << 20) | (1u << 4));
                    setShapeFopt(&shape, 0x0180, 0); // msofillSolid
                    setShapeFopt(&shape, 0x0181, quint32((quint32(st.fillBlue) << 16)
                                                          | (quint32(st.fillGreen) << 8)
                                                          | st.fillRed));
                }
                // The template shapes are msosptNil (0x0fff).  Calligra's PPT
                // importer intentionally clears text insets for every shape
                // except real text boxes, so export ODF text frames as
                // msosptTextBox (0x00ca) to preserve their fo:padding values.
                if (Record *fsp = findRecord(shape.children, 0xF00A))
                    fsp->inst = 0x00ca;
                setShapeFopt(&shape, 0x0004, fixedPointFromDegrees(st.rotation));
            }
            setShapeFopt(&shape, 0x0085, st.noWrap ? 2u : 0u);
            setShapeFopt(&shape, 0x0081, quint32(qRound(st.margins.left * 12700)));
            setShapeFopt(&shape, 0x0082, quint32(qRound(st.margins.top * 12700)));
            setShapeFopt(&shape, 0x0083, quint32(qRound(st.margins.right * 12700)));
            setShapeFopt(&shape, 0x0084, quint32(qRound(st.margins.bottom * 12700)));
            quint32 anchor = msoanchorTop;
            if (st.verticalAlignment.testFlag(Qt::AlignVCenter)) {
                anchor = msoanchorMiddle;
            } else if (st.verticalAlignment.testFlag(Qt::AlignBottom)) {
                anchor = msoanchorBottom;
            }
            setShapeFopt(&shape, 0x0087, anchor);
            // TextBooleanProperties (0xBF): fFitShapeToText (bit 1) and its
            // validity flag fUsefFitShapeToText (bit 17) preserve ODF's
            // draw:auto-grow-width/height across the binary round-trip.
            const quint32 textBits = shapeFopt(&shape, 0x00BF);
            const quint32 fitMask = (1u << 1) | (1u << 17);
            setShapeFopt(&shape, 0x00BF, st.autoGrow ? (textBits | fitMask)
                                                     : (textBits & ~fitMask));
        }

        // The template slide carries its own sample text.  Clear any text
        // shapes we did not fill above, otherwise that sample text leaks
        // into the exported slide.  They must also have their template fill
        // and outline neutralized: an empty shape still paints the template's
        // green rectangle and dark-green border when it is re-imported.
        for (int t = plainTextCount; t < templateShapeCount; ++t) {
            Record &shape = group->children[textShapeIndices.at(t)];
            Record *cd = findRecord(shape.children, 0xF00D);
            if (!cd) continue;
            Record *textAtom = findRecord(cd->children, 0x0FA0);
            Record *styleAtom = findRecord(cd->children, 0x0FA1);
            if (!textAtom || !styleAtom) continue;
            textAtom->payload.clear();
            styleAtom->payload = emptyStyleAtomPayload();
            neutralizeTextShapeStyle(&shape);
        }

        // Restore the original mixed shape order. A background exported as a
        // picture becomes an ordinary image on reload; appending every image
        // above the text would cover the entire slide on its next save.
        if (!info.images.isEmpty() && group) {
            for (const SlideImage &img : info.images) {
                stackingOrders.insert(group->children.size(), img.behindText ? -1 : img.stackingOrder);
                group->children.append(buildPictureShape(img));
            }
        }
        if (group) {
            QList<int> indices;
            for (int i = 0; i < group->children.size(); ++i) indices.append(i);
            std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) {
                return stackingOrders.value(a, std::numeric_limits<int>::min())
                    < stackingOrders.value(b, std::numeric_limits<int>::min());
            });
            QList<Record> ordered;
            ordered.reserve(indices.size());
            for (int i : indices) ordered.append(std::move(group->children[i]));
            group->children = std::move(ordered);
        }
        slideBlobs.append(slide.serialize());
    }

    // Build one NotesContainer for every slide that has speaker notes.  The
    // persist IDs are allocated after the slides and handout so they do not
    // collide with the template's fixed IDs (1..5 and 6.. for extra slides).
    const int notesPersistBase = 5 + slides.size();
    QList<QByteArray> notesBlobs;
    QList<quint32> notesPersistIds;
    int notesCount = 0;
    for (int si = 0; si < slides.size(); ++si) {
        if (!slides.at(si).hasNotes) {
            notesBlobs.append(QByteArray());
            notesPersistIds.append(0);
            continue;
        }
        notesBlobs.append(buildNotesContainer(slides.at(si).notes, si, fonts).serialize());
        // Persist IDs must be contiguous starting at notesPersistBase: the
        // persist directory below registers notesPersistBase + i for i in
        // [0, nNotes).  notesBlobs has one entry per slide, so using its size
        // here would yield notesPersistBase + si and leave notes on later
        // slides with an ID that is absent from the directory, making the
        // importer drop them.
        notesPersistIds.append(quint32(notesPersistBase + notesCount));
        ++notesCount;
    }

    // Replace the template's single NotesPersistAtom with one per notes slide.
    if (Record *notesList = findRecord(doc, 0x0FF0, 2); notesList && !notesList->children.isEmpty()) {
        const Record tplNotesPersist = notesList->children.first();
        notesList->children.clear();
        for (int si = 0; si < slides.size(); ++si) {
            if (!slides.at(si).hasNotes) continue;
            Record np = tplNotesPersist;
            if (np.payload.size() >= 16) {
                writeU32(np.payload, 0, quint32(notesPersistIds.at(si)));
                writeU32(np.payload, 12, 256 + quint32(si));
            }
            notesList->children.append(np);
        }
    }

    // Assemble top-level stream: document container, master, notes master,
    // slides..., handout, per-slide notes containers, persist directory,
    // user edit.  We keep the template's static records in order and
    // substitute slides where the template slide was.
    QList<QByteArray> topBlobs;
    QList<quint16> topTypes;
    QList<int> notesBlobIndices;
    int handoutSeen = 0;
    for (Record &r : doc) {
        if (r.type == 0x03EE) {
            for (const QByteArray &b : slideBlobs) {
                topBlobs.append(b);
                topTypes.append(0x03EE);
            }
        } else if (r.type == 0x1772 || r.type == 0x0FF5) {
            // rebuilt below
        } else {
            topBlobs.append(r.serialize());
            topTypes.append(r.type);
            if (r.type == 0x03F0 && ++handoutSeen == 2) {
                // The second 0x03F0 is the handout master; append the
                // per-slide notes containers right after it.
                for (const QByteArray &b : notesBlobs) {
                    if (b.isEmpty()) continue;
                    notesBlobIndices.append(topBlobs.size());
                    topBlobs.append(b);
                    topTypes.append(0x03F0);
                }
            }
        }
    }

    const auto firstStorageIndex = topBlobs.size();
    topBlobs.append(embeddedStorages);

    // Compute offsets of each top-level record.
    QList<quint32> offsets;
    quint32 pos = 0;
    for (const QByteArray &b : topBlobs) {
        offsets.append(pos);
        pos += quint32(b.size());
    }

    int firstSlideBlobIndex = topTypes.indexOf(0x03EE);
    if (firstSlideBlobIndex < 0) firstSlideBlobIndex = 3;
    int handoutIndex = -1;
    int notesSeen = 0;
    for (int k = 0; k < topTypes.size(); ++k) {
        if (topTypes.at(k) != 0x03F0) continue;
        if (++notesSeen == 2) { handoutIndex = k; break; }
    }

    // Persist directory.  The template's persist IDs are:
    //   1=document, 2=master, 3=notes master, 4=first slide, 5=handout,
    //   6..=additional slides.  Notes containers use IDs after all slides.
    const int nSlides = slideBlobs.size();
    const int nNotes = notesBlobIndices.size();
    const int maxPersistId = embeddedPersistBase + embeddedStorages.size() - 1;
    QList<quint32> persistOffsets(maxPersistId + 1, 0);
    persistOffsets[1] = 0;                                        // document
    persistOffsets[2] = offsets.value(1);                         // master
    persistOffsets[3] = offsets.value(2);                         // notes master
    persistOffsets[4] = offsets.value(firstSlideBlobIndex);       // first slide
    persistOffsets[5] = offsets.value(handoutIndex);              // handout
    for (int i = 1; i < nSlides; ++i) {
        persistOffsets[5 + i] = offsets.value(firstSlideBlobIndex + i); // extra slides: 6,7,...
    }
    for (int i = 0; i < nNotes; ++i) {
        persistOffsets[notesPersistBase + i] = offsets.value(notesBlobIndices.at(i));
    }

    for (qsizetype i = 0; i < embeddedStorages.size(); ++i) {
        persistOffsets[embeddedPersistBase + i] = offsets.at(firstStorageIndex + i);
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
    writeU32(uePayload, 20, quint32(maxPersistId + 1));  // persistIdSeed
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

    QList<ExportedImage> images = registerImages(slides);
    const QByteArray picturesStream = buildPicturesStream(images);
    const QSizeF slideSize = document->pages().first()->size();
    const QByteArray documentStream = buildDocument(slides, images, slideSize);
    if (documentStream.isEmpty()) return StupidError;

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
