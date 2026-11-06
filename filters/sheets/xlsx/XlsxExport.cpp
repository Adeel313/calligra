/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "XlsxExport.h"

#include <KPluginFactory>
#include <KoDocument.h>
#include <KoFilterChain.h>
#include <KoImageData.h>
#include <KoPageLayout.h>
#include <KoShape.h>
#include <KoShapePainter.h>
#include <KoStore.h>

#include <sheets/core/Cell.h>
#include <sheets/core/CellStorage.h>
#include <sheets/core/Condition.h>
#include <sheets/core/ConditionsStorage.h>
#include <sheets/core/DocBase.h>
#include <sheets/core/PrintSettings.h>
#include <sheets/core/Map.h>
#include <sheets/core/RowFormatStorage.h>
#include <sheets/core/ColFormatStorage.h>
#include <sheets/core/Sheet.h>
#include <sheets/core/Style.h>
#include <sheets/core/StyleManager.h>
#include <sheets/engine/CellBase.h>
#include <sheets/engine/MapBase.h>
#include <sheets/engine/Value.h>

#include <ChartShape.h>
#include <ChartProxyModel.h>
#include <ChartTableModel.h>
#include <kochart_global.h>

#include <QBuffer>
#include <QImage>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamWriter>

#include <functional>
#include <memory>

