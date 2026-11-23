/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "CellVisuals.h"
#include "Cell.h"
#include "Map.h"
#include "Sheet.h"
#include "RowFormatStorage.h"
#include "ColFormatStorage.h"
#include "engine/Formula.h"
#include "engine/Region.h"
#include "engine/Value.h"
#include <KoXmlReader.h>
#include <KoXmlWriter.h>
#include <QPainter>
#include <QPainterPath>
#include <QXmlStreamWriter>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace Calligra::Sheets;
namespace {
const QString visualNS = QStringLiteral("http://www.calligra.org/2002/sheets");
bool flag(const QMap<QString, QString> &options, const QString &name, bool fallback = false)
{
    if (!options.contains(name)) return fallback;
    return options.value(name) == QLatin1String("1") || options.value(name) == QLatin1String("true");
}
Region region(const QString &name, Sheet *sheet)
{
    // OOXML sqref lists are space separated, whereas the region parser uses ';'.
    QString ranges = name;
    if (!ranges.contains(QLatin1Char('!')) && !ranges.contains(QLatin1Char('\'')))
        ranges.replace(QLatin1Char(' '), QLatin1Char(';'));
    return sheet->map()->regionFromName(ranges, sheet);
}
QVector<double> rangeNumbers(const Region &r)
{
    QVector<double> result;
    for (auto it = r.constBegin(); it != r.constEnd(); ++it) {
        if (!(*it)->sheet()) continue;
        const QRect rect = (*it)->rect();
        for (int row = rect.top(); row <= rect.bottom(); ++row)
            for (int col = rect.left(); col <= rect.right(); ++col) {
                const auto value = CellBase((*it)->sheet(), col, row).value();
                if (value.isNumber() && std::isfinite(double(value.asFloat()))) result.append(value.asFloat());
            }
    }
    std::sort(result.begin(), result.end());
    return result;
}
double threshold(const CellVisuals::Threshold &t, const QVector<double> &values, const Cell &cell)
{
    if (values.isEmpty()) return std::numeric_limits<double>::quiet_NaN();
    if (t.type == QLatin1String("min")) return values.first();
    if (t.type == QLatin1String("max")) return values.last();
    if (t.type == QLatin1String("formula")) {
        Formula formula(cell.sheet(), cell);
        formula.setExpression(t.value.startsWith(QLatin1Char('=')) ? t.value : QLatin1Char('=') + t.value);
        const Value value = formula.eval();
        return value.isNumber() ? double(value.asFloat()) : std::numeric_limits<double>::quiet_NaN();
    }
    bool ok = false;
    const double value = t.value.toDouble(&ok);
    if (!ok) return std::numeric_limits<double>::quiet_NaN();
    if (t.type == QLatin1String("percent")) return values.first() + (values.last() - values.first()) * value / 100.0;
    if (t.type == QLatin1String("percentile")) {
        const double index = std::clamp(value / 100.0, 0.0, 1.0) * (values.size() - 1);
        const int lower = int(std::floor(index)), upper = int(std::ceil(index));
        return values[lower] + (values[upper] - values[lower]) * (index - lower);
    }
    return value;
}
QString rgb(const QColor &color) { return QStringLiteral("FF") + color.name(QColor::HexRgb).mid(1).toUpper(); }
void writeThreshold(QXmlStreamWriter &xml, const CellVisuals::Threshold &t)
{
    xml.writeEmptyElement(QStringLiteral("cfvo"));
    xml.writeAttribute(QStringLiteral("type"), t.type);
    if (!t.value.isEmpty()) xml.writeAttribute(QStringLiteral("val"), t.value);
    if (!t.inclusive) xml.writeAttribute(QStringLiteral("gte"), QStringLiteral("0"));
}
}

