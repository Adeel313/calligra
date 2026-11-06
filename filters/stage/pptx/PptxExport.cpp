/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "PptxExport.h"

#include <KoDocument.h>
#include <KoFilterChain.h>
#include <KoPageLayout.h>
#include <KoPAPageBase.h>
#include <KoPAPage.h>
#include <KoPAMasterPage.h>
#include <KoPADocument.h>
#include <KoColorBackground.h>
#include <KoGradientBackground.h>
#include <KoPatternBackground.h>
#include <KoImageShape.h>
#include <KoMediaShape.h>
#include <QMimeDatabase>
#include <QFileInfo>
#include <QUrl>
#include <KPrPageApplicationData.h>
#include <pageeffects/KPrPageEffect.h>
#include <KoImageData.h>
#include <KoPathShape.h>
#include <KoShapeContainer.h>
#include <KoShapeLayer.h>
#include <KoConnectionShape.h>
#include <KoShapePainter.h>
#include <KoShapeStroke.h>
#include <KoStore.h>
#include <KoTextShapeDataBase.h>
#include <KoTextShapeData.h>
#include <styles/KoTableCellStyle.h>
#include <styles/KoListLevelProperties.h>
#include <styles/KoParagraphStyle.h>
#include <styles/KoCharacterStyle.h>
#include <ChartShape.h>
#include <ChartProxyModel.h>
#include <PlotArea.h>
#include <DataSet.h>
#include <kochart_global.h>
#include <KPrNotes.h>
#include <KPrPage.h>
#include <KPrShapeAnimations.h>
#include <animations/KPrShapeAnimation.h>

#include <KPluginFactory>
#include <QBuffer>
#include <QImage>
#include <QAbstractItemModel>
#include <QHash>
#include <QPainterPath>
#include <QLinearGradient>
#include <QSet>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QXmlStreamWriter>

#include <functional>
#include <memory>

K_PLUGIN_FACTORY_WITH_JSON(PptxExportFactory, "calligra_filter_odp2pptx.json", registerPlugin<PptxExport>();)

namespace {
QByteArray xmlPart(const std::function<void(QXmlStreamWriter &)> &write)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter xml(&buffer);
    // OOXML parts are machine-readable; indentation adds CPU work and bytes.
    xml.setAutoFormatting(false);
    xml.writeStartDocument();
    write(xml);
    xml.writeEndDocument();
    return data;
}

bool addPart(KoStore *store, const QString &name, const QByteArray &data)
{
    if (!store->open(name)) return false;
    const bool wrote = store->write(data) == data.size();
    const bool closed = store->close();
    return wrote && closed;
}

void startShapeTree(QXmlStreamWriter &xml)
{
    xml.writeStartElement(QStringLiteral("p:spTree"));
    xml.writeStartElement(QStringLiteral("p:nvGrpSpPr"));
    xml.writeEmptyElement(QStringLiteral("p:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QStringLiteral("1")); xml.writeAttribute(QStringLiteral("name"), QStringLiteral(""));
    xml.writeEmptyElement(QStringLiteral("p:cNvGrpSpPr"));
    xml.writeEmptyElement(QStringLiteral("p:nvPr")); xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("p:grpSpPr"));
    xml.writeStartElement(QStringLiteral("a:xfrm"));
    for (const QString &tag : {QStringLiteral("a:off"), QStringLiteral("a:ext"), QStringLiteral("a:chOff"), QStringLiteral("a:chExt")}) {
        xml.writeEmptyElement(tag);
        if (tag == QLatin1String("a:off") || tag == QLatin1String("a:chOff")) { xml.writeAttribute(QStringLiteral("x"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("y"), QStringLiteral("0")); }
        else { xml.writeAttribute(QStringLiteral("cx"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("cy"), QStringLiteral("0")); }
    }
    xml.writeEndElement(); xml.writeEndElement();
}

struct SlideObject {
    KoShape *shape = nullptr;
    KoTextShapeDataBase *textData = nullptr;
    QTextTable *table = nullptr;
    QByteArray imageData;
    bool imageFill = false;
    QRectF imageCrop = QRectF(0, 0, 1, 1);
    QString imageSuffix;
    QByteArray chartData;
    QByteArray mediaData;
    QString mediaSuffix;
    QString mediaMimeType;
    QString mediaExternalTarget;
    int mediaRelationshipId = 0;
    int mediaEmbedRelationshipId = 0;
    QHash<QString, int> hyperlinkIds;
    int imageId = 0;
    int chartId = 0;
    int relationshipId = 0;
    int objectId = 0;
    bool connector = false;
    bool groupStart = false;
    bool groupEnd = false;
    bool inGroup = false;
};

QByteArray chartXml(const KoChart::ChartShape *chart)
{
    const auto dataSets = chart->proxyModel()->dataSets();
    if (dataSets.isEmpty()) return {};
    QString type;
    switch (chart->chartType()) {
    case KoChart::BarChartType: type = QStringLiteral("barChart"); break;
    case KoChart::LineChartType: type = QStringLiteral("lineChart"); break;
    case KoChart::AreaChartType: type = QStringLiteral("areaChart"); break;
    case KoChart::CircleChartType: type = QStringLiteral("pieChart"); break;
    case KoChart::ScatterChartType: type = QStringLiteral("scatterChart"); break;
    case KoChart::RingChartType: type = QStringLiteral("doughnutChart"); break;
    default: return {};
    }
    const bool scatter = type == QLatin1String("scatterChart");
    const bool horizontal = chart->chartType() == KoChart::BarChartType && chart->plotArea()->isVertical();
    const bool pie = type == QLatin1String("pieChart") || type == QLatin1String("doughnutChart");
    QString grouping = QStringLiteral("standard");
    if (chart->chartSubType() == KoChart::StackedChartSubtype) grouping = QStringLiteral("stacked");
    else if (chart->chartSubType() == KoChart::PercentChartSubtype) grouping = QStringLiteral("percentStacked");
    else if (type == QLatin1String("barChart")) grouping = QStringLiteral("clustered");
    return xmlPart([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("c:chartSpace"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"), QStringLiteral("c"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
        xml.writeStartElement(QStringLiteral("c:chart"));
        xml.writeStartElement(QStringLiteral("c:plotArea")); xml.writeEmptyElement(QStringLiteral("c:layout"));
        xml.writeStartElement(QStringLiteral("c:") + type);
        if (type == QLatin1String("barChart")) { xml.writeEmptyElement(QStringLiteral("c:barDir")); xml.writeAttribute(QStringLiteral("val"), chart->plotArea()->isVertical() ? QStringLiteral("bar") : QStringLiteral("col")); }
        if (scatter) { xml.writeEmptyElement(QStringLiteral("c:scatterStyle")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("lineMarker")); }
        if (!pie && !scatter) { xml.writeEmptyElement(QStringLiteral("c:grouping")); xml.writeAttribute(QStringLiteral("val"), grouping); }
        xml.writeEmptyElement(QStringLiteral("c:varyColors")); xml.writeAttribute(QStringLiteral("val"), pie ? QStringLiteral("1") : QStringLiteral("0"));
        const int seriesCount = pie ? 1 : dataSets.size();
        for (int series = 0; series < seriesCount; ++series) {
            const auto *data = dataSets.at(series);
            xml.writeStartElement(QStringLiteral("c:ser"));
            xml.writeEmptyElement(QStringLiteral("c:idx")); xml.writeAttribute(QStringLiteral("val"), QString::number(series));
            xml.writeEmptyElement(QStringLiteral("c:order")); xml.writeAttribute(QStringLiteral("val"), QString::number(series));
            xml.writeStartElement(QStringLiteral("c:tx")); xml.writeTextElement(QStringLiteral("c:v"), data->labelData().toString()); xml.writeEndElement();
            auto values = [&](const QString &element, bool numeric, bool x) {
                xml.writeStartElement(element);
                xml.writeStartElement(numeric ? QStringLiteral("c:numLit") : QStringLiteral("c:strLit"));
                if (numeric) xml.writeTextElement(QStringLiteral("c:formatCode"), QStringLiteral("General"));
                xml.writeEmptyElement(QStringLiteral("c:ptCount")); xml.writeAttribute(QStringLiteral("val"), QString::number(data->size()));
                for (int point = 0; point < data->size(); ++point) {
                    const QVariant value = x ? (scatter ? data->xData(point) : data->categoryData(point)) : data->yData(point);
                    QString text = value.toString();
                    if (numeric) {
                        bool ok = false;
                        const double number = value.toDouble(&ok);
                        if (!ok) continue;
                        text = QString::number(number, 'g', 15);
                    }
                    xml.writeStartElement(QStringLiteral("c:pt")); xml.writeAttribute(QStringLiteral("idx"), QString::number(point));
                    xml.writeTextElement(QStringLiteral("c:v"), text); xml.writeEndElement();
                }
                xml.writeEndElement(); xml.writeEndElement();
            };
            values(scatter ? QStringLiteral("c:xVal") : QStringLiteral("c:cat"), scatter, true);
            values(scatter ? QStringLiteral("c:yVal") : QStringLiteral("c:val"), true, false);
            xml.writeEndElement();
        }
        if (type == QLatin1String("barChart") && grouping != QLatin1String("clustered")) { xml.writeEmptyElement(QStringLiteral("c:overlap")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("100")); }
        if (!pie) { xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000001")); xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000002")); }
        else if (type == QLatin1String("doughnutChart")) { xml.writeEmptyElement(QStringLiteral("c:holeSize")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("50")); }
        xml.writeEndElement();
        if (!pie) {
            xml.writeStartElement(scatter ? QStringLiteral("c:valAx") : QStringLiteral("c:catAx")); xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000001"));
            xml.writeStartElement(QStringLiteral("c:scaling")); xml.writeEmptyElement(QStringLiteral("c:orientation")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("minMax")); xml.writeEndElement(); xml.writeEmptyElement(QStringLiteral("c:axPos")); xml.writeAttribute(QStringLiteral("val"), horizontal ? QStringLiteral("l") : QStringLiteral("b")); xml.writeEmptyElement(QStringLiteral("c:crossAx")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000002")); xml.writeEmptyElement(QStringLiteral("c:crosses")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("autoZero")); if (!scatter) { xml.writeEmptyElement(QStringLiteral("c:lblOffset")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("100")); } xml.writeEndElement();
            xml.writeStartElement(QStringLiteral("c:valAx")); xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000002")); xml.writeStartElement(QStringLiteral("c:scaling")); xml.writeEmptyElement(QStringLiteral("c:orientation")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("minMax")); xml.writeEndElement(); xml.writeEmptyElement(QStringLiteral("c:axPos")); xml.writeAttribute(QStringLiteral("val"), horizontal ? QStringLiteral("b") : QStringLiteral("l")); xml.writeEmptyElement(QStringLiteral("c:crossAx")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000001")); xml.writeEmptyElement(QStringLiteral("c:crosses")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("autoZero")); xml.writeEmptyElement(QStringLiteral("c:crossBetween")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("between")); xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("c:legend")); xml.writeEmptyElement(QStringLiteral("c:legendPos")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("b")); xml.writeEmptyElement(QStringLiteral("c:layout")); xml.writeEmptyElement(QStringLiteral("c:overlay")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("0")); xml.writeEndElement();
        xml.writeEmptyElement(QStringLiteral("c:plotVisOnly")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("1"));
        xml.writeEndElement(); xml.writeEndElement();
    });
}

// Raster formats that PowerPoint can embed as-is. Keeping the original bytes
// avoids re-encoding, preserves quality, and skips the expensive QPainter
// initialisation that a full re-render would trigger.
bool isEmbeddableImageSuffix(const QString &suffix)
{
    static const QSet<QString> suffixes{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                        QStringLiteral("gif"), QStringLiteral("bmp"), QStringLiteral("tif"),
                                        QStringLiteral("tiff")};
    return suffixes.contains(suffix);
}

QString imageContentType(const QString &suffix)
{
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) return QStringLiteral("image/jpeg");
    if (suffix == QLatin1String("gif")) return QStringLiteral("image/gif");
    if (suffix == QLatin1String("bmp")) return QStringLiteral("image/bmp");
    if (suffix == QLatin1String("tif") || suffix == QLatin1String("tiff")) return QStringLiteral("image/tiff");
    return QStringLiteral("image/png");
}

