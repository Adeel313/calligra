/* This file is part of the KDE project
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "DocxDrawReaderBackend.h"
#include "DocxChartWriter.h"
#include "DocxExportDebug.h"
#include "DocxMathWriter.h"

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

QString shapeKey(const KoXmlStreamAttributes &attributes)
{
    QStringList parts;
    for (int i = 0; i < attributes.size(); ++i) {
        const auto attribute = attributes.at(i);
        parts.append(attribute.namespaceUri().toString() + QLatin1Char(':') + attribute.name().toString()
                     + QLatin1Char('=') + attribute.value().toString());
    }
    parts.sort();
    return parts.join(QLatin1Char('|'));
}

QString rawAttribute(const KoXmlStreamAttributes &attributes, const QString &ns, const QString &name)
{
    for (int i = 0; i < attributes.size(); ++i) {
        if (attributes.at(i).namespaceUri() == ns && attributes.at(i).name() == name) {
            return attributes.at(i).value().toString();
        }
    }
    return {};
}

QString normalizedOdfPath(QString href)
{
    href = QUrl::fromPercentEncoding(href.toUtf8());
    while (href.startsWith(QLatin1Char('/'))) {
        href.remove(0, 1);
    }
    while (href.startsWith(QLatin1String("./"))) {
        href.remove(0, 2);
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
    QSet<QString> objectPaths;
    const QStringList parts{QStringLiteral("content.xml"), QStringLiteral("styles.xml")};
    for (const QString &part : parts) {
        if (!store->open(part)) {
            continue;
        }
        KoXmlStreamReader reader(store->device());
        QString customShapeKey;
        bool inShapeText = false;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.namespaceUri() == KoXmlNS::draw && reader.name() == QLatin1StringView("custom-shape")) {
                if (reader.isStartElement()) {
                    customShapeKey = shapeKey(reader.attributes());
                    m_presetShapes.insert(customShapeKey, PresetShape());
                } else if (reader.isEndElement()) {
                    customShapeKey.clear();
                }
            }
            if (!customShapeKey.isEmpty()) {
                PresetShape &shape = m_presetShapes[customShapeKey];
                if (reader.isStartElement() && reader.namespaceUri() == KoXmlNS::draw && reader.name() == QLatin1StringView("enhanced-geometry")) {
                    shape.preset = rawAttribute(reader.attributes(), KoXmlNS::draw, QStringLiteral("type"));
                    shape.flipH = rawAttribute(reader.attributes(), KoXmlNS::draw, QStringLiteral("mirror-horizontal")) == QLatin1String("true");
                    shape.flipV = rawAttribute(reader.attributes(), KoXmlNS::draw, QStringLiteral("mirror-vertical")) == QLatin1String("true");
                }
                if (reader.namespaceUri() == KoXmlNS::text && reader.name() == QLatin1StringView("p")) {
                    if (reader.isStartElement()) {
                        if (!shape.text.isEmpty()) { shape.text += QLatin1Char('\n'); }
                        inShapeText = true;
                    } else if (reader.isEndElement()) { inShapeText = false; }
                }
                if (inShapeText && reader.isCharacters()) { shape.text += reader.text().toString(); }
            }
            if (reader.isStartElement() && (reader.name() == QLatin1StringView("image") || reader.name() == QLatin1StringView("object")) && reader.namespaceUri() == KoXmlNS::draw) {
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
                    if (reader.name() == QLatin1StringView("object")) {
                        objectPaths.insert(path);
                    } else {
                        imagePaths.insert(path);
                    }
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
    for (QString path : objectPaths) {
        while (path.endsWith(QLatin1Char('/'))) { path.chop(1); }
        if (store->open(path + QStringLiteral("/content.xml"))) {
            m_chartData.insert(path, store->read(store->size()));
            store->close();
        }
    }

}

void DocxDrawReaderBackend::elementDrawFrame(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    Q_UNUSED(context);
    if (reader.isStartElement()) {
        m_frameHasChart = false;
        const auto attributes = reader.attributes();
        const qreal width = KoUnit::parseValue(attributes.value(QStringLiteral("svg:width")).toString(), 144.0);
        const qreal height = KoUnit::parseValue(attributes.value(QStringLiteral("svg:height")).toString(), 96.0);
        m_frameSize = QSizeF(qMax<qreal>(1.0, width), qMax<qreal>(1.0, height));
        // The anchor type, not a nonzero position, determines whether an image
        // is floating. Valid floating images are often positioned at (0, 0).
        m_frameFloating = attributes.value(QStringLiteral("text:anchor-type")) != QLatin1StringView("as-char");
        m_frameWrap.clear();
        m_frameRunThrough.clear();
        m_frameWrapContour = false;
        m_frameWrapContourMode.clear();
        m_frameClip.clear();
        const QString styleName = attributes.value(QStringLiteral("draw:style-name")).toString();
        if (KoOdfStyle *style = m_context->styleManager()->style(styleName, QStringLiteral("graphic"))) {
            if (KoOdfStyleProperties *properties = style->properties(QStringLiteral("style:graphic-properties"))) {
                m_frameWrap = properties->attribute(QStringLiteral("style:wrap"));
                m_frameRunThrough = properties->attribute(QStringLiteral("style:run-through"));
                m_frameWrapContour = properties->attribute(QStringLiteral("style:wrap-contour")) == QLatin1String("true");
                m_frameWrapContourMode = properties->attribute(QStringLiteral("style:wrap-contour-mode"));
                m_frameClip = properties->attribute(QStringLiteral("fo:clip"));
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
        m_frameWrapContour = false;
        m_frameWrapContourMode.clear();
        m_frameClip.clear();
        m_frameRotation = 0;
    }
}

void DocxDrawReaderBackend::elementDrawImage(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    if (!reader.isStartElement() || !m_context || m_frameHasChart) {
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

    writeDrawing(image, false, sourcePath);
}

void DocxDrawReaderBackend::elementDrawObject(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    Q_UNUSED(context);
    if (!reader.isStartElement() || !m_context) { return; }
    QString path = normalizedOdfPath(reader.attributes().value(QStringLiteral("xlink:href")).toString());
    while (path.endsWith(QLatin1Char('/'))) { path.chop(1); }
    // Embedded ODF formula objects are MathML documents. Word stores formulas
    // as OMML inside the paragraph, not as a drawing, so convert and emit the
    // equation inline instead of failing the whole export.
    if (m_chartData.contains(path)) {
        const QByteArray omml = mathmlToOmml(m_chartData.value(path));
        if (!omml.isEmpty()) {
            m_context->m_documentWriter->addCompleteElement(omml.constData());
            m_frameHasChart = true;
            return;
        }
    }
    EmbeddedImage chart = m_charts.value(path);
    if (chart.target.isEmpty()) {
        chart.name = QStringLiteral("Chart %1").arg(m_nextChartId);
        chart.target = QStringLiteral("charts/chart%1.xml").arg(m_nextChartId);
        const QString workbookTarget = QStringLiteral("embeddings/chart%1.xlsx").arg(m_nextChartId);
        const QString workbookRelationship = m_context->m_docxFile->addRelationship(QStringLiteral("word/") + chart.target,
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/package"), QStringLiteral("../") + workbookTarget);
        QByteArray workbook;
        const QByteArray xml = writeDocxChart(m_chartData.value(path), workbookRelationship, &workbook);
        if (xml.isEmpty()) {
            warnDocx << "Cannot export embedded chart without data loss:" << path;
            m_chartError = true;
            return;
        }
        ++m_nextChartId;
        m_context->m_docxFile->addContentFile(QString(), QStringLiteral("/word/") + workbookTarget,
            QByteArrayLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"), workbook);
        m_context->m_docxFile->addContentFile(QString(), QStringLiteral("/word/") + chart.target,
            QByteArrayLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml"), xml);
        m_charts.insert(path, chart);
    }
    chart.relationshipId = m_context->m_docxFile->addRelationship(m_context->outputPart(),
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart"), chart.target);
    if (chart.relationshipId.isEmpty()) { m_chartError = true; return; }
    writeDrawing(chart, true);
    m_frameHasChart = true;
}

void DocxDrawReaderBackend::elementDrawPath(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    if (reader.namespaceUri() != KoXmlNS::draw || reader.name() != QLatin1StringView("custom-shape")) { return; }
    if (!reader.isStartElement()) {
        elementDrawFrame(reader, context);
        return;
    }
    PresetShape shape = m_presetShapes.value(shapeKey(reader.attributes()));
    if (!shape.preset.startsWith(QLatin1String("ooxml-"))) {
        warnDocx << "Cannot export custom shape without losing its geometry";
        m_chartError = true;
        return;
    }
    shape.preset.remove(0, 6);
    elementDrawFrame(reader, context);
    const QString styleName = reader.attributes().value(QStringLiteral("draw:style-name")).toString();
    shape.fill = QStringLiteral("#5b9bd5");
    shape.stroke = QStringLiteral("#34495e");
    if (KoOdfStyle *style = m_context->styleManager()->style(styleName, QStringLiteral("graphic"))) {
        if (KoOdfStyleProperties *properties = style->properties(QStringLiteral("style:graphic-properties"))) {
            shape.fill = properties->attribute(QStringLiteral("draw:fill")) == QLatin1String("none") ? QString()
                : properties->attribute(QStringLiteral("draw:fill-color"));
            shape.stroke = properties->attribute(QStringLiteral("draw:stroke")) == QLatin1String("none") ? QString()
                : properties->attribute(QStringLiteral("svg:stroke-color"));
        }
    }
    EmbeddedImage drawing;
    drawing.name = shape.preset;
    writeDrawing(drawing, false, QString(), &shape);
}

void DocxDrawReaderBackend::writePrimitive(KoXmlStreamReader &reader, const QString &preset, bool line)
{
    if (!reader.isStartElement() || !m_context) {
        return;
    }

    const auto attributes = reader.attributes();
    PresetShape shape;
    shape.preset = preset;
    const QString anchorType = attributes.value(QStringLiteral("text:anchor-type")).toString();
    m_frameFloating = !anchorType.isEmpty() && anchorType != QLatin1String("as-char");
    m_frameWrap = QStringLiteral("run-through");
    m_frameRunThrough.clear();
    m_frameWrapContour = false;
    m_frameWrapContourMode.clear();
    m_frameClip.clear();
    m_frameRotation = 0;

    if (line) {
        const qreal x1 = KoUnit::parseValue(attributes.value(QStringLiteral("svg:x1")).toString(), 0.0);
        const qreal y1 = KoUnit::parseValue(attributes.value(QStringLiteral("svg:y1")).toString(), 0.0);
        const qreal x2 = KoUnit::parseValue(attributes.value(QStringLiteral("svg:x2")).toString(), 0.0);
        const qreal y2 = KoUnit::parseValue(attributes.value(QStringLiteral("svg:y2")).toString(), 0.0);
        shape.flipH = x2 < x1;
        shape.flipV = y2 < y1;
        m_framePosX = qMin(x1, x2);
        m_framePosY = qMin(y1, y2);
        m_frameSize = QSizeF(qAbs(x2 - x1), qAbs(y2 - y1));
    } else {
        m_framePosX = KoUnit::parseValue(attributes.value(QStringLiteral("svg:x")).toString(), 0.0);
        m_framePosY = KoUnit::parseValue(attributes.value(QStringLiteral("svg:y")).toString(), 0.0);
        m_frameSize = QSizeF(qMax<qreal>(1.0, KoUnit::parseValue(attributes.value(QStringLiteral("svg:width")).toString(), 1.0)),
                              qMax<qreal>(1.0, KoUnit::parseValue(attributes.value(QStringLiteral("svg:height")).toString(), 1.0)));
    }

    if (KoOdfStyle *style = m_context->styleManager()->style(attributes.value(QStringLiteral("draw:style-name")).toString(), QStringLiteral("graphic"))) {
        if (KoOdfStyleProperties *properties = style->properties(QStringLiteral("style:graphic-properties"))) {
            const QString fillType = properties->attribute(QStringLiteral("draw:fill"));
            shape.fill = fillType == QLatin1String("none") ? QString() : properties->attribute(QStringLiteral("draw:fill-color"));
            const QString strokeType = properties->attribute(QStringLiteral("draw:stroke"));
            shape.stroke = strokeType == QLatin1String("none") ? QString() : properties->attribute(QStringLiteral("svg:stroke-color"));
        }
    }

    EmbeddedImage drawing;
    drawing.name = attributes.value(QStringLiteral("draw:name")).toString();
    if (drawing.name.isEmpty()) {
        drawing.name = line ? QStringLiteral("Line") : QStringLiteral("Rectangle");
    }
    writeDrawing(drawing, false, QString(), &shape);
}

void DocxDrawReaderBackend::elementDrawLine(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    Q_UNUSED(context);
    writePrimitive(reader, QStringLiteral("line"), true);
}

void DocxDrawReaderBackend::elementDrawRect(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    Q_UNUSED(context);
    writePrimitive(reader, QStringLiteral("rect"), false);
}

void DocxDrawReaderBackend::writeDrawing(const EmbeddedImage &image, bool chart, const QString &sourcePath, const PresetShape *shape)
{
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
        writer->startElement("wp:simplePos");
        writer->addAttribute("x", 0);
        writer->addAttribute("y", 0);
        writer->endElement();
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
        if (m_frameWrapContour) {
            writer->startElement(m_frameWrapContourMode == QLatin1String("full") ? "wp:wrapTight" : "wp:wrapThrough");
            writer->addAttribute("wrapText", "bothSides");
        } else if (m_frameWrap == QLatin1String("none")) {
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
    if (shape) {
        writer->addAttribute("uri", "http://schemas.microsoft.com/office/word/2010/wordprocessingShape");
        writer->startElement("wps:wsp");
        writer->addAttribute("xmlns:wps", "http://schemas.microsoft.com/office/word/2010/wordprocessingShape");
        writer->startElement("wps:cNvSpPr");
        writer->endElement();
        writer->startElement("wps:spPr");
        writer->startElement("a:xfrm");
        if (shape->flipH) { writer->addAttribute("flipH", "1"); }
        if (shape->flipV) { writer->addAttribute("flipV", "1"); }
        if (m_frameRotation) { writer->addAttribute("rot", m_frameRotation); }
        writer->startElement("a:off");
        writer->addAttribute("x", 0);
        writer->addAttribute("y", 0);
        writer->endElement();
        writer->startElement("a:ext");
        writer->addAttribute("cx", QString::number(cx));
        writer->addAttribute("cy", QString::number(cy));
        writer->endElement();
        writer->endElement();
        writer->startElement("a:prstGeom");
        writer->addAttribute("prst", shape->preset);
        writer->startElement("a:avLst");
        writer->endElement();
        writer->endElement();
        const auto fill = [writer](const QString &color) {
            if (color.isEmpty()) {
                writer->startElement("a:noFill");
                writer->endElement();
            } else {
                writer->startElement("a:solidFill");
                writer->startElement("a:srgbClr");
                writer->addAttribute("val", color.mid(1));
                writer->endElement();
                writer->endElement();
            }
        };
        fill(shape->fill);
        writer->startElement("a:ln");
        writer->addAttribute("w", 12700);
        fill(shape->stroke);
        writer->endElement();
        writer->endElement(); // spPr
        if (!shape->text.isEmpty()) {
            writer->startElement("wps:txbx");
            writer->startElement("w:txbxContent");
            for (const QString &paragraph : shape->text.split(QLatin1Char('\n'))) {
                writer->startElement("w:p");
                writer->startElement("w:r");
                writer->startElement("w:t");
                writer->addAttribute("xml:space", "preserve");
                writer->addTextNode(paragraph);
                writer->endElement();
                writer->endElement();
                writer->endElement();
            }
            writer->endElement();
            writer->endElement();
        }
        writer->startElement("wps:bodyPr");
        writer->addAttribute("anchor", "ctr");
        writer->endElement();
        writer->endElement(); // wsp
    } else if (chart) {
        writer->addAttribute("uri", "http://schemas.openxmlformats.org/drawingml/2006/chart");
        writer->startElement("c:chart");
        writer->addAttribute("xmlns:c", "http://schemas.openxmlformats.org/drawingml/2006/chart");
        writer->addAttribute("r:id", image.relationshipId);
        writer->endElement();
    } else {
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
        const QRegularExpression clipExpression(QStringLiteral("^\\s*rect\\(\\s*([^,]+),\\s*([^,]+),\\s*([^,]+),\\s*([^,]+)\\s*\\)\\s*$"));
        const QRegularExpressionMatch clipMatch = clipExpression.match(m_frameClip);
        if (clipMatch.hasMatch()) {
            const qreal top = KoUnit::parseValue(clipMatch.captured(1), 0.0);
            const qreal right = KoUnit::parseValue(clipMatch.captured(2), 0.0);
            const qreal bottom = KoUnit::parseValue(clipMatch.captured(3), 0.0);
            const qreal left = KoUnit::parseValue(clipMatch.captured(4), 0.0);
            // ODF stores clip distances in points. PictureShape uses its natural
            // raster size when it serializes fo:clip. Convert those distances
            // using the raster's physical resolution before writing percentages.
            QBuffer sourceBuffer(&m_imageData[sourcePath]);
            sourceBuffer.open(QIODevice::ReadOnly);
            QImageReader sourceReader(&sourceBuffer);
            const QImage sourceImage = sourceReader.read();
            const int dpmX = sourceImage.dotsPerMeterX();
            const int dpmY = sourceImage.dotsPerMeterY();
            if (!sourceImage.isNull()) {
                // 1 meter = 2834.6457 points. A zero DPM means the image has no
                // physical resolution metadata; ODF uses the 96 dpi default.
                const qreal pointsPerPixelX = dpmX > 0 ? 2834.645669 / dpmX : 0.75;
                const qreal pointsPerPixelY = dpmY > 0 ? 2834.645669 / dpmY : 0.75;
                const int l = qBound(0, qRound(left * 100000.0 / (sourceImage.width() * pointsPerPixelX)), 99999);
                const int r = qBound(0, qRound(right * 100000.0 / (sourceImage.width() * pointsPerPixelX)), 99999);
                const int t = qBound(0, qRound(top * 100000.0 / (sourceImage.height() * pointsPerPixelY)), 99999);
                const int b = qBound(0, qRound(bottom * 100000.0 / (sourceImage.height() * pointsPerPixelY)), 99999);
                if (l || r || t || b) {
                    writer->startElement("a:srcRect");
                    if (l) writer->addAttribute("l", l);
                    if (r) writer->addAttribute("r", r);
                    if (t) writer->addAttribute("t", t);
                    if (b) writer->addAttribute("b", b);
                    writer->endElement(); // a:srcRect
                }
            }
        }
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
    }
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