QColor CellVisuals::background(const Cell &cell) const
{
    if (rules.isEmpty() || !cell.value().isNumber()) return {};
    const Rule *selected = nullptr;
    for (const Rule &rule : rules)
        if (rule.type == QLatin1String("colorScale") && rule.colors.size() == rule.thresholds.size() && rule.colors.size() >= 2
            && region(rule.range, cell.fullSheet()).contains(cell.cellPosition(), cell.sheet())
            && (!selected || rule.priority < selected->priority)) selected = &rule;
    if (!selected) return {};
    const auto values = rangeNumbers(region(selected->range, cell.fullSheet()));
    QVector<double> limits;
    for (const auto &t : selected->thresholds) {
        const double limit = threshold(t, values, cell);
        if (!std::isfinite(limit)) return {};
        limits.append(limit);
    }
    const double value = cell.value().asFloat();
    if (value <= limits.first()) return selected->colors.first();
    for (int i = 1; i < limits.size(); ++i) {
        if (value <= limits[i]) {
            const double fraction = limits[i] > limits[i - 1] ? (value - limits[i - 1]) / (limits[i] - limits[i - 1]) : 0.0;
            const auto a = selected->colors[i - 1], b = selected->colors[i];
            return QColor(qRound(a.red() + (b.red() - a.red()) * fraction), qRound(a.green() + (b.green() - a.green()) * fraction),
                          qRound(a.blue() + (b.blue() - a.blue()) * fraction));
        }
    }
    return selected->colors.last();
}

int CellVisuals::trafficLight(const Cell &cell, bool *showValue) const
{
    if (showValue) *showValue = true;
    if (rules.isEmpty() || !cell.value().isNumber()) return -1;
    const Rule *selected = nullptr;
    for (const Rule &rule : rules)
        if (rule.type == QLatin1String("iconSet") && rule.iconSet.startsWith(QLatin1String("3TrafficLights")) && rule.thresholds.size() == 3
            && region(rule.range, cell.fullSheet()).contains(cell.cellPosition(), cell.sheet())
            && (!selected || rule.priority < selected->priority)) selected = &rule;
    if (!selected) return -1;
    if (showValue) *showValue = selected->showValue;
    const auto values = rangeNumbers(region(selected->range, cell.fullSheet()));
    int index = 0;
    for (int i = 1; i < 3; ++i) {
        const double limit = threshold(selected->thresholds[i], values, cell);
        if (selected->thresholds[i].inclusive ? cell.value().asFloat() >= limit : cell.value().asFloat() > limit) index = i;
    }
    return selected->reverse ? 2 - index : index;
}

QVector<double> CellVisuals::sparklineValues(const Sparkline &sparkline, Sheet *sheet) const
{
    QVector<double> result;
    const Region source = region(sparkline.source, sheet);
    const double gap = std::numeric_limits<double>::quiet_NaN();
    for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
        auto *sourceSheet = dynamic_cast<Sheet *>((*it)->sheet());
        if (!sourceSheet) continue;
        const QRect rect = (*it)->rect();
        for (int row = rect.top(); row <= rect.bottom(); ++row)
            for (int col = rect.left(); col <= rect.right(); ++col) {
                if (!flag(sparkline.options, QStringLiteral("displayHidden"))
                    && (sourceSheet->rowFormats()->isHiddenOrFiltered(row) || sourceSheet->columnFormats()->isHiddenOrFiltered(col))) continue;
                const Value value = Cell(sourceSheet, col, row).value();
                result.append(value.isNumber() ? double(value.asFloat()) : value.isEmpty() && sparkline.options.value(QStringLiteral("displayEmptyCellsAs")) == QLatin1String("zero") ? 0.0 : gap);
            }
    }
    return result;
}