QByteArray shapeImage(KoShape *shape)
{
    if (!shape) return {};
    // KoShapePainter paints into the supplied image and does not allocate it.
    // Render at twice the shape's point dimensions to retain readable detail
    // while keeping embedded slide images reasonably small.
    const QSizeF shapeSize = shape->size();
    const QSize pixelSize(qBound(1, qRound(shapeSize.width() * 2.0), 8192),
                          qBound(1, qRound(shapeSize.height() * 2.0), 8192));
    QImage image(pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    KoShapePainter painter;
    painter.setShapes({shape});
    painter.paint(image);
    if (image.isNull()) return {};
    QByteArray data;
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return data;
}

void collectShape(KoShape *shape, QList<SlideObject> &objects, int &nextObjectId, bool inGroup = false)
{
    if (!shape) return;
    // A page owns its shapes through layers. A layer is an invisible
    // organisational container, not a group shape: recurse into its children
    // and never rasterise the (often empty) layer itself.
    if (auto *layer = dynamic_cast<KoShapeLayer *>(shape)) {
        for (KoShape *child : layer->shapes()) collectShape(child, objects, nextObjectId, inGroup);
        return;
    }
    SlideObject object;
    object.shape = shape;
    object.inGroup = inGroup;
    object.connector = dynamic_cast<KoConnectionShape *>(shape) != nullptr;
    // ODF stores ordinary straight connectors as draw:path.  The ODF loader
    // can therefore return a KoPathShape instead of KoConnectionShape; retain
    // the connector element for line-only paths so consumers can edit it as a
    // connector after reopening the PPTX.
    if (!object.connector) {
        if (auto *path = dynamic_cast<KoPathShape *>(shape)) {
            const QRectF bounds = path->outline().boundingRect();
            const QPainterPath outline = path->outline();
            object.connector = bounds.width() < 0.01 || bounds.height() < 0.01
                || (outline.elementCount() == 2 && outline.elementAt(1).isLineTo());
        }
    }
    if (auto *media = dynamic_cast<KoMediaShape *>(shape)) {
        QString mime = media->mediaMimeType();
        if (!mime.startsWith(QLatin1String("video/")))
            mime = QMimeDatabase().mimeTypeForFile(QUrl(media->mediaFileName()).path(), QMimeDatabase::MatchExtension).name();
        if (mime.startsWith(QLatin1String("video/"))) {
            object.mediaData = media->mediaData();
            if (object.mediaData.isEmpty() && !QUrl(media->mediaFileName()).isRelative())
                object.mediaExternalTarget = media->mediaFileName();
            if (!object.mediaData.isEmpty() || !object.mediaExternalTarget.isEmpty()) {
                object.mediaMimeType = mime;
                object.mediaSuffix = QFileInfo(QUrl(media->mediaFileName()).path()).suffix().toLower();
                if (object.mediaSuffix.isEmpty()) object.mediaSuffix = QStringLiteral("mp4");
                object.objectId = nextObjectId++;
                object.imageSuffix = QStringLiteral("png");
                QBuffer buffer(&object.imageData);
                if (buffer.open(QIODevice::WriteOnly)) media->mediaPreview().save(&buffer, "PNG");
                if (object.imageData.isEmpty()) object.imageData = shapeImage(shape);
                objects.append(object);
                return;
            }
        }
    }
    object.textData = qobject_cast<KoTextShapeDataBase *>(shape->userData());
    if (auto *pattern = dynamic_cast<KoPatternBackground *>(shape->background().data())) {
        KoImageData *data = pattern->patternData();
        if (data && isEmbeddableImageSuffix(data->suffix().toLower())) {
            QBuffer buffer(&object.imageData);
            if (buffer.open(QIODevice::WriteOnly) && data->saveData(buffer)) {
                object.imageFill = true;
                object.imageSuffix = data->suffix().toLower();
            }
        }
        if (!object.imageFill) {
            object.imageData.clear();
            QBuffer buffer(&object.imageData);
            if (buffer.open(QIODevice::WriteOnly) && pattern->pattern().save(&buffer, "PNG")) {
                object.imageFill = true;
                object.imageSuffix = QStringLiteral("png");
            }
        }
    }
    if (shape->shapeId() == QLatin1String("VectorShapeID")) {
        object.objectId = nextObjectId++;
        object.imageData = shapeImage(shape);
        object.imageSuffix = QStringLiteral("png");
        if (!object.imageData.isEmpty()) objects.append(object);
        return;
    }
    // A picture can own auxiliary text (LibreOffice writes even an empty
    // paragraph inside draw:image). Its image payload must take precedence
    // over that text child, otherwise it becomes an empty text shape.
    if (!object.textData && !qobject_cast<KoImageData *>(shape->userData())) {
        if (auto *container = dynamic_cast<KoTosContainer *>(shape)) {
            for (KoShape *child : container->shapes()) {
                object.textData = qobject_cast<KoTextShapeDataBase *>(child->userData());
                if (object.textData) break;
            }
        }
    }
    object.objectId = nextObjectId++;
    if (object.textData) {
        const auto frames = object.textData->document()->rootFrame()->childFrames();
        if (frames.size() == 1) object.table = qobject_cast<QTextTable *>(frames.first());
        objects.append(object);
        return;
    }
    if (auto *image = qobject_cast<KoImageData *>(shape->userData())) {
        if (auto *imageShape = dynamic_cast<KoImageShape *>(shape)) object.imageCrop = imageShape->cropRect();
        QByteArray data;
        QBuffer buffer(&data);
        QString suffix;
        if (buffer.open(QIODevice::WriteOnly) && image->saveData(buffer)) {
            suffix = image->suffix().toLower();
        }
        if (data.isEmpty() || !isEmbeddableImageSuffix(suffix)) {
            // The source is not a format PowerPoint embeds directly (for
            // example SVG or WMF); fall back to a PNG rendering.
            data = shapeImage(shape);
            suffix = QStringLiteral("png");
        }
        if (!data.isEmpty()) {
            object.imageData = data;
            object.imageSuffix = suffix;
            objects.append(object);
        }
        return;
    }
    if (auto *chart = dynamic_cast<KoChart::ChartShape *>(shape)) {
        object.chartData = chartXml(chart);
        if (object.chartData.isEmpty()) {
            object.imageData = shapeImage(shape);
            object.imageSuffix = QStringLiteral("png");
        }
        if (!object.chartData.isEmpty() || !object.imageData.isEmpty()) objects.append(object);
        return;
    }
    if (dynamic_cast<KoPathShape *>(shape)) {
        objects.append(object);
        return;
    }
    if (auto *container = dynamic_cast<KoShapeContainer *>(shape); container && !container->shapes().isEmpty()) {
        SlideObject group;
        group.shape = shape;
        group.objectId = nextObjectId++;
        group.inGroup = inGroup;
        group.groupStart = true;
        objects.append(group);
        for (KoShape *child : container->shapes()) collectShape(child, objects, nextObjectId, true);
        group.groupStart = false;
        group.groupEnd = true;
        objects.append(group);
        return;
    }
    // Nothing to represent natively. Do not rasterise empty containers or
    // unsupported shapes: a transparent bitmap would only bloat the package.
    if (dynamic_cast<KoShapeContainer *>(shape)) return;
    object.imageData = shapeImage(shape);
    if (!object.imageData.isEmpty()) {
        object.imageSuffix = QStringLiteral("png");
        objects.append(object);
    }
}

int pathCoord(qreal value, qreal extent)
{
    if (extent <= 0) return 0;
    // Bezier control points may lie outside the curve's bounding rectangle.
    // DrawingML permits these coordinates; clamping changes the curve.
    return qRound(value * 100000.0 / extent);
}

void writePathGeometry(QXmlStreamWriter &xml, KoShape *shape, const QPainterPath &mappedPath = {})
{
    const QPainterPath path = mappedPath.isEmpty() ? shape->outline() : mappedPath;
    const QRectF bounds = path.boundingRect();
    if (path.isEmpty()) {
        xml.writeStartElement(QStringLiteral("a:prstGeom")); xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect")); xml.writeEmptyElement(QStringLiteral("a:avLst")); xml.writeEndElement();
        return;
    }
    xml.writeStartElement(QStringLiteral("a:custGeom"));
    xml.writeEmptyElement(QStringLiteral("a:avLst")); xml.writeEmptyElement(QStringLiteral("a:gdLst")); xml.writeEmptyElement(QStringLiteral("a:ahLst")); xml.writeEmptyElement(QStringLiteral("a:cxnLst"));
    xml.writeStartElement(QStringLiteral("a:rect")); xml.writeAttribute(QStringLiteral("l"), QStringLiteral("l")); xml.writeAttribute(QStringLiteral("t"), QStringLiteral("t")); xml.writeAttribute(QStringLiteral("r"), QStringLiteral("r")); xml.writeAttribute(QStringLiteral("b"), QStringLiteral("b")); xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("a:pathLst")); xml.writeStartElement(QStringLiteral("a:path")); xml.writeAttribute(QStringLiteral("w"), QStringLiteral("100000")); xml.writeAttribute(QStringLiteral("h"), QStringLiteral("100000"));
    auto point = [&](qreal x, qreal y) { xml.writeStartElement(QStringLiteral("a:pt")); xml.writeAttribute(QStringLiteral("x"), QString::number(pathCoord(x - bounds.left(), bounds.width()))); xml.writeAttribute(QStringLiteral("y"), QString::number(pathCoord(y - bounds.top(), bounds.height()))); xml.writeEndElement(); };
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo()) { xml.writeStartElement(QStringLiteral("a:moveTo")); point(e.x, e.y); xml.writeEndElement(); }
        else if (e.isLineTo()) { xml.writeStartElement(QStringLiteral("a:lnTo")); point(e.x, e.y); xml.writeEndElement(); }
        else if (e.type == QPainterPath::CurveToElement && i + 2 < path.elementCount()) {
            const auto c2 = path.elementAt(i + 1); const auto end = path.elementAt(i + 2);
            xml.writeStartElement(QStringLiteral("a:cubicBezTo")); point(e.x, e.y); point(c2.x, c2.y); point(end.x, end.y); xml.writeEndElement();
            i += 2;
        }
    }
    xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
}

