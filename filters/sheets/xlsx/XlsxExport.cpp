/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "XlsxExport.h"
#include "XlsxUtils.h"

#include <ValidityStorage.h>

#include "DocxMathWriter.h"

#include <KPluginFactory>
#include <KoDocument.h>
#include <KoEmbeddedDocumentSaver.h>
#include <KoFilterChain.h>
#include <KoGenStyles.h>
#include <KoImageData.h>
#include <KoPageLayout.h>
#include <KoShape.h>
#include <KoShapePainter.h>
#include <KoShapeSavingContext.h>
#include <KoStore.h>
#include <KoXmlNS.h>
#include <KoXmlWriter.h>

#include <sheets/core/Cell.h>
#include <sheets/core/CellVisuals.h>
#include <sheets/core/CellStorage.h>
#include <sheets/core/Condition.h>
#include <sheets/core/ConditionsStorage.h>
#include <sheets/core/DataFilter.h>
#include <sheets/core/Database.h>
#include <sheets/core/DocBase.h>
#include <sheets/core/PrintSettings.h>
#include <sheets/core/Map.h>
#include <sheets/core/RowFormatStorage.h>
#include <sheets/core/ColFormatStorage.h>
#include <sheets/core/Sheet.h>
#include <sheets/core/ShapeApplicationData.h>
#include <sheets/core/Style.h>
#include <sheets/core/StyleManager.h>
#include <sheets/core/StyleStorage.h>
#include <sheets/engine/CellBase.h>
#include <sheets/engine/MapBase.h>
#include <sheets/engine/NamedAreaManager.h>
#include <sheets/engine/Region.h>
#include <sheets/engine/CalculationSettings.h>
#include <sheets/engine/Value.h>

#include <ChartShape.h>
#include <ChartProxyModel.h>
#include <DataSet.h>
#include <Axis.h>
#include <Legend.h>
#include <PlotArea.h>
#include <CellRegion.h>
#include <TableSource.h>
#include <KoTextShapeData.h>
#include <QTextDocument>
#include <ChartTableModel.h>
#include <kochart_global.h>

#include <QBuffer>
#include <QDomDocument>
#include <QTextStream>
#include <QXmlStreamReader>
#include <QElapsedTimer>
#include <QImage>
#include <QHash>
#include <QRegularExpression>
#include <QRegion>
#include <QSet>
#include <QXmlStreamWriter>

#include <algorithm>
#include <functional>
#include <memory>

using namespace Calligra::Sheets;

Q_LOGGING_CATEGORY(lcXlsxTiming, "calligra.filter.xlsx.timing")

K_PLUGIN_FACTORY_WITH_JSON(XlsxExportFactory, "calligra_filter_ods2xlsx.json", registerPlugin<XlsxExport>();)

namespace {

QByteArray xmlBytes(const std::function<void(QXmlStreamWriter &)> &write)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter xml(&buffer);
    // OOXML parts are machine-readable; indentation adds CPU work and bytes.
    xml.setAutoFormatting(false);
    xml.writeStartDocument();
    write(xml);
    xml.writeEndDocument();
    return bytes;
}

bool putFile(KoStore *store, const QString &path, const QByteArray &bytes)
{
    if (!store->open(path)) return false;
    const bool ok = store->write(bytes) == bytes.size();
    const bool closed = store->close();
    return ok && closed;
}

QString columnName(int column)
{
    QString result;
    while (column > 0) {
        const int digit = (column - 1) % 26;
        result.prepend(QChar(QLatin1Char('A').unicode() + digit));
        column = (column - 1) / 26;
    }
    return result;
}

QString safeSheetName(QString name)
{
    name.replace(QRegularExpression(QStringLiteral("[\\\\/?*\\[\\]:]")), QStringLiteral("_"));
    if (name.isEmpty()) name = QStringLiteral("Sheet");
    if (name.startsWith(QLatin1Char('\''))) name[0] = QLatin1Char('_');
    if (name.endsWith(QLatin1Char('\''))) name[name.size() - 1] = QLatin1Char('_');
    return name.left(31);
}

QString uniqueSheetName(const QString &source, const QStringList &used)
{
    const QString base = safeSheetName(source);
    QString candidate = base;
    for (int suffix = 2; used.contains(candidate, Qt::CaseInsensitive); ++suffix) {
        const QString tail = QStringLiteral(" (%1)").arg(suffix);
        candidate = base.left(31 - tail.size()) + tail;
    }
    return candidate;
}

// Generate explicit formats when the built-in two-decimal formats would lose
// the cell's authored precision. Custom formats always take precedence.
QString excelFormatCode(const Style &style)
{
    if (!style.customFormat().isEmpty()) return style.customFormat();
    const auto type = style.formatType();
    if (type != Format::Number && type != Format::Percentage && type != Format::Scientific)
        return {};
    const int precision = style.precision();
    if (precision < 0) return {};
    QString code = style.thousandsSep() && type == Format::Number ? QStringLiteral("#,##0") : QStringLiteral("0");
    if (precision > 0) code += QLatin1Char('.') + QString(qMin(precision, 30), QLatin1Char('0'));
    if (type == Format::Percentage) code += QLatin1Char('%');
    if (type == Format::Scientific) code += QStringLiteral("E+00");
    return code;
}

int excelNumberFormat(const Style &style)
{
    if (!excelFormatCode(style).isEmpty()) return 164;
    if (Format::isDateTime(style.formatType())) return 22;
    if (Format::isDate(style.formatType())) return 14;
    if (Format::isTime(style.formatType())) return 20;
    switch (style.formatType()) {
    case Format::Number: return style.thousandsSep() ? 4 : 2;
    case Format::Money: return 4;
    case Format::Percentage: return style.precision() == 0 ? 9 : 10;
    case Format::Scientific: return 11;
    case Format::Text: return 49;
    default: return 0;
    }
}

QString colorValue(const QColor &color)
{
    return color.isValid() ? QStringLiteral("FF%1").arg(color.rgb() & 0xffffff, 6, 16, QLatin1Char('0')).toUpper() : QString();
}

QString excelFormula(QString formula)
{
    if (formula.startsWith(QLatin1Char('='))) formula.remove(0, 1);
    bool inString = false;
    bool inQuotedSheetName = false;
    for (qsizetype i = 0; i < formula.size(); ++i) {
        const QChar ch = formula.at(i);
        if (ch == QLatin1Char('"') && !inQuotedSheetName) {
            if (inString && i + 1 < formula.size() && formula.at(i + 1) == QLatin1Char('"')) { ++i; continue; }
            inString = !inString;
        } else if (ch == QLatin1Char('\'') && !inString) {
            if (inQuotedSheetName && i + 1 < formula.size() && formula.at(i + 1) == QLatin1Char('\'')) { ++i; continue; }
            inQuotedSheetName = !inQuotedSheetName;
        } else if (ch == QLatin1Char(';') && !inString && !inQuotedSheetName) {
            formula[i] = QLatin1Char(',');
        }
    }
    return formula;
}

