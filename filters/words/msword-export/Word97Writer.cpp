/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Adeel <adeel@example.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "Word97Writer.h"
#include "CFBWriter.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDebug>
#include <QIODevice>
#include <QList>
#include <QString>
#include <QUuid>

#include <cstring>

using namespace Word97;

namespace {

// FIB fc/lcb pair indices, not byte offsets.
constexpr int FIB_INDEX_STSHF = 1;
constexpr int FIB_INDEX_PLCFSED = 6;
constexpr int FIB_INDEX_PLCFBTECHPX = 12;
constexpr int FIB_INDEX_PLCFBTEPAPX = 13;
constexpr int FIB_INDEX_STTBFFFN = 15;
constexpr int FIB_INDEX_DOP = 31;
constexpr int FIB_INDEX_STTBFASSOC = 32;
constexpr int FIB_INDEX_CLX = 33;
constexpr int FIB_INDEX_PLCFSPAMOM = 40;
constexpr int FIB_INDEX_PLCFSPA_HDR = 41;
constexpr int FIB_INDEX_DGGINFO = 50;

constexpr int FIB_SIZE = 1468;
constexpr int TEXT_OFFSET = 4096;

void put16(QByteArray &a, int off, quint16 v)
{
    if (off < 0 || off + 2 > a.size()) {
        return;
    }
    a[off] = char(v & 0xff);
    a[off + 1] = char((v >> 8) & 0xff);
}

void put32(QByteArray &a, int off, quint32 v)
{
    if (off < 0 || off + 4 > a.size()) {
        return;
    }
    a[off] = char(v & 0xff);
    a[off + 1] = char((v >> 8) & 0xff);
    a[off + 2] = char((v >> 16) & 0xff);
    a[off + 3] = char((v >> 24) & 0xff);
}

void append8(QByteArray &a, quint8 v)
{
    a.append(char(v));
}

void append16(QByteArray &a, quint16 v)
{
    append8(a, quint8(v & 0xff));
    append8(a, quint8((v >> 8) & 0xff));
}

void append32(QByteArray &a, quint32 v)
{
    append16(a, quint16(v & 0xffff));
    append16(a, quint16((v >> 16) & 0xffff));
}

void appendEscherRecord(QByteArray &out, quint8 version, quint16 instance, quint16 type, const QByteArray &body)
{
    append16(out, quint16((quint16(instance) << 4) | version));
    append16(out, type);
    append32(out, quint32(body.size()));
    out.append(body);
}

void setFcLcb(QByteArray &fib, int index, quint32 fc, quint32 lcb)
{
    const int off = 154 + index * 8;
    put32(fib, off, fc);
    put32(fib, off + 4, lcb);
}

QByteArray buildFib(quint32 ccpText, quint32 cbMac, quint32 fcMac)
{
    QByteArray fib(FIB_SIZE, '\0');
    put16(fib, 0, 0xA5EC);                   // wIdent
    put16(fib, 2, 0x00C1);                   // nFib: Word 97
    put16(fib, 6, 0x0409);                   // lid: en-US
    put16(fib, 10, 0x12F0); // cQuickSaves=15, fWhichTblStm=1, fExtChar=1, fHasPic=1
    put16(fib, 12, 0x00BF);                  // nFibBack

    put32(fib, 24, TEXT_OFFSET);             // fcMin
    put32(fib, 28, fcMac);                   // fcMac

    put16(fib, 32, 14);                      // csw
    put16(fib, 60, 0x0409);                  // FibRgW97.lidFE
    put16(fib, 62, 22);                      // cslw
    put32(fib, 64, cbMac);                   // FibRgLw97.cbMac
    put32(fib, 76, ccpText);                 // FibRgLw97.ccpText
    put16(fib, 152, 164);                    // cbRgFcLcb
    return fib;
}

/**
 * Minimal, standards-shaped stylesheet.  wv2 requires a non-empty STSHF and a
 * valid fixed-index "Normal" style.  The remaining reserved slots are empty.
 */
QByteArray buildStshf()
{
    QByteArray stsh;

    // STSHI: cbStshi=18, cstd=15, cbSTDBaseInFile=10.
    append16(stsh, 0);                       // patched below
    const int stshiStart = stsh.size();
    append16(stsh, 15);                      // cstd
    append16(stsh, 10);                      // cbSTDBaseInFile
    append16(stsh, 0);                       // flags
    append16(stsh, 91);                      // stiMaxWhenSaved
    append16(stsh, 15);                      // istdMaxFixedWhenSaved
    append16(stsh, 0);                       // nVerBuiltInNamesWhenSaved
    append16(stsh, 0);
    append16(stsh, 0);
    append16(stsh, 0);                       // rgftcStandardChpStsh[3]
    put16(stsh, 0, quint16(stsh.size() - stshiStart));

    // LPStd for the Normal style: sti=0, paragraph, istdBase=0xfff.
    // cbStd=26: 10 bytes of STD + cch + "Normal" + its terminator.
    const QString normalName = QStringLiteral("Normal");
    append16(stsh, quint16(10 + 2 + (normalName.size() + 1) * 2));
    append16(stsh, 0);                       // sti=0
    append16(stsh, 0x0001 | (0x0fff << 4));  // sgcPara, istdBase=0xfff
    append16(stsh, 0);                       // cupx=0, istdNext=0
    append16(stsh, 0);                       // bchUpe
    append16(stsh, 0);                       // fAutoRedef, fHidden, unused
    append16(stsh, quint16(normalName.size()));
    for (QChar c : normalName) {
        append16(stsh, c.unicode());
    }
    append16(stsh, 0);                       // XSTZ terminator

    // Empty reserved style slots (Normal is the only one we need).
    for (int i = 1; i < 15; ++i) {
        append16(stsh, 0);
    }
    return stsh;
}

QByteArray buildPlcfsed(quint32 ccpText)
{
    QByteArray plc;
    append32(plc, 0);
    append32(plc, ccpText);
    plc.append(12, '\0'); // one SED
    return plc;
}

QByteArray buildPlcfbte(quint32 fcStart, quint32 fcLim, quint32 pageNumber)
{
    QByteArray plc;
    append32(plc, fcStart);
    append32(plc, fcLim);
    append32(plc, pageNumber);
    return plc;
}

/** A 512-byte FKP page with one run and no grpprl. */
QByteArray buildEmptyFkp(quint32 fcStart, quint32 fcLim)
{
    QByteArray fkp(512, '\0');
    put32(fkp, 0, fcStart);
    put32(fkp, 4, fcLim);
    fkp[511] = 1; // crun
    return fkp;
}

struct Piece
{
    quint32 start = 0;
    quint32 end = 0;
    quint32 fc = 0;
    quint16 prm = 0;
};

QByteArray buildClx(const QList<Piece> &pieces, bool needsSpec)
{
    QByteArray clx;
    if (needsSpec) {
        // clxtGrpprl with sprmCFSpec = 1, applied to the object anchor piece.
        append8(clx, 0x01);
        append16(clx, 3);
        append8(clx, 0x55);
        append8(clx, 0x08);
        append8(clx, 0x01);
    }

    append8(clx, 0x02); // clxtPlcfpcd
    append32(clx, quint32(4 * (pieces.size() + 1) + 8 * pieces.size()));
    if (!pieces.isEmpty()) {
        append32(clx, pieces.first().start);
    }
    for (const Piece &piece : pieces) {
        append32(clx, piece.end);
    }
    for (const Piece &piece : pieces) {
        append16(clx, 0);        // PCD flags
        append32(clx, piece.fc); // uncompressed UTF-16 offset
        append16(clx, piece.prm);
    }
    return clx;
}

QByteArray buildPlcspa(quint32 anchorCp, quint32 ccpText, const Image &image)
{
    QByteArray plc;
    append32(plc, anchorCp);
    append32(plc, qMax(anchorCp + 1, ccpText));

    append32(plc, 1026); // spid, matches the picture shape
    append32(plc, quint32(image.posX));
    append32(plc, quint32(image.posY));
    append32(plc, quint32(image.posX + image.width));
    append32(plc, quint32(image.posY + image.height));
    // wr=3 (in front of text), fAnchorLock=1.
    append16(plc, 0x8060);
    append32(plc, 0); // cTxbx
    return plc;
}

QByteArray blipForImage(const Image &image, const QByteArray &uid)
{
    QByteArray blip;
    quint16 instance = 0;
    quint16 type = 0;
    if (image.mimeType.contains(QLatin1String("png"), Qt::CaseInsensitive)) {
        instance = 0x06E0;
        type = 0xF01E;
    } else {
        instance = 0x046A;
        type = 0xF01D;
    }

    append16(blip, quint16(quint16(instance) << 4)); // recVer = 0
    append16(blip, type);
    append32(blip, quint32(17 + image.data.size()));
    blip.append(uid);
    append8(blip, 0); // tag
    blip.append(image.data);
    return blip;
}

QByteArray buildDggInfo(const Image &image)
{
    const QByteArray uid = QCryptographicHash::hash(image.data, QCryptographicHash::Md5).left(16);
    const QByteArray blip = blipForImage(image, uid);

    QByteArray fbseBody;
    append8(fbseBody, 5); // btWin32: JPEG/PNG
    append8(fbseBody, 5); // btMacOS
    fbseBody.append(uid);
    append16(fbseBody, 0);                       // tag
    append32(fbseBody, quint32(image.data.size()));
    append32(fbseBody, 1);                       // cRef
    append32(fbseBody, 0);                       // foDelay: data is embedded
    append8(fbseBody, 0);
    append8(fbseBody, 0);                        // cbName
    append8(fbseBody, 0);
    append8(fbseBody, 0);
    fbseBody.append(blip);

    QByteArray bstore;
    appendEscherRecord(bstore, 2, 5, 0xF007, fbseBody);

    QByteArray dggBody;
    QByteArray f006Body;
    append32(f006Body, 2048); // spidMax
    append32(f006Body, 2);    // cidcl
    append32(f006Body, 2);    // cspSaved
    append32(f006Body, 1);    // cdgSaved
    append32(f006Body, 1);    // OfficeArtIDCL.dgid
    append32(f006Body, 1026); // OfficeArtIDCL.cid
    appendEscherRecord(dggBody, 0, 0, 0xF006, f006Body);
    appendEscherRecord(dggBody, 15, 1, 0xF001, bstore);

    QByteArray dggInfo;
    appendEscherRecord(dggInfo, 15, 0, 0xF000, dggBody);

    // Word inserts one byte between the drawing group and the drawing.
    append8(dggInfo, 0);

    QByteArray dgBody;
    QByteArray f008Body;
    append32(f008Body, 1);
    append32(f008Body, 0x00100000);
    appendEscherRecord(dgBody, 0, 1, 0xF008, f008Body);

    QByteArray spgrBody;

    QByteArray groupSpBody;
    QByteArray groupRect(16, '\0');
    appendEscherRecord(groupSpBody, 1, 0, 0xF009, groupRect);
    QByteArray groupFspBody;
    append32(groupFspBody, 1024);
    append32(groupFspBody, 0x00000005); // fGroup | fPatriarch
    appendEscherRecord(groupSpBody, 2, 0, 0xF00A, groupFspBody);
    appendEscherRecord(spgrBody, 15, 0, 0xF004, groupSpBody);

    QByteArray pictureSpBody;
    QByteArray pictureFspBody;
    append32(pictureFspBody, 1026);
    append32(pictureFspBody, 0x0000000a); // fChild | fHaveAnchor
    appendEscherRecord(pictureSpBody, 2, 75, 0xF00A, pictureFspBody); // msosptPictureFrame

    QByteArray foptBody;
    append16(foptBody, 0x0104); // Pib
    append32(foptBody, 1);
    appendEscherRecord(pictureSpBody, 3, 1, 0xF00B, foptBody);

    QByteArray anchorBody;
    append32(anchorBody, 0); // index into plcspaMom
    appendEscherRecord(pictureSpBody, 0, 0, 0xF010, anchorBody);

    QByteArray clientDataBody;
    append32(clientDataBody, 1);
    appendEscherRecord(pictureSpBody, 0, 0, 0xF011, clientDataBody);

    appendEscherRecord(spgrBody, 15, 0, 0xF004, pictureSpBody);
    appendEscherRecord(dgBody, 15, 0, 0xF003, spgrBody);
    appendEscherRecord(dggInfo, 15, 0, 0xF002, dgBody);

    // Calligra's importer expects a second drawing container for headers.
    append8(dggInfo, 1);
    appendEscherRecord(dggInfo, 15, 0, 0xF002, QByteArray());

    return dggInfo;
}

} // namespace