using namespace Calligra::Sheets;

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
        auto writeSide = [&](const QString &name, const QPen &pen) { xml.writeStartElement(name); if (pen.style() != Qt::NoPen) { const QString lineStyle = pen.style() == Qt::DashLine ? QStringLiteral("dashed") : pen.style() == Qt::DotLine ? QStringLiteral("dotted") : pen.style() == Qt::DashDotLine ? QStringLiteral("dashDot") : pen.style() == Qt::DashDotDotLine ? QStringLiteral("dashDotDot") : QStringLiteral("thin"); xml.writeAttribute(QStringLiteral("style"), lineStyle); if (pen.color().isValid()) { xml.writeEmptyElement(QStringLiteral("color")); xml.writeAttribute(QStringLiteral("rgb"), colorValue(pen.color())); } } xml.writeEndElement(); };
        auto writeBorder = [&](const Style *s) { xml.writeStartElement(QStringLiteral("border")); if (s) { writeSide(QStringLiteral("left"), s->leftBorderPen()); writeSide(QStringLiteral("right"), s->rightBorderPen()); writeSide(QStringLiteral("top"), s->topBorderPen()); writeSide(QStringLiteral("bottom"), s->bottomBorderPen()); } else { for (const auto &n : {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("bottom")}) { xml.writeEmptyElement(n); } } xml.writeEmptyElement(QStringLiteral("diagonal")); xml.writeEndElement(); };
        writeBorder(nullptr); for (const Style &s : styles) writeBorder(&s); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("cellStyleXfs")); xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1")); xml.writeEmptyElement(QStringLiteral("xf")); xml.writeAttribute(QStringLiteral("numFmtId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("fontId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0")); xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("cellXfs")); xml.writeAttribute(QStringLiteral("count"), QString::number(styles.size() + 1));
        xml.writeEmptyElement(QStringLiteral("xf")); xml.writeAttribute(QStringLiteral("numFmtId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("fontId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0"));
        for (int i = 0; i < styles.size(); ++i) { const Style &s = styles[i]; const int numFmtId = excelNumberFormat(s) == 164 ? customFormatIds.value(excelFormatCode(s), 164) : excelNumberFormat(s); xml.writeStartElement(QStringLiteral("xf")); xml.writeAttribute(QStringLiteral("numFmtId"), QString::number(numFmtId)); xml.writeAttribute(QStringLiteral("fontId"), QString::number(i + 1)); xml.writeAttribute(QStringLiteral("fillId"), QString::number(i + 2)); xml.writeAttribute(QStringLiteral("borderId"), QString::number(i + 1)); xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("applyFont"), QStringLiteral("1")); xml.writeAttribute(QStringLiteral("applyFill"), QStringLiteral("1")); xml.writeAttribute(QStringLiteral("applyBorder"), QStringLiteral("1")); xml.writeAttribute(QStringLiteral("applyNumberFormat"), QStringLiteral("1"));
            if (s.halign() != Style::HAlignUndefined || s.valign() != Style::VAlignUndefined || s.wrapText()) { xml.writeStartElement(QStringLiteral("alignment")); if (s.halign() != Style::HAlignUndefined) xml.writeAttribute(QStringLiteral("horizontal"), s.halign() == Style::Center ? QStringLiteral("center") : s.halign() == Style::Right ? QStringLiteral("right") : s.halign() == Style::Justified ? QStringLiteral("justify") : QStringLiteral("left")); if (s.valign() != Style::VAlignUndefined) xml.writeAttribute(QStringLiteral("vertical"), s.valign() == Style::Top ? QStringLiteral("top") : s.valign() == Style::Middle ? QStringLiteral("center") : QStringLiteral("bottom")); if (s.wrapText()) xml.writeAttribute(QStringLiteral("wrapText"), QStringLiteral("1")); xml.writeEndElement(); }
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
    int index = 0;
};

QByteArray chartXml(const KoChart::ChartShape *chart)
{
    const QAbstractItemModel *model = chart->proxyModel();
    if (!model || model->rowCount() < 2 || model->columnCount() < 2) return {};
    QString chartType;
    switch (chart->chartType()) {
    case KoChart::BarChartType: chartType = QStringLiteral("barChart"); break;
    case KoChart::LineChartType: chartType = QStringLiteral("lineChart"); break;
    case KoChart::AreaChartType: chartType = QStringLiteral("areaChart"); break;
    case KoChart::CircleChartType: chartType = QStringLiteral("pieChart"); break;
    case KoChart::RingChartType: chartType = QStringLiteral("doughnutChart"); break;
    default: return {};
    }
    const int rows = model->rowCount();
    const int columns = model->columnCount();
    const bool pie = chartType == QLatin1String("pieChart") || chartType == QLatin1String("doughnutChart");
    return xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("c:chartSpace"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"), QStringLiteral("c"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
        xml.writeStartElement(QStringLiteral("c:chart"));
        xml.writeStartElement(QStringLiteral("c:plotArea")); xml.writeEmptyElement(QStringLiteral("c:layout"));
        xml.writeStartElement(QStringLiteral("c:") + chartType);
        if (chartType == QLatin1String("barChart")) { xml.writeEmptyElement(QStringLiteral("c:barDir")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("col")); xml.writeEmptyElement(QStringLiteral("c:grouping")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("clustered")); }
        else if (chartType == QLatin1String("lineChart") || chartType == QLatin1String("areaChart")) { xml.writeEmptyElement(QStringLiteral("c:grouping")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("standard")); }
        xml.writeEmptyElement(QStringLiteral("c:varyColors")); xml.writeAttribute(QStringLiteral("val"), pie ? QStringLiteral("1") : QStringLiteral("0"));
        const int seriesCount = pie ? qMin(columns, 2) : columns;
        for (int col = 1; col < seriesCount; ++col) {
            xml.writeStartElement(QStringLiteral("c:ser"));
            xml.writeEmptyElement(QStringLiteral("c:idx")); xml.writeAttribute(QStringLiteral("val"), QString::number(col - 1));
            xml.writeEmptyElement(QStringLiteral("c:order")); xml.writeAttribute(QStringLiteral("val"), QString::number(col - 1));
            xml.writeStartElement(QStringLiteral("c:tx")); xml.writeTextElement(QStringLiteral("c:v"), model->index(0, col).data().toString()); xml.writeEndElement();
            xml.writeStartElement(QStringLiteral("c:cat")); xml.writeStartElement(QStringLiteral("c:strLit")); xml.writeEmptyElement(QStringLiteral("c:ptCount")); xml.writeAttribute(QStringLiteral("val"), QString::number(rows - 1));
            for (int row = 1; row < rows; ++row) { xml.writeStartElement(QStringLiteral("c:pt")); xml.writeAttribute(QStringLiteral("idx"), QString::number(row - 1)); xml.writeTextElement(QStringLiteral("c:v"), model->index(row, 0).data().toString()); xml.writeEndElement(); }
            xml.writeEndElement(); xml.writeEndElement();
            xml.writeStartElement(QStringLiteral("c:val")); xml.writeStartElement(QStringLiteral("c:numLit")); xml.writeTextElement(QStringLiteral("c:formatCode"), QStringLiteral("General")); xml.writeEmptyElement(QStringLiteral("c:ptCount")); xml.writeAttribute(QStringLiteral("val"), QString::number(rows - 1));
            for (int row = 1; row < rows; ++row) { xml.writeStartElement(QStringLiteral("c:pt")); xml.writeAttribute(QStringLiteral("idx"), QString::number(row - 1)); xml.writeTextElement(QStringLiteral("c:v"), model->index(row, col).data().toString()); xml.writeEndElement(); }
            xml.writeEndElement(); xml.writeEndElement();
            xml.writeEndElement();
        }
        if (chartType == QLatin1String("barChart") || chartType == QLatin1String("lineChart") || chartType == QLatin1String("areaChart")) { xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000001")); xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000002")); }
        if (pie) { xml.writeEmptyElement(QStringLiteral("c:firstSliceAng")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("0")); }
        xml.writeEndElement();
        if (!pie) {
            xml.writeStartElement(QStringLiteral("c:catAx")); xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000001"));
            xml.writeStartElement(QStringLiteral("c:scaling")); xml.writeEmptyElement(QStringLiteral("c:orientation")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("minMax")); xml.writeEndElement(); xml.writeEmptyElement(QStringLiteral("c:axPos")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("b")); xml.writeEmptyElement(QStringLiteral("c:crossAx")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000002")); xml.writeEmptyElement(QStringLiteral("c:crosses")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("autoZero")); xml.writeEmptyElement(QStringLiteral("c:lblOffset")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("100")); xml.writeEndElement();
            xml.writeStartElement(QStringLiteral("c:valAx")); xml.writeEmptyElement(QStringLiteral("c:axId")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000002")); xml.writeStartElement(QStringLiteral("c:scaling")); xml.writeEmptyElement(QStringLiteral("c:orientation")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("minMax")); xml.writeEndElement(); xml.writeEmptyElement(QStringLiteral("c:axPos")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("l")); xml.writeEmptyElement(QStringLiteral("c:crossAx")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("80000001")); xml.writeEmptyElement(QStringLiteral("c:crosses")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("autoZero")); xml.writeEmptyElement(QStringLiteral("c:crossBetween")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("between")); xml.writeEndElement();
        }
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("c:legend")); xml.writeEmptyElement(QStringLiteral("c:legendPos")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("b")); xml.writeEmptyElement(QStringLiteral("c:layout")); xml.writeEmptyElement(QStringLiteral("c:overlay")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("0")); xml.writeEndElement();
        xml.writeEmptyElement(QStringLiteral("c:plotVisOnly")); xml.writeAttribute(QStringLiteral("val"), QStringLiteral("1"));
        xml.writeEndElement(); xml.writeEndElement();
    });
}