QByteArray stylesXml(const QList<Style> &styles, const QList<Style> &conditionalStyles)
{
    QHash<QString, int> customFormatIds;
    for (const Style &style : styles) {
        if (excelNumberFormat(style) == 164 && !customFormatIds.contains(excelFormatCode(style)))
            customFormatIds.insert(excelFormatCode(style), 164 + customFormatIds.size());
    }
    return xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("styleSheet"));
        xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
        if (!customFormatIds.isEmpty()) {
            xml.writeStartElement(QStringLiteral("numFmts")); xml.writeAttribute(QStringLiteral("count"), QString::number(customFormatIds.size()));
            for (auto it = customFormatIds.cbegin(); it != customFormatIds.cend(); ++it) { xml.writeEmptyElement(QStringLiteral("numFmt")); xml.writeAttribute(QStringLiteral("numFmtId"), QString::number(it.value())); xml.writeAttribute(QStringLiteral("formatCode"), it.key()); }
            xml.writeEndElement();
        }
        xml.writeStartElement(QStringLiteral("fonts")); xml.writeAttribute(QStringLiteral("count"), QString::number(styles.size() + 1));
        auto writeFont = [&](const Style *s) {
            xml.writeStartElement(QStringLiteral("font"));
            if (s) {
                if (s->bold()) xml.writeEmptyElement(QStringLiteral("b"));
                if (s->italic()) xml.writeEmptyElement(QStringLiteral("i"));
                if (s->underline()) xml.writeEmptyElement(QStringLiteral("u"));
                if (s->strikeOut()) xml.writeEmptyElement(QStringLiteral("strike"));
                xml.writeEmptyElement(QStringLiteral("sz")); xml.writeAttribute(QStringLiteral("val"), QString::number(s->fontSize()));
                xml.writeEmptyElement(QStringLiteral("name")); xml.writeAttribute(QStringLiteral("val"), s->fontFamily().isEmpty() ? QStringLiteral("Calibri") : s->fontFamily());
                if (s->fontColor().isValid()) { xml.writeEmptyElement(QStringLiteral("color")); xml.writeAttribute(QStringLiteral("rgb"), colorValue(s->fontColor())); }
            } else { xml.writeEmptyElement(QStringLiteral("sz")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("11")); xml.writeEmptyElement(QStringLiteral("name")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("Calibri")); }
            xml.writeEndElement();
        };
        writeFont(nullptr); for (const Style &s : styles) writeFont(&s); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("fills")); xml.writeAttribute(QStringLiteral("count"), QString::number(styles.size() + 2));
        auto writeFill = [&](const Style *s) { xml.writeStartElement(QStringLiteral("fill")); xml.writeStartElement(QStringLiteral("patternFill"));
            const QColor c = s ? s->backgroundColor() : QColor();
            if (c.isValid() && c.alpha() != 0) { xml.writeAttribute(QStringLiteral("patternType"), QStringLiteral("solid")); xml.writeEmptyElement(QStringLiteral("fgColor")); xml.writeAttribute(QStringLiteral("rgb"), colorValue(c)); xml.writeEmptyElement(QStringLiteral("bgColor")); xml.writeAttribute(QStringLiteral("indexed"), QStringLiteral("64")); }
            else xml.writeAttribute(QStringLiteral("patternType"), QStringLiteral("none"));
            xml.writeEndElement(); xml.writeEndElement(); };
        writeFill(nullptr); xml.writeStartElement(QStringLiteral("fill")); xml.writeStartElement(QStringLiteral("patternFill")); xml.writeAttribute(QStringLiteral("patternType"), QStringLiteral("gray125")); xml.writeEndElement(); xml.writeEndElement();
        for (const Style &s : styles) writeFill(&s); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("borders")); xml.writeAttribute(QStringLiteral("count"), QString::number(styles.size() + 1));
        auto writeSide = [&](const QString &name, const QPen &pen, Style::BorderStyle borderStyle) {
            xml.writeStartElement(name);
            if (pen.style() != Qt::NoPen) {
                QString lineStyle;
                // The pen only carries width and dashing; styles such as double
                // live in the cell's Style because QPen cannot express them.
                switch (borderStyle) {
                case Style::BorderStyleDouble:
                case Style::BorderStyleTriple:
                    lineStyle = QStringLiteral("double");
                    break;
                case Style::BorderStyleSlash:
                    lineStyle = QStringLiteral("dashDot");
                    break;
                default:
                    switch (pen.style()) {
                    case Qt::DashLine: lineStyle = QStringLiteral("dashed"); break;
                    case Qt::DotLine: lineStyle = QStringLiteral("dotted"); break;
                    case Qt::DashDotLine: lineStyle = QStringLiteral("dashDot"); break;
                    case Qt::DashDotDotLine: lineStyle = QStringLiteral("dashDotDot"); break;
                    // Midpoints between Excel's 1/2/3pt steps, so that
                    // LibreOffice's 0.74/1.76/2.49pt land in the right bucket.
                    default: lineStyle = pen.widthF() >= 2.25 ? QStringLiteral("thick") : pen.widthF() >= 1.35 ? QStringLiteral("medium") : QStringLiteral("thin"); break;
                    }
                    break;
                }
                xml.writeAttribute(QStringLiteral("style"), lineStyle);
                if (pen.color().isValid()) { xml.writeEmptyElement(QStringLiteral("color")); xml.writeAttribute(QStringLiteral("rgb"), colorValue(pen.color())); }
            }
            xml.writeEndElement();
        };
        auto writeBorder = [&](const Style *s) { xml.writeStartElement(QStringLiteral("border")); if (s) { writeSide(QStringLiteral("left"), s->leftBorderPen(), s->leftBorderStyle()); writeSide(QStringLiteral("right"), s->rightBorderPen(), s->rightBorderStyle()); writeSide(QStringLiteral("top"), s->topBorderPen(), s->topBorderStyle()); writeSide(QStringLiteral("bottom"), s->bottomBorderPen(), s->bottomBorderStyle()); } else { for (const auto &n : {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("bottom")}) { xml.writeEmptyElement(n); } } xml.writeEmptyElement(QStringLiteral("diagonal")); xml.writeEndElement(); };
        writeBorder(nullptr); for (const Style &s : styles) writeBorder(&s); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("cellStyleXfs")); xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1")); xml.writeEmptyElement(QStringLiteral("xf")); xml.writeAttribute(QStringLiteral("numFmtId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("fontId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0")); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("cellXfs")); xml.writeAttribute(QStringLiteral("count"), QString::number(styles.size() + 1));
        xml.writeEmptyElement(QStringLiteral("xf")); xml.writeAttribute(QStringLiteral("numFmtId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("fontId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0"));
        for (int i = 0; i < styles.size(); ++i) { const Style &s = styles[i]; const int numFmtId = excelNumberFormat(s) == 164 ? customFormatIds.value(excelFormatCode(s), 164) : excelNumberFormat(s); xml.writeStartElement(QStringLiteral("xf")); xml.writeAttribute(QStringLiteral("numFmtId"), QString::number(numFmtId)); xml.writeAttribute(QStringLiteral("fontId"), QString::number(i + 1)); xml.writeAttribute(QStringLiteral("fillId"), QString::number(i + 2)); xml.writeAttribute(QStringLiteral("borderId"), QString::number(i + 1)); xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("applyFont"), QStringLiteral("1")); xml.writeAttribute(QStringLiteral("applyFill"), QStringLiteral("1")); xml.writeAttribute(QStringLiteral("applyBorder"), QStringLiteral("1")); xml.writeAttribute(QStringLiteral("applyNumberFormat"), QStringLiteral("1"));
            if (s.halign() != Style::HAlignUndefined || s.valign() != Style::VAlignUndefined || s.wrapText() || s.angle() != 0 || s.verticalText() || s.shrinkToFit()) { xml.writeStartElement(QStringLiteral("alignment")); if (s.halign() != Style::HAlignUndefined) xml.writeAttribute(QStringLiteral("horizontal"), s.halign() == Style::Center ? QStringLiteral("center") : s.halign() == Style::Right ? QStringLiteral("right") : s.halign() == Style::Justified ? QStringLiteral("justify") : QStringLiteral("left")); if (s.valign() != Style::VAlignUndefined) xml.writeAttribute(QStringLiteral("vertical"), s.valign() == Style::Top ? QStringLiteral("top") : s.valign() == Style::Middle ? QStringLiteral("center") : QStringLiteral("bottom")); if (s.wrapText()) xml.writeAttribute(QStringLiteral("wrapText"), QStringLiteral("1")); if (s.shrinkToFit()) xml.writeAttribute(QStringLiteral("shrinkToFit"), QStringLiteral("1")); if (s.verticalText()) xml.writeAttribute(QStringLiteral("textRotation"), QStringLiteral("255")); else if (s.angle() != 0) { int rotation = -s.angle(); if (rotation < 0) rotation = 90 - rotation; xml.writeAttribute(QStringLiteral("textRotation"), QString::number(rotation)); } xml.writeEndElement(); }
            xml.writeEndElement(); }
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("cellStyles")); xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1")); xml.writeEmptyElement(QStringLiteral("cellStyle")); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Normal")); xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("builtinId"), QStringLiteral("0")); xml.writeEndElement();
        if (!conditionalStyles.isEmpty()) {
            xml.writeStartElement(QStringLiteral("dxfs"));
            xml.writeAttribute(QStringLiteral("count"), QString::number(conditionalStyles.size()));
            for (const Style &style : conditionalStyles) {
                xml.writeStartElement(QStringLiteral("dxf"));
                if (style.bold() || style.italic() || style.underline() || style.strikeOut() || style.fontColor().isValid()) {
                    xml.writeStartElement(QStringLiteral("font"));
                    if (style.bold()) xml.writeEmptyElement(QStringLiteral("b"));
                    if (style.italic()) xml.writeEmptyElement(QStringLiteral("i"));
                    if (style.underline()) xml.writeEmptyElement(QStringLiteral("u"));
                    if (style.strikeOut()) xml.writeEmptyElement(QStringLiteral("strike"));
                    if (style.fontColor().isValid()) { xml.writeEmptyElement(QStringLiteral("color")); xml.writeAttribute(QStringLiteral("rgb"), colorValue(style.fontColor())); }
                    xml.writeEndElement();
                }
                if (style.backgroundColor().isValid() && style.backgroundColor().alpha() != 0) {
                    xml.writeStartElement(QStringLiteral("fill")); xml.writeStartElement(QStringLiteral("patternFill")); xml.writeAttribute(QStringLiteral("patternType"), QStringLiteral("solid"));
                    xml.writeEmptyElement(QStringLiteral("fgColor")); xml.writeAttribute(QStringLiteral("rgb"), colorValue(style.backgroundColor()));
                    xml.writeEmptyElement(QStringLiteral("bgColor")); xml.writeAttribute(QStringLiteral("indexed"), QStringLiteral("64"));
                    xml.writeEndElement(); xml.writeEndElement();
                }
                const QPen left = style.leftBorderPen(), right = style.rightBorderPen();
                const QPen top = style.topBorderPen(), bottom = style.bottomBorderPen();
                if (left.style() != Qt::NoPen || right.style() != Qt::NoPen || top.style() != Qt::NoPen || bottom.style() != Qt::NoPen) {
                    xml.writeStartElement(QStringLiteral("border"));
                    auto writeDxfSide = [&](const QString &name, const QPen &pen) {
                        xml.writeStartElement(name);
                        if (pen.style() != Qt::NoPen) {
                            xml.writeAttribute(QStringLiteral("style"), pen.style() == Qt::DashLine ? QStringLiteral("dashed") : pen.style() == Qt::DotLine ? QStringLiteral("dotted") : QStringLiteral("thin"));
                            if (pen.color().isValid()) { xml.writeEmptyElement(QStringLiteral("color")); xml.writeAttribute(QStringLiteral("rgb"), colorValue(pen.color())); }
                        }
                        xml.writeEndElement();
                    };
                    writeDxfSide(QStringLiteral("left"), left); writeDxfSide(QStringLiteral("right"), right);
                    writeDxfSide(QStringLiteral("top"), top); writeDxfSide(QStringLiteral("bottom"), bottom);
                    xml.writeEmptyElement(QStringLiteral("diagonal")); xml.writeEndElement();
                }
                if (style.halign() != Style::HAlignUndefined || style.valign() != Style::VAlignUndefined || style.wrapText()) {
                    xml.writeStartElement(QStringLiteral("alignment"));
                    if (style.halign() != Style::HAlignUndefined) xml.writeAttribute(QStringLiteral("horizontal"), style.halign() == Style::Center ? QStringLiteral("center") : style.halign() == Style::Right ? QStringLiteral("right") : style.halign() == Style::Justified ? QStringLiteral("justify") : QStringLiteral("left"));
                    if (style.valign() != Style::VAlignUndefined) xml.writeAttribute(QStringLiteral("vertical"), style.valign() == Style::Top ? QStringLiteral("top") : style.valign() == Style::Middle ? QStringLiteral("center") : QStringLiteral("bottom"));
                    if (style.wrapText()) xml.writeAttribute(QStringLiteral("wrapText"), QStringLiteral("1"));
                    xml.writeEndElement();
                }
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }
        xml.writeEndElement();
    });
}

struct EmbeddedImage {
    KoShape *shape = nullptr;
    QByteArray png;
    QByteArray chartXml;
    QByteArray mathXml;
    int index = 0;
};

static QByteArray shapeMathML(KoShape *shape)
{
    if (shape->shapeId() != QLatin1String("FormulaShapeID"))
        return {};

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
    shape->saveOdf(context);
    writer.endElement();

    QDomDocument document;
    if (!document.setContent(frame.data()))
        return {};
    QDomElement math = document.elementsByTagName(QStringLiteral("math:math")).item(0).toElement();
    if (math.isNull())
        return {};
    math.setAttribute(QStringLiteral("xmlns:math"), KoXmlNS::math);
    QString xml;
    QTextStream output(&xml);
    math.save(output, -1);
    return xml.toUtf8();
}