bool Word97::writeDocument(QIODevice *device, const Document &doc)
{
    if (!device) {
        return false;
    }

    // A floating object is anchored through an ASCII 8 character whose CHP has
    // fSpec set.  The anchor is placed before all visible text; its plcspa
    // start CP is therefore zero.
    const quint32 anchorCp = doc.images.isEmpty() ? 0 : 1;
    QString text;
    if (!doc.images.isEmpty()) {
        text += QChar(0x0008);
    }
    for (const Paragraph &paragraph : doc.paragraphs) {
        text += paragraph.text;
        text += QChar(0x000D);
    }

    const QByteArray textBytes(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
    const quint32 ccpText = quint32(text.size());
    const quint32 cbMac = TEXT_OFFSET + quint32(textBytes.size());

    QByteArray wordDocument = buildFib(ccpText, cbMac, cbMac);
    wordDocument.resize(TEXT_OFFSET);

    // Minimal CHPX/PAPX FKP pages.  The importer still consults these even
    // when no direct paragraph or character formatting is present.
    const quint32 chpxPage = 3;
    const quint32 papxPage = 4;
    const QByteArray chpxFkp = buildEmptyFkp(TEXT_OFFSET, TEXT_OFFSET + quint32(textBytes.size()));
    const QByteArray papxFkp = buildEmptyFkp(TEXT_OFFSET, TEXT_OFFSET + quint32(textBytes.size()));
    wordDocument.replace(int(chpxPage * 512), chpxFkp.size(), chpxFkp);
    wordDocument.replace(int(papxPage * 512), papxFkp.size(), papxFkp);

    put32(wordDocument, 112, chpxPage); // pnChpFirst
    put32(wordDocument, 116, 1);        // cpnBteChp
    put32(wordDocument, 124, papxPage); // pnPapFirst
    put32(wordDocument, 128, 1);        // cpnBtePap

    wordDocument.append(textBytes);

    // Assemble the table stream and remember each part's offset.
    QByteArray table;
    auto addTablePart = [&table](const QByteArray &part) {
        const quint32 fc = quint32(table.size());
        table.append(part);
        return fc;
    };

    const QByteArray stshf = buildStshf();
    const QByteArray plcfsed = buildPlcfsed(ccpText);
    const QByteArray plcfbteChpx = buildPlcfbte(TEXT_OFFSET, TEXT_OFFSET + quint32(textBytes.size()), chpxPage);
    const QByteArray plcfbtePapx = buildPlcfbte(TEXT_OFFSET, TEXT_OFFSET + quint32(textBytes.size()), papxPage);
    const QByteArray sttbfffn(4, '\0'); // empty font table: count=0, extraData=0
    const QByteArray dop(512, '\0');

    QList<Piece> pieces;
    if (!doc.images.isEmpty()) {
        // PRM.fComplex is the low bit; isprm=0 selects grpprl 0 in the CLX.
        pieces.append({0, anchorCp, TEXT_OFFSET, 1});
        pieces.append({anchorCp, ccpText, TEXT_OFFSET + anchorCp * 2, 0});
    } else {
        pieces.append({0, ccpText, TEXT_OFFSET, 0});
    }
    // Drop degenerate pieces; a zero-length piece with a complex property is
    // harmless but needlessly confuses importers.
    for (int i = pieces.size() - 1; i >= 0; --i) {
        if (pieces.at(i).start >= pieces.at(i).end) {
            pieces.removeAt(i);
        }
    }
    const bool needsSpec = !doc.images.isEmpty();
    const QByteArray clx = buildClx(pieces, needsSpec);

    const quint32 fcStshf = addTablePart(stshf);
    const quint32 fcPlcfsed = addTablePart(plcfsed);
    const quint32 fcPlcfbteChpx = addTablePart(plcfbteChpx);
    const quint32 fcPlcfbtePapx = addTablePart(plcfbtePapx);
    const quint32 fcSttbfffn = addTablePart(sttbfffn);
    const quint32 fcDop = addTablePart(dop);
    const quint32 fcClx = addTablePart(clx);

    setFcLcb(wordDocument, FIB_INDEX_STSHF, fcStshf, quint32(stshf.size()));
    setFcLcb(wordDocument, FIB_INDEX_PLCFSED, fcPlcfsed, quint32(plcfsed.size()));
    setFcLcb(wordDocument, FIB_INDEX_PLCFBTECHPX, fcPlcfbteChpx, quint32(plcfbteChpx.size()));
    setFcLcb(wordDocument, FIB_INDEX_PLCFBTEPAPX, fcPlcfbtePapx, quint32(plcfbtePapx.size()));
    setFcLcb(wordDocument, FIB_INDEX_STTBFFFN, fcSttbfffn, quint32(sttbfffn.size()));
    setFcLcb(wordDocument, FIB_INDEX_DOP, fcDop, quint32(dop.size()));
    setFcLcb(wordDocument, FIB_INDEX_STTBFASSOC, 0, 0);
    setFcLcb(wordDocument, FIB_INDEX_CLX, fcClx, quint32(clx.size()));
    setFcLcb(wordDocument, FIB_INDEX_PLCFSPA_HDR, 0, 0);

    if (!doc.images.isEmpty()) {
        // The first image is the document's floating illustration.
        const QByteArray plcspa = buildPlcspa(0, ccpText, doc.images.first());
        const QByteArray dggInfo = buildDggInfo(doc.images.first());
        const quint32 fcPlcspa = addTablePart(plcspa);
        const quint32 fcDggInfo = addTablePart(dggInfo);
        setFcLcb(wordDocument, FIB_INDEX_PLCFSPAMOM, fcPlcspa, quint32(plcspa.size()));
        setFcLcb(wordDocument, FIB_INDEX_DGGINFO, fcDggInfo, quint32(dggInfo.size()));
    }

    CFBWriter writer(false);
    if (!writer.open(device)) {
        qWarning("Word97Writer: cannot open CFB output");
        return false;
    }
    writer.setRootClassId(QUuid(QStringLiteral("{00020906-0000-0000-C000-000000000046}")));

    QIODevice *wd = writer.openSubStream(QStringLiteral("WordDocument"));
    if (!wd || wd->write(wordDocument) != wordDocument.size()) {
        qWarning("Word97Writer: cannot write WordDocument stream");
        return false;
    }
    QIODevice *tbl = writer.openSubStream(QStringLiteral("1Table"));
    if (!tbl || tbl->write(table) != table.size()) {
        qWarning("Word97Writer: cannot write 1Table stream");
        return false;
    }
    wd->close();
    tbl->close();
    writer.close();
    return true;
}