void CellVisuals::paint(QPainter &painter, const QRectF &rect, const Cell &cell) const
{
    if (isEmpty()) return;
    const int light = trafficLight(cell);
    if (light >= 0) {
        const QColor colors[] = {QColor(248, 105, 107), QColor(255, 211, 91), QColor(99, 190, 123)};
        const qreal diameter = qMin<qreal>(10, rect.height() - 2);
        if (diameter > 0) {
            painter.save(); painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(QPen(colors[light].darker(125), 0.5)); painter.setBrush(colors[light]);
            const qreal x = cell.fullSheet()->layoutDirection() == Qt::RightToLeft ? rect.right() - diameter - 1 : rect.left() + 1;
            painter.drawEllipse(QRectF(x, rect.center().y() - diameter / 2, diameter, diameter)); painter.restore();
        }
    }
    for (const Sparkline &sparkline : sparklines) {
        if (!region(sparkline.target, cell.fullSheet()).contains(cell.cellPosition(), cell.sheet())) continue;
        auto values = sparklineValues(sparkline, cell.fullSheet());
        if (values.isEmpty()) continue;
        if (flag(sparkline.options, QStringLiteral("rightToLeft"))) std::reverse(values.begin(), values.end());
        double minimum = std::numeric_limits<double>::infinity(), maximum = -minimum;
        for (double v : values) if (std::isfinite(v)) { minimum = qMin(minimum, v); maximum = qMax(maximum, v); }
        if (!std::isfinite(minimum)) continue;
        const QString type = sparkline.options.value(QStringLiteral("type"), QStringLiteral("line"));
        const bool column = type == QLatin1String("column") || type == QLatin1String("stacked");
        if (column) { minimum = qMin(0.0, minimum); maximum = qMax(0.0, maximum); }
        if (type == QLatin1String("stacked")) { minimum = -1; maximum = 1; }
        if (sparkline.options.value(QStringLiteral("minAxisType")) == QLatin1String("custom")) minimum = sparkline.options.value(QStringLiteral("manualMin")).toDouble();
        if (sparkline.options.value(QStringLiteral("maxAxisType")) == QLatin1String("custom")) maximum = sparkline.options.value(QStringLiteral("manualMax")).toDouble();
        if (maximum <= minimum) { minimum -= 0.5; maximum += 0.5; }
        const QRectF plot = rect.adjusted(2, 2, -2, -2);
        if (plot.width() <= 0 || plot.height() <= 0) continue;
        const auto y = [&](double v) { return plot.bottom() - (v - minimum) / (maximum - minimum) * plot.height(); };
        const QColor seriesColor = sparkline.colors.value(QStringLiteral("colorSeries"), QColor(79,129,189));
        const auto pointColor = [&](int i, double v) {
            QString key = QStringLiteral("colorSeries");
            if (flag(sparkline.options, QStringLiteral("negative")) && v < 0) key = QStringLiteral("colorNegative");
            if (flag(sparkline.options, QStringLiteral("first")) && i == 0) key = QStringLiteral("colorFirst");
            if (flag(sparkline.options, QStringLiteral("last")) && i == values.size() - 1) key = QStringLiteral("colorLast");
            if (flag(sparkline.options, QStringLiteral("high")) && v == maximum) key = QStringLiteral("colorHigh");
            if (flag(sparkline.options, QStringLiteral("low")) && v == minimum) key = QStringLiteral("colorLow");
            return sparkline.colors.value(key, seriesColor);
        };
        painter.save(); painter.setClipRect(rect); painter.setRenderHint(QPainter::Antialiasing);
        if (flag(sparkline.options, QStringLiteral("displayXAxis")) && minimum < 0 && maximum > 0) {
            painter.setPen(QPen(sparkline.colors.value(QStringLiteral("colorAxis"), Qt::black), 0.5));
            painter.drawLine(QPointF(plot.left(), y(0)), QPointF(plot.right(), y(0)));
        }
        QPainterPath path; bool connected = false;
        for (int i = 0; i < values.size(); ++i) {
            double v = values[i];
            if (!std::isfinite(v)) {
                if (sparkline.options.value(QStringLiteral("displayEmptyCellsAs")) != QLatin1String("span")) connected = false;
                continue;
            }
            if (type == QLatin1String("stacked")) v = v > 0 ? 1 : v < 0 ? -1 : 0;
            const qreal x = plot.left() + (column ? (i + 0.5) / values.size() : values.size() > 1 ? double(i) / (values.size() - 1) : 0.5) * plot.width();
            if (column) {
                painter.setPen(Qt::NoPen); painter.setBrush(pointColor(i, v));
                const qreal width = plot.width() / values.size() * 0.75;
                painter.drawRect(QRectF(x - width / 2, qMin(y(v), y(0)), width, qAbs(y(v) - y(0))));
            } else {
                if (connected) path.lineTo(x, y(v)); else path.moveTo(x, y(v));
                connected = true;
                if (flag(sparkline.options, QStringLiteral("markers")) || pointColor(i, v) != seriesColor) {
                    painter.setPen(Qt::NoPen); painter.setBrush(pointColor(i, v)); painter.drawEllipse(QPointF(x, y(v)), 1.5, 1.5);
                }
            }
        }
        if (!column) {
            const qreal width = sparkline.options.value(QStringLiteral("lineWeight"), QStringLiteral("0.75")).toDouble();
            painter.setPen(QPen(seriesColor, width)); painter.setBrush(Qt::NoBrush); painter.drawPath(path);
        }
        painter.restore();
    }
}