QByteArray chartXml(const KoChart::ChartShape *chart, const Map *map)
{
    // ChartProxyModel::data() is not implemented; its rows count series, not
    // spreadsheet rows. Read the actual DataSets, including single-series and
    // single-point charts, instead of treating it as a table with headers.
    const auto dataSets = chart->proxyModel()->dataSets();
    if (dataSets.isEmpty()) return {};
    QString chartType;
    switch (chart->chartType()) {
    case KoChart::BarChartType: chartType = QStringLiteral("barChart"); break;
    case KoChart::LineChartType: chartType = QStringLiteral("lineChart"); break;
    case KoChart::AreaChartType: chartType = QStringLiteral("areaChart"); break;
    case KoChart::CircleChartType: chartType = QStringLiteral("pieChart"); break;
    case KoChart::RingChartType: chartType = QStringLiteral("doughnutChart"); break;
    case KoChart::ScatterChartType: chartType = QStringLiteral("scatterChart"); break;
    default: return {};
    }
    const bool pie = chartType == QLatin1String("pieChart") || chartType == QLatin1String("doughnutChart");
    const bool scatter = chartType == QLatin1String("scatterChart");
    return xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("c:chartSpace"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"), QStringLiteral("c"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
        xml.writeEmptyElement(QStringLiteral("c:roundedCorners")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("0"));
        xml.writeStartElement(QStringLiteral("c:chart"));
        if (chart->title() && chart->title()->isVisible()) {
            const auto *text = dynamic_cast<KoTextShapeData *>(chart->title()->userData());
            if (text && text->document() && !text->document()->toPlainText().isEmpty()) {
                xml.writeStartElement(QStringLiteral("c:title"));
                xml.writeStartElement(QStringLiteral("c:tx"));
                xml.writeStartElement(QStringLiteral("c:rich"));
                xml.writeEmptyElement(QStringLiteral("a:bodyPr"));
                xml.writeEmptyElement(QStringLiteral("a:lstStyle"));
                xml.writeStartElement(QStringLiteral("a:p"));
                xml.writeStartElement(QStringLiteral("a:r"));
                xml.writeTextElement(QStringLiteral("a:t"), text->document()->toPlainText());
                xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
                xml.writeEndElement(); // tx
                xml.writeEmptyElement(QStringLiteral("c:overlay"));
                xml.writeAttribute(QStringLiteral("val"), QStringLiteral("0"));
                xml.writeEndElement(); // title
            }
        }
        xml.writeStartElement(QStringLiteral("c:plotArea")); xml.writeEmptyElement(QStringLiteral("c:layout"));
        xml.writeStartElement(QStringLiteral("c:") + chartType);
        if (chartType == QLatin1String("barChart")) { xml.writeEmptyElement(QStringLiteral("c:barDir")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("col")); xml.writeEmptyElement(QStringLiteral("c:grouping")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("clustered")); }
        else if (chartType == QLatin1String("lineChart") || chartType == QLatin1String("areaChart")) { xml.writeEmptyElement(QStringLiteral("c:grouping")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("standard")); }
        if (scatter) { xml.writeEmptyElement(QStringLiteral("c:scatterStyle")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("lineMarker")); }
        xml.writeEmptyElement(QStringLiteral("c:varyColors")); xml.writeAttribute(QStringLiteral("val"), pie ? QStringLiteral("1") : QStringLiteral("0"));
        auto reference = [&](const KoChart::CellRegion &region) -> QString {
            if (!region.isValid() || !region.table() || region.rects().size() != 1
                || !map->findSheet(region.table()->name())) return {};
            QString name = region.table()->name();
            name.replace(QLatin1Char('\''), QStringLiteral("''"));
            const QRect rect = region.rects().first();
            auto address = [](int col, int row) {
                return QLatin1Char('$') + columnName(col) + QLatin1Char('$') + QString::number(row);
            };
            return QLatin1Char('\'') + name + QStringLiteral("'!") + address(rect.left(), rect.top())
                + (rect.size() == QSize(1, 1) ? QString() : QLatin1Char(':') + address(rect.right(), rect.bottom()));
        };
        const int seriesCount = pie ? qMin(dataSets.size(), qsizetype(1)) : dataSets.size();
        for (int series = 0; series < seriesCount; ++series) {
            const KoChart::DataSet *dataSet = dataSets[series];
            const int points = dataSet->size();
            xml.writeStartElement(QStringLiteral("c:ser"));
            xml.writeEmptyElement(QStringLiteral("c:idx")); xml.writeAttribute(QStringLiteral("val"), QString::number(series));
            xml.writeEmptyElement(QStringLiteral("c:order")); xml.writeAttribute(QStringLiteral("val"), QString::number(series));
            xml.writeStartElement(QStringLiteral("c:tx"));
            const QString labelRef = reference(dataSet->labelDataRegion());
            if (!labelRef.isEmpty()) {
                xml.writeStartElement(QStringLiteral("c:strRef"));
                xml.writeTextElement(QStringLiteral("c:f"), labelRef);
                xml.writeStartElement(QStringLiteral("c:strCache"));
                xml.writeEmptyElement(QStringLiteral("c:ptCount")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("1"));
                xml.writeStartElement(QStringLiteral("c:pt")); xml.writeAttribute(QStringLiteral("idx"), QStringLiteral("0"));
                xml.writeTextElement(QStringLiteral("c:v"), dataSet->labelData().toString());
                xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
            } else {
                xml.writeTextElement(QStringLiteral("c:v"), dataSet->labelData().toString());
            }
            xml.writeEndElement();
            // Keep per-point colours and labels instead of reducing every
            // series to Excel's default blue bars and a "Series 1" legend.
            auto writeFill = [&](const QColor &color) {
                xml.writeStartElement(QStringLiteral("c:spPr"));
                xml.writeStartElement(QStringLiteral("a:solidFill"));
                xml.writeEmptyElement(QStringLiteral("a:srgbClr"));
                xml.writeAttribute(QStringLiteral("val"), color.name(QColor::HexRgb).mid(1).toUpper());
                xml.writeEndElement(); xml.writeEndElement();
            };
            if (dataSet->brush().style() != Qt::NoBrush && dataSet->brush().color().isValid())
                writeFill(dataSet->brush().color());
            for (int point = 0; point < points; ++point) {
                const QBrush brush = dataSet->brush(point);
                if (brush.style() == Qt::NoBrush || !brush.color().isValid()) continue;
                xml.writeStartElement(QStringLiteral("c:dPt"));
                xml.writeEmptyElement(QStringLiteral("c:idx")); xml.writeAttribute(QStringLiteral("val"), QString::number(point));
                writeFill(brush.color());
                xml.writeEndElement();
            }
            const auto labels = dataSet->valueLabelType();
            xml.writeStartElement(QStringLiteral("c:dLbls"));
            for (int point = 0; point < points; ++point) {
                const auto pointLabels = dataSet->valueLabelType(point);
                if (pointLabels.number == labels.number) continue;
                xml.writeStartElement(QStringLiteral("c:dLbl"));
                xml.writeEmptyElement(QStringLiteral("c:idx")); xml.writeAttribute(QStringLiteral("val"), QString::number(point));
                if (chartType == QLatin1String("barChart") && pointLabels.number) {
                    xml.writeEmptyElement(QStringLiteral("c:dLblPos")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("outEnd"));
                }
                for (const auto &label : {qMakePair(QStringLiteral("c:showLegendKey"), false),
                                          qMakePair(QStringLiteral("c:showVal"), pointLabels.number),
                                          qMakePair(QStringLiteral("c:showCatName"), pointLabels.category),
                                          qMakePair(QStringLiteral("c:showSerName"), false),
                                          qMakePair(QStringLiteral("c:showPercent"), pointLabels.percentage),
                                          qMakePair(QStringLiteral("c:showBubbleSize"), false)}) {
                    xml.writeEmptyElement(label.first); xml.writeAttribute(QStringLiteral("val"), label.second ? QStringLiteral("1") : QStringLiteral("0"));
                }
                xml.writeEndElement();
            }
            if (chartType == QLatin1String("barChart") && labels.number) {
                xml.writeEmptyElement(QStringLiteral("c:dLblPos")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("outEnd"));
            }
            for (const auto &label : {qMakePair(QStringLiteral("c:showLegendKey"), false),
                                      qMakePair(QStringLiteral("c:showVal"), labels.number),
                                      qMakePair(QStringLiteral("c:showCatName"), labels.category),
                                      qMakePair(QStringLiteral("c:showSerName"), false),
                                      qMakePair(QStringLiteral("c:showPercent"), labels.percentage)}) {
                xml.writeEmptyElement(label.first); xml.writeAttribute(QStringLiteral("val"), label.second ? QStringLiteral("1") : QStringLiteral("0"));
            }
            xml.writeEndElement();
            auto writeData = [&](const QString &element, const KoChart::CellRegion &region, bool numeric,
                                 const std::function<QVariant(int)> &value) {
                const QString formula = reference(region);
                xml.writeStartElement(element);
                xml.writeStartElement(numeric ? (formula.isEmpty() ? QStringLiteral("c:numLit") : QStringLiteral("c:numRef"))
                                              : (formula.isEmpty() ? QStringLiteral("c:strLit") : QStringLiteral("c:strRef")));
                if (!formula.isEmpty()) {
                    xml.writeTextElement(QStringLiteral("c:f"), formula);
                    xml.writeStartElement(numeric ? QStringLiteral("c:numCache") : QStringLiteral("c:strCache"));
                }
                if (numeric) xml.writeTextElement(QStringLiteral("c:formatCode"), QStringLiteral("General"));
                xml.writeEmptyElement(QStringLiteral("c:ptCount")); xml.writeAttribute(QStringLiteral("val"), QString::number(points));
                for (int point = 0; point < points; ++point) {
                    const QVariant data = value(point);
                    bool ok = false;
                    const double number = numeric ? data.toDouble(&ok) : 0;
                    if (numeric && !ok) continue;
                    xml.writeStartElement(QStringLiteral("c:pt")); xml.writeAttribute(QStringLiteral("idx"), QString::number(point));
                    xml.writeTextElement(QStringLiteral("c:v"), numeric ? QString::number(number, 'g', 15) : data.toString());
                    xml.writeEndElement();
                }
                if (!formula.isEmpty()) xml.writeEndElement();
                xml.writeEndElement(); xml.writeEndElement();
            };
            if (scatter) {
                writeData(QStringLiteral("c:xVal"), dataSet->xDataRegion(), true, [&](int i) { return dataSet->xData(i); });
            } else {
                writeData(QStringLiteral("c:cat"), dataSet->categoryDataRegion(), false, [&](int i) { return dataSet->categoryData(i); });
            }
            writeData(scatter ? QStringLiteral("c:yVal") : QStringLiteral("c:val"), dataSet->yDataRegion(), true,
                      [&](int i) { return dataSet->yData(i); });
            xml.writeEndElement();
        }
        if (chartType == QLatin1String("barChart") || chartType == QLatin1String("lineChart") || chartType == QLatin1String("areaChart") || scatter) { xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000001")); xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000002")); }
        if (pie) { xml.writeEmptyElement(QStringLiteral("c:firstSliceAng")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("0")); }
        xml.writeEndElement();
        auto writeAxis = [&](const KoChart::Axis *axis, bool category, const QString &id, const QString &crossId, const QString &position) {
            xml.writeStartElement(category ? QStringLiteral("c:catAx") : QStringLiteral("c:valAx"));
            xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), id);
            xml.writeStartElement(QStringLiteral("c:scaling"));
            xml.writeEmptyElement(QStringLiteral("c:orientation"));
            xml.writeAttribute(QStringLiteral("val"), axis && axis->axisDirectionReversed() ? QStringLiteral("maxMin") : QStringLiteral("minMax"));
            xml.writeEndElement();
            xml.writeEmptyElement(QStringLiteral("c:delete")); xml.writeAttribute(QStringLiteral("val"), axis && !axis->isVisible() ? QStringLiteral("1") : QStringLiteral("0"));
            xml.writeEmptyElement(QStringLiteral("c:axPos")); xml.writeAttribute(QStringLiteral("val"), position);
            if (axis && axis->showMajorGrid()) {
                xml.writeStartElement(QStringLiteral("c:majorGridlines"));
                xml.writeStartElement(QStringLiteral("c:spPr"));
                xml.writeStartElement(QStringLiteral("a:ln")); xml.writeAttribute(QStringLiteral("w"), QStringLiteral("6350"));
                xml.writeStartElement(QStringLiteral("a:solidFill"));
                xml.writeEmptyElement(QStringLiteral("a:srgbClr")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("BFBFBF"));
                xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
            }
            xml.writeEmptyElement(QStringLiteral("c:numFmt"));
            xml.writeAttribute(QStringLiteral("formatCode"), QStringLiteral("General")); xml.writeAttribute(QStringLiteral("sourceLinked"), QStringLiteral("1"));
            xml.writeEmptyElement(QStringLiteral("c:majorTickMark"));
            xml.writeAttribute(QStringLiteral("val"), axis && axis->showOuterMajorTicks() ? QStringLiteral("out") : QStringLiteral("none"));
            xml.writeEmptyElement(QStringLiteral("c:minorTickMark")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("none"));
            xml.writeEmptyElement(QStringLiteral("c:tickLblPos"));
            xml.writeAttribute(QStringLiteral("val"), axis && !axis->showLabels() ? QStringLiteral("none") : QStringLiteral("nextTo"));
            // Explicit, bounded text metrics avoid importing chart text as
            // uninitialized defaults in consumers that do not infer them.
            xml.writeStartElement(QStringLiteral("c:txPr"));
            xml.writeEmptyElement(QStringLiteral("a:bodyPr"));
            xml.writeEmptyElement(QStringLiteral("a:lstStyle"));
            xml.writeStartElement(QStringLiteral("a:p"));
            xml.writeStartElement(QStringLiteral("a:pPr"));
            xml.writeStartElement(QStringLiteral("a:defRPr"));
            xml.writeAttribute(QStringLiteral("sz"), QString::number(qRound(qBound(5.0, axis ? axis->fontSize() : 10.0, 72.0) * 100)));
            xml.writeStartElement(QStringLiteral("a:solidFill"));
            xml.writeEmptyElement(QStringLiteral("a:srgbClr")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("000000"));
            xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
            xml.writeEmptyElement(QStringLiteral("a:endParaRPr"));
            xml.writeEndElement(); xml.writeEndElement();
            xml.writeEmptyElement(QStringLiteral("c:crossAx")); xml.writeAttribute(QStringLiteral("val"), crossId);
            xml.writeEmptyElement(QStringLiteral("c:crosses")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("autoZero"));
            if (category) {
                xml.writeEmptyElement(QStringLiteral("c:auto")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("1"));
                xml.writeEmptyElement(QStringLiteral("c:lblAlgn")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("ctr"));
                xml.writeEmptyElement(QStringLiteral("c:lblOffset")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("100"));
            } else {
                xml.writeEmptyElement(QStringLiteral("c:crossBetween")); xml.writeAttribute(QStringLiteral("val"), scatter ? QStringLiteral("midCat") : QStringLiteral("between"));
            }
            xml.writeEndElement();
        };
        if (!pie) {
            writeAxis(chart->plotArea()->xAxis(), !scatter, QStringLiteral("80000001"), QStringLiteral("80000002"), QStringLiteral("b"));
            writeAxis(chart->plotArea()->yAxis(), false, QStringLiteral("80000002"), QStringLiteral("80000001"), QStringLiteral("l"));
        }
        xml.writeStartElement(QStringLiteral("c:spPr"));
        xml.writeStartElement(QStringLiteral("a:solidFill"));
        xml.writeEmptyElement(QStringLiteral("a:srgbClr")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("FFFFFF"));
        xml.writeEndElement(); xml.writeEndElement();
        xml.writeEndElement(); // plotArea
        if (chart->legend() && chart->legend()->isVisible()) {
            xml.writeStartElement(QStringLiteral("c:legend"));
            xml.writeEmptyElement(QStringLiteral("c:legendPos")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("b"));
            xml.writeEmptyElement(QStringLiteral("c:layout"));
            xml.writeEmptyElement(QStringLiteral("c:overlay")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("0"));
            xml.writeEndElement();
        }
        xml.writeEmptyElement(QStringLiteral("c:plotVisOnly")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("1"));
        xml.writeEndElement(); // chart
        // Do not let worksheet borders bleed through an unspecified chart fill.
        xml.writeStartElement(QStringLiteral("c:spPr"));
        xml.writeStartElement(QStringLiteral("a:solidFill"));
        xml.writeEmptyElement(QStringLiteral("a:srgbClr")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("FFFFFF"));
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("a:ln")); xml.writeEmptyElement(QStringLiteral("a:noFill")); xml.writeEndElement();
        xml.writeEndElement();
        xml.writeEndElement(); // chartSpace
    });
}

struct DrawingMarker {
    int column;
    int row;
    qint64 columnOffset;
    qint64 rowOffset;

    bool isValid() const
    {
        return column >= 1 && column <= 16384 && row >= 1 && row <= 1048576 && columnOffset >= 0 && rowOffset >= 0;
    }
};

DrawingMarker drawingMarker(const Sheet &sheet, const QPointF &position)
{
    double left = 0;
    double top = 0;
    const int column = sheet.leftColumn(position.x(), left);
    const int row = sheet.topRow(position.y(), top);
    return {column, row, qRound64((position.x() - left) * 12700.0), qRound64((position.y() - top) * 12700.0)};
}

void writeDrawingMarker(QXmlStreamWriter &xml, const QString &tag, const DrawingMarker &marker)
{
    xml.writeStartElement(tag);
    xml.writeTextElement(QStringLiteral("xdr:col"), QString::number(marker.column - 1));
    xml.writeTextElement(QStringLiteral("xdr:colOff"), QString::number(marker.columnOffset));
    xml.writeTextElement(QStringLiteral("xdr:row"), QString::number(marker.row - 1));
    xml.writeTextElement(QStringLiteral("xdr:rowOff"), QString::number(marker.rowOffset));
    xml.writeEndElement();
}