QByteArray drawingXml(const QList<EmbeddedImage> &images)
{
    return xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("xdr:wsDr"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing"), QStringLiteral("xdr"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
        xml.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
        for (const EmbeddedImage &image : images) {
            const QPointF pos = image.shape->absolutePosition(KoFlake::TopLeftCorner);
            const QSizeF size = image.shape->size();
            xml.writeStartElement(QStringLiteral("xdr:absoluteAnchor"));
            xml.writeEmptyElement(QStringLiteral("xdr:pos")); xml.writeAttribute(QStringLiteral("x"), QString::number(qRound64(pos.x() * 12700.0))); xml.writeAttribute(QStringLiteral("y"), QString::number(qRound64(pos.y() * 12700.0)));
            xml.writeStartElement(QStringLiteral("xdr:ext")); xml.writeAttribute(QStringLiteral("cx"), QString::number(qRound64(size.width() * 12700.0))); xml.writeAttribute(QStringLiteral("cy"), QString::number(qRound64(size.height() * 12700.0))); xml.writeEndElement();
            if (!image.chartXml.isEmpty()) {
                xml.writeStartElement(QStringLiteral("xdr:graphicFrame")); xml.writeAttribute(QStringLiteral("macro"), QString());
                xml.writeStartElement(QStringLiteral("xdr:nvGraphicFramePr")); xml.writeEmptyElement(QStringLiteral("xdr:cNvPr")); xml.writeAttribute(QStringLiteral("id"), QString::number(image.index)); xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Chart %1").arg(image.index)); xml.writeEmptyElement(QStringLiteral("xdr:cNvGraphicFramePr")); xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("xdr:xfrm")); xml.writeEmptyElement(QStringLiteral("a:off")); xml.writeAttribute(QStringLiteral("x"), QStringLiteral("0")); xml.writeAttribute(QStringLiteral("y"), QStringLiteral("0")); xml.writeEmptyElement(QStringLiteral("a:ext")); xml.writeAttribute(QStringLiteral("cx"), QString::number(qRound64(size.width() * 12700.0))); xml.writeAttribute(QStringLiteral("cy"), QString::number(qRound64(size.height() * 12700.0))); xml.writeEndElement();
                xml.writeStartElement(QStringLiteral("a:graphic")); xml.writeStartElement(QStringLiteral("a:graphicData")); xml.writeAttribute(QStringLiteral("uri"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart")); xml.writeEmptyElement(QStringLiteral("c:chart")); xml.writeAttribute(QStringLiteral("xmlns:c"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart")); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId%1").arg(image.index)); xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
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

QString rangeAddress(const QRect &range)
{
    const QString first = columnName(range.left()) + QString::number(range.top());
    const QString last = columnName(range.right()) + QString::number(range.bottom());
    return first == last ? first : first + QLatin1Char(':') + last;
}

QByteArray worksheetXml(const Sheet *sheet, const QHash<Style, int> &styleIds, const QHash<QString, int> &conditionalStyleIds, bool hasDrawing)
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
        // zero character. Calligra stores points; 5.25pt is the Calibri 11
        // default character width used by the XLSX default style above.
        xml.writeEmptyElement(QStringLiteral("dimension"));
        xml.writeAttribute(QStringLiteral("ref"), area.isEmpty() ? QStringLiteral("A1") : rangeAddress(area));
        const ColFormatStorage *columns = sheet->columnFormats();
        const int lastColumn = qMax(area.right(), columns->lastNonDefaultCol());
        bool wroteCols = false;
        for (int col = 1; col <= lastColumn;) {
            int last = col;
            const ColFormat format = columns->getColFormat(col);
            while (last < lastColumn) {
                const ColFormat next = columns->getColFormat(last + 1);
                if (next.width != format.width || next.hidden != format.hidden || next.filtered != format.filtered) break;
                ++last;
            }
            if (format.width > 0 || format.hidden || format.filtered) {
                if (!wroteCols) { xml.writeStartElement(QStringLiteral("cols")); wroteCols = true; }
                xml.writeStartElement(QStringLiteral("col"));
                xml.writeAttribute(QStringLiteral("min"), QString::number(col));
                xml.writeAttribute(QStringLiteral("max"), QString::number(last));
                if (format.width > 0) xml.writeAttribute(QStringLiteral("width"), QString::number(format.width / 5.25, 'f', 4));
                xml.writeAttribute(QStringLiteral("customWidth"), QStringLiteral("1"));
                if (format.hidden || format.filtered) xml.writeAttribute(QStringLiteral("hidden"), QStringLiteral("1"));
                xml.writeEndElement();
            }
            col = last + 1;
        }
        if (wroteCols) xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("sheetData"));
        const RowFormatStorage *rowFormats = sheet->rowFormats();
        if (!area.isEmpty()) {
            auto *storage = static_cast<CellStorage *>(sheet->cellStorage());
            // Row formats are often defined for the whole sheet (LibreOffice
            // writes one table-row style covering all rows), which makes
            // lastNonDefaultRow() return the sheet limit. Iterating to it
            // emitted a million empty rows per sheet and inflated the
            // worksheet from a few kilobytes to tens of megabytes. Only rows
            // inside the used area carry data; rows outside it are described
            // by the default row height, exactly like LibreOffice exports them.
            const int lastRow = area.bottom();
            for (int row = 1; row <= lastRow; ++row) {
                Cell cell = storage->firstInRow(row, CellStorage::VisitContent);
                const RowFormat format = rowFormats->getRowFormat(row);
                // CellStorage returns a null Cell when the row has no content.
                // Check nullness before inspecting its default state or position.
                const bool hasCells = !cell.isNull() && !cell.isDefault() && cell.column() <= area.right();
                if (!hasCells && rowFormats->isDefaultRow(row)) continue;
                xml.writeStartElement(QStringLiteral("row"));
                xml.writeAttribute(QStringLiteral("r"), QString::number(row));
                if (format.height > 0) { xml.writeAttribute(QStringLiteral("ht"), QString::number(format.height, 'f', 2)); xml.writeAttribute(QStringLiteral("customHeight"), QStringLiteral("1")); }
                if (format.hidden || format.filtered) xml.writeAttribute(QStringLiteral("hidden"), QStringLiteral("1"));
                while (!cell.isNull() && !cell.isDefault() && cell.column() <= area.right()) {
                    if (!cell.isEmpty()) {
                        xml.writeStartElement(QStringLiteral("c"));
                        xml.writeAttribute(QStringLiteral("r"), columnName(cell.column()) + QString::number(row));
                        const auto styleIt = styleIds.constFind(cell.style());
                        if (styleIt != styleIds.cend()) xml.writeAttribute(QStringLiteral("s"), QString::number(styleIt.value()));
                        const Value value = cell.value();
                        if (!cell.isFormula()) {
                            if (value.isBoolean()) xml.writeAttribute(QStringLiteral("t"), QStringLiteral("b"));
                            else if (!value.isNumber()) xml.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
                        } else if (value.isBoolean()) {
                            xml.writeAttribute(QStringLiteral("t"), QStringLiteral("b"));
                        }
                        if (cell.isFormula()) {
                            const QString formula = excelFormula(cell.userInput());
                            xml.writeStartElement(QStringLiteral("f"));
                            xml.writeCharacters(formula);
                            xml.writeEndElement();
                            if (value.isNumber()) {
                                xml.writeTextElement(QStringLiteral("v"), QString::number(value.asFloat(), 'g', 15));
                            } else if (value.isBoolean()) {
                                xml.writeTextElement(QStringLiteral("v"), value.asBoolean() ? QStringLiteral("1") : QStringLiteral("0"));
                            }
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
                        xml.writeEndElement();
                    }
                    cell = storage->nextInRow(cell.column(), row, CellStorage::VisitContent);
                }
                xml.writeEndElement();
            }
        }
        xml.writeEndElement();
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
            int priority = 1;
            for (const auto &entry : entries) {
                const Conditions conditions = entry.second;
                if (conditions.isEmpty()) continue;
                QStringList ranges;
                for (const QRect &rect : Region(entry.first.toRect().intersected(conditionArea), const_cast<Sheet *>(sheet)).rects()) ranges.append(rangeAddress(rect));
                if (ranges.isEmpty()) continue;
                xml.writeStartElement(QStringLiteral("conditionalFormatting"));
                xml.writeAttribute(QStringLiteral("sqref"), ranges.join(QLatin1Char(' ')));
                for (const Conditional &condition : conditions.conditionList()) {
                    const bool formulaRule = condition.cond == Validity::IsTrueFormula;
                    const QString op = conditionOperator(condition.cond);
                    if (!formulaRule && op.isEmpty()) continue;
                    xml.writeStartElement(QStringLiteral("cfRule"));
                    xml.writeAttribute(QStringLiteral("type"), formulaRule ? QStringLiteral("expression") : QStringLiteral("cellIs"));
                    if (!formulaRule) xml.writeAttribute(QStringLiteral("operator"), op);
                    xml.writeAttribute(QStringLiteral("priority"), QString::number(priority++));
                    const auto dxfIt = conditionalStyleIds.constFind(condition.styleName);
                    if (dxfIt != conditionalStyleIds.cend()) xml.writeAttribute(QStringLiteral("dxfId"), QString::number(dxfIt.value()));
                    if (condition.cond == Validity::Between) xml.writeAttribute(QStringLiteral("operator"), QStringLiteral("between"));
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
        if (hasDrawing) { xml.writeStartElement(QStringLiteral("drawing")); xml.writeAttribute(QStringLiteral("r:id"), QStringLiteral("rId1")); xml.writeEndElement(); }
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
        xml.writeEndElement();
    });
}

} // namespace

KoFilter::ConversionStatus XlsxExport::convert(const QByteArray &from, const QByteArray &to)
{
    if (from != "application/vnd.oasis.opendocument.spreadsheet"
        || to != "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") return BadMimeType;

    auto *document = qobject_cast<const DocBase *>(m_chain->inputDocument());
    if (!document || !document->map()) return StupidError;

    std::unique_ptr<KoStore> store(KoStore::createStore(m_chain->outputFile(), KoStore::Write, QByteArray(), KoStore::Zip));
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
    for (const Sheet *sheet : sheets) {
        const QRect area = sheet->usedArea(true);
        CellStorage *storage = static_cast<CellStorage *>(sheet->cellStorage());
        for (int row = area.top(); !area.isEmpty() && row <= area.bottom(); ++row) {
            Cell cell = storage->firstInRow(row, CellStorage::VisitContent);
            while (!cell.isNull() && !cell.isDefault() && cell.column() <= area.right()) {
                const Style style = cell.style();
                if (!style.isDefault() && !styleIds.contains(style)) {
                    styles.append(style);
                    styleIds.insert(style, styles.size());
                }
                cell = storage->nextInRow(cell.column(), row, CellStorage::VisitContent);
            }
        }
        const QRect cfArea = sheet->conditionsStorage()->usedArea();
        if (!cfArea.isEmpty()) {
            const auto entries = sheet->conditionsStorage()->currentData(Region(cfArea, const_cast<Sheet *>(sheet)));
            for (const auto &entry : entries) {
                for (const Conditional &condition : entry.second.conditionList()) {
                    if (condition.styleName.isEmpty() || conditionalStyleIds.contains(condition.styleName)) continue;
                    CustomStyle *namedStyle = document->map()->styleManager()->style(condition.styleName);
                    if (!namedStyle) continue;
                    conditionalStyles.append(static_cast<const Style &>(*namedStyle));
                    conditionalStyleIds.insert(condition.styleName, conditionalStyles.size() - 1);
                }
            }
        }
    }

    QList<QList<EmbeddedImage>> sheetImages;
    int nextImageIndex = 1;
    for (const Sheet *sheet : sheets) {
        QList<EmbeddedImage> images;
        for (KoShape *shape : sheet->shapes()) {
            if (!shape || shape->size().isEmpty()) continue;
            QByteArray chartPart;
            if (auto *chart = dynamic_cast<KoChart::ChartShape *>(shape)) chartPart = chartXml(chart);
            QByteArray png;
            if (chartPart.isEmpty()) {
                QImage image;
                KoShapePainter painter;
                painter.setShapes({shape});
                painter.paint(image);
                if (image.isNull()) continue;
                QBuffer buffer(&png);
                if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) continue;
            }
            images.append({shape, png, chartPart, nextImageIndex++});
        }
        sheetImages.append(images);
    }

    auto write = [&](const QString &path, const QByteArray &data) {
        if (putFile(store.get(), path, data)) return true;
        m_chain->setErrorMessage(QStringLiteral("Could not write XLSX part: %1").arg(path));
        return false;
    };

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
        xml.writeEndElement(); xml.writeEndElement();
    }))) return CreationError;

    if (!write(QStringLiteral("xl/_rels/workbook.xml.rels"), xmlBytes([&](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships"));
        for (int i = 0; i < sheets.size(); ++i) { xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(i + 1)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("worksheets/sheet%1.xml").arg(i + 1)); }
        xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId%1").arg(sheets.size() + 1)); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("styles.xml"));
        xml.writeEndElement();
    }))) return CreationError;

    for (int i = 0; i < sheets.size(); ++i) {
        if (!write(QStringLiteral("xl/worksheets/sheet%1.xml").arg(i + 1), worksheetXml(sheets[i], styleIds, conditionalStyleIds, !sheetImages[i].isEmpty()))) return CreationError;
        if (!sheetImages[i].isEmpty()) {
            if (!write(QStringLiteral("xl/worksheets/_rels/sheet%1.xml.rels").arg(i + 1), xmlBytes([i](QXmlStreamWriter &xml) {
                xml.writeStartElement(QStringLiteral("Relationships")); xml.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships")); xml.writeEmptyElement(QStringLiteral("Relationship")); xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1")); xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing")); xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("../drawings/drawing%1.xml").arg(i + 1)); xml.writeEndElement();
            }))) return CreationError;
            if (!write(QStringLiteral("xl/drawings/drawing%1.xml").arg(i + 1), drawingXml(sheetImages[i]))) return CreationError;
            if (!write(QStringLiteral("xl/drawings/_rels/drawing%1.xml.rels").arg(i + 1), drawingRelsXml(sheetImages[i]))) return CreationError;
            for (const EmbeddedImage &image : sheetImages[i]) {
                if (image.chartXml.isEmpty()) { if (!write(QStringLiteral("xl/media/image%1.png").arg(image.index), image.png)) return CreationError; }
                else if (!write(QStringLiteral("xl/charts/chart%1.xml").arg(image.index), image.chartXml)) return CreationError;
            }
        }
        Q_EMIT sigProgress((i + 1) * 100 / sheets.size());
    }
    if (!write(QStringLiteral("xl/styles.xml"), stylesXml(styles, conditionalStyles))) return CreationError;
    return OK;
}

#include "XlsxExport.moc"
