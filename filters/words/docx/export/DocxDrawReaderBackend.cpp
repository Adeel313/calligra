/* This file is part of the KDE project
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "DocxDrawReaderBackend.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QUrl>
#include <QSet>
#include <QStringList>
#include <QtMath>

#include <KoOdfStyle.h>
#include <KoOdfStyleManager.h>
#include <KoOdfStyleProperties.h>
#include <KoStore.h>
#include <KoUnit.h>
#include <KoXmlNS.h>
#include <KoXmlStreamReader.h>
#include <KoXmlWriter.h>

#include "DocxFile.h"
#include "OdfReaderContext.h"
#include "OdfReaderDocxContext.h"

namespace
{
constexpr qreal EmusPerPoint = 12700.0;

struct ImageType {
    const char *mime;
    const char *extension;
};

ImageType imageTypeForMime(const QString &mime)
{
    if (mime == QLatin1String("image/png")) {
        return {"image/png", "png"};
    }
    if (mime == QLatin1String("image/jpeg")) {
        return {"image/jpeg", "jpeg"};
    }
    if (mime == QLatin1String("image/gif")) {
        return {"image/gif", "gif"};
    }
    if (mime == QLatin1String("image/bmp")) {
        return {"image/bmp", "bmp"};
    }
    if (mime == QLatin1String("image/tiff")) {
        return {"image/tiff", "tiff"};
    }
    return {nullptr, nullptr};
}

QString normalizedOdfPath(QString href)
{
    href = QUrl::fromPercentEncoding(href.toUtf8());
    while (href.startsWith(QLatin1Char('/'))) {
        href.remove(0, 1);
    }
    return href;
}
}

void DocxDrawReaderBackend::setContext(OdfReaderDocxContext *context)
{
    m_context = context;
    if (!m_context || !m_context->odfStore()) {
        return;
    }

    // KoStore only allows one open member at a time. Discover image paths in
    // a separate pass, close each part, and cache the image bytes before the
    // regular ODT traversal starts. This must run after analyzeContent(): the
    // manifest is only loaded by analyzeOdfFile() then, and the store must be
    // free to open the parts. Header/footer images (the letterhead) live in
    // styles.xml, so scan that part too.
    KoStore *store = m_context->odfStore();
    QSet<QString> imagePaths;
    const QStringList parts{QStringLiteral("content.xml"), QStringLiteral("styles.xml")};
    for (const QString &part : parts) {
        if (!store->open(part)) {
            continue;
        }
        KoXmlStreamReader reader(store->device());
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement() && reader.name() == QLatin1StringView("image") && reader.namespaceUri() == KoXmlNS::draw) {
                // Look the attribute up by namespace URI and local name instead of by
                // qualified name: the namespace soundness has not been checked yet at
                // this point (it gets checked on the first prefix() call during
                // traversal), so qualifiedName() would resolve to ":href" here, and
                // the ns+name overloads of KoXmlStreamAttributes are declared but not
                // implemented. namespaceUri(), name() and value() are always raw.
                const auto attrs = reader.attributes();
                QString href;
                for (int i = 0; i < attrs.size(); ++i) {
                    if (attrs.at(i).namespaceUri() == KoXmlNS::xlink && attrs.at(i).name() == QLatin1StringView("href")) {
                        href = attrs.at(i).value().toString();
                        break;
                    }
                }
                const QString path = normalizedOdfPath(href);
                if (!path.isEmpty() && !path.contains(QStringLiteral("://"))) {
                    imagePaths.insert(path);
                }
            }
        }
        store->close();
    }

    for (const QString &path : imagePaths) {
        // The manifest's media type is not a reliable measure of whether the
        // image can be exported: vector formats and less common raster types
        // can still be decoded and embedded as PNG below.
        if (!store->open(path)) {
            continue;
        }
        const QByteArray data = store->read(store->size());
        store->close();
        if (!data.isEmpty()) {
            m_imageData.insert(path, data);
        }
    }
}

void DocxDrawReaderBackend::elementDrawFrame(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    Q_UNUSED(context);
    if (reader.isStartElement()) {
        const auto attributes = reader.attributes();
        const qreal width = KoUnit::parseValue(attributes.value(QStringLiteral("svg:width")).toString(), 144.0);
        const qreal height = KoUnit::parseValue(attributes.value(QStringLiteral("svg:height")).toString(), 96.0);
        m_frameSize = QSizeF(qMax<qreal>(1.0, width), qMax<qreal>(1.0, height));
        // The anchor type, not a nonzero position, determines whether an image
        // is floating. Valid floating images are often positioned at (0, 0).
        m_frameFloating = attributes.value(QStringLiteral("text:anchor-type")) != QLatin1StringView("as-char");
        m_frameWrap.clear();
        m_frameRunThrough.clear();
        const QString styleName = attributes.value(QStringLiteral("draw:style-name")).toString();
        if (KoOdfStyle *style = m_context->styleManager()->style(styleName, QStringLiteral("graphic"))) {
            if (KoOdfStyleProperties *properties = style->properties(QStringLiteral("style:graphic-properties"))) {
                m_frameWrap = properties->attribute(QStringLiteral("style:wrap"));
                m_frameRunThrough = properties->attribute(QStringLiteral("style:run-through"));
            }
        }
        m_framePosX = KoUnit::parseValue(attributes.value(QStringLiteral("svg:x")).toString(), 0.0);
        m_framePosY = KoUnit::parseValue(attributes.value(QStringLiteral("svg:y")).toString(), 0.0);
        m_frameRotation = 0;

        // ODF rotates counterclockwise in radians; DrawingML rotates clockwise
        // in 1/60000 degree units. The ODF translation includes the offset that
        // keeps the frame center fixed, so remove it when recovering the DOCX
        // top-left position.
        const QString transform = attributes.value(QStringLiteral("draw:transform")).toString();
        const QRegularExpression rotateExpression(QStringLiteral("rotate\\s*\\(\\s*([-+0-9.eE]+)\\s*\\)"));
        const QRegularExpressionMatch rotationMatch = rotateExpression.match(transform);
        if (rotationMatch.hasMatch()) {
            const qreal angle = rotationMatch.captured(1).toDouble();
            constexpr int FullTurn = 360 * 60000;
            m_frameRotation = qRound(-angle * 180.0 * 60000.0 / M_PI) % FullTurn;
            if (m_frameRotation < 0) {
                m_frameRotation += FullTurn;
            }
            const QRegularExpression translateExpression(QStringLiteral("translate\\s*\\(\\s*([^\\s,)]+)[\\s,]+([^\\s,)]+)\\s*\\)"));
            const QRegularExpressionMatch translateMatch = translateExpression.match(transform);
            if (translateMatch.hasMatch()) {
                const qreal translatedX = KoUnit::parseValue(translateMatch.captured(1), 0.0);
                const qreal translatedY = KoUnit::parseValue(translateMatch.captured(2), 0.0);
                const qreal width = m_frameSize.width();
                const qreal height = m_frameSize.height();
                const qreal xDiff = width / 2.0 - qCos(-angle) * width / 2.0 + qSin(-angle) * height / 2.0;
                const qreal yDiff = height / 2.0 - qSin(-angle) * width / 2.0 - qCos(-angle) * height / 2.0;
                m_framePosX = translatedX - xDiff;
                m_framePosY = translatedY - yDiff;
            }
        }
    } else {
        m_frameSize = QSizeF();
        m_framePosX = 0.0;
        m_framePosY = 0.0;
        m_frameFloating = false;
        m_frameWrap.clear();
        m_frameRunThrough.clear();
        m_frameRotation = 0;
    }
}

void DocxDrawReaderBackend::elementDrawImage(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    if (!reader.isStartElement() || !m_context) {
        return;
    }

    const QString sourcePath = normalizedOdfPath(reader.attributes().value(QStringLiteral("xlink:href")).toString());
    if (sourcePath.isEmpty() || sourcePath.contains(QStringLiteral("://"))) {
        return;
    }

    EmbeddedImage image = m_images.value(sourcePath);
    if (image.target.isEmpty()) {
        const QString mime = context->manifest().value(sourcePath);
        ImageType type = imageTypeForMime(mime);
        QByteArray imageData = m_imageData.value(sourcePath);
        if (imageData.isEmpty()) {
            return;
        }

        // DOCX readers commonly support PNG, JPEG, GIF, BMP and TIFF. Keep
        // those source bytes unchanged; convert other image types (including
        // SVG/WMF/EMF when a Qt image handler is available) to a portable PNG
        // rather than silently omitting the drawing.
        if (!type.mime) {
            QBuffer inputBuffer(&imageData);
            if (!inputBuffer.open(QIODevice::ReadOnly)) {
                return;
            }
            QImageReader imageReader(&inputBuffer);
            const QImage decodedImage = imageReader.read();
            if (decodedImage.isNull()) {
                return;
            }
            QByteArray pngData;
            QBuffer outputBuffer(&pngData);
            if (!outputBuffer.open(QIODevice::WriteOnly) || !decodedImage.save(&outputBuffer, "PNG")) {
                return;
            }
            imageData = pngData;
            type = {"image/png", "png"};
        }

        image.name = QStringLiteral("image%1.%2").arg(m_nextImageId++).arg(QString::fromLatin1(type.extension));
        image.target = QStringLiteral("media/") + image.name;
        m_context->m_docxFile->addContentFile(QString(),
                                              QStringLiteral("/word/") + image.target,
                                              QByteArray(type.mime),
                                              imageData);
        m_images.insert(sourcePath, image);
    }

    // OPC relationships belong to the part containing the DrawingML. The
    // same ODF image may be referenced from document.xml and header1.xml, so
    // create (or reuse) a relationship for the active output part each time.
    image.relationshipId = m_context->m_docxFile->addRelationship(
        m_context->outputPart(),
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"),
        image.target);
    if (image.relationshipId.isEmpty()) {
        return;
    }

    const qint64 cx = qMax<qint64>(12700, qRound64(m_frameSize.width() * EmusPerPoint));
    const qint64 cy = qMax<qint64>(12700, qRound64(m_frameSize.height() * EmusPerPoint));
    KoXmlWriter *writer = m_context->m_documentWriter;
    const int drawingId = m_nextDrawingId++;

    // A w:drawing run is only valid inside a paragraph.  Frames that are
    // direct children of office:text (floating images without a paragraph
    // in the ODF source) arrive here outside any text:p; without a wrapper
    // paragraph the emitted document.xml is schema-invalid and readers
    // silently drop the drawing when the file is opened again.
    const bool wrapped = !m_context->m_insideParagraph;
    if (wrapped) {
        writer->startElement("w:p");
        m_context->m_insideParagraph = true;
    }

    const bool wrappedRun = !m_context->m_insideRun;
    if (wrappedRun) {
        writer->startElement("w:r");
    }
    writer->startElement("w:drawing");
    writer->addAttribute("xmlns:wp", "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing");
    writer->addAttribute("xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main");
    writer->addAttribute("xmlns:pic", "http://schemas.openxmlformats.org/drawingml/2006/picture");
    if (m_frameFloating) {
        writer->startElement("wp:anchor");
        writer->addAttribute("distT", "0");
        writer->addAttribute("distB", "0");
        writer->addAttribute("distL", "0");
        writer->addAttribute("distR", "0");
        writer->addAttribute("simplePos", "0");
        writer->addAttribute("relativeHeight", "0");
        writer->addAttribute("behindDoc", m_frameRunThrough == QLatin1String("background") ? "1" : "0");
        writer->addAttribute("locked", "0");
        writer->addAttribute("layoutInCell", "1");
        writer->addAttribute("allowOverlap", "1");
        writer->startElement("wp:positionH");
        writer->addAttribute("relativeFrom", "page");
        writer->startElement("wp:posOffset");
        writer->addTextNode(QString::number(qRound64(m_framePosX * EmusPerPoint)));
        writer->endElement(); // wp:posOffset
        writer->endElement(); // wp:positionH
        writer->startElement("wp:positionV");
        writer->addAttribute("relativeFrom", "page");
        writer->startElement("wp:posOffset");
        writer->addTextNode(QString::number(qRound64(m_framePosY * EmusPerPoint)));
        writer->endElement(); // wp:posOffset
        writer->endElement(); // wp:positionV
        writer->startElement("wp:extent");
        writer->addAttribute("cx", QString::number(cx));
        writer->addAttribute("cy", QString::number(cy));
        writer->endElement(); // wp:extent

        // wp:anchor requires exactly one wrapping mode. Preserve the ODF
        // wrapping side where it has a direct WordprocessingML equivalent.
        if (m_frameWrap == QLatin1String("none")) {
            writer->startElement("wp:wrapTopAndBottom");
        } else if (m_frameWrap.isEmpty() || m_frameWrap == QLatin1String("run-through")) {
            writer->startElement("wp:wrapNone");
        } else {
            writer->startElement("wp:wrapSquare");
            QString wrapText = QStringLiteral("bothSides");
            if (m_frameWrap == QLatin1String("left") || m_frameWrap == QLatin1String("right")) {
                wrapText = m_frameWrap;
            } else if (m_frameWrap == QLatin1String("biggest")) {
                wrapText = QStringLiteral("largest");
            }
            writer->addAttribute("wrapText", wrapText);
        }
        writer->endElement(); // wrapping mode
    } else {
        writer->startElement("wp:inline");
        writer->addAttribute("distT", 0);
        writer->addAttribute("distB", 0);
        writer->addAttribute("distL", 0);
        writer->addAttribute("distR", 0);
        writer->startElement("wp:extent");
        writer->addAttribute("cx", QString::number(cx));
        writer->addAttribute("cy", QString::number(cy));
        writer->endElement(); // wp:extent
    }
    writer->startElement("wp:docPr");
    writer->addAttribute("id", drawingId);
    writer->addAttribute("name", image.name);
    writer->endElement(); // wp:docPr
    writer->startElement("wp:cNvGraphicFramePr");
    writer->startElement("a:graphicFrameLocks");
    writer->addAttribute("noChangeAspect", "1");
    writer->endElement(); // a:graphicFrameLocks
    writer->endElement(); // wp:cNvGraphicFramePr
    writer->startElement("a:graphic");
    writer->startElement("a:graphicData");
    writer->addAttribute("uri", "http://schemas.openxmlformats.org/drawingml/2006/picture");
    writer->startElement("pic:pic");
    writer->startElement("pic:nvPicPr");
    writer->startElement("pic:cNvPr");
    writer->addAttribute("id", 0);
    writer->addAttribute("name", image.name);
    writer->endElement(); // pic:cNvPr
    writer->startElement("pic:cNvPicPr");
    writer->endElement(); // pic:cNvPicPr
    writer->endElement(); // pic:nvPicPr
    writer->startElement("pic:blipFill");
    writer->startElement("a:blip");
    writer->addAttribute("r:embed", image.relationshipId);
    writer->endElement(); // a:blip
    writer->startElement("a:stretch");
    writer->startElement("a:fillRect");
    writer->endElement(); // a:fillRect
    writer->endElement(); // a:stretch
    writer->endElement(); // pic:blipFill
    writer->startElement("pic:spPr");
    writer->startElement("a:xfrm");
    if (m_frameRotation != 0) {
        writer->addAttribute("rot", m_frameRotation);
    }
    writer->startElement("a:off");
    writer->addAttribute("x", 0);
    writer->addAttribute("y", 0);
    writer->endElement(); // a:off
    writer->startElement("a:ext");
    writer->addAttribute("cx", QString::number(cx));
    writer->addAttribute("cy", QString::number(cy));
    writer->endElement(); // a:ext
    writer->endElement(); // a:xfrm
    writer->startElement("a:prstGeom");
    writer->addAttribute("prst", "rect");
    writer->startElement("a:avLst");
    writer->endElement(); // a:avLst
    writer->endElement(); // a:prstGeom
    writer->endElement(); // pic:spPr
    writer->endElement(); // pic:pic
    writer->endElement(); // a:graphicData
    writer->endElement(); // a:graphic
    // Close the anchor/inline element. positionH and positionV were closed
    // immediately after their offsets above; closing them here would corrupt
    // the nesting of the DrawingML elements.
    if (m_frameFloating) {
        writer->endElement(); // wp:anchor
    } else {
        // Close wp:inline
        writer->endElement(); // wp:inline
    }
    writer->endElement(); // w:drawing
    if (wrappedRun) {
        writer->endElement(); // w:r
    }

    if (wrapped) {
        writer->endElement(); // w:p
        m_context->m_insideParagraph = false;
    }
}