QByteArray drawingXml(const Sheet &sheet, const QList<EmbeddedImage> &images)
{
    return xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("xdr:wsDr"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"), QStringLiteral("xdr"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/markup-compatibility/2006"), QStringLiteral("mc"));
        xml.writeNamespace(QStringLiteral("http://schemas.microsoft.com/office/drawing/2010/main"), QStringLiteral("a14"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/math"), QStringLiteral("m"));
        for (const EmbeddedImage &image : images) {
            const QPointF pos = image.shape->absolutePosition(KoFlake::TopLeftCorner);
            const QSizeF size = image.shape->size();
            const bool isChart = !image.chartXml.isEmpty();
            const bool isMath = !image.mathXml.isEmpty();
            const auto *data = dynamic_cast<const ShapeApplicationData *>(image.shape->applicationData());
            bool cellAnchor = false;
            if ((isChart || isMath) && data && data->isAnchoredToCell() && pos.x() >= 0 && pos.y() >= 0) {
                const DrawingMarker from = drawingMarker(sheet, pos);
                const DrawingMarker to = drawingMarker(sheet, pos + QPointF(size.width(), size.height()));
                cellAnchor = from.isValid() && to.isValid();
                if (cellAnchor) {
                    xml.writeStartElement(QStringLiteral("xdr:twoCellAnchor"));
                    writeDrawingMarker(xml, QStringLiteral("xdr:from"), from);
                    writeDrawingMarker(xml, QStringLiteral("xdr:to"), to);
                }
            }
            if (!cellAnchor) {
                xml.writeStartElement(QStringLiteral("xdr:absoluteAnchor"));
                xml.writeEmptyElement(QStringLiteral("xdr:pos"));
                xml.writeAttribute(QStringLiteral("x"), QString::number(qRound64(pos.x() * 12700.0)));
                xml.writeAttribute(QStringLiteral("y"), QString::number(qRound64(pos.y() * 12700.0)));
                xml.writeEmptyElement(QStringLiteral("xdr:ext"));
                xml.writeAttribute(QStringLiteral("cx"), QString::number(qRound64(size.width() * 12700.0)));
                xml.writeAttribute(QStringLiteral("cy"), QString::number(qRound64(size.height() * 12700.0)));
            }
            if (isChart) {
                xml.writeStartElement(QStringLiteral("xdr:graphicFrame")); xml.writeAttribute(QStringLiteral("macro"), QString());
                xml.writeStartElement(QStringLiteral("xdr:nvGraphicFramePr")); xml.writeEmptyElement(QStringLiteral("xdr:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QString::number(image.index)); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Chart %1").arg(image.index)); xml.writeEmptyElement(QStringLiteral("xdr:cNvGraphicFramePr")); xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("xdr:xfrm"));
                xml.writeEmptyElement(QStringLiteral("a:off"));
                xml.writeAttribute(QStringLiteral("x"), QStringLiteral("0"));
                xml.writeAttribute(QStringLiteral("y"), QStringLiteral("0"));
                xml.writeEmptyElement(QStringLiteral("a:ext"));
                xml.writeAttribute(QStringLiteral("cx"), QString::number(cellAnchor ? 0 : qRound64(size.width() * 12700.0)));
                xml.writeAttribute(QStringLiteral("cy"), QString::number(cellAnchor ? 0 : qRound64(size.height() * 12700.0)));
                xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("a:graphic")); xml.writeStartElement(QStringLiteral("a:graphicData")); xml.writeAttribute(QStringLiteral("uri"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart")); xml.writeEmptyElement(QStringLiteral("c:chart")); xml.writeAttribute(QStringLiteral("xmlns:c"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart")); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(image.index)); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
            } else if (isMath) {
                xml.writeStartElement(QStringLiteral("mc:AlternateContent"));
                xml.writeStartElement(QStringLiteral("mc:Choice"));
                xml.writeAttribute(QStringLiteral("Requires"), QStringLiteral("a14"));
                xml.writeStartElement(QStringLiteral("a14:m"));
                QXmlStreamReader omml(image.mathXml);
                while (!omml.atEnd()) {
                    omml.readNext();
                    if (omml.isStartDocument() || omml.isEndDocument())
                        continue;
                    if (omml.isStartElement()) {
                        xml.writeStartElement(omml.qualifiedName().toString());
                        const auto attrs = omml.attributes();
                        for (const auto &a : attrs)
                            xml.writeAttribute(a.qualifiedName().toString(), a.value().toString());
                        for (const auto &ns : omml.namespaceDeclarations())
                            xml.writeNamespace(ns.namespaceUri().toString(), ns.prefix().toString());
                    } else if (omml.isEndElement()) {
                        xml.writeEndElement();
                    } else if (omml.isCharacters() && !omml.isWhitespace()) {
                        xml.writeCharacters(omml.text().toString());
                    }
                }
                xml.writeEndElement(); // a14:m
                xml.writeEndElement(); // mc:Choice
                xml.writeStartElement(QStringLiteral("mc:Fallback"));
                xml.writeStartElement(QStringLiteral("xdr:pic"));
                xml.writeStartElement(QStringLiteral("xdr:nvPicPr")); xml.writeEmptyElement(QStringLiteral("xdr:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QString::number(image.index)); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Equation %1").arg(image.index)); xml.writeEmptyElement(QStringLiteral("xdr:cNvPicPr")); xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("xdr:blipFill")); xml.writeEmptyElement(QStringLiteral("a:blip")); xml.writeAttribute(QStringLiteral("r:embed"), QStringLiteral("rId%1").arg(image.index)); xml.writeStartElement(QStringLiteral("a:stretch")); xml.writeEmptyElement(QStringLiteral("a:fillRect")); xml.writeEndElement(); xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("xdr:spPr")); xml.writeStartElement(QStringLiteral("a:prstGeom")); xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect")); xml.writeEmptyElement(QStringLiteral("a:avLst")); xml.writeEndElement(); xml.writeEndElement();
                xml.writeEndElement(); // xdr:pic
                xml.writeEndElement(); // mc:Fallback
                xml.writeEndElement(); // mc:AlternateContent
            } else {
            xml.writeStartElement(QStringLiteral("xdr:pic"));
            xml.writeStartElement(QStringLiteral("xdr:nvPicPr")); xml.writeEmptyElement(QStringLiteral("xdr:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QString::number(image.index)); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Image %1").arg(image.index)); xml.writeEmptyElement(QStringLiteral("xdr:cNvPicPr")); xml.writeEndElement();
            xml.writeStartElement(QStringLiteral("xdr:blipFill")); xml.writeEmptyElement(QStringLiteral("a:blip")); xml.writeAttribute(QStringLiteral("r:embed"), QStringLiteral("rId%1").arg(image.index)); xml.writeStartElement(QStringLiteral("a:stretch")); xml.writeEmptyElement(QStringLiteral("a:fillRect")); xml.writeEndElement(); xml.writeEndElement();
            xml.writeStartElement(QStringLiteral("xdr:spPr")); xml.writeStartElement(QStringLiteral("a:prstGeom")); xml.writeAttribute(QStringLiteral("prst"), QStringLiteral("rect")); xml.writeEmptyElement(QStringLiteral("a:avLst")); xml.writeEndElement(); xml.writeEndElement();
            xml.writeEndElement();
            }
            xml.writeEmptyElement(QStringLiteral("xdr:clientData"));
            xml.writeEndElement();
        }
        xml.writeEndElement();
    });
}

QByteArray drawingRelsXml(const QList<EmbeddedImage> &images)
{
    return xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
        for (const EmbeddedImage &image : images) { xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(image.index)); xml.writeAttribute(QStringLiteral("Type"), image.chartXml.isEmpty() ? QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/image") : QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart")); xml.writeAttribute(QStringLiteral("Target"), image.chartXml.isEmpty() ? QStringLiteral("../media/image%1.png").arg(image.index) : QStringLiteral("../charts/chart%1.xml").arg(image.index)); }
        xml.writeEndElement();
    });
}

QString conditionFormula(const Value &value)
{
    if (value.isString()) return excelFormula(value.asString());
    if (value.isNumber()) return QString::number(value.asFloat(), 'g', 15);
    if (value.isBoolean()) return value.asBoolean() ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    return {};
}

QString conditionOperator(Validity::Type type)
{
    switch (type) {
    case Validity::Equal: return QStringLiteral("equal");
    case Validity::DifferentTo: return QStringLiteral("notEqual");
    case Validity::Different: return QStringLiteral("notBetween");
    case Validity::Superior: return QStringLiteral("greaterThan");
    case Validity::Inferior: return QStringLiteral("lessThan");
    case Validity::SuperiorEqual: return QStringLiteral("greaterThanOrEqual");
    case Validity::InferiorEqual: return QStringLiteral("lessThanOrEqual");
    case Validity::Between: return QStringLiteral("between");
    default: return {};
    }
}

//! Formats a validity limit the way an Excel data validation expects it.
QString validationValue(const Value &value, const CalculationSettings *settings)
{
    if (value.format() == Value::fmt_Date) {
        const QDate date = value.asDate(settings);
        if (date.isValid())
            return QStringLiteral("DATE(%1,%2,%3)").arg(date.year()).arg(date.month()).arg(date.day());
    }
    if (value.format() == Value::fmt_Time) {
        const Time time = value.asTime();
        return QStringLiteral("TIME(%1,%2,%3)").arg(time.hour()).arg(time.minute()).arg(time.second());
    }
    if (value.isNumber())
        return QString::number(numToDouble(value.asFloat()), 'g', 15);
    return value.asString();
}

//! Turns an OpenFormula expression back into Excel notation.
QString validationFormula(const QString &formula)
{
    QString result = formula;
    // Relative/absolute references are written as [.A1] or [$Sheet.A1].
    static const QRegularExpression reference(QStringLiteral("\\[(?:\\$?[^].\\[]*\\.)?\\$?([A-Za-z]+)\\$?([0-9]+)\\]"));
    result.replace(reference, QStringLiteral("\\1\\2"));
    result.replace(QLatin1Char(';'), QLatin1Char(','));
    return result;
}

//! Absolute Excel reference like 'Sheet 1'!$B$2.
QString excelReference(SheetBase *sheet, int column, int row)
{
    QString sheetName = sheet ? sheet->sheetName() : QString();
    bool needsQuotes = sheetName.isEmpty();
    for (const QChar c : sheetName) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_')) {
            needsQuotes = true;
            break;
        }
    }
    if (needsQuotes) {
        sheetName.replace(QLatin1Char('\''), QStringLiteral("''"));
        sheetName = QLatin1Char('\'') + sheetName + QLatin1Char('\'');
    }
    return sheetName + QLatin1Char('!') + QLatin1Char('$') + columnName(column) + QLatin1Char('$') + QString::number(row);
}

QString rangeAddress(const QRect &range)
{
    const QString first = columnName(range.left()) + QString::number(range.top());
    const QString last = columnName(range.right()) + QString::number(range.bottom());
    return first == last ? first : first + QLatin1Char(':') + last;
}

//! One filtered column: equal values go to <filters>, everything else to
//! <customFilters> (Excel has no operator for "contains"; it uses wildcards).
struct SheetFilterColumn
{
    int index = 0;
    QStringList values;
    QList<QPair<QString, QString>> customFilters;
    bool blank = false;
};

//! An autofilter range with the filtered columns of its database range.
struct SheetFilter
{
    QRect range;
    QList<SheetFilterColumn> columns;
    bool isAnd = false;
};

/*!
 Turns an ODF filter regular expression back into the wildcard syntax Excel
 uses in custom filters. Only the shapes the importer produces are recognised:
 the anchors and ".*" of a contains/begins-with/ends-with match.
 */
static QString wildcardFromRegex(const QString &expression)
{
    QString pattern = expression;
    if (pattern.startsWith(QLatin1Char('^')))
        pattern.remove(0, 1);
    if (pattern.endsWith(QLatin1Char('$')))
        pattern.chop(1);

    QString wildcard;
    wildcard.reserve(pattern.size());
    for (int i = 0; i < pattern.size(); ++i) {
        const QChar c = pattern.at(i);
        if (c == QLatin1Char('\\') && i + 1 < pattern.size()) {
            wildcard += pattern.at(++i);
        } else if (c == QLatin1Char('.')) {
            if (i + 1 < pattern.size() && pattern.at(i + 1) == QLatin1Char('*')) {
                wildcard += QLatin1Char('*');
                ++i;
            } else {
                wildcard += QLatin1Char('?');
            }
        } else {
            wildcard += c;
        }
    }
    return wildcard;
}

//! Whether a filter value is the alternation the importer writes for a list.
static bool isValueList(const QString &value)
{
    return value.startsWith(QLatin1String("^(")) && value.endsWith(QLatin1String(")$"));
}

//! Splits the alternation of a value list into its single values.
static void appendEqualValues(const QString &value, QStringList *values)
{
    if (!isValueList(value)) {
        *values << value;
        return;
    }
    QString alternation = value.mid(2, value.size() - 4);
    const QStringList alternatives = alternation.split(QLatin1Char('|'));
    for (QString alternative : alternatives) {
        alternative.remove(QRegularExpression(QStringLiteral("\\(.)")));
        *values << alternative;
    }
}