void writeFill(QXmlStreamWriter &xml, const QSharedPointer<KoShapeBackground> &background, int fillImageRel = 0)
{
    const auto *solid = background ? dynamic_cast<const KoColorBackground *>(background.data()) : nullptr;
    const auto *gradientBackground = background ? dynamic_cast<const KoGradientBackground *>(background.data()) : nullptr;
    const QGradient *gradient = gradientBackground ? gradientBackground->gradient() : nullptr;
    if (fillImageRel) {
        xml.writeStartElement(QStringLiteral("a:blipFill"));
        xml.writeEmptyElement(QStringLiteral("a:blip"));
        xml.writeAttribute(QStringLiteral("r:embed"), QStringLiteral("rId%1").arg(fillImageRel));
        xml.writeStartElement(QStringLiteral("a:stretch"));
        xml.writeEmptyElement(QStringLiteral("a:fillRect"));
        xml.writeEndElement(); xml.writeEndElement();
    } else if (solid && solid->color().isValid() && solid->color().alpha() > 0) {
        xml.writeStartElement(QStringLiteral("a:solidFill"));
        xml.writeStartElement(QStringLiteral("a:srgbClr"));
        xml.writeAttribute(QStringLiteral("val"), solid->color().name().mid(1).toUpper());
        if (solid->color().alphaF() < 1) { xml.writeEmptyElement(QStringLiteral("a:alpha")); xml.writeAttribute(QStringLiteral("val"), QString::number(qRound(solid->color().alphaF() * 100000))); }
        xml.writeEndElement();
        xml.writeEndElement();
    } else if (gradient && gradient->type() == QGradient::LinearGradient && gradient->stops().size() >= 2) {
        const auto *linear = static_cast<const QLinearGradient *>(gradient);
        const QPointF direction = linear->finalStop() - linear->start();
        // Both Qt and DrawingML use the screen coordinate system (Y down).
        // DrawingML's positive fixed angle must also wrap into [0, 360).
        const qreal degrees = qRadiansToDegrees(std::atan2(direction.y(), direction.x()));
        const int fullTurn = 360 * 60000;
        const int angle = (qRound(degrees * 60000.0) % fullTurn + fullTurn) % fullTurn;
        xml.writeStartElement(QStringLiteral("a:gradFill"));
        xml.writeAttribute(QStringLiteral("rotWithShape"), QStringLiteral("1"));
        xml.writeStartElement(QStringLiteral("a:gsLst"));
        for (const auto &stop : gradient->stops()) {
            xml.writeStartElement(QStringLiteral("a:gs"));
            xml.writeAttribute(QStringLiteral("pos"), QString::number(qBound(0, qRound(stop.first * 100000.0), 100000)));
            xml.writeStartElement(QStringLiteral("a:srgbClr"));
            xml.writeAttribute(QStringLiteral("val"), stop.second.name().mid(1).toUpper());
            if (stop.second.alphaF() < 1) { xml.writeEmptyElement(QStringLiteral("a:alpha")); xml.writeAttribute(QStringLiteral("val"), QString::number(qRound(stop.second.alphaF() * 100000))); }
            xml.writeEndElement();
            xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeEmptyElement(QStringLiteral("a:lin"));
        xml.writeAttribute(QStringLiteral("ang"), QString::number(angle));
        xml.writeAttribute(QStringLiteral("scaled"), QStringLiteral("1"));
        xml.writeEndElement();
    } else if (gradient && !gradient->stops().isEmpty()) {
        // OOXML also supports path gradients, but this exporter currently
        // writes their center color until path-gradient geometry is modeled.
        const QColor color = gradient->stops().at(gradient->stops().size() / 2).second;
        xml.writeStartElement(QStringLiteral("a:solidFill"));
        xml.writeEmptyElement(QStringLiteral("a:srgbClr"));
        xml.writeAttribute(QStringLiteral("val"), color.name().mid(1).toUpper());
        xml.writeEndElement();
    } else {
        xml.writeEmptyElement(QStringLiteral("a:noFill"));
    }
}

void writeFillAndLine(QXmlStreamWriter &xml, KoShape *shape, int fillImageRel = 0)
{
    writeFill(xml, shape->background(), fillImageRel);
    if (auto *stroke = dynamic_cast<KoShapeStroke *>(shape->stroke())) {
        xml.writeStartElement(QStringLiteral("a:ln")); xml.writeAttribute(QStringLiteral("w"), QString::number(qMax(1, qRound64(stroke->lineWidth() * 12700.0))));
        xml.writeStartElement(QStringLiteral("a:solidFill")); xml.writeEmptyElement(QStringLiteral("a:srgbClr")); xml.writeAttribute(QStringLiteral("val"), stroke->color().name().mid(1).toUpper()); xml.writeEndElement();
        xml.writeEmptyElement(QStringLiteral("a:prstDash")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("solid")); xml.writeEndElement();
    } else { xml.writeStartElement(QStringLiteral("a:ln")); xml.writeEmptyElement(QStringLiteral("a:noFill")); xml.writeEndElement(); }
}

void writeTextBody(QXmlStreamWriter &xml, const SlideObject &object, QTextDocument *cellDocument = nullptr,
                   const QString &tag = QStringLiteral("p:txBody"))
{
    QTextDocument *document = cellDocument ? cellDocument : (object.textData ? object.textData->document() : nullptr);
    if (!document) return;
    xml.writeStartElement(tag);
    xml.writeStartElement(QStringLiteral("a:bodyPr"));
    KoInsets insets = cellDocument ? KoInsets() : object.textData->shapeMargins();
    if (!cellDocument) {
        if (auto *textData = dynamic_cast<KoTextShapeData *>(object.textData)) {
            insets.left += textData->leftPadding();
            insets.top += textData->topPadding();
            insets.right += textData->rightPadding();
            insets.bottom += textData->bottomPadding();
        }
    }
    xml.writeAttribute(QStringLiteral("lIns"), QString::number(qRound64(insets.left * 12700)));
    xml.writeAttribute(QStringLiteral("tIns"), QString::number(qRound64(insets.top * 12700)));
    xml.writeAttribute(QStringLiteral("rIns"), QString::number(qRound64(insets.right * 12700)));
    xml.writeAttribute(QStringLiteral("bIns"), QString::number(qRound64(insets.bottom * 12700)));
    xml.writeAttribute(QStringLiteral("wrap"), QStringLiteral("square"));
    const Qt::Alignment vertical = object.textData->verticalAlignment();
    xml.writeAttribute(QStringLiteral("anchor"), vertical.testFlag(Qt::AlignBottom) ? QStringLiteral("b") : vertical.testFlag(Qt::AlignVCenter) ? QStringLiteral("ctr") : QStringLiteral("t"));
    if (object.textData->resizeMethod() == KoTextShapeDataBase::ShrinkToFitResize) {
        xml.writeEmptyElement(QStringLiteral("a:normAutofit"));
        if (object.textData->autofitFontScale() > 0)
            xml.writeAttribute(QStringLiteral("fontScale"), QString::number(object.textData->autofitFontScale()));
        if (object.textData->autofitLineSpacingReduction() > 0)
            xml.writeAttribute(QStringLiteral("lnSpcReduction"), QString::number(object.textData->autofitLineSpacingReduction()));
    }
    xml.writeEndElement();
    xml.writeEmptyElement(QStringLiteral("a:lstStyle"));
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        xml.writeStartElement(QStringLiteral("a:p"));
        xml.writeStartElement(QStringLiteral("a:pPr"));
        if (block.blockFormat().alignment() & Qt::AlignHCenter) xml.writeAttribute(QStringLiteral("algn"), QStringLiteral("ctr"));
        else if (block.blockFormat().alignment() & Qt::AlignRight) xml.writeAttribute(QStringLiteral("algn"), QStringLiteral("r"));
        else if (block.blockFormat().alignment() & Qt::AlignJustify) xml.writeAttribute(QStringLiteral("algn"), QStringLiteral("just"));
        const QTextBlockFormat blockFormat = block.blockFormat();
        KoParagraphStyle paragraphStyle(blockFormat, block.charFormat());
        QTextList *list = block.textList();
        const KoListLevelProperties properties = list ? KoListLevelProperties::fromTextList(list) : KoListLevelProperties();
        qreal margin = paragraphStyle.leftMargin();
        qreal indent = paragraphStyle.textIndent();
        if (list && properties.alignmentMode()) {
            if (qFuzzyIsNull(margin)) margin = properties.margin();
            if (qFuzzyIsNull(indent)) indent = properties.textIndent();
        } else if (list) {
            // Legacy ODF lists reserve a label box after text:space-before.
            if (qFuzzyIsNull(margin)) margin = properties.indent() + properties.minimumWidth();
            if (qFuzzyIsNull(indent)) indent = -properties.minimumWidth();
        }
        xml.writeAttribute(QStringLiteral("marL"), QString::number(qRound64(margin * 12700)));
        xml.writeAttribute(QStringLiteral("marR"), QString::number(qRound64(paragraphStyle.rightMargin() * 12700)));
        xml.writeAttribute(QStringLiteral("indent"), QString::number(qRound64(indent * 12700)));
        if (blockFormat.layoutDirection() == Qt::RightToLeft)
            xml.writeAttribute(QStringLiteral("rtl"), QStringLiteral("1"));
        if (list) xml.writeAttribute(QStringLiteral("lvl"), QString::number(qBound(0, properties.level() - 1, 8)));
        const auto spacing = [&](const QString &tag, const QString &unit, qreal value) {
            xml.writeStartElement(tag);
            xml.writeEmptyElement(unit);
            xml.writeAttribute(QStringLiteral("val"), QString::number(qMax(0, qRound(value))));
            xml.writeEndElement();
        };
        if (paragraphStyle.lineHeightAbsolute() > 0)
            spacing(QStringLiteral("a:lnSpc"), QStringLiteral("a:spcPts"), paragraphStyle.lineHeightAbsolute() * 100);
        else if (paragraphStyle.lineHeightPercent() > 0)
            spacing(QStringLiteral("a:lnSpc"), QStringLiteral("a:spcPct"), paragraphStyle.lineHeightPercent() * 1000);
        else if (blockFormat.lineHeightType() == QTextBlockFormat::FixedHeight)
            spacing(QStringLiteral("a:lnSpc"), QStringLiteral("a:spcPts"), blockFormat.lineHeight() * 100);
        else if (blockFormat.lineHeightType() == QTextBlockFormat::ProportionalHeight)
            spacing(QStringLiteral("a:lnSpc"), QStringLiteral("a:spcPct"), blockFormat.lineHeight() * 1000);
        spacing(QStringLiteral("a:spcBef"), QStringLiteral("a:spcPts"), paragraphStyle.topMargin() * 100);
        spacing(QStringLiteral("a:spcAft"), QStringLiteral("a:spcPts"), paragraphStyle.bottomMargin() * 100);
        if (list) {
            if (properties.labelType() == KoListStyle::BulletCharLabelType) {
                const auto bulletStyle = properties.characterProperties();
                if (bulletStyle && !bulletStyle->fontFamily().isEmpty()) {
                    xml.writeEmptyElement(QStringLiteral("a:buFont"));
                    xml.writeAttribute(QStringLiteral("typeface"), bulletStyle->fontFamily());
                }
                xml.writeEmptyElement(QStringLiteral("a:buChar"));
                xml.writeAttribute(QStringLiteral("char"), properties.bulletCharacter().isNull() ? QStringLiteral("•") : QString(properties.bulletCharacter()));
            } else {
                QString numberType = QStringLiteral("arabic");
                switch (properties.numberFormat()) {
                case KoOdfNumberDefinition::AlphabeticLowerCase: numberType = QStringLiteral("alphaLc"); break;
                case KoOdfNumberDefinition::AlphabeticUpperCase: numberType = QStringLiteral("alphaUc"); break;
                case KoOdfNumberDefinition::RomanLowerCase: numberType = QStringLiteral("romanLc"); break;
                case KoOdfNumberDefinition::RomanUpperCase: numberType = QStringLiteral("romanUc"); break;
                default: break;
                }
                if (properties.listItemSuffix() == QLatin1String(")"))
                    numberType += properties.listItemPrefix() == QLatin1String("(") ? QStringLiteral("ParenBoth") : QStringLiteral("ParenR");
                else numberType += QStringLiteral("Period");
                xml.writeEmptyElement(QStringLiteral("a:buAutoNum"));
                xml.writeAttribute(QStringLiteral("type"), numberType);
                xml.writeAttribute(QStringLiteral("startAt"), QString::number(qBound(1, properties.startValue() + list->itemNumber(block), 32767)));
            }
        } else {
            xml.writeEmptyElement(QStringLiteral("a:buNone"));
        }
        if (!blockFormat.tabPositions().isEmpty()) {
            xml.writeStartElement(QStringLiteral("a:tabLst"));
            for (const QTextOption::Tab &tab : blockFormat.tabPositions()) {
                xml.writeEmptyElement(QStringLiteral("a:tab"));
                xml.writeAttribute(QStringLiteral("pos"), QString::number(qRound64(tab.position * 12700)));
                xml.writeAttribute(QStringLiteral("algn"), tab.type == QTextOption::RightTab ? QStringLiteral("r") : tab.type == QTextOption::CenterTab ? QStringLiteral("ctr") : tab.type == QTextOption::DelimiterTab ? QStringLiteral("dec") : QStringLiteral("l"));
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
        for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            QTextCharFormat fmt = block.charFormat();
            fmt.merge(fragment.charFormat());
            QString runText = fragment.text();
            runText.replace(QChar::LineSeparator, QLatin1Char('\n'));
            const QStringList lines = runText.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            for (int line = 0; line < lines.size(); ++line) {
                if (line > 0)
                    xml.writeEmptyElement(QStringLiteral("a:br"));
            xml.writeStartElement(QStringLiteral("a:r")); xml.writeStartElement(QStringLiteral("a:rPr"));
            xml.writeAttribute(QStringLiteral("lang"), QStringLiteral("en-US"));
            const qreal points = fmt.fontPointSize() > 0 ? fmt.fontPointSize() : document->defaultFont().pointSizeF() > 0 ? document->defaultFont().pointSizeF() : 18.0;
            xml.writeAttribute(QStringLiteral("sz"), QString::number(qRound(points * 100.0)));
            if (fmt.fontWeight() >= QFont::DemiBold) xml.writeAttribute(QStringLiteral("b"), QStringLiteral("1"));
            if (fmt.fontItalic()) xml.writeAttribute(QStringLiteral("i"), QStringLiteral("1"));
            if (fmt.fontUnderline()) xml.writeAttribute(QStringLiteral("u"), QStringLiteral("sng"));
            if (fmt.fontStrikeOut()) xml.writeAttribute(QStringLiteral("strike"), QStringLiteral("sngStrike"));
            if (fmt.verticalAlignment() == QTextCharFormat::AlignSuperScript)
                xml.writeAttribute(QStringLiteral("baseline"), QStringLiteral("30000"));
            else if (fmt.verticalAlignment() == QTextCharFormat::AlignSubScript)
                xml.writeAttribute(QStringLiteral("baseline"), QStringLiteral("-25000"));
            if (fmt.fontCapitalization() == QFont::AllUppercase)
                xml.writeAttribute(QStringLiteral("cap"), QStringLiteral("all"));
            else if (fmt.fontCapitalization() == QFont::SmallCaps)
                xml.writeAttribute(QStringLiteral("cap"), QStringLiteral("small"));
            const qreal letterSpacing = fmt.hasProperty(KoCharacterStyle::FontLetterSpacing)
                ? fmt.doubleProperty(KoCharacterStyle::FontLetterSpacing)
                : fmt.fontLetterSpacingType() == QFont::AbsoluteSpacing ? fmt.fontLetterSpacing() : 0;
            if (!qFuzzyIsNull(letterSpacing))
                xml.writeAttribute(QStringLiteral("spc"), QString::number(qRound(letterSpacing * 100)));

            const QColor color = fmt.foreground().color();
            if (color.isValid()) { xml.writeStartElement(QStringLiteral("a:solidFill")); xml.writeEmptyElement(QStringLiteral("a:srgbClr")); xml.writeAttribute(QStringLiteral("val"), color.name().mid(1).toUpper()); xml.writeEndElement(); }
            if (fmt.background().style() != Qt::NoBrush && fmt.background().color().alpha() > 0) {
                xml.writeStartElement(QStringLiteral("a:highlight"));
                xml.writeEmptyElement(QStringLiteral("a:srgbClr"));
                xml.writeAttribute(QStringLiteral("val"), fmt.background().color().name().mid(1).toUpper());
                xml.writeEndElement();
            }
            const QStringList fontFamilies = fmt.fontFamilies().toStringList();
            xml.writeStartElement(QStringLiteral("a:latin")); xml.writeAttribute(QStringLiteral("typeface"), fontFamilies.isEmpty() ? QStringLiteral("Arial") : fontFamilies.first()); xml.writeEndElement();
            const QString hyperlink = fmt.anchorHref();
            if (fmt.isAnchor() && object.hyperlinkIds.contains(hyperlink)) {
                xml.writeEmptyElement(QStringLiteral("a:hlinkClick"));
                xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(object.hyperlinkIds.value(hyperlink)));
            }
            xml.writeEndElement(); xml.writeStartElement(QStringLiteral("a:t"));
            if (lines.at(line).startsWith(QLatin1Char(' ')) || lines.at(line).endsWith(QLatin1Char(' '))) xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
            xml.writeCharacters(lines.at(line)); xml.writeEndElement(); xml.writeEndElement();
            }
        }
        xml.writeStartElement(QStringLiteral("a:endParaRPr")); xml.writeAttribute(QStringLiteral("lang"), QStringLiteral("en-US")); xml.writeEndElement(); xml.writeEndElement();
    }
    xml.writeEndElement();
}