QRect CellVisuals::usedArea(Sheet *sheet) const
{
    QRect result;
    for (const auto &sparkline : sparklines) for (const QRect &r : region(sparkline.target, sheet).rects()) result |= r;
    return result;
}

void CellVisuals::saveOdf(KoXmlWriter &writer) const
{
    if (isEmpty()) return;
    writer.startElement("calligra:cell-visuals");
    writer.addAttribute("xmlns:calligra", visualNS);
    for (const Rule &rule : rules) {
        writer.startElement("calligra:rule");
        writer.addAttribute("range", rule.range); writer.addAttribute("type", rule.type);
        writer.addAttribute("priority", rule.priority); writer.addAttribute("icon-set", rule.iconSet);
        writer.addAttribute("reverse", rule.reverse); writer.addAttribute("show-value", rule.showValue);
        for (const auto &t : rule.thresholds) {
            writer.startElement("calligra:threshold"); writer.addAttribute("type", t.type);
            writer.addAttribute("value", t.value); writer.addAttribute("inclusive", t.inclusive); writer.endElement();
        }
        for (const QColor &color : rule.colors) {
            writer.startElement("calligra:color"); writer.addAttribute("rgb", color.name(QColor::HexArgb)); writer.endElement();
        }
        writer.endElement();
    }
    for (const Sparkline &sparkline : sparklines) {
        writer.startElement("calligra:sparkline");
        writer.addAttribute("source", sparkline.source); writer.addAttribute("target", sparkline.target);
        for (auto it = sparkline.options.cbegin(); it != sparkline.options.cend(); ++it) {
            writer.startElement("calligra:option"); writer.addAttribute("name", it.key()); writer.addAttribute("value", it.value()); writer.endElement();
        }
        for (auto it = sparkline.colors.cbegin(); it != sparkline.colors.cend(); ++it) {
            writer.startElement("calligra:color"); writer.addAttribute("name", it.key()); writer.addAttribute("rgb", it.value().name(QColor::HexArgb)); writer.endElement();
        }
        writer.endElement();
    }
    writer.endElement();
}