//! An Excel table as exported to xl/tables/tableN.xml.
struct SheetTable
{
    QString name;
    QRect range;
    QStringList headers;
};

//! Collects the Excel tables (database ranges with a name) of a sheet.
QList<SheetTable> sheetTables(const Sheet *sheet, const Map *map)
{
    QList<SheetTable> tables;
    if (!map || !map->namedAreaManager()) return tables;
    // Only the used part of the sheet can hold a database range. Querying the
    // whole grid here made the export walk every column of the sheet.
    const QRect used = sheet->usedArea(true);
    if (used.isEmpty()) return tables;
    CellStorage *storage = static_cast<CellStorage *>(sheet->cellStorage());
    const Region usedRegion(used, const_cast<Sheet *>(sheet));
    const auto databases = storage->databases(usedRegion);
    for (const auto &database : databases) {
        QRect rect = database.first.toRect();
        if (rect.isEmpty() || rect.top() < 1 || rect.left() < 1) continue;
        rect = rect.intersected(used);
        if (rect.isEmpty()) continue;
        QString name;
        for (const QString &areaName : map->namedAreaManager()->areaNames()) {
            const Region region = map->namedAreaManager()->namedArea(areaName);
            if (!region.isValid() || region.firstSheet() != sheet) continue;
            // The stored range may be clipped to the used area.
            if (region.lastRange() == rect || rect.contains(region.lastRange())) { name = areaName; break; }
        }
        if (name.isEmpty()) continue;
        SheetTable table;
        table.name = name;
        table.range = rect;
        // Header names come from the first row of the table.
        for (int column = rect.left(); column <= rect.right(); ++column) {
            QString header = sheet->cellStorage()->value(column, rect.top()).asString();
            if (header.isEmpty()) header = columnName(column);
            table.headers << header;
        }
        tables.append(table);
    }
    return tables;
}

QByteArray tableXml(const SheetTable &table, int id)
{
    return xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("table"));
        xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
        xml.writeAttribute(QStringLiteral("id"), QString::number(id));
        xml.writeAttribute(QStringLiteral("name"), table.name);
        xml.writeAttribute(QStringLiteral("displayName"), table.name);
        xml.writeAttribute(QStringLiteral("ref"), rangeAddress(table.range));
        xml.writeAttribute(QStringLiteral("totalsRowShown"), QStringLiteral("0"));
        xml.writeEmptyElement(QStringLiteral("autoFilter"));
        xml.writeAttribute(QStringLiteral("ref"), rangeAddress(table.range));
        xml.writeStartElement(QStringLiteral("tableColumns"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(table.headers.size()));
        for (int i = 0; i < table.headers.size(); ++i) {
            xml.writeEmptyElement(QStringLiteral("tableColumn"));
            xml.writeAttribute(QStringLiteral("id"), QString::number(i + 1));
            xml.writeAttribute(QStringLiteral("name"), table.headers.at(i));
        }
        xml.writeEndElement();
        xml.writeEmptyElement(QStringLiteral("tableStyleInfo"));
        xml.writeAttribute(QStringLiteral("name"), QStringLiteral("TableStyleMedium2"));
        xml.writeAttribute(QStringLiteral("showFirstColumn"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("showLastColumn"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("showRowStripes"), QStringLiteral("1"));
        xml.writeAttribute(QStringLiteral("showColumnStripes"), QStringLiteral("0"));
        xml.writeEndElement();
    });
}

static inline qint64 cellStyleKey(int column, int row)
{
    return (qint64(row) << 32) | quint32(column);
}