void writeObjectXml(QXmlStreamWriter &xml, const SlideObject &object, int imageRel)
{
    if (object.groupStart) {
        KoShape *shape = object.shape;
        const QTransform groupTransform = object.inGroup ? shape->transformation() : shape->absoluteTransformation(nullptr);

        const qreal groupScaleX = std::hypot(groupTransform.m11(), groupTransform.m12());
        const qreal groupScaleY = std::hypot(groupTransform.m21(), groupTransform.m22());
        const QSizeF groupSize = shape->size();
        const QSizeF placedSize(groupSize.width() * groupScaleX, groupSize.height() * groupScaleY);
        const QPointF groupPosition = groupTransform.map(QPointF(groupSize.width()/2, groupSize.height()/2))
            - QPointF(placedSize.width()/2, placedSize.height()/2);
        const qreal groupRotation = qRadiansToDegrees(std::atan2(groupTransform.m12(), groupTransform.m11()));
        const qint64 groupX = qRound64(groupPosition.x() * 12700.0);
        const qint64 groupY = qRound64(groupPosition.y() * 12700.0);
        const qint64 groupWidth = qMax<qint64>(1, qRound64(groupSize.width() * 12700.0));
        const qint64 groupHeight = qMax<qint64>(1, qRound64(groupSize.height() * 12700.0));
        xml.writeStartElement(QStringLiteral("p:grpSp"));
        xml.writeStartElement(QStringLiteral("p:nvGrpSpPr"));
        xml.writeEmptyElement(QStringLiteral("p:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QString::number(object.objectId)); xml.writeAttribute(QStringLiteral("name"), object.shape->name().isEmpty() ? QStringLiteral("Group %1").arg(object.objectId) : object.shape->name());
        xml.writeEmptyElement(QStringLiteral("p:cNvGrpSpPr")); xml.writeEmptyElement(QStringLiteral("p:nvPr")); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:grpSpPr")); xml.writeStartElement(QStringLiteral("a:xfrm"));
        if (!qFuzzyIsNull(groupRotation)) xml.writeAttribute(QStringLiteral("rot"), QString::number(qRound64(groupRotation * 60000)));
        if (groupTransform.determinant() < 0) xml.writeAttribute(QStringLiteral("flipV"), QStringLiteral("1"));
        xml.writeEmptyElement(QStringLiteral("a:off")); xml.writeAttribute(QStringLiteral("x"), QString::number(groupX)); xml.writeAttribute(QStringLiteral("y"), QString::number(groupY));
        xml.writeEmptyElement(QStringLiteral("a:ext")); xml.writeAttribute(QStringLiteral("cx"), QString::number(qMax<qint64>(1, qRound64(groupWidth * groupScaleX)))); xml.writeAttribute(QStringLiteral("cy"), QString::number(qMax<qint64>(1, qRound64(groupHeight * groupScaleY))));
        xml.writeEmptyElement(QStringLiteral("a:chOff")); xml.writeAttribute(QStringLiteral("x"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
        xml.writeEmptyElement(QStringLiteral("a:chExt")); xml.writeAttribute(QStringLiteral("cx"), QString::number(groupWidth)); xml.writeAttribute(QStringLiteral("cy"), QString::number(groupHeight));
        xml.writeEndElement(); xml.writeEndElement();
        return;
    }
    if (object.groupEnd) { xml.writeEndElement(); return; }
    if (!object.shape) return;
    KoShape *shape = object.shape;
    QPainterPath mappedPath;
    const QTransform transform = object.inGroup ? shape->transformation() : shape->absoluteTransformation(nullptr);
    const QSizeF intrinsicSize = shape->size();
    QSizeF size(intrinsicSize.width() * std::hypot(transform.m11(), transform.m12()),
                intrinsicSize.height() * std::hypot(transform.m21(), transform.m22()));
    QPointF pos = transform.map(QPointF(intrinsicSize.width()/2, intrinsicSize.height()/2)) - QPointF(size.width()/2, size.height()/2);
    const qreal rotation = qRadiansToDegrees(std::atan2(transform.m12(), transform.m11()));
    if (dynamic_cast<KoPathShape *>(shape) && !object.textData && !object.imageFill) {
        mappedPath = transform.map(shape->outline());
        const QRectF bounds = mappedPath.boundingRect();
        pos = bounds.topLeft();
        size = bounds.size();
    }
    const qint64 x = qRound64(pos.x() * 12700.0), y = qRound64(pos.y() * 12700.0);
    const qint64 cx = qMax<qint64>(1, qRound64(size.width() * 12700.0)), cy = qMax<qint64>(1, qRound64(size.height() * 12700.0));
    if (object.table) {
        QTextTable *table = object.table;
        xml.writeStartElement(QStringLiteral("p:graphicFrame"));
        xml.writeStartElement(QStringLiteral("p:nvGraphicFramePr"));
        xml.writeEmptyElement(QStringLiteral("p:cNvPr"));
        xml.writeAttribute(QStringLiteral("id"), QString::number(object.objectId));
        xml.writeAttribute(QStringLiteral("name"), shape->name());
        xml.writeEmptyElement(QStringLiteral("p:cNvGraphicFramePr"));
        xml.writeEmptyElement(QStringLiteral("p:nvPr")); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:xfrm"));
        xml.writeEmptyElement(QStringLiteral("a:off")); xml.writeAttribute(QStringLiteral("x"), QString::number(x)); xml.writeAttribute(QStringLiteral("y"), QString::number(y));
        xml.writeEmptyElement(QStringLiteral("a:ext")); xml.writeAttribute(QStringLiteral("cx"), QString::number(cx)); xml.writeAttribute(QStringLiteral("cy"), QString::number(cy)); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("a:graphic")); xml.writeStartElement(QStringLiteral("a:graphicData"));
        xml.writeAttribute(QStringLiteral("uri"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/table"));
        xml.writeStartElement(QStringLiteral("a:tbl")); xml.writeEmptyElement(QStringLiteral("a:tblPr"));
        xml.writeStartElement(QStringLiteral("a:tblGrid"));
        const auto widths = table->format().columnWidthConstraints();
        for (int col = 0; col < table->columns(); ++col) {
            qint64 width = cx / table->columns();
            if (col < widths.size()) {
                if (widths[col].type() == QTextLength::PercentageLength) width = qRound64(cx * widths[col].rawValue() / 100.0);
                else if (widths[col].type() == QTextLength::FixedLength) width = qRound64(widths[col].rawValue() * 12700.0);
            }
            xml.writeEmptyElement(QStringLiteral("a:gridCol")); xml.writeAttribute(QStringLiteral("w"), QString::number(qMax<qint64>(1, width)));
        }
        xml.writeEndElement();
        for (int row = 0; row < table->rows(); ++row) {
            xml.writeStartElement(QStringLiteral("a:tr")); xml.writeAttribute(QStringLiteral("h"), QString::number(qMax<qint64>(1, cy / table->rows())));
            for (int col = 0; col < table->columns(); ++col) {
                const QTextTableCell cell = table->cellAt(row, col);
                xml.writeStartElement(QStringLiteral("a:tc"));
                if (col > cell.column()) xml.writeAttribute(QStringLiteral("hMerge"), QStringLiteral("1"));
                if (row > cell.row()) xml.writeAttribute(QStringLiteral("vMerge"), QStringLiteral("1"));
                if (row == cell.row() && col == cell.column()) {
                    if (cell.columnSpan() > 1) xml.writeAttribute(QStringLiteral("gridSpan"), QString::number(cell.columnSpan()));
                    if (cell.rowSpan() > 1) xml.writeAttribute(QStringLiteral("rowSpan"), QString::number(cell.rowSpan()));
                }
                QTextDocument content;
                if (row == cell.row() && col == cell.column()) {
                    QTextCursor cursor = cell.firstCursorPosition();
                    cursor.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
                    QTextCursor destination(&content); destination.insertFragment(QTextDocumentFragment(cursor));
                }
                writeTextBody(xml, object, &content, QStringLiteral("a:txBody"));
                const QTextTableCellFormat cellFormat = cell.format().toTableCellFormat();
                const KoTableCellStyle format(cellFormat);
                xml.writeStartElement(QStringLiteral("a:tcPr"));
                xml.writeAttribute(QStringLiteral("marL"), QString::number(qRound64(format.leftPadding() * 12700.0)));
                xml.writeAttribute(QStringLiteral("marR"), QString::number(qRound64(format.rightPadding() * 12700.0)));
                xml.writeAttribute(QStringLiteral("marT"), QString::number(qRound64(format.topPadding() * 12700.0)));
                xml.writeAttribute(QStringLiteral("marB"), QString::number(qRound64(format.bottomPadding() * 12700.0)));
                auto border = [&](const QString &tag, qreal width, const QBrush &brush) {
                    if (width <= 0 || brush.style() == Qt::NoBrush) return;
                    xml.writeStartElement(tag); xml.writeAttribute(QStringLiteral("w"), QString::number(qRound64(width * 12700.0)));
                    xml.writeStartElement(QStringLiteral("a:solidFill")); xml.writeEmptyElement(QStringLiteral("a:srgbClr"));
                    xml.writeAttribute(QStringLiteral("val"), brush.color().name().mid(1).toUpper()); xml.writeEndElement(); xml.writeEndElement();
                };
                const KoBorder borders = format.borders();
                border(QStringLiteral("a:lnL"), borders.borderWidth(KoBorder::LeftBorder), borders.borderData(KoBorder::LeftBorder).outerPen.brush());
                border(QStringLiteral("a:lnR"), borders.borderWidth(KoBorder::RightBorder), borders.borderData(KoBorder::RightBorder).outerPen.brush());
                border(QStringLiteral("a:lnT"), borders.borderWidth(KoBorder::TopBorder), borders.borderData(KoBorder::TopBorder).outerPen.brush());
                border(QStringLiteral("a:lnB"), borders.borderWidth(KoBorder::BottomBorder), borders.borderData(KoBorder::BottomBorder).outerPen.brush());
                if (format.background().style() == Qt::SolidPattern) {
                    xml.writeStartElement(QStringLiteral("a:solidFill")); xml.writeEmptyElement(QStringLiteral("a:srgbClr"));
                    xml.writeAttribute(QStringLiteral("val"), format.background().color().name().mid(1).toUpper()); xml.writeEndElement();
                }
                xml.writeEndElement(); xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
        return;
    }
    if (!object.chartData.isEmpty()) {
        xml.writeStartElement(QStringLiteral("p:graphicFrame"));
        xml.writeStartElement(QStringLiteral("p:nvGraphicFramePr")); xml.writeEmptyElement(QStringLiteral("p:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QString::number(object.objectId)); xml.writeAttribute(QStringLiteral("name"), shape->name().isEmpty() ? QStringLiteral("Chart %1").arg(object.objectId) : shape->name()); xml.writeEmptyElement(QStringLiteral("p:cNvGraphicFramePr")); xml.writeEmptyElement(QStringLiteral("p:nvPr")); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:xfrm")); xml.writeEmptyElement(QStringLiteral("a:off")); xml.writeAttribute(QStringLiteral("x"), QString::number(x)); xml.writeAttribute(QStringLiteral("y"), QString::number(y)); xml.writeEmptyElement(QStringLiteral("a:ext")); xml.writeAttribute(QStringLiteral("cx"), QString::number(cx)); xml.writeAttribute(QStringLiteral("cy"), QString::number(cy)); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("a:graphic")); xml.writeStartElement(QStringLiteral("a:graphicData")); xml.writeAttribute(QStringLiteral("uri"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart")); xml.writeEmptyElement(QStringLiteral("c:chart")); xml.writeAttribute(QStringLiteral("xmlns:c"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart")); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(object.relationshipId)); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
        return;
    }
    if (!object.imageData.isEmpty() && !object.imageFill) {
        xml.writeStartElement(QStringLiteral("p:pic")); xml.writeStartElement(QStringLiteral("p:nvPicPr"));
        xml.writeStartElement(QStringLiteral("p:cNvPr"));
        xml.writeAttribute(QStringLiteral("id"), QString::number(object.objectId));
        xml.writeAttribute(QStringLiteral("name"), shape->name().isEmpty() ? QStringLiteral("Image %1").arg(object.objectId) : shape->name());
        if (object.mediaRelationshipId) {
            xml.writeEmptyElement(QStringLiteral("a:hlinkClick"));
            xml.writeAttribute(QStringLiteral("r:id"), QString());
            xml.writeAttribute(QStringLiteral("action"), QStringLiteral("ppaction://media"));
        }
        xml.writeEndElement();
        xml.writeEmptyElement(QStringLiteral("p:cNvPicPr"));
        xml.writeStartElement(QStringLiteral("p:nvPr"));
        if (object.mediaRelationshipId) {
            xml.writeEmptyElement(QStringLiteral("a:videoFile"));
            xml.writeAttribute(QStringLiteral("r:link"), QStringLiteral("rId%1").arg(object.mediaRelationshipId));
            xml.writeStartElement(QStringLiteral("p:extLst"));
            xml.writeStartElement(QStringLiteral("p:ext"));
            xml.writeAttribute(QStringLiteral("uri"), QStringLiteral("{DAA4B4D4-6D71-4841-9C94-3DE7FCFB5B9F}"));
            xml.writeEmptyElement(QStringLiteral("p14:media"));
            xml.writeAttribute(QStringLiteral("xmlns:p14"), QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2010/main"));
            xml.writeAttribute(QStringLiteral("r:embed"), QStringLiteral("rId%1").arg(object.mediaEmbedRelationshipId));
            xml.writeEndElement(); xml.writeEndElement();
        }
        xml.writeEndElement(); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:blipFill")); xml.writeEmptyElement(QStringLiteral("a:blip")); xml.writeAttribute(QStringLiteral("r:embed"), QStringLiteral("rId%1").arg(imageRel));
        if (object.imageCrop != QRectF(0, 0, 1, 1)) {
            xml.writeEmptyElement(QStringLiteral("a:srcRect"));
            xml.writeAttribute(QStringLiteral("l"), QString::number(qRound64(object.imageCrop.left() * 100000)));
            xml.writeAttribute(QStringLiteral("t"), QString::number(qRound64(object.imageCrop.top() * 100000)));
            xml.writeAttribute(QStringLiteral("r"), QString::number(qRound64((1 - object.imageCrop.right()) * 100000)));
            xml.writeAttribute(QStringLiteral("b"), QString::number(qRound64((1 - object.imageCrop.bottom()) * 100000)));
        }
        xml.writeStartElement(QStringLiteral("a:stretch")); xml.writeEmptyElement(QStringLiteral("a:fillRect")); xml.writeEndElement(); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:spPr")); xml.writeStartElement(QStringLiteral("a:xfrm")); if (mappedPath.isEmpty() && qIsFinite(rotation) && !qFuzzyIsNull(rotation)) xml.writeAttribute(QStringLiteral("rot"), QString::number(qRound64(rotation * 60000.0))); if (mappedPath.isEmpty() && transform.determinant() < 0) xml.writeAttribute(QStringLiteral("flipV"), QStringLiteral("1")); xml.writeEmptyElement(QStringLiteral("a:off")); xml.writeAttribute(QStringLiteral("x"), QString::number(x)); xml.writeAttribute(QStringLiteral("y"), QString::number(y)); xml.writeEmptyElement(QStringLiteral("a:ext")); xml.writeAttribute(QStringLiteral("cx"), QString::number(cx)); xml.writeAttribute(QStringLiteral("cy"), QString::number(cy)); xml.writeEndElement(); xml.writeStartElement(QStringLiteral("a:prstGeom")); xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect")); xml.writeEmptyElement(QStringLiteral("a:avLst")); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
        return;
    }
    const QString shapeTag = object.connector ? QStringLiteral("p:cxnSp") : QStringLiteral("p:sp");
    xml.writeStartElement(shapeTag);
    xml.writeStartElement(object.connector ? QStringLiteral("p:nvCxnSpPr") : QStringLiteral("p:nvSpPr")); xml.writeEmptyElement(QStringLiteral("p:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QString::number(object.objectId)); xml.writeAttribute(QStringLiteral("name"), shape->name().isEmpty() ? QStringLiteral("Shape %1").arg(object.objectId) : shape->name());
    if (object.connector) { xml.writeEmptyElement(QStringLiteral("p:cNvCxnSpPr")); }
    else { xml.writeEmptyElement(QStringLiteral("p:cNvSpPr")); }
    xml.writeEmptyElement(QStringLiteral("p:nvPr")); xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("p:spPr")); xml.writeStartElement(QStringLiteral("a:xfrm")); if (mappedPath.isEmpty() && qIsFinite(rotation) && !qFuzzyIsNull(rotation)) xml.writeAttribute(QStringLiteral("rot"), QString::number(qRound64(rotation * 60000.0))); if (mappedPath.isEmpty() && transform.determinant() < 0) xml.writeAttribute(QStringLiteral("flipV"), QStringLiteral("1")); xml.writeEmptyElement(QStringLiteral("a:off")); xml.writeAttribute(QStringLiteral("x"), QString::number(x)); xml.writeAttribute(QStringLiteral("y"), QString::number(y)); xml.writeEmptyElement(QStringLiteral("a:ext")); xml.writeAttribute(QStringLiteral("cx"), QString::number(cx)); xml.writeAttribute(QStringLiteral("cy"), QString::number(cy)); xml.writeEndElement();
    writePathGeometry(xml, shape, mappedPath); writeFillAndLine(xml, shape, object.imageFill ? imageRel : 0); xml.writeEndElement();
    if (!object.connector) writeTextBody(xml, object);
    xml.writeEndElement();
}

void writeTiming(QXmlStreamWriter &xml, const QList<SlideObject> &objects, KPrPage *page)
{
    if (!page) return;
    struct Effect { int shape; int paragraph; KPrShapeAnimation::NodeType type; int duration; bool entrance; };
    QList<Effect> effects;
    auto &animations = page->animations();
    for (int row = 0; row < animations.rowCount(); ++row) {
        KPrShapeAnimation::NodeType type;
        const auto *animation = animations.animationByRow(row, nullptr, &type);
        if (!animation) continue;
        const bool entrance = animation->presetClass() == KPrShapeAnimation::Entrance && animation->id() == QLatin1String("ooo-entrance-appear");
        const bool exit = animation->presetClass() == KPrShapeAnimation::Exit && animation->id() == QLatin1String("ooo-exit-disappear");
        if (!entrance && !exit) continue;
        for (const auto &object : objects) {
            if (object.groupEnd) continue;
            if (object.shape != animation->shape()
                && (!object.textData || object.textData != qobject_cast<KoTextShapeDataBase *>(animation->shape()->userData()))) continue;
            int paragraph = -1;
            if (animation->textBlockUserData() && object.textData && object.textData->document()) {
                int index = 0;
                for (auto block = object.textData->document()->begin(); block.isValid(); block = block.next(), ++index)
                    if (block.userData() == animation->textBlockUserData()) { paragraph = index; break; }
            }
            effects.append({object.objectId, paragraph, type, qMax(1, animation->globalDuration()), entrance});
            break;
        }
    }
    bool hasMedia = false;
    for (const SlideObject &object : objects) hasMedia |= object.mediaRelationshipId != 0;
    if (effects.isEmpty() && !hasMedia) return;
    int nextId = 1;
    const auto attr = [&](const QString &key, const QString &value) { xml.writeAttribute(key, value); };
    xml.writeStartElement(QStringLiteral("p:timing")); xml.writeStartElement(QStringLiteral("p:tnLst")); xml.writeStartElement(QStringLiteral("p:par"));
    xml.writeStartElement(QStringLiteral("p:cTn")); attr(QStringLiteral("id"),QString::number(nextId++)); attr(QStringLiteral("dur"),QStringLiteral("indefinite")); attr(QStringLiteral("restart"),QStringLiteral("never")); attr(QStringLiteral("nodeType"),QStringLiteral("tmRoot"));
    xml.writeStartElement(QStringLiteral("p:childTnLst"));
    for (const SlideObject &object : objects) {
        if (!object.mediaRelationshipId) continue;
        xml.writeStartElement(QStringLiteral("p:video"));
        xml.writeStartElement(QStringLiteral("p:cMediaNode")); attr(QStringLiteral("vol"), QStringLiteral("80000"));
        xml.writeStartElement(QStringLiteral("p:cTn")); attr(QStringLiteral("id"), QString::number(nextId++)); attr(QStringLiteral("fill"), QStringLiteral("hold")); attr(QStringLiteral("display"), QStringLiteral("0"));
        xml.writeStartElement(QStringLiteral("p:stCondLst")); xml.writeEmptyElement(QStringLiteral("p:cond")); attr(QStringLiteral("delay"), QStringLiteral("indefinite"));
        xml.writeEndElement(); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:tgtEl")); xml.writeEmptyElement(QStringLiteral("p:spTgt")); attr(QStringLiteral("spid"), QString::number(object.objectId));
        xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
    }
    if (effects.isEmpty()) {
        xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
        return;
    }
    xml.writeStartElement(QStringLiteral("p:seq")); attr(QStringLiteral("concurrent"),QStringLiteral("1")); attr(QStringLiteral("nextAc"),QStringLiteral("seek"));
    xml.writeStartElement(QStringLiteral("p:cTn")); attr(QStringLiteral("id"),QString::number(nextId++)); attr(QStringLiteral("dur"),QStringLiteral("indefinite")); attr(QStringLiteral("nodeType"),QStringLiteral("mainSeq"));
    xml.writeStartElement(QStringLiteral("p:childTnLst"));
    for (const auto &effect : effects) {
        xml.writeStartElement(QStringLiteral("p:par")); xml.writeStartElement(QStringLiteral("p:cTn")); attr(QStringLiteral("id"),QString::number(nextId++)); attr(QStringLiteral("fill"),QStringLiteral("hold"));
        xml.writeStartElement(QStringLiteral("p:stCondLst")); xml.writeEmptyElement(QStringLiteral("p:cond")); attr(QStringLiteral("delay"),effect.type == KPrShapeAnimation::OnClick ? QStringLiteral("indefinite") : QStringLiteral("0")); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:childTnLst")); xml.writeStartElement(QStringLiteral("p:par"));
        xml.writeStartElement(QStringLiteral("p:cTn")); attr(QStringLiteral("id"),QString::number(nextId++)); attr(QStringLiteral("presetID"),QStringLiteral("1")); attr(QStringLiteral("presetClass"),effect.entrance ? QStringLiteral("entr") : QStringLiteral("exit")); attr(QStringLiteral("presetSubtype"),QStringLiteral("0")); attr(QStringLiteral("fill"),QStringLiteral("hold"));
        attr(QStringLiteral("nodeType"),effect.type == KPrShapeAnimation::OnClick ? QStringLiteral("clickEffect") : effect.type == KPrShapeAnimation::AfterPrevious ? QStringLiteral("afterEffect") : QStringLiteral("withEffect"));
        xml.writeStartElement(QStringLiteral("p:stCondLst")); xml.writeEmptyElement(QStringLiteral("p:cond")); attr(QStringLiteral("delay"),QStringLiteral("0")); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:childTnLst")); xml.writeStartElement(QStringLiteral("p:set")); xml.writeStartElement(QStringLiteral("p:cBhvr"));
        xml.writeEmptyElement(QStringLiteral("p:cTn")); attr(QStringLiteral("id"),QString::number(nextId++)); attr(QStringLiteral("dur"),QString::number(effect.duration)); attr(QStringLiteral("fill"),QStringLiteral("hold"));
        xml.writeStartElement(QStringLiteral("p:tgtEl")); xml.writeStartElement(QStringLiteral("p:spTgt")); attr(QStringLiteral("spid"),QString::number(effect.shape));
        if (effect.paragraph >= 0) { xml.writeStartElement(QStringLiteral("p:txEl")); xml.writeEmptyElement(QStringLiteral("p:pRg")); attr(QStringLiteral("st"),QString::number(effect.paragraph)); attr(QStringLiteral("end"),QString::number(effect.paragraph)); xml.writeEndElement(); }
        xml.writeEndElement(); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:attrNameLst")); xml.writeTextElement(QStringLiteral("p:attrName"),QStringLiteral("style.visibility")); xml.writeEndElement(); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:to")); xml.writeEmptyElement(QStringLiteral("p:strVal")); attr(QStringLiteral("val"),effect.entrance ? QStringLiteral("visible") : QStringLiteral("hidden")); xml.writeEndElement(); xml.writeEndElement();
        xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
    }
    xml.writeEndElement(); xml.writeEndElement();
    for (const auto &name : {QStringLiteral("p:prevCondLst"),QStringLiteral("p:nextCondLst")}) {
        xml.writeStartElement(name); xml.writeStartElement(QStringLiteral("p:cond")); attr(QStringLiteral("evt"), name == QLatin1String("p:prevCondLst") ? QStringLiteral("onPrev") : QStringLiteral("onNext")); attr(QStringLiteral("delay"),QStringLiteral("0")); xml.writeStartElement(QStringLiteral("p:tgtEl")); xml.writeEmptyElement(QStringLiteral("p:sldTgt")); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
    }
    xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
}

void writeTransition(QXmlStreamWriter &xml, KPrPage *page)
{
    if (!page) return;
    auto *data = dynamic_cast<KPrPageApplicationData *>(page->applicationData());
    if (!data) return;
    const KPrPageTransition &transition = data->pageTransition();
    const auto *effect = data->pageEffect();
    const bool fade = effect && effect->id() == QLatin1String("FadeEffect");
    if (transition.type() == KPrPageTransition::Manual && !fade) return;
    xml.writeStartElement(QStringLiteral("p:transition"));
    xml.writeAttribute(QStringLiteral("advClick"), transition.type() == KPrPageTransition::Automatic ? QStringLiteral("0") : QStringLiteral("1"));
    if (transition.type() != KPrPageTransition::Manual)
        xml.writeAttribute(QStringLiteral("advTm"), QString::number(qMax(0, transition.milliseconds())));
    if (fade) {
        xml.writeAttribute(QStringLiteral("spd"), effect->duration() >= 1000 ? QStringLiteral("slow") : effect->duration() >= 750 ? QStringLiteral("med") : QStringLiteral("fast"));
        xml.writeEmptyElement(QStringLiteral("p:fade"));
    }
    xml.writeEndElement();
}

QByteArray slideXml(const QList<SlideObject> &objects, bool hidden, KPrPage *page)
{
    return xmlPart([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("p:sld"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main"), QStringLiteral("p"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        if (hidden) xml.writeAttribute(QStringLiteral("show"), QStringLiteral("0"));
        xml.writeStartElement(QStringLiteral("p:cSld"));
        QSharedPointer<KoShapeBackground> background = page ? page->background() : QSharedPointer<KoShapeBackground>();
        if (page && page->displayMasterBackground() && page->masterPage()) background = page->masterPage()->background();
        if (background) {
            xml.writeStartElement(QStringLiteral("p:bg"));
            xml.writeStartElement(QStringLiteral("p:bgPr"));
            writeFill(xml, background);
            xml.writeEmptyElement(QStringLiteral("a:effectLst"));
            xml.writeEndElement(); xml.writeEndElement();
        }
        startShapeTree(xml);
        for (const SlideObject &object : objects) writeObjectXml(xml, object, object.relationshipId);
        xml.writeEndElement(); // spTree
        xml.writeEndElement(); // cSld
        xml.writeStartElement(QStringLiteral("p:clrMapOvr")); xml.writeEmptyElement(QStringLiteral("a:masterClrMapping")); xml.writeEndElement();
        writeTransition(xml, page);
        writeTiming(xml, objects, page);
        xml.writeEndElement();
    });
}

QByteArray notesSlideXml(const SlideObject &notes, qint64 width, qint64 height)
{
    return xmlPart([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("p:notes"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main"), QStringLiteral("p"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        xml.writeStartElement(QStringLiteral("p:cSld"));
        startShapeTree(xml);
        xml.writeStartElement(QStringLiteral("p:sp"));
        xml.writeStartElement(QStringLiteral("p:nvSpPr"));
        xml.writeEmptyElement(QStringLiteral("p:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QStringLiteral("2")); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Notes"));
        xml.writeEmptyElement(QStringLiteral("p:cNvSpPr"));
        xml.writeStartElement(QStringLiteral("p:nvPr")); xml.writeEmptyElement(QStringLiteral("p:ph")); xml.writeAttribute(QStringLiteral("type"), QStringLiteral("body")); xml.writeEndElement();
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:spPr")); xml.writeStartElement(QStringLiteral("a:xfrm"));
        xml.writeEmptyElement(QStringLiteral("a:off")); xml.writeAttribute(QStringLiteral("x"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
        xml.writeEmptyElement(QStringLiteral("a:ext")); xml.writeAttribute(QStringLiteral("cx"), QString::number(width)); xml.writeAttribute(QStringLiteral("cy"), QString::number(height));
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("a:prstGeom")); xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect")); xml.writeEmptyElement(QStringLiteral("a:avLst")); xml.writeEndElement();
        xml.writeEndElement();
        writeTextBody(xml, notes);
        xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement();
    });
}

QByteArray notesMasterXml()
{
    return xmlPart([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("p:notesMaster"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main"), QStringLiteral("p"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        xml.writeStartElement(QStringLiteral("p:cSld")); startShapeTree(xml);
        xml.writeEndElement(); // spTree
        xml.writeEndElement(); // cSld
        xml.writeStartElement(QStringLiteral("p:clrMap"));
        for (const auto &pair : {qMakePair(QStringLiteral("bg1"), QStringLiteral("lt1")), qMakePair(QStringLiteral("tx1"), QStringLiteral("dk1")), qMakePair(QStringLiteral("bg2"), QStringLiteral("lt2")), qMakePair(QStringLiteral("tx2"), QStringLiteral("dk2")), qMakePair(QStringLiteral("accent1"), QStringLiteral("accent1")), qMakePair(QStringLiteral("accent2"), QStringLiteral("accent2")), qMakePair(QStringLiteral("accent3"), QStringLiteral("accent3")), qMakePair(QStringLiteral("accent4"), QStringLiteral("accent4")), qMakePair(QStringLiteral("accent5"), QStringLiteral("accent5")), qMakePair(QStringLiteral("accent6"), QStringLiteral("accent6")), qMakePair(QStringLiteral("hlink"), QStringLiteral("hlink")), qMakePair(QStringLiteral("folHlink"), QStringLiteral("folHlink"))}) xml.writeAttribute(pair.first, pair.second);
        xml.writeEndElement();
        xml.writeEndElement();
    });
}

QByteArray themeXml()
{
    return xmlPart([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("a:theme")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a")); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Calligra"));
        xml.writeStartElement(QStringLiteral("a:themeElements"));
        xml.writeStartElement(QStringLiteral("a:clrScheme")); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Calligra"));
        const QList<QPair<QString, QString>> colors{{"dk1", "000000"}, {"lt1", "FFFFFF"}, {"dk2", "1F1F1F"}, {"lt2", "E7E6E6"}, {"accent1", "4472C4"}, {"accent2", "ED7D31"}, {"accent3", "A5A5A5"}, {"accent4", "FFC000"}, {"accent5", "5B9BD5"}, {"accent6", "70AD47"}, {"hlink", "0563C1"}, {"folHlink", "954F72"}};
        for (const auto &color : colors) { xml.writeStartElement(QStringLiteral("a:") + color.first); xml.writeEmptyElement(QStringLiteral("a:srgbClr")); xml.writeAttribute(QStringLiteral("val"), color.second); xml.writeEndElement(); }
        xml.writeEndElement();
        for (const QString &scheme : {QStringLiteral("fontScheme"), QStringLiteral("fmtScheme")}) {
            xml.writeStartElement(QStringLiteral("a:") + scheme); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Calligra"));
            if (scheme == QLatin1String("fontScheme")) { for (const QString &set : {QStringLiteral("majorFont"), QStringLiteral("minorFont")}) { xml.writeStartElement(QStringLiteral("a:") + set); xml.writeEmptyElement(QStringLiteral("a:latin")); xml.writeAttribute(QStringLiteral("typeface"), QStringLiteral("Arial")); xml.writeEmptyElement(QStringLiteral("a:ea")); xml.writeAttribute(QStringLiteral("typeface"), QString()); xml.writeEmptyElement(QStringLiteral("a:cs")); xml.writeAttribute(QStringLiteral("typeface"), QString()); xml.writeEndElement(); } }
            else {
                auto solidFill = [&]() { xml.writeStartElement(QStringLiteral("a:solidFill")); xml.writeEmptyElement(QStringLiteral("a:schemeClr")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("phClr")); xml.writeEndElement(); };
                auto gradientFill = [&]() { xml.writeStartElement(QStringLiteral("a:gradFill")); xml.writeAttribute(QStringLiteral("rotWithShape"), QStringLiteral("1")); xml.writeStartElement(QStringLiteral("a:gsLst")); for (int pos : {0, 100000}) { xml.writeStartElement(QStringLiteral("a:gs")); xml.writeAttribute(QStringLiteral("pos"), QString::number(pos)); xml.writeEmptyElement(QStringLiteral("a:schemeClr")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("phClr")); xml.writeEndElement(); } xml.writeEndElement(); xml.writeEmptyElement(QStringLiteral("a:lin")); xml.writeAttribute(QStringLiteral("ang"), QStringLiteral("5400000")); xml.writeAttribute(QStringLiteral("scaled"), QStringLiteral("1")); xml.writeEndElement(); };
                xml.writeStartElement(QStringLiteral("a:fillStyleLst")); solidFill(); gradientFill(); gradientFill(); xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("a:lnStyleLst"));
                for (int width : {6350, 12700, 19050}) { xml.writeStartElement(QStringLiteral("a:ln")); xml.writeAttribute(QStringLiteral("w"), QString::number(width)); xml.writeAttribute(QStringLiteral("cap"), QStringLiteral("flat")); xml.writeAttribute(QStringLiteral("cmpd"), QStringLiteral("sng")); xml.writeAttribute(QStringLiteral("algn"), QStringLiteral("ctr")); solidFill(); xml.writeEmptyElement(QStringLiteral("a:prstDash")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("solid")); xml.writeEndElement(); }
                xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("a:effectStyleLst")); for (int i = 0; i < 3; ++i) { xml.writeStartElement(QStringLiteral("a:effectStyle")); xml.writeEmptyElement(QStringLiteral("a:effectLst")); xml.writeEndElement(); } xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("a:bgFillStyleLst")); solidFill(); gradientFill(); gradientFill(); xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement(); xml.writeEndElement();
    });
}
}

KoFilter::ConversionStatus PptxExport::convert(const QByteArray &from, const QByteArray &to)
{
    if (from != "application/vnd.oasis.opendocument.presentation"
        || to != "application/vnd.openxmlformats-officedocument.presentationml.presentation") return BadMimeType;
    auto *document = qobject_cast<KoPADocument *>(m_chain->inputDocument());
    if (!document || document->pages().isEmpty()) return StupidError;
    const auto pages = document->pages();
    const QSizeF pageSize = pages.first()->size();
    const qint64 width = qMax<qint64>(1, qRound64(pageSize.width() * 12700.0));
    const qint64 height = qMax<qint64>(1, qRound64(pageSize.height() * 12700.0));

    QList<QList<SlideObject>> slideObjects;
    QList<SlideObject> slideNotes;
    QList<int> notesRelationshipIds;
    int chartCount = 0;
    for (KoPAPageBase *page : pages) {
        QList<SlideObject> objects;
        int nextObjectId = 2;
        for (KoShape *shape : page->shapes()) collectShape(shape, objects, nextObjectId);
        int nextImageId = 0;
        int nextRelationshipId = 2;
        QHash<QString, int> slideHyperlinkIds;
        for (SlideObject &object : objects) {
            if (!object.imageData.isEmpty()) object.imageId = ++nextImageId;
            if (!object.chartData.isEmpty()) object.chartId = ++chartCount;
            if (object.imageId || object.chartId) object.relationshipId = nextRelationshipId++;
            if (!object.mediaData.isEmpty() || !object.mediaExternalTarget.isEmpty()) {
                object.mediaRelationshipId = nextRelationshipId++;
                object.mediaEmbedRelationshipId = nextRelationshipId++;
            }
            if (object.textData && object.textData->document()) {
                QTextDocument *textDocument = object.textData->document();
                for (QTextBlock block = textDocument->begin(); block.isValid(); block = block.next()) {
                    for (QTextBlock::Iterator it = block.begin(); !it.atEnd(); ++it) {
                        const QTextFragment fragment = it.fragment();
                        if (!fragment.isValid()) continue;
                        const QTextCharFormat format = fragment.charFormat();
                        if (!format.isAnchor()) continue;
                        const QString target = format.anchorHref();
                        if (target.isEmpty()) continue;
                        if (!slideHyperlinkIds.contains(target)) slideHyperlinkIds.insert(target, nextRelationshipId++);
                        object.hyperlinkIds.insert(target, slideHyperlinkIds.value(target));
                    }
                }
            }
        }
        SlideObject notesObject;
        int notesRelationshipId = 0;
        if (auto *presentationPage = dynamic_cast<KPrPage *>(page)) {
            KPrNotes *notes = presentationPage->pageNotes();
            KoShape *notesShape = notes ? notes->textShape() : nullptr;
            notesObject.textData = notesShape ? qobject_cast<KoTextShapeDataBase *>(notesShape->userData()) : nullptr;
            if (notesObject.textData && notesObject.textData->document()
                && !notesObject.textData->document()->toPlainText().trimmed().isEmpty()) {
                notesRelationshipId = nextRelationshipId++;
            } else {
                notesObject.textData = nullptr;
            }
        }
        slideObjects.append(objects);
        slideNotes.append(notesObject);
        notesRelationshipIds.append(notesRelationshipId);
    }
    const bool hasNotes = std::any_of(notesRelationshipIds.cbegin(), notesRelationshipIds.cend(), [](int id) { return id > 0; });

    std::unique_ptr<KoStore> store(KoStore::createStore(m_chain->outputFile(), KoStore::Write, QByteArray(), KoStore::Zip));
    if (!store) return CreationError;
    auto write = [&](const QString &name, const QByteArray &data) {
        if (addPart(store.get(), name, data)) return true;
        m_chain->setErrorMessage(QStringLiteral("Could not write PPTX part: %1").arg(name));
        return false;
    };
    // Embedded media is already compressed (PNG/JPEG/MP4), so deflating it
    // again only burns CPU for near-zero size savings.
    auto writeMedia = [&](const QString &name, const QByteArray &data) {
        store->setCompressionEnabled(false);
        const bool ok = addPart(store.get(), name, data);
        store->setCompressionEnabled(true);
        if (ok) return true;
        m_chain->setErrorMessage(QStringLiteral("Could not write PPTX part: %1").arg(name));
        return false;
    };

    if (!write(QStringLiteral("[Content_Types].xml"), xmlPart([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Types")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/content-types"));
        xml.writeEmptyElement(QStringLiteral("Default")); xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("rels")); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-package.relationships+xml"));
        xml.writeEmptyElement(QStringLiteral("Default")); xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("xml")); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/xml"));
        QSet<QString> imageSuffixes;
        for (const QList<SlideObject> &objects : slideObjects) {
            for (const SlideObject &object : objects) {
                if (object.imageId) imageSuffixes.insert(object.imageSuffix);
            }
        }
        for (const QString &suffix : imageSuffixes) {
            xml.writeEmptyElement(QStringLiteral("Default")); xml.writeAttribute(QStringLiteral("Extension"), suffix); xml.writeAttribute(QStringLiteral("ContentType"), imageContentType(suffix));
        }
        QMap<QString, QString> mediaTypes;
        for (const auto &objects : slideObjects)
            for (const auto &object : objects)
                if (!object.mediaData.isEmpty()) mediaTypes.insert(object.mediaSuffix, object.mediaMimeType);
        for (auto it = mediaTypes.cbegin(); it != mediaTypes.cend(); ++it) {
            xml.writeEmptyElement(QStringLiteral("Default"));
            xml.writeAttribute(QStringLiteral("Extension"), it.key()); xml.writeAttribute(QStringLiteral("ContentType"), it.value());
        }
        const QList<QPair<QString, QString>> parts{{"/ppt/presentation.xml", "application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"}, {"/ppt/slideMasters/slideMaster1.xml", "application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml"}, {"/ppt/slideLayouts/slideLayout1.xml", "application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml"}, {"/ppt/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml"}};
        for (const auto &part : parts) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), part.first); xml.writeAttribute(QStringLiteral("ContentType"), part.second); }
        if (hasNotes) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/ppt/notesMasters/notesMaster1.xml")); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.notesMaster+xml")); }
        for (int i = 0; i < pages.size(); ++i) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/ppt/slides/slide%1.xml").arg(i + 1)); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.slide+xml")); }
        for (int i = 0; i < pages.size(); ++i) if (notesRelationshipIds[i]) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/ppt/notesSlides/notesSlide%1.xml").arg(i + 1)); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.notesSlide+xml")); }
        for (int i = 1; i <= chartCount; ++i) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/ppt/charts/chart%1.xml").arg(i)); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml")); }
        xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("_rels/.rels"), xmlPart([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships")); xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("ppt/presentation.xml")); xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("ppt/presentation.xml"), xmlPart([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("p:presentation")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main"), QStringLiteral("p")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        xml.writeStartElement(QStringLiteral("p:sldMasterIdLst")); xml.writeEmptyElement(QStringLiteral("p:sldMasterId")); xml.writeAttribute(QStringLiteral("id"), QStringLiteral("2147483648")); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId1")); xml.writeEndElement();
        if (hasNotes) { xml.writeStartElement(QStringLiteral("p:notesMasterIdLst")); xml.writeEmptyElement(QStringLiteral("p:notesMasterId")); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(pages.size() + 2)); xml.writeEndElement(); }
        xml.writeStartElement(QStringLiteral("p:sldIdLst")); for (int i = 0; i < pages.size(); ++i) { xml.writeEmptyElement(QStringLiteral("p:sldId")); xml.writeAttribute(QStringLiteral("id"), QString::number(256 + i)); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(i + 2)); } xml.writeEndElement();
        xml.writeEmptyElement(QStringLiteral("p:sldSz")); xml.writeAttribute(QStringLiteral("cx"), QString::number(width)); xml.writeAttribute(QStringLiteral("cy"), QString::number(height)); xml.writeAttribute(QStringLiteral("type"), QStringLiteral("custom")); xml.writeEmptyElement(QStringLiteral("p:notesSz")); xml.writeAttribute(QStringLiteral("cx"), QString::number(height)); xml.writeAttribute(QStringLiteral("cy"), QString::number(width));
        xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("ppt/_rels/presentation.xml.rels"), xmlPart([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
        xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("slideMasters/slideMaster1.xml"));
        for (int i = 0; i < pages.size(); ++i) { xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(i + 2)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("slides/slide%1.xml").arg(i + 1)); }
        if (hasNotes) { xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(pages.size() + 2)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesMaster")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("notesMasters/notesMaster1.xml")); }
        xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("ppt/slideMasters/slideMaster1.xml"), xmlPart([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("p:sldMaster")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main"), QStringLiteral("p")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        xml.writeStartElement(QStringLiteral("p:cSld")); startShapeTree(xml);
        xml.writeEndElement(); // spTree
        xml.writeEndElement(); // cSld
        xml.writeStartElement(QStringLiteral("p:clrMap")); for (const auto &pair : {qMakePair(QStringLiteral("bg1"), QStringLiteral("lt1")), qMakePair(QStringLiteral("tx1"), QStringLiteral("dk1")), qMakePair(QStringLiteral("bg2"), QStringLiteral("lt2")), qMakePair(QStringLiteral("tx2"), QStringLiteral("dk2")), qMakePair(QStringLiteral("accent1"), QStringLiteral("accent1")), qMakePair(QStringLiteral("accent2"), QStringLiteral("accent2")), qMakePair(QStringLiteral("accent3"), QStringLiteral("accent3")), qMakePair(QStringLiteral("accent4"), QStringLiteral("accent4")), qMakePair(QStringLiteral("accent5"), QStringLiteral("accent5")), qMakePair(QStringLiteral("accent6"), QStringLiteral("accent6")), qMakePair(QStringLiteral("hlink"), QStringLiteral("hlink")), qMakePair(QStringLiteral("folHlink"), QStringLiteral("folHlink"))}) xml.writeAttribute(pair.first, pair.second); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("p:sldLayoutIdLst")); xml.writeEmptyElement(QStringLiteral("p:sldLayoutId")); xml.writeAttribute(QStringLiteral("id"), QStringLiteral("2147483649")); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId1")); xml.writeEndElement(); xml.writeEmptyElement(QStringLiteral("p:txStyles")); xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("ppt/slideMasters/_rels/slideMaster1.xml.rels"), xmlPart([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
        xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../slideLayouts/slideLayout1.xml"));
        xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId2")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../theme/theme1.xml")); xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("ppt/slideLayouts/slideLayout1.xml"), xmlPart([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("p:sldLayout")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main"), QStringLiteral("p")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a")); xml.writeAttribute(QStringLiteral("type"), QStringLiteral("blank"));
        xml.writeStartElement(QStringLiteral("p:cSld")); startShapeTree(xml);
        xml.writeEndElement(); // spTree
        xml.writeEndElement(); // cSld
        xml.writeStartElement(QStringLiteral("p:clrMapOvr")); xml.writeEmptyElement(QStringLiteral("a:masterClrMapping")); xml.writeEndElement(); xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("ppt/slideLayouts/_rels/slideLayout1.xml.rels"), xmlPart([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships")); xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../slideMasters/slideMaster1.xml")); xml.writeEndElement();
    }))) return CreationError;
    if (!write(QStringLiteral("ppt/theme/theme1.xml"), themeXml())) return CreationError;

    for (int i = 0; i < pages.size(); ++i) {
        const QList<SlideObject> &objects = slideObjects[i];
        if (!write(QStringLiteral("ppt/slides/slide%1.xml").arg(i + 1), slideXml(objects, dynamic_cast<KPrPage *>(pages[i]) && static_cast<KPrPage *>(pages[i])->isHidden(), dynamic_cast<KPrPage *>(pages[i])))) return CreationError;
        if (!write(QStringLiteral("ppt/slides/_rels/slide%1.xml.rels").arg(i + 1), xmlPart([i, &objects, notesRelationshipId = notesRelationshipIds[i]](QXmlStreamWriter &xml) {
            xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
            xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../slideLayouts/slideLayout1.xml"));
            QSet<int> writtenHyperlinks;
            for (const SlideObject &object : objects) {
                if (object.mediaRelationshipId) {
                    const QString target = object.mediaExternalTarget.isEmpty() ? QStringLiteral("../media/slide%1-media%2.%3").arg(i+1).arg(object.objectId).arg(object.mediaSuffix) : object.mediaExternalTarget;
                    for (const auto &relationship : {qMakePair(object.mediaRelationshipId, QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/video")), qMakePair(object.mediaEmbedRelationshipId, QStringLiteral("http://schemas.microsoft.com/office/2007/relationships/media"))}) {
                        xml.writeEmptyElement(QStringLiteral("Relationship"));
                        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(relationship.first));
                        xml.writeAttribute(QStringLiteral("Type"), relationship.second); xml.writeAttribute(QStringLiteral("Target"), target);
                        if (!object.mediaExternalTarget.isEmpty()) xml.writeAttribute(QStringLiteral("TargetMode"), QStringLiteral("External"));
                    }
                }
                if (object.imageId) { xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(object.relationshipId)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/image")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../media/slide%1-image%2.%3").arg(i + 1).arg(object.imageId).arg(object.imageSuffix)); }
                if (object.chartId) { xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(object.relationshipId)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../charts/chart%1.xml").arg(object.chartId)); }
                for (auto it = object.hyperlinkIds.cbegin(); it != object.hyperlinkIds.cend(); ++it) {
                    if (writtenHyperlinks.contains(it.value())) continue;
                    writtenHyperlinks.insert(it.value());
                    xml.writeEmptyElement(QStringLiteral("Relationship"));
                    xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(it.value()));
                    xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink"));
                    xml.writeAttribute(QStringLiteral("Target"), it.key());
                    xml.writeAttribute(QStringLiteral("TargetMode"), QStringLiteral("External"));
                }
            }
            if (notesRelationshipId) { xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(notesRelationshipId)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesSlide")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../notesSlides/notesSlide%1.xml").arg(i + 1)); }
            xml.writeEndElement();
        }))) return CreationError;
        for (const SlideObject &object : objects)
            if (!object.mediaData.isEmpty() && !writeMedia(QStringLiteral("ppt/media/slide%1-media%2.%3").arg(i+1).arg(object.objectId).arg(object.mediaSuffix), object.mediaData)) return CreationError;
        for (const SlideObject &object : objects) if (object.imageId && !writeMedia(QStringLiteral("ppt/media/slide%1-image%2.%3").arg(i + 1).arg(object.imageId).arg(object.imageSuffix), object.imageData)) return CreationError;
        for (const SlideObject &object : objects) if (object.chartId && !write(QStringLiteral("ppt/charts/chart%1.xml").arg(object.chartId), object.chartData)) return CreationError;
        if (notesRelationshipIds[i]) {
            if (!write(QStringLiteral("ppt/notesSlides/notesSlide%1.xml").arg(i + 1), notesSlideXml(slideNotes[i], width, height))) return CreationError;
            if (!write(QStringLiteral("ppt/notesSlides/_rels/notesSlide%1.xml.rels").arg(i + 1), xmlPart([i](QXmlStreamWriter &xml) {
                xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
                xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../slides/slide%1.xml").arg(i + 1));
                xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId2")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesMaster")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../notesMasters/notesMaster1.xml"));
                xml.writeEndElement();
            }))) return CreationError;
        }
        Q_EMIT sigProgress((i + 1) * 100 / pages.size());
    }
    if (hasNotes) {
        if (!write(QStringLiteral("ppt/notesMasters/notesMaster1.xml"), notesMasterXml())) return CreationError;
        if (!write(QStringLiteral("ppt/notesMasters/_rels/notesMaster1.xml.rels"), xmlPart([](QXmlStreamWriter &xml) {
            xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
            xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../theme/theme1.xml"));
            xml.writeEndElement();
        }))) return CreationError;
    }
    return OK;
}

#include "PptxExport.moc"