void CellVisuals::loadOdf(const KoXmlElement &element)
{
    rules.clear(); sparklines.clear();
    KoXmlElement child;
    forEachElement(child, element) {
        if (child.namespaceURI() != visualNS) continue;
        if (child.localName() == QLatin1String("rule")) {
            Rule rule;
            rule.range = child.attribute(QStringLiteral("range")); rule.type = child.attribute(QStringLiteral("type"));
            rule.priority = child.attribute(QStringLiteral("priority"), QStringLiteral("1")).toInt();
            rule.iconSet = child.attribute(QStringLiteral("icon-set"), QStringLiteral("3TrafficLights1"));
            rule.reverse = child.attribute(QStringLiteral("reverse")) == QLatin1String("true");
            rule.showValue = child.attribute(QStringLiteral("show-value"), QStringLiteral("true")) != QLatin1String("false");
            KoXmlElement item;
            forEachElement(item, child) {
                if (item.localName() == QLatin1String("threshold")) {
                    Threshold t; t.type = item.attribute(QStringLiteral("type")); t.value = item.attribute(QStringLiteral("value"));
                    t.inclusive = item.attribute(QStringLiteral("inclusive"), QStringLiteral("true")) != QLatin1String("false"); rule.thresholds.append(t);
                } else if (item.localName() == QLatin1String("color")) rule.colors.append(QColor(item.attribute(QStringLiteral("rgb"))));
            }
            rules.append(rule);
        } else if (child.localName() == QLatin1String("sparkline")) {
            Sparkline sparkline;
            sparkline.source = child.attribute(QStringLiteral("source")); sparkline.target = child.attribute(QStringLiteral("target"));
            KoXmlElement item;
            forEachElement(item, child) {
                if (item.localName() == QLatin1String("option")) sparkline.options.insert(item.attribute(QStringLiteral("name")), item.attribute(QStringLiteral("value")));
                else if (item.localName() == QLatin1String("color")) sparkline.colors.insert(item.attribute(QStringLiteral("name")), QColor(item.attribute(QStringLiteral("rgb"))));
            }
            sparklines.append(sparkline);
        }
    }
}

void CellVisuals::saveXlsxRules(QXmlStreamWriter &xml) const
{
    for (const auto &rule : rules) {
        xml.writeStartElement(QStringLiteral("conditionalFormatting")); xml.writeAttribute(QStringLiteral("sqref"), rule.range);
        xml.writeStartElement(QStringLiteral("cfRule")); xml.writeAttribute(QStringLiteral("type"), rule.type);
        xml.writeAttribute(QStringLiteral("priority"), QString::number(rule.priority));
        xml.writeStartElement(rule.type);
        if (rule.type == QLatin1String("iconSet")) {
            xml.writeAttribute(QStringLiteral("iconSet"), rule.iconSet);
            xml.writeAttribute(QStringLiteral("reverse"), rule.reverse ? QStringLiteral("1") : QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("showValue"), rule.showValue ? QStringLiteral("1") : QStringLiteral("0"));
        }
        for (const auto &t : rule.thresholds) writeThreshold(xml, t);
        for (const auto &color : rule.colors) { xml.writeEmptyElement(QStringLiteral("color")); xml.writeAttribute(QStringLiteral("rgb"), rgb(color)); }
        xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
    }
}

void CellVisuals::saveXlsxSparklines(QXmlStreamWriter &xml) const
{
    if (sparklines.isEmpty()) return;
    const QString x14 = QStringLiteral("http://schemas.microsoft.com/office/spreadsheetml/2009/9/main");
    const QString xm = QStringLiteral("http://schemas.microsoft.com/office/excel/2006/main");
    xml.writeStartElement(QStringLiteral("extLst")); xml.writeStartElement(QStringLiteral("ext"));
    xml.writeAttribute(QStringLiteral("uri"), QStringLiteral("{05C60535-1F16-4fd2-B633-F4F36F0B64E0}"));
    xml.writeNamespace(x14, QStringLiteral("x14")); xml.writeNamespace(xm, QStringLiteral("xm"));
    xml.writeStartElement(x14, QStringLiteral("sparklineGroups"));
    for (const auto &sparkline : sparklines) {
        xml.writeStartElement(x14, QStringLiteral("sparklineGroup"));
        for (auto it = sparkline.options.cbegin(); it != sparkline.options.cend(); ++it) xml.writeAttribute(it.key(), it.value());
        for (auto it = sparkline.colors.cbegin(); it != sparkline.colors.cend(); ++it) {
            xml.writeEmptyElement(x14, it.key()); xml.writeAttribute(QStringLiteral("rgb"), rgb(it.value()));
        }
        xml.writeStartElement(x14, QStringLiteral("sparklines")); xml.writeStartElement(x14, QStringLiteral("sparkline"));
        xml.writeTextElement(xm, QStringLiteral("f"), sparkline.source); xml.writeTextElement(xm, QStringLiteral("sqref"), sparkline.target);
        xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
    }
    xml.writeEndElement(); xml.writeEndElement(); xml.writeEndElement();
}