QByteArray worksheetXml(const Sheet *sheet, const QHash<Style, int> &styleIds, const QHash<qint64, int> &cellStyleIds, const QHash<QString, int> &conditionalStyleIds, bool hasDrawing, QList<QPair<QString, QString>> *links, const QList<SheetTable> &tables, const QList<SheetFilter> &filters)
{
    const QRect area = sheet->usedArea(true);
    return xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("worksheet"));
        xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        const PrintSettings *print = sheet->printSettings();
        const QSize pageLimits = print->pageLimits();
        const bool fitToPage = pageLimits.width() > 0 || pageLimits.height() > 0;
        if (fitToPage) {
            xml.writeStartElement(QStringLiteral("sheetPr"));
            xml.writeEmptyElement(QStringLiteral("pageSetUpPr"));
            xml.writeAttribute(QStringLiteral("fitToPage"), QStringLiteral("1"));
            xml.writeEndElement();
        }
        // Excel column widths are measured in the width of the Normal-style
        // zero character. Calligra stores points; the import filter converts
        // one character as 7.5px at 96dpi, i.e. 5.625pt.
        xml.writeEmptyElement(QStringLiteral("dimension"));
        xml.writeAttribute(QStringLiteral("ref"), area.isEmpty() ? QStringLiteral("A1") : rangeAddress(area));
        const QPoint freeze = sheet->freezePoint();
        const int frozenColumns = freeze.x() - 1;
        const int frozenRows = freeze.y() - 1;
        if (frozenColumns > 0 || frozenRows > 0) {
            const QString topLeft = columnName(freeze.x()) + QString::number(freeze.y());
            const char *activePane = frozenColumns > 0 && frozenRows > 0 ? "bottomRight"
                                    : frozenRows > 0                   ? "bottomLeft"
                                                                       : "topRight";
            xml.writeStartElement(QStringLiteral("sheetViews"));
            xml.writeStartElement(QStringLiteral("sheetView"));
            xml.writeAttribute(QStringLiteral("workbookViewId"), QStringLiteral("0"));
            xml.writeEmptyElement(QStringLiteral("pane"));
            if (frozenColumns > 0) xml.writeAttribute(QStringLiteral("xSplit"), QString::number(frozenColumns));
            if (frozenRows > 0) xml.writeAttribute(QStringLiteral("ySplit"), QString::number(frozenRows));
            xml.writeAttribute(QStringLiteral("topLeftCell"), topLeft);
            xml.writeAttribute(QStringLiteral("activePane"), QString::fromLatin1(activePane));
            xml.writeAttribute(QStringLiteral("state"), QStringLiteral("frozen"));
            xml.writeEmptyElement(QStringLiteral("selection"));
            xml.writeAttribute(QStringLiteral("pane"), QString::fromLatin1(activePane));
            xml.writeAttribute(QStringLiteral("activeCell"), topLeft);
            xml.writeAttribute(QStringLiteral("sqref"), topLeft);
            xml.writeEndElement();
            xml.writeEndElement();
        }
        xml.writeEmptyElement(QStringLiteral("sheetFormatPr"));
        xml.writeAttribute(QStringLiteral("defaultRowHeight"), QString::number(sheet->fullMap()->defaultRowFormat().height, 'f', 2));
        const ColFormatStorage *columns = sheet->columnFormats();
        // Width of a column outside the used area: the sheet's default width.
        // Columns that still carry it keep the default and are not written.
        const double defaultColumnWidth = columns->getColFormat(area.isEmpty() ? 1 : area.right() + 1).width;
        // Sheets supports 32,767 columns. Whole-sheet column formatting must
        // stop at Excel's XFD (16,384), or Excel repairs the worksheet.
        const int lastColumn = qMin(16384, qMax(area.right(), columns->lastNonDefaultCol()));
        bool wroteCols = false;
        for (int col = 1; col <= lastColumn;) {
            int last = col;
            const ColFormat format = columns->getColFormat(col);
            while (last < lastColumn) {
                const ColFormat next = columns->getColFormat(last + 1);
                if (next.width != format.width || next.hidden != format.hidden || next.filtered != format.filtered) break;
                ++last;
            }
            const bool customWidth = format.width > 0 && (col <= area.right() || qAbs(format.width - defaultColumnWidth) > 0.001);
            if (customWidth || format.hidden || format.filtered) {
                if (!wroteCols) { xml.writeStartElement(QStringLiteral("cols")); wroteCols = true; }
                xml.writeStartElement(QStringLiteral("col"));
                xml.writeAttribute(QStringLiteral("min"), QString::number(col));
                xml.writeAttribute(QStringLiteral("max"), QString::number(last));
                if (format.width > 0) xml.writeAttribute(QStringLiteral("width"), QString::number(format.width / 5.625, 'f', 4));
                if (customWidth) xml.writeAttribute(QStringLiteral("customWidth"), QStringLiteral("1"));
                if (format.hidden || format.filtered) xml.writeAttribute(QStringLiteral("hidden"), QStringLiteral("1"));
                xml.writeEndElement();
            }
            col = last + 1;
        }
        if (wroteCols) xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("sheetData"));
        qint64 emptyNanos = 0, styleNanos = 0, valueNanos = 0, writeNanos = 0, iterNanos = 0;
        const RowFormatStorage *rowFormats = sheet->rowFormats();
        if (!area.isEmpty()) {
            QHash<int, QList<int>> rowColumns;
            for (auto it = cellStyleIds.cbegin(); it != cellStyleIds.cend(); ++it) {
                rowColumns[int(it.key() >> 32)].append(int(quint32(it.key())));
            }
            // Row formats are often defined for the whole sheet (LibreOffice
            // writes one table-row style covering all rows), which makes
            // lastNonDefaultRow() return the sheet limit. Iterating to it
            // emitted a million empty rows per sheet and inflated the
            // worksheet from a few kilobytes to tens of megabytes. Only rows
            // inside the used area carry data; rows outside it are described
            // by the default row height, exactly like LibreOffice exports them.
            const int lastRow = area.bottom();
            for (int row = 1; row <= lastRow; ++row) {
                QList<int> columns = rowColumns.value(row);
                std::sort(columns.begin(), columns.end());
                const RowFormat format = rowFormats->getRowFormat(row);
                const bool hasCells = !columns.isEmpty();
                if (!hasCells && rowFormats->isDefaultRow(row)) continue;
                xml.writeStartElement(QStringLiteral("row"));
                xml.writeAttribute(QStringLiteral("r"), QString::number(row));
                // Preserve the stored height even when it is the most common
                // height: Excel otherwise uses its default or auto-fits the row.
                if (format.height > 0) { xml.writeAttribute(QStringLiteral("ht"), QString::number(format.height, 'f', 2)); xml.writeAttribute(QStringLiteral("customHeight"), QStringLiteral("1")); }
                if (format.hidden || format.filtered) xml.writeAttribute(QStringLiteral("hidden"), QStringLiteral("1"));
                for (int col : columns) {
                    const Cell cell(const_cast<Sheet *>(sheet), col, row);
                    QElapsedTimer cellTimer;
                    cellTimer.start();
                    const bool cellEmpty = cell.isEmpty();
                    emptyNanos += cellTimer.nsecsElapsed();
                    if (!cellEmpty || cellStyleIds.value(cellStyleKey(col, row)) > 0) {
                        xml.writeStartElement(QStringLiteral("c"));
                        xml.writeAttribute(QStringLiteral("r"), columnName(cell.column()) + QString::number(row));
                        cellTimer.restart();
                        const auto styleIt = cellStyleIds.constFind(cellStyleKey(cell.column(), row));
                        if (styleIt != cellStyleIds.cend()) {
                            if (styleIt.value() > 0) xml.writeAttribute(QStringLiteral("s"), QString::number(styleIt.value()));
                        } else {
                            const auto fallbackIt = styleIds.constFind(cell.style());
                            if (fallbackIt != styleIds.cend()) xml.writeAttribute(QStringLiteral("s"), QString::number(fallbackIt.value()));
                        }
                        styleNanos += cellTimer.nsecsElapsed();
                        cellTimer.restart();
                        if (!cellEmpty) {
                            const Value value = cell.value();
                            valueNanos += cellTimer.nsecsElapsed();
                            cellTimer.restart();
                            const QString hyperlink = cell.link();
                            if (!hyperlink.isEmpty() && links) {
                                links->append(qMakePair(columnName(cell.column()) + QString::number(row), hyperlink));
                            }
                            const bool isFormula = cell.isFormula();
                            if (!isFormula) {
                                if (value.isError()) xml.writeAttribute(QStringLiteral("t"), QStringLiteral("e"));
                                else if (value.isBoolean()) xml.writeAttribute(QStringLiteral("t"), QStringLiteral("b"));
                                else if (!value.isNumber()) xml.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
                            } else if (value.isError()) {
                                xml.writeAttribute(QStringLiteral("t"), QStringLiteral("e"));
                            } else if (value.isBoolean()) {
                                xml.writeAttribute(QStringLiteral("t"), QStringLiteral("b"));
                            } else if (value.isString()) {
                                xml.writeAttribute(QStringLiteral("t"), QStringLiteral("str"));
                            }
                            if (isFormula) {
                                const QString formula = excelFormula(cell.userInput());
                                xml.writeStartElement(QStringLiteral("f"));
                                xml.writeCharacters(formula);
                                xml.writeEndElement();
                                if (value.isError()) {
                                    xml.writeTextElement(QStringLiteral("v"), value.errorMessage());
                                } else if (value.isNumber()) {
                                    xml.writeTextElement(QStringLiteral("v"), QString::number(value.asFloat(), 'g', 15));
                                } else if (value.isBoolean()) {
                                    xml.writeTextElement(QStringLiteral("v"), value.asBoolean() ? QStringLiteral("1") : QStringLiteral("0"));
                                } else if (value.isString()) {
                                    xml.writeTextElement(QStringLiteral("v"), value.asString());
                                }
                            } else if (value.isError()) {
                                xml.writeTextElement(QStringLiteral("v"), value.errorMessage());
                            } else if (value.isNumber()) {
                                xml.writeTextElement(QStringLiteral("v"), QString::number(value.asFloat(), 'g', 15));
                            } else if (value.isBoolean()) {
                                xml.writeTextElement(QStringLiteral("v"), value.asBoolean() ? QStringLiteral("1") : QStringLiteral("0"));
                            } else {
                                xml.writeStartElement(QStringLiteral("is"));
                                xml.writeStartElement(QStringLiteral("t"));
                                xml.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
                                xml.writeCharacters(value.isString() ? value.asString() : cell.displayText());
                                xml.writeEndElement();
                                xml.writeEndElement();
                            }
                        }
                        xml.writeEndElement();
                    }
                    writeNanos += cellTimer.nsecsElapsed();
                }
                xml.writeEndElement();
            }
        }
        qCDebug(lcXlsxTiming, "XLSX worksheet %s: empty %lld style %lld value %lld write %lld iter %lld", qPrintable(sheet->sheetName()), (long long)(emptyNanos/1000000), (long long)(styleNanos/1000000), (long long)(valueNanos/1000000), (long long)(writeNanos/1000000), (long long)(iterNanos/1000000));
        xml.writeEndElement(); // sheetData
        for (const SheetFilter &filter : filters) {
            if (filter.range.isEmpty()) continue;
            xml.writeStartElement(QStringLiteral("autoFilter"));
            xml.writeAttribute(QStringLiteral("ref"), rangeAddress(filter.range));
            for (const SheetFilterColumn &column : filter.columns) {
                xml.writeStartElement(QStringLiteral("filterColumn"));
                xml.writeAttribute(QStringLiteral("colId"), QString::number(column.index));
                if (!column.values.isEmpty() || column.blank) {
                    // Equality only: <filters> compares the cell literally.
                    xml.writeStartElement(QStringLiteral("filters"));
                    if (column.blank)
                        xml.writeAttribute(QStringLiteral("blank"), QStringLiteral("1"));
                    for (const QString &value : column.values) {
                        xml.writeEmptyElement(QStringLiteral("filter"));
                        xml.writeAttribute(QStringLiteral("val"), value);
                    }
                    xml.writeEndElement();
                }
                if (!column.customFilters.isEmpty()) {
                    xml.writeStartElement(QStringLiteral("customFilters"));
                    if (column.customFilters.size() > 1)
                        xml.writeAttribute(QStringLiteral("and"), filter.isAnd ? QStringLiteral("true") : QStringLiteral("false"));
                    for (const auto &customFilter : column.customFilters) {
                        xml.writeEmptyElement(QStringLiteral("customFilter"));
                        xml.writeAttribute(QStringLiteral("operator"), customFilter.first);
                        xml.writeAttribute(QStringLiteral("val"), customFilter.second);
                    }
                    xml.writeEndElement();
                }
                xml.writeEndElement();
            }
            xml.writeEndElement();
        }

        auto *storage = static_cast<CellStorage *>(sheet->cellStorage());
        const QList<Cell> mergedCells = storage->masterCells(Region(area, const_cast<Sheet *>(sheet)));
        QList<QString> mergedRanges;
        QSet<QString> seenMergedRanges;
        for (const Cell &cell : mergedCells) {
            if (cell.mergedXCells() <= 0 && cell.mergedYCells() <= 0)
                continue;
            const QString first = columnName(cell.column()) + QString::number(cell.row());
            const QString last = columnName(cell.column() + cell.mergedXCells()) + QString::number(cell.row() + cell.mergedYCells());
            const QString range = first + QLatin1Char(':') + last;
            if (!seenMergedRanges.contains(range)) {
                seenMergedRanges.insert(range);
                mergedRanges.append(range);
            }
        }
        if (!mergedRanges.isEmpty()) {
            xml.writeStartElement(QStringLiteral("mergeCells"));
            xml.writeAttribute(QStringLiteral("count"), QString::number(mergedRanges.size()));
            for (const QString &range : mergedRanges) {
                xml.writeEmptyElement(QStringLiteral("mergeCell"));
                xml.writeAttribute(QStringLiteral("ref"), range);
            }
            xml.writeEndElement();
        }
        const ConditionsStorage *conditionStorage = sheet->conditionsStorage();
        const QRect conditionArea = conditionStorage->usedArea();
        if (!conditionArea.isEmpty()) {
            const auto entries = conditionStorage->currentData(Region(conditionArea, const_cast<Sheet *>(sheet)));
            // The model stores conditions per cell; group cells that carry the
            // same rules so one <conditionalFormatting> covers the whole range,
            // and ignore conditions that come from number-format style maps
            // (they have no dxf style and are not conditional formatting).
            struct RuleGroup { QString signature; Conditions conditions; QList<QString> ranges; };
            QList<RuleGroup> groups;
            for (const auto &entry : entries) {
                const Conditions conditions = entry.second;
                if (conditions.isEmpty()) continue;
                QString signature;
                bool usable = false;
                for (const Conditional &condition : conditions.conditionList()) {
                    const bool formulaRule = condition.cond == Validity::IsTrueFormula;
                    const QString op = conditionOperator(condition.cond);
                    if (!formulaRule && op.isEmpty()) continue;
                    if (!conditionalStyleIds.contains(condition.styleName)) continue;
                    usable = true;
                    QString descriptor;
                    if (formulaRule) {
                        descriptor = QStringLiteral("E:") + condition.value1.asString();
                    } else {
                        descriptor = op + QLatin1Char(':') + condition.value1.asString() + QLatin1Char(':') + condition.value2.asString();
                    }
                    signature += descriptor + QLatin1Char('|') + condition.styleName + QLatin1Char(';');
                }
                if (!usable) continue;
                const QList<QRect> rects = Region(entry.first.toRect().intersected(conditionArea), const_cast<Sheet *>(sheet)).rects();
                RuleGroup *group = nullptr;
                for (RuleGroup &candidate : groups) {
                    if (candidate.signature == signature) { group = &candidate; break; }
                }
                if (!group) {
                    RuleGroup created;
                    created.signature = signature;
                    created.conditions = conditions;
                    groups.append(created);
                    group = &groups.last();
                }
                for (const QRect &rect : rects) {
                    const QString address = rangeAddress(rect);
                    if (!group->ranges.contains(address)) group->ranges.append(address);
                }
            }
            int priority = 1;
            for (const RuleGroup &group : groups) {
                if (group.ranges.isEmpty()) continue;
                xml.writeStartElement(QStringLiteral("conditionalFormatting"));
                xml.writeAttribute(QStringLiteral("sqref"), group.ranges.join(QLatin1Char(' ')));
                for (const Conditional &condition : group.conditions.conditionList()) {
                    const bool formulaRule = condition.cond == Validity::IsTrueFormula;
                    const QString op = conditionOperator(condition.cond);
                    if (!formulaRule && op.isEmpty()) continue;
                    const auto dxfIt = conditionalStyleIds.constFind(condition.styleName);
                    if (dxfIt == conditionalStyleIds.cend()) continue;
                    xml.writeStartElement(QStringLiteral("cfRule"));
                    xml.writeAttribute(QStringLiteral("type"), formulaRule ? QStringLiteral("expression") : QStringLiteral("cellIs"));
                    if (!formulaRule) xml.writeAttribute(QStringLiteral("operator"), op);
                    xml.writeAttribute(QStringLiteral("priority"), QString::number(priority++));
                    xml.writeAttribute(QStringLiteral("dxfId"), QString::number(dxfIt.value()));
                    QString formula = formulaRule ? excelFormula(condition.value1.asString()) : conditionFormula(condition.value1);
                    if (!formula.isEmpty()) xml.writeTextElement(QStringLiteral("formula"), formula);
                    if (condition.cond == Validity::Between || condition.cond == Validity::Different) {
                        const QString second = conditionFormula(condition.value2);
                        if (!second.isEmpty()) xml.writeTextElement(QStringLiteral("formula"), second);
                    }
                    xml.writeEndElement();
                }
                xml.writeEndElement();
            }
        }

        sheet->cellVisuals()->saveXlsxRules(xml);

        // Data validations. The model stores one Validity per cell; cells that
        // share the same rule are merged into a single sqref again.
        const ValidityStorage *validities = sheet->validityStorage();
        if (validities) {
            const QRect validationArea = validities->usedArea();
            if (!validationArea.isEmpty()) {
                const auto entries = validities->currentData(Region(validationArea, const_cast<Sheet *>(sheet)));
                struct ValidationGroup { Validity validity; QList<QString> ranges; };
                QList<ValidationGroup> groups;
                for (const auto &entry : entries) {
                    const Validity &validity = entry.second;
                    if (validity.isEmpty()) continue;
                    ValidationGroup *group = nullptr;
                    for (ValidationGroup &candidate : groups) {
                        if (candidate.validity == validity) { group = &candidate; break; }
                    }
                    if (!group) {
                        ValidationGroup created;
                        created.validity = validity;
                        groups.append(created);
                        group = &groups.last();
                    }
                    const QList<QRect> rects = Region(entry.first.toRect().intersected(validationArea), const_cast<Sheet *>(sheet)).rects();
                    for (const QRect &rect : rects) {
                        const QString address = rangeAddress(rect);
                        if (!group->ranges.contains(address)) group->ranges.append(address);
                    }
                }
                if (!groups.isEmpty()) {
                    xml.writeStartElement(QStringLiteral("dataValidations"));
                    xml.writeAttribute(QStringLiteral("count"), QString::number(groups.size()));
                    for (const ValidationGroup &group : groups) {
                        if (group.ranges.isEmpty()) continue;
                        const Validity &validity = group.validity;
                        QString type, formula1, formula2;
                        switch (validity.restriction()) {
                        case Validity::Number: type = QStringLiteral("whole"); break;
                        case Validity::Integer: type = QStringLiteral("decimal"); break;
                        case Validity::Date: type = QStringLiteral("date"); break;
                        case Validity::Time: type = QStringLiteral("time"); break;
                        case Validity::TextLength: type = QStringLiteral("textLength"); break;
                        case Validity::List: {
                            type = QStringLiteral("list");
                            const QStringList items = validity.validityList();
                            if (items.size() == 1 && (items.first().contains(QLatin1Char('.')) || items.first().startsWith(QLatin1Char('$')))) {
                                formula1 = items.first();
                            } else {
                                QStringList values;
                                for (QString item : items) {
                                    if (item.startsWith(QLatin1Char('"')) && item.endsWith(QLatin1Char('"')))
                                        item = item.mid(1, item.size() - 2);
                                    values << item;
                                }
                                formula1 = QLatin1Char('"') + values.join(QLatin1Char(',')) + QLatin1Char('"');
                            }
                            break;
                        }
                        case Validity::Formula:
                            type = QStringLiteral("custom");
                            formula1 = validationFormula(validity.minimumValue().asString());
                            break;
                        default:
                            type = QStringLiteral("custom");
                            break;
                        }
                        if (type.isEmpty()) continue;
                        const CalculationSettings *settings = sheet->doc() ? sheet->doc()->map()->calculationSettings() : nullptr;
                        if (formula1.isEmpty() && type != QLatin1String("list") && type != QLatin1String("custom")) {
                            formula1 = validationValue(validity.minimumValue(), settings);
                            formula2 = validationValue(validity.maximumValue(), settings);
                        }
                        QString op;
                        switch (validity.condition()) {
                        case Validity::Equal: op = QStringLiteral("equal"); break;
                        case Validity::DifferentTo: op = QStringLiteral("notEqual"); break;
                        case Validity::Superior: op = QStringLiteral("greaterThan"); break;
                        case Validity::Inferior: op = QStringLiteral("lessThan"); break;
                        case Validity::SuperiorEqual: op = QStringLiteral("greaterThanOrEqual"); break;
                        case Validity::InferiorEqual: op = QStringLiteral("lessThanOrEqual"); break;
                        case Validity::Between: op = QStringLiteral("between"); break;
                        case Validity::Different: op = QStringLiteral("notBetween"); break;
                        default: break;
                        }
                        if (type == QLatin1String("custom") || type == QLatin1String("list")) op.clear();
                        else if (op.isEmpty()) op = QStringLiteral("equal");

                        xml.writeStartElement(QStringLiteral("dataValidation"));
                        xml.writeAttribute(QStringLiteral("sqref"), group.ranges.join(QLatin1Char(' ')));
                        xml.writeAttribute(QStringLiteral("type"), type);
                        if (!op.isEmpty()) xml.writeAttribute(QStringLiteral("operator"), op);
                        xml.writeAttribute(QStringLiteral("allowBlank"), validity.allowEmptyCell() ? QStringLiteral("1") : QStringLiteral("0"));
                        xml.writeAttribute(QStringLiteral("showInputMessage"), validity.displayValidationInformation() ? QStringLiteral("1") : QStringLiteral("0"));
                        xml.writeAttribute(QStringLiteral("showErrorMessage"), validity.displayMessage() ? QStringLiteral("1") : QStringLiteral("0"));
                        if (!validity.titleInfo().isEmpty()) xml.writeAttribute(QStringLiteral("promptTitle"), validity.titleInfo());
                        if (!validity.messageInfo().isEmpty()) xml.writeAttribute(QStringLiteral("prompt"), validity.messageInfo());
                        if (!validity.title().isEmpty()) xml.writeAttribute(QStringLiteral("errorTitle"), validity.title());
                        if (!validity.message().isEmpty()) xml.writeAttribute(QStringLiteral("error"), validity.message());
                        if (!formula1.isEmpty()) xml.writeTextElement(QStringLiteral("formula1"), formula1);
                        if (!formula2.isEmpty() && (validity.condition() == Validity::Between || validity.condition() == Validity::Different)) {
                            xml.writeTextElement(QStringLiteral("formula2"), formula2);
                        }
                        xml.writeEndElement();
                    }
                    xml.writeEndElement();
                }
            }
        }

        if (links && !links->isEmpty()) {
            xml.writeStartElement(QStringLiteral("hyperlinks"));
            for (int i = 0; i < links->size(); ++i) {
                xml.writeEmptyElement(QStringLiteral("hyperlink"));
                xml.writeAttribute(QStringLiteral("ref"), links->at(i).first);
                xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(hasDrawing ? i + 2 : i + 1));
            }
            xml.writeEndElement();
        }
        const KoPageLayout pageLayout = print->pageLayout();
        constexpr qreal pointsPerInch = 72.0;
        xml.writeEmptyElement(QStringLiteral("pageMargins"));
        xml.writeAttribute(QStringLiteral("left"), QString::number(pageLayout.leftMargin / pointsPerInch, 'f', 6));
        xml.writeAttribute(QStringLiteral("right"), QString::number(pageLayout.rightMargin / pointsPerInch, 'f', 6));
        xml.writeAttribute(QStringLiteral("top"), QString::number(pageLayout.topMargin / pointsPerInch, 'f', 6));
        xml.writeAttribute(QStringLiteral("bottom"), QString::number(pageLayout.bottomMargin / pointsPerInch, 'f', 6));
        xml.writeAttribute(QStringLiteral("header"), QStringLiteral("0.3"));
        xml.writeAttribute(QStringLiteral("footer"), QStringLiteral("0.3"));
        xml.writeEmptyElement(QStringLiteral("pageSetup"));
        xml.writeAttribute(QStringLiteral("orientation"), pageLayout.orientation == KoPageFormat::Landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"));
        const QString paper = print->paperFormatString();
        int paperSize = 9;
        if (paper == QLatin1String("Letter")) paperSize = 1;
        else if (paper == QLatin1String("Legal")) paperSize = 5;
        else if (paper == QLatin1String("A3")) paperSize = 8;
        else if (paper == QLatin1String("A5")) paperSize = 11;
        else if (paper == QLatin1String("B5")) paperSize = 17;
        xml.writeAttribute(QStringLiteral("paperSize"), QString::number(paperSize));
        if (fitToPage) {
            xml.writeAttribute(QStringLiteral("fitToWidth"), QString::number(qMax(0, pageLimits.width())));
            xml.writeAttribute(QStringLiteral("fitToHeight"), QString::number(qMax(0, pageLimits.height())));
        } else {
            xml.writeAttribute(QStringLiteral("scale"), QString::number(qBound(10, qRound(print->zoom() * 100.0), 400)));
        }
        if (hasDrawing) { xml.writeStartElement(QStringLiteral("drawing")); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId1")); xml.writeEndElement(); }
        if (!tables.isEmpty()) {
            // Hyperlinks and the drawing are written before the tables, so the
            // table relationships continue their numbering.
            const int firstTableLinkId = (hasDrawing ? 2 : 1) + links->size();
            xml.writeStartElement(QStringLiteral("tableParts"));
            xml.writeAttribute(QStringLiteral("count"), QString::number(tables.size()));
            for (int t = 0; t < tables.size(); ++t) {
                xml.writeEmptyElement(QStringLiteral("tablePart"));
                xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(firstTableLinkId + t));
            }
            xml.writeEndElement();
        }
        sheet->cellVisuals()->saveXlsxSparklines(xml);
        xml.writeEndElement(); // worksheet
    });
}

} // namespace

KoFilter::ConversionStatus XlsxExport::convert(const QByteArray &from, const QByteArray &to)
{
    if (from != "application/vnd.oasis.opendocument.spreadsheet"
        || to != "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") return BadMimeType;

    auto *document = qobject_cast<const DocBase *>(m_chain->inputDocument());
    if (!document || !document->map()) return StupidError;

    QElapsedTimer timer;
    timer.start();
    qCDebug(lcXlsxTiming, "XLSX export: start");

    // OPC packages must not contain the ODF-specific, untyped mimetype entry.
    std::unique_ptr<KoStore> store(KoStore::createStore(m_chain->outputFile(), KoStore::Write, QByteArray(), KoStore::Zip, false));
    if (!store) return CreationError;

    const QList<SheetBase *> bases = document->map()->sheetList();
    QList<const Sheet *> sheets;
    for (SheetBase *base : bases) sheets.append(static_cast<const Sheet *>(base));
    if (sheets.isEmpty()) return InternalError;
    QStringList sheetNames;
    for (const Sheet *sheet : sheets) sheetNames.append(uniqueSheetName(sheet->sheetName(), sheetNames));

    QList<Style> styles;
    QHash<Style, int> styleIds;
    QList<Style> conditionalStyles;
    QHash<QString, int> conditionalStyleIds;
    QList<QHash<qint64, int>> sheetCellStyleIds;
    for (const Sheet *sheet : sheets) {
        QElapsedTimer sheetTimer;
        sheetTimer.start();
        const QRect area = sheet->usedArea(true);
        CellStorage *storage = static_cast<CellStorage *>(sheet->cellStorage());
        qCDebug(lcXlsxTiming, "XLSX styles: sheet %s area %d,%d %dx%d", qPrintable(sheet->sheetName()), area.left(), area.top(), area.width(), area.height());
        int cellCount = 0;
        QHash<qint64, int> cellStyleIds;
        auto registerStyle = [&](const Style &style) {
            if (style.isDefault()) return 0;
            const auto existing = styleIds.constFind(style);
            if (existing != styleIds.cend()) return existing.value();
            styles.append(style);
            const int id = styles.size();
            styleIds.insert(style, id);
            return id;
        };
        qint64 styleNanos = 0, hashNanos = 0, iterNanos = 0;
        for (int row = area.top(); !area.isEmpty() && row <= area.bottom(); ++row) {
            QElapsedTimer it;
            it.start();
            Cell cell = storage->firstInRow(row, CellStorage::VisitContent);
            iterNanos += it.nsecsElapsed();
            while (!cell.isNull() && !cell.isDefault() && cell.column() <= area.right()) {
                it.restart();
                const Style style = cell.style();
                styleNanos += it.nsecsElapsed();
                it.restart();
                const int styleId = registerStyle(style);
                cellStyleIds.insert(cellStyleKey(cell.column(), row), styleId);
                hashNanos += it.nsecsElapsed();
                it.restart();
                cell = storage->nextInRow(cell.column(), row, CellStorage::VisitContent);
                iterNanos += it.nsecsElapsed();
                ++cellCount;
            }
        }
        // Content iterators omit empty cells carrying borders, fills or other
        // formatting. Visit actual styled ranges within the export area, rather
        // than scanning the bounding rectangle of a sparse sheet cell by cell.
        QRegion styledCells;
        const auto styleEntries = storage->styleStorage()->currentData(Region(area, const_cast<Sheet *>(sheet)));
        for (const auto &entry : styleEntries) {
            if (entry.second.data() && entry.second->type() != Style::DefaultStyleKey)
                styledCells += entry.first.toRect();
        }
        for (const QRect &rect : styledCells) {
            for (int row = rect.top(); row <= rect.bottom(); ++row) {
                for (int col = rect.left(); col <= rect.right(); ++col) {
                    const qint64 key = cellStyleKey(col, row);
                    if (cellStyleIds.contains(key)) continue;
                    const int id = registerStyle(Cell(const_cast<Sheet *>(sheet), col, row).style());
                    if (id > 0) cellStyleIds.insert(key, id);
                }
            }
        }
        // Sheets draws a merged master's border around the whole merged
        // rectangle. Excel needs the corresponding sides on its perimeter
        // cells too; merely exporting the covered cells leaves gaps.
        const auto mergedCells = storage->masterCells(Region(area, const_cast<Sheet *>(sheet)));
        for (const Cell &master : mergedCells) {
            const int left = master.column(), top = master.row();
            const int right = left + master.mergedXCells();
            const int bottom = top + master.mergedYCells();
            if (left == right && top == bottom) continue;
            const Style masterStyle = master.style();
            auto extendBorder = [&](int col, int row) {
                const qint64 key = cellStyleKey(col, row);
                const int oldId = cellStyleIds.value(key);
                Style style = oldId > 0 ? styles[oldId - 1] : Cell(const_cast<Sheet *>(sheet), col, row).style();
                style.clearAttribute(Style::DefaultStyleKey);
                if (col == left && style.leftBorderPen().style() == Qt::NoPen && masterStyle.leftBorderPen().style() != Qt::NoPen) {
                    style.setLeftBorderPen(masterStyle.leftBorderPen());
                    style.setLeftBorderStyle(masterStyle.leftBorderStyle());
                }
                if (col == right && style.rightBorderPen().style() == Qt::NoPen && masterStyle.rightBorderPen().style() != Qt::NoPen) {
                    style.setRightBorderPen(masterStyle.rightBorderPen());
                    style.setRightBorderStyle(masterStyle.rightBorderStyle());
                }
                if (row == top && style.topBorderPen().style() == Qt::NoPen && masterStyle.topBorderPen().style() != Qt::NoPen) {
                    style.setTopBorderPen(masterStyle.topBorderPen());
                    style.setTopBorderStyle(masterStyle.topBorderStyle());
                }
                if (row == bottom && style.bottomBorderPen().style() == Qt::NoPen && masterStyle.bottomBorderPen().style() != Qt::NoPen) {
                    style.setBottomBorderPen(masterStyle.bottomBorderPen());
                    style.setBottomBorderStyle(masterStyle.bottomBorderStyle());
                }
                const int id = registerStyle(style);
                if (id > 0) cellStyleIds.insert(key, id);
            };
            for (int col = left; col <= right; ++col) {
                extendBorder(col, top);
                if (bottom != top) extendBorder(col, bottom);
            }
            for (int row = top + 1; row < bottom; ++row) {
                extendBorder(left, row);
                if (right != left) extendBorder(right, row);
            }
        }
        qCDebug(lcXlsxTiming, "XLSX styles: sheet %s %d cells, %d styles, %lld ms (style %lld, hash %lld, iter %lld)", qPrintable(sheet->sheetName()), cellCount, styles.size(), (long long)sheetTimer.elapsed(), (long long)(styleNanos/1000000), (long long)(hashNanos/1000000), (long long)(iterNanos/1000000));
        const QRect cfArea = sheet->conditionsStorage()->usedArea();
        if (!cfArea.isEmpty()) {
            const auto entries = sheet->conditionsStorage()->currentData(Region(cfArea, const_cast<Sheet *>(sheet)));
            for (const auto &entry : entries) {
                for (const Conditional &condition : entry.second.conditionList()) {
                    if (condition.styleName.isEmpty() || conditionalStyleIds.contains(condition.styleName)) continue;
                    CustomStyle *namedStyle = document->map()->styleManager()->style(condition.styleName);
                    if (!namedStyle) continue;
                    // A style carrying a number format is one of the variants a
                    // number format generates (positive/negative/zero), not a
                    // conditional-formatting style; those must not become dxfs.
                    if (namedStyle->formatType() != Format::Generic || !namedStyle->customFormat().isEmpty()) continue;
                    conditionalStyles.append(static_cast<const Style &>(*namedStyle));
                    conditionalStyleIds.insert(condition.styleName, conditionalStyles.size() - 1);
                }
            }
        }
        sheetCellStyleIds.append(cellStyleIds);
    }

    qCDebug(lcXlsxTiming, "XLSX export: styles done at %lld ms", (long long)timer.elapsed());

    QList<QList<EmbeddedImage>> sheetImages;
    int nextImageIndex = 1;
    for (const Sheet *sheet : sheets) {
        QList<EmbeddedImage> images;
        for (KoShape *shape : sheet->shapes()) {
            if (!shape || shape->size().isEmpty()) continue;
            // Placeholder frames the import could not resolve carry no content;
            // painting them would add empty pictures to the exported drawing.
            if (shape->shapeId() == QLatin1String("UnavailShapeID")) continue;
            QByteArray chartPart;
            if (auto *chart = dynamic_cast<KoChart::ChartShape *>(shape)) { chartPart = chartXml(chart, document->map()); }
            QByteArray mathPart;
            if (chartPart.isEmpty()) {
                const QByteArray mathml = shapeMathML(shape);
                if (!mathml.isEmpty())
                    mathPart = mathmlToOmml(mathml);
            }
            QByteArray png;
            if (chartPart.isEmpty() && mathPart.isEmpty()) {
                // KoShapePainter::paint(QImage&) paints into an existing image;
                // a default-constructed QImage is null and nothing is drawn.
                const QRectF bounds = shape->boundingRect();
                const qreal toPixels = 96.0 / 72.0; // shape geometry is in points
                QImage image(QSize(qMax(1, qRound(bounds.width() * toPixels)),
                                   qMax(1, qRound(bounds.height() * toPixels))),
                             QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                KoShapePainter painter;
                painter.setShapes({shape});
                painter.paint(image);
                if (image.isNull()) continue;
                QBuffer buffer(&png);
                if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) continue;
            }
            images.append({shape, png, chartPart, mathPart, nextImageIndex++});
        }
        sheetImages.append(images);
    }

    qCDebug(lcXlsxTiming, "XLSX export: images done at %lld ms", (long long)timer.elapsed());

    auto write = [&](const QString &path, const QByteArray &data) {
        if (putFile(store.get(), path, data)) return true;
        m_chain->setErrorMessage(QStringLiteral("Could not write XLSX part: %1").arg(path));
        return false;
    };

    // Excel tables and plain database ranges per sheet.
    QList<QList<SheetTable>> sheetTableList;
    QList<QList<SheetFilter>> sheetFilterList;
    for (const Sheet *sheet : sheets) {
        const QList<SheetTable> tables = sheetTables(sheet, document->map());
        sheetTableList.append(tables);
        QList<SheetFilter> filters;
        const QRect used = sheet->usedArea(true);
        if (used.isEmpty()) { sheetFilterList.append(filters); continue; }
        CellStorage *storage = static_cast<CellStorage *>(sheet->cellStorage());
        const Region usedRegion(used, const_cast<Sheet *>(sheet));
        for (const auto &database : storage->databases(usedRegion)) {
            QRect rect = database.first.toRect();
            if (rect.isEmpty() || rect.top() < 1 || rect.left() < 1) continue;
            rect = rect.intersected(used);
            if (rect.isEmpty()) continue;
            bool isTable = false;
            for (const SheetTable &table : tables) {
                if (table.range == rect) { isTable = true; break; }
            }
            if (isTable) continue;
            SheetFilter filter;
            filter.range = rect;
            // The filter conditions are stored per field, relative to the
            // first column of the range.
            const Filter databaseFilter = database.second.filter();
            const AbstractCondition *rootCondition = databaseFilter.rootCondition();
            filter.isAnd = rootCondition && rootCondition->type() == AbstractCondition::And;
            for (int field = 0; field < rect.width(); ++field) {
                const QMap<QString, AbstractCondition::Comparison> conditions = databaseFilter.conditions(field);
                if (conditions.isEmpty()) continue;
                SheetFilterColumn column;
                column.index = field;
                for (auto it = conditions.constBegin(); it != conditions.constEnd(); ++it) {
                    const QString value = it.key();
                    switch (it.value()) {
                    case AbstractCondition::Comparison::Equal:
                        appendEqualValues(value, &column.values);
                        break;
                    case AbstractCondition::Comparison::Match:
                        // A list of values is stored as an alternation, any
                        // other expression is a wildcard match.
                        if (isValueList(value))
                            appendEqualValues(value, &column.values);
                        else
                            column.customFilters.append(qMakePair(QStringLiteral("equal"), wildcardFromRegex(value)));
                        break;
                    case AbstractCondition::Comparison::NotMatch:
                        column.customFilters.append(qMakePair(QStringLiteral("notEqual"), wildcardFromRegex(value)));
                        break;
                    case AbstractCondition::Comparison::NotEqual:
                        column.customFilters.append(qMakePair(QStringLiteral("notEqual"), value));
                        break;
                    case AbstractCondition::Comparison::Less:
                        column.customFilters.append(qMakePair(QStringLiteral("lessThan"), value));
                        break;
                    case AbstractCondition::Comparison::LessOrEqual:
                        column.customFilters.append(qMakePair(QStringLiteral("lessThanOrEqual"), value));
                        break;
                    case AbstractCondition::Comparison::Greater:
                        column.customFilters.append(qMakePair(QStringLiteral("greaterThan"), value));
                        break;
                    case AbstractCondition::Comparison::GreaterOrEqual:
                        column.customFilters.append(qMakePair(QStringLiteral("greaterThanOrEqual"), value));
                        break;
                    case AbstractCondition::Comparison::Empty:
                        column.blank = true;
                        break;
                    default:
                        break;
                    }
                }
                if (!column.values.isEmpty() || !column.customFilters.isEmpty() || column.blank)
                    filter.columns.append(column);
            }
            filters.append(filter);
        }
        sheetFilterList.append(filters);
    }

    int totalTableCount = 0;
    for (const auto &tables : sheetTableList) totalTableCount += tables.size();
    if (!write(QStringLiteral("[Content_Types].xml"), xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Types"));
        xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/content-types"));
        xml.writeEmptyElement(QStringLiteral("Default")); xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("rels")); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-package.relationships+xml"));
        xml.writeEmptyElement(QStringLiteral("Default")); xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("xml")); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/xml"));
        if (nextImageIndex > 1 && std::any_of(sheetImages.cbegin(), sheetImages.cend(), [](const QList<EmbeddedImage> &images) { return std::any_of(images.cbegin(), images.cend(), [](const EmbeddedImage &image) { return image.chartXml.isEmpty(); }); })) { xml.writeEmptyElement(QStringLiteral("Default")); xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("png")); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("image/png")); }
        xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/workbook.xml")); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"));
        xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/styles.xml")); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"));
        for (int i = 0; i < sheets.size(); ++i) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/worksheets/sheet%1.xml").arg(i + 1)); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml")); }
        for (int i = 0; i < sheets.size(); ++i) if (!sheetImages[i].isEmpty()) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/drawings/drawing%1.xml").arg(i + 1)); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.drawing+xml")); }
        for (const auto &images : sheetImages) for (const EmbeddedImage &image : images) if (!image.chartXml.isEmpty()) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/charts/chart%1.xml").arg(image.index)); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml")); }
        for (int t = 1; t <= totalTableCount; ++t) { xml.writeEmptyElement(QStringLiteral("Override")); xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/tables/table%1.xml").arg(t)); xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml")); }
        xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("_rels/.rels"), xmlBytes([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
        xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("xl/workbook.xml")); xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("xl/workbook.xml"), xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("workbook")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main")); xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        xml.writeStartElement(QStringLiteral("sheets"));
        for (int i = 0; i < sheets.size(); ++i) { xml.writeEmptyElement(QStringLiteral("sheet")); xml.writeAttribute(QStringLiteral("name"), sheetNames[i]); xml.writeAttribute(QStringLiteral("sheetId"), QString::number(i + 1)); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(i + 1)); }
        xml.writeEndElement(); // sheets
        if (const NamedAreaManager *manager = document->map()->namedAreaManager()) {
            struct NamedRange { QString name; QString reference; };
            QList<NamedRange> namedRanges;
            for (const QString &name : manager->areaNames()) {
                const Region region = manager->namedArea(name);
                if (!region.isValid() || !region.firstSheet()) continue;
                // A database range is an Excel table (or an autofilter) and is
                // written to the table part, not as a defined name.
                bool isDatabaseRange = false;
                for (SheetBase *base : document->map()->sheetList()) {
                    Sheet *namedSheet = dynamic_cast<Sheet *>(base);
                    if (!namedSheet) continue;
                    CellStorage *namedStorage = static_cast<CellStorage *>(namedSheet->cellStorage());
                    const Region namedRegion(region.lastRange(), namedSheet);
                    for (const auto &database : namedStorage->databases(namedRegion)) {
                        if (database.first.toRect() == region.lastRange())
                            isDatabaseRange = true;
                    }
                }
                if (isDatabaseRange) continue;
                QStringList ranges;
                for (Region::ConstIterator it = region.constBegin(); it != region.constEnd(); ++it) {
                    const QRect rect = (*it)->rect();
                    QString range = excelReference((*it)->sheet(), rect.left(), rect.top());
                    if (rect.width() > 1 || rect.height() > 1)
                        range += QLatin1Char(':') + excelReference((*it)->sheet(), rect.right(), rect.bottom());
                    ranges << range;
                }
                if (!ranges.isEmpty()) namedRanges.append({name, ranges.join(QLatin1Char(' '))});
            }
            if (!namedRanges.isEmpty()) {
                xml.writeStartElement(QStringLiteral("definedNames"));
                for (const NamedRange &namedRange : namedRanges) {
                    xml.writeStartElement(QStringLiteral("definedName"));
                    xml.writeAttribute(QStringLiteral("name"), namedRange.name);
                    xml.writeCharacters(namedRange.reference);
                    xml.writeEndElement();
                }
                xml.writeEndElement();
            }
        }
    }))) return CreationError;

    if (!write(QStringLiteral("xl/_rels/workbook.xml.rels"), xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
        for (int i = 0; i < sheets.size(); ++i) { xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(i + 1)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("worksheets/sheet%1.xml").arg(i + 1)); }
        xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(sheets.size() + 1)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("styles.xml"));
        xml.writeEndElement();
    }))) return CreationError;

    int nextTableId = 1;
    for (int i = 0; i < sheets.size(); ++i) {
        qCDebug(lcXlsxTiming, "XLSX export: sheet %d (%s) begin at %lld ms", i + 1, qPrintable(sheetNames[i]), (long long)timer.elapsed());
        QList<QPair<QString, QString>> sheetLinks;
        if (!write(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), worksheetXml(sheets[i], styleIds, sheetCellStyleIds[i], conditionalStyleIds, !sheetImages[i].isEmpty(), &sheetLinks, sheetTableList[i], sheetFilterList[i]))) return CreationError;
        qCDebug(lcXlsxTiming, "XLSX export: sheet %d done at %lld ms", i + 1, (long long)timer.elapsed());
        if (!sheetImages[i].isEmpty() || !sheetLinks.isEmpty() || !sheetTableList[i].isEmpty()) {
            const int firstLinkId = sheetImages[i].isEmpty() ? 1 : 2;
            // sheetLinks is complete now: worksheetXml has filled it.
            const int firstTableId = firstLinkId + sheetLinks.size();
            if (!write(QStringLiteral("xl/worksheets/_rels/sheet%1.xml.rels").arg(i + 1), xmlBytes([i, &sheetImages, &sheetLinks, &sheetTableList, firstLinkId, firstTableId, nextTableId](QXmlStreamWriter &xml) {
                xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
                if (!sheetImages[i].isEmpty()) {
                    xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../drawings/drawing%1.xml").arg(i + 1));
                }
                for (int li = 0; li < sheetLinks.size(); ++li) {
                    xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(firstLinkId + li)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink")); xml.writeAttribute(QStringLiteral("Target"), sheetLinks.at(li).second); xml.writeAttribute(QStringLiteral("TargetMode"), QStringLiteral("External"));
                }
                for (int ti = 0; ti < sheetTableList[i].size(); ++ti) {
                    xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(firstTableId + ti)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/table")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../tables/table%1.xml").arg(nextTableId + ti));
                }
                xml.writeEndElement();
            }))) return CreationError;
            if (!sheetImages[i].isEmpty() && !write(QStringLiteral("xl/drawings/drawing%1.xml").arg(i + 1), drawingXml(*sheets[i], sheetImages[i]))) return CreationError;
        }
        if (!sheetImages[i].isEmpty()) {
            if (!write(QStringLiteral("xl/drawings/_rels/drawing%1.xml.rels").arg(i + 1), drawingRelsXml(sheetImages[i]))) return CreationError;
            for (const EmbeddedImage &image : sheetImages[i]) {
                if (image.chartXml.isEmpty()) { if (!write(QStringLiteral("xl/media/image%1.png").arg(image.index), image.png)) return CreationError; }
                else if (!write(QStringLiteral("xl/charts/chart%1.xml").arg(image.index), image.chartXml)) return CreationError;
            }
        }
        for (const SheetTable &table : sheetTableList[i]) {
            if (!write(QStringLiteral("xl/tables/table%1.xml").arg(nextTableId), tableXml(table, nextTableId))) return CreationError;
            ++nextTableId;
        }
        Q_EMIT sigProgress((i + 1) * 100 / sheets.size());
    }
    if (!write(QStringLiteral("xl/styles.xml"), stylesXml(styles, conditionalStyles))) return CreationError;
    qCDebug(lcXlsxTiming, "XLSX export: finished at %lld ms", (long long)timer.elapsed());
    return OK;
}

#include "XlsxExport.moc"
