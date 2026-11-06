/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "DocxChartWriter.h"

#include <QDomDocument>
#include <QBuffer>
#include <KoStore.h>
#include <memory>
#include <QHash>
#include <QPoint>
#include <QRegularExpression>
#include <QXmlStreamWriter>

namespace
{
const QString ChartNs = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:chart:1.0");
const QString TableNs = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:table:1.0");
const QString StyleNs = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:style:1.0");
const QString OfficeNs = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:office:1.0");
const QString TextNs = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0");
const QString DrawNs = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:drawing:1.0");

QDomElement first(const QDomElement &parent, const QString &ns, const QString &name)
{
    return parent.elementsByTagNameNS(ns, name).at(0).toElement();
}

QDomElement child(const QDomElement &parent, const QString &ns, const QString &name)
{
    for (QDomElement element = parent.firstChildElement(); !element.isNull(); element = element.nextSiblingElement()) {
        if (element.namespaceURI() == ns && element.localName() == name) { return element; }
    }
    return {};
}

QString attribute(const QDomElement &element, const QString &name)
{
    return element.attributeNS(ChartNs, name);
}

void value(QXmlStreamWriter &writer, const QString &element, const QString &val)
{
    writer.writeEmptyElement(element);
    writer.writeAttribute(QStringLiteral("val"), val);
}

void title(QXmlStreamWriter &writer, const QDomElement &element)
{
    const QString text = first(element, TextNs, QStringLiteral("p")).text();
    if (text.isEmpty()) {
        return;
    }
    writer.writeStartElement(QStringLiteral("c:title"));
    writer.writeStartElement(QStringLiteral("c:tx"));
    writer.writeStartElement(QStringLiteral("c:rich"));
    writer.writeEmptyElement(QStringLiteral("a:bodyPr"));
    writer.writeEmptyElement(QStringLiteral("a:lstStyle"));
    writer.writeStartElement(QStringLiteral("a:p"));
    writer.writeStartElement(QStringLiteral("a:r"));
    writer.writeTextElement(QStringLiteral("a:t"), text);
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
}

// ODF references may quote the table name and use absolute cell addresses.
// Keep coordinates independent of those optional prefixes.
QList<QPoint> cells(const QString &range)
{
    static const QRegularExpression cell(QStringLiteral("(?<![A-Za-z0-9_])\\$?([A-Za-z]+)\\$?([1-9][0-9]*)(?=$|:)"));
    QList<QPoint> result;
    auto matches = cell.globalMatch(range);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString letters = match.captured(1).toUpper();
        if (letters.size() > 4) {
            return {};
        }
        int column = 0;
        for (QChar letter : letters) {
            column = column * 26 + letter.unicode() - 'A' + 1;
        }
        result.append(QPoint(column - 1, match.captured(2).toInt() - 1));
    }
    return result;
}

QStringList readRange(const QList<QStringList> &table, const QString &range)
{
    const auto bounds = cells(range);
    if (bounds.isEmpty() || bounds.size() > 2) {
        return {};
    }
    const QPoint start = bounds.first();
    const QPoint end = bounds.last();
    if (end.y() < start.y() || end.x() < start.x() || end.y() >= table.size()) {
        return {};
    }
    QStringList result;
    for (int row = start.y(); row <= end.y(); ++row) {
        if (end.x() >= table[row].size()) {
            return {};
        }
        for (int col = start.x(); col <= end.x(); ++col) {
            result.append(table[row][col]);
        }
    }
    return result;
}

struct DataColumn {
    QStringList values;
    bool numeric;
};

QString columnName(int index)
{
    QString name;
    while (index > 0) {
        name.prepend(QChar(QLatin1Char('A').unicode() + (index - 1) % 26));
        index = (index - 1) / 26;
    }
    return name;
}

QByteArray workbookPackage(const QList<DataColumn> &columns)
{
    QByteArray worksheet;
    QXmlStreamWriter writer(&worksheet);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("worksheet"));
    writer.writeDefaultNamespace(QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
    writer.writeStartElement(QStringLiteral("sheetData"));
    int rows = 0;
    for (const DataColumn &column : columns) { rows = qMax(rows, column.values.size()); }
    for (int row = 0; row < rows; ++row) {
        writer.writeStartElement(QStringLiteral("row"));
        writer.writeAttribute(QStringLiteral("r"), QString::number(row + 1));
        for (int col = 0; col < columns.size(); ++col) {
            if (row >= columns[col].values.size()) { continue; }
            const QString text = columns[col].values[row];
            bool number = false;
            text.toDouble(&number);
            if (columns[col].numeric && !number) { continue; }
            writer.writeStartElement(QStringLiteral("c"));
            writer.writeAttribute(QStringLiteral("r"), columnName(col + 1) + QString::number(row + 1));
            if (columns[col].numeric) {
                writer.writeTextElement(QStringLiteral("v"), text);
            } else {
                writer.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
                writer.writeStartElement(QStringLiteral("is"));
                writer.writeStartElement(QStringLiteral("t"));
                writer.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
                writer.writeCharacters(text);
                writer.writeEndElement();
                writer.writeEndElement();
            }
            writer.writeEndElement();
        }
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();

    QByteArray package;
    QBuffer buffer(&package);
    buffer.open(QIODevice::ReadWrite);
    std::unique_ptr<KoStore> store(KoStore::createStore(&buffer, KoStore::Write, QByteArray(), KoStore::Zip, false));
    if (!store || store->bad()) { return {}; }
    const QMap<QString, QByteArray> parts{
        {QStringLiteral("[Content_Types].xml"), QByteArrayLiteral(
            "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
            "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
            "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
            "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
            "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
            "</Types>")},
        {QStringLiteral("_rels/.rels"), QByteArrayLiteral(
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
            "</Relationships>")},
        {QStringLiteral("xl/workbook.xml"), QByteArrayLiteral(
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<sheets><sheet name=\"Sheet1\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>")},
        {QStringLiteral("xl/_rels/workbook.xml.rels"), QByteArrayLiteral(
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
            "</Relationships>")},
        {QStringLiteral("xl/worksheets/sheet1.xml"), worksheet},
    };
    for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
        if (!store->open(it.key()) || store->write(it.value()) != it.value().size() || !store->close()) { return {}; }
    }
    store.reset(); // Finalize the ZIP directory before returning the bytes.
    return package;
}

void cachedReference(QXmlStreamWriter &writer, const QString &element, const QStringList &data, bool numeric, QList<DataColumn> &columns)
{
    writer.writeStartElement(element);
    writer.writeStartElement(numeric ? QStringLiteral("c:numRef") : QStringLiteral("c:strRef"));
    const QString column = columnName(columns.size() + 1);
    columns.append({data, numeric});
    const QString formula = QStringLiteral("Sheet1!$%1$1:$%1$%2").arg(column).arg(qMax(1, data.size()));
    writer.writeTextElement(QStringLiteral("c:f"), formula);
    writer.writeStartElement(numeric ? QStringLiteral("c:numCache") : QStringLiteral("c:strCache"));
    if (numeric) {
        writer.writeTextElement(QStringLiteral("c:formatCode"), QStringLiteral("General"));
    }
    value(writer, QStringLiteral("c:ptCount"), QString::number(data.size()));
    for (int i = 0; i < data.size(); ++i) {
        bool number = true;
        if (numeric) {
            data[i].toDouble(&number);
        }
        // Keep an entry for a missing point so readers that reconstruct the
        // worksheet from caches retain the range length and the blank cell.

        writer.writeStartElement(QStringLiteral("c:pt"));
        writer.writeAttribute(QStringLiteral("idx"), QString::number(i));
        writer.writeTextElement(QStringLiteral("c:v"), number ? data[i] : QString());
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
}
}

QByteArray writeDocxChart(const QByteArray &odfChart, const QString &workbookRelationshipId, QByteArray *workbook)
{
    QDomDocument document;
    if (!document.setContent(odfChart, QDomDocument::ParseOption::UseNamespaceProcessing)) {
        return {};
    }
    const QDomElement chart = first(document.documentElement(), ChartNs, QStringLiteral("chart"));
    if (chart.isNull()) {
        return {};
    }
    const QString type = attribute(chart, QStringLiteral("class")).section(QLatin1Char(':'), -1);
    const QHash<QString, QString> types{
        {QStringLiteral("bar"), QStringLiteral("c:barChart")},
        {QStringLiteral("line"), QStringLiteral("c:lineChart")},
        {QStringLiteral("area"), QStringLiteral("c:areaChart")},
        {QStringLiteral("circle"), QStringLiteral("c:pieChart")},
        {QStringLiteral("ring"), QStringLiteral("c:doughnutChart")},
        {QStringLiteral("scatter"), QStringLiteral("c:scatterChart")},
        {QStringLiteral("bubble"), QStringLiteral("c:bubbleChart")},
        {QStringLiteral("radar"), QStringLiteral("c:radarChart")},
        {QStringLiteral("filled-radar"), QStringLiteral("c:radarChart")},
        {QStringLiteral("stock"), QStringLiteral("c:stockChart")},
    };
    if (!types.contains(type)) {
        return {};
    }
    const QDomElement dataTable = first(chart, TableNs, QStringLiteral("table"));
    QList<QStringList> table;
    const auto rows = dataTable.elementsByTagNameNS(TableNs, QStringLiteral("table-row"));
    for (int i = 0; i < rows.size(); ++i) {
        const QDomElement row = rows.at(i).toElement();
        QStringList values;
        for (QDomElement cell = row.firstChildElement(); !cell.isNull(); cell = cell.nextSiblingElement()) {
            if (cell.namespaceURI() != TableNs || (cell.localName() != QLatin1String("table-cell") && cell.localName() != QLatin1String("covered-table-cell"))) {
                continue;
            }
            const int repeat = qMax(1, cell.attributeNS(TableNs, QStringLiteral("number-columns-repeated"), QStringLiteral("1")).toInt());
            if (repeat > 16384 || values.size() + repeat > 16384) {
                return {};
            }
            QString val = cell.attributeNS(OfficeNs, QStringLiteral("value"));
            if (val.isEmpty()) {
                val = cell.attributeNS(OfficeNs, QStringLiteral("string-value"), first(cell, TextNs, QStringLiteral("p")).text());
            }
            for (int c = 0; c < repeat; ++c) {
                values.append(val);
            }
        }
        const int repeat = qMax(1, row.attributeNS(TableNs, QStringLiteral("number-rows-repeated"), QStringLiteral("1")).toInt());
        if (repeat > 65536 || table.size() + repeat > 65536) {
            return {};
        }
        for (int r = 0; r < repeat; ++r) {
            table.append(values);
        }
    }
    const QDomElement plot = first(chart, ChartNs, QStringLiteral("plot-area"));
    const auto series = plot.elementsByTagNameNS(ChartNs, QStringLiteral("series"));
    if (table.isEmpty() || series.isEmpty()) {
        return {};
    }
    QHash<QString, QDomElement> styles;
    const auto styleNodes = document.elementsByTagNameNS(StyleNs, QStringLiteral("style"));
    for (int i = 0; i < styleNodes.size(); ++i) {
        QDomElement style = styleNodes.at(i).toElement();
        styles.insert(style.attributeNS(StyleNs, QStringLiteral("name")), style);
    }
    const auto properties = [&styles](const QDomElement &element) {
        return first(styles.value(attribute(element, QStringLiteral("style-name"))), StyleNs, QStringLiteral("chart-properties"));
    };
    const QDomElement plotProperties = properties(plot);
    const bool pie = type == QLatin1String("circle") || type == QLatin1String("ring");
    const bool scatter = type == QLatin1String("scatter") || type == QLatin1String("bubble");
    const QString categoryRange = first(plot, ChartNs, QStringLiteral("categories")).attributeNS(TableNs, QStringLiteral("cell-range-address"));
    const QStringList categories = readRange(table, categoryRange);

    QByteArray result;
    QXmlStreamWriter writer(&result);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("c:chartSpace"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart"), QStringLiteral("c"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("a"));
    writer.writeNamespace(QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships"), QStringLiteral("r"));
    writer.writeStartElement(QStringLiteral("c:chart"));
    const QDomElement chartTitle = child(chart, ChartNs, QStringLiteral("title"));
    title(writer, chartTitle);
    value(writer, QStringLiteral("c:autoTitleDeleted"), chartTitle.isNull() ? QStringLiteral("1") : QStringLiteral("0"));
    writer.writeStartElement(QStringLiteral("c:plotArea"));
    writer.writeEmptyElement(QStringLiteral("c:layout"));
    writer.writeStartElement(types.value(type));
    if (type == QLatin1String("bar")) {
        value(writer, QStringLiteral("c:barDir"), attribute(plotProperties, QStringLiteral("vertical")) == QLatin1String("true") ? QStringLiteral("bar") : QStringLiteral("col"));
    }
    if (type == QLatin1String("bar") || type == QLatin1String("line") || type == QLatin1String("area")) {
        QString grouping = type == QLatin1String("bar") ? QStringLiteral("clustered") : QStringLiteral("standard");
        if (attribute(plotProperties, QStringLiteral("percentage")) == QLatin1String("true")) {
            grouping = QStringLiteral("percentStacked");
        } else if (attribute(plotProperties, QStringLiteral("stacked")) == QLatin1String("true")) {
            grouping = QStringLiteral("stacked");
        }
        value(writer, QStringLiteral("c:grouping"), grouping);
    }
    if (type == QLatin1String("scatter")) {
        value(writer, QStringLiteral("c:scatterStyle"), QStringLiteral("lineMarker"));
    } else if (type.contains(QLatin1String("radar"))) {
        value(writer, QStringLiteral("c:radarStyle"), type == QLatin1String("filled-radar") ? QStringLiteral("filled") : QStringLiteral("standard"));
    }
    if (pie || type == QLatin1String("bar") || type == QLatin1String("line") || type == QLatin1String("area")) {
        value(writer, QStringLiteral("c:varyColors"), pie ? QStringLiteral("1") : QStringLiteral("0"));
    }
    QList<DataColumn> columns;
    QStringList sharedX;
    QStringList sharedY;
    for (int i = 0; i < series.size(); ++i) {
        // DrawingML and ODF paint area series in opposite orders.
        const int sourceIndex = type == QLatin1String("area") ? series.size() - i - 1 : i;
        const QDomElement source = series.at(sourceIndex).toElement();
        // Mixed chart types need multiple plot groups and independent axes.
        const QString seriesType = attribute(source, QStringLiteral("class")).section(QLatin1Char(':'), -1);
        if (!seriesType.isEmpty() && seriesType != type && !(type == QLatin1String("ring") && seriesType == QLatin1String("circle"))) {
            return {};
        }
        const QStringList values = readRange(table, attribute(source, QStringLiteral("values-cell-range-address")));
        if (values.isEmpty()) {
            return {};
        }
        writer.writeStartElement(QStringLiteral("c:ser"));
        value(writer, QStringLiteral("c:idx"), QString::number(i));
        value(writer, QStringLiteral("c:order"), QString::number(i));
        const QStringList label = readRange(table, attribute(source, QStringLiteral("label-cell-address")));
        if (!label.isEmpty()) {
            cachedReference(writer, QStringLiteral("c:tx"), {label.first()}, false, columns);
        }
        const QDomElement graphic = first(styles.value(attribute(source, QStringLiteral("style-name"))), StyleNs, QStringLiteral("graphic-properties"));
        const QString color = graphic.attributeNS(DrawNs, QStringLiteral("fill-color"));
        if (QRegularExpression(QStringLiteral("^#[0-9a-fA-F]{6}$")).match(color).hasMatch()) {
            writer.writeStartElement(QStringLiteral("c:spPr"));
            writer.writeStartElement(QStringLiteral("a:solidFill"));
            value(writer, QStringLiteral("a:srgbClr"), color.mid(1));
            writer.writeEndElement();
            writer.writeEndElement();
        }
        if (scatter) {
            const bool bubble = type == QLatin1String("bubble");
            const auto domains = source.elementsByTagNameNS(ChartNs, QStringLiteral("domain"));
            // In ODF, bubble values describe sizes; domains are Y then X.
            // Scatter values describe Y and its single domain describes X.
            if (!domains.isEmpty()) {
                const QStringList data = readRange(table, domains.at(0).toElement().attributeNS(TableNs, QStringLiteral("cell-range-address")));
                if (bubble) { sharedY = data; } else { sharedX = data; }
            }
            if (bubble && domains.size() > 1) {
                sharedX = readRange(table, domains.at(1).toElement().attributeNS(TableNs, QStringLiteral("cell-range-address")));
            }
            QStringList x = sharedX.isEmpty() ? categories : sharedX;
            bool numericCategories = !x.isEmpty();
            for (const QString &entry : x) {
                bool number = false;
                entry.toDouble(&number);
                numericCategories = numericCategories && number;
            }
            if (!numericCategories) {
                x.clear();
                for (int j = 0; j < values.size(); ++j) { x.append(QString::number(j + 1)); }
            }
            cachedReference(writer, QStringLiteral("c:xVal"), x, true, columns);
            cachedReference(writer, QStringLiteral("c:yVal"), bubble && !sharedY.isEmpty() ? sharedY : values, true, columns);
            if (bubble) {
                cachedReference(writer, QStringLiteral("c:bubbleSize"), values, true, columns);
            }
        } else {
            if (!categories.isEmpty()) {
                cachedReference(writer, QStringLiteral("c:cat"), categories, false, columns);
            }
            cachedReference(writer, QStringLiteral("c:val"), values, true, columns);
        }
        writer.writeEndElement(); // ser
    }
    if (type == QLatin1String("ring")) {
        value(writer, QStringLiteral("c:holeSize"), QStringLiteral("50"));
    }
    if (!pie) {
        value(writer, QStringLiteral("c:axId"), QStringLiteral("100"));
        value(writer, QStringLiteral("c:axId"), QStringLiteral("200"));
    }
    writer.writeEndElement(); // chart type
    if (!pie) {
        for (int i = 0; i < 2; ++i) {
            const bool categorical = i == 0 && !scatter;
            writer.writeStartElement(categorical ? QStringLiteral("c:catAx") : QStringLiteral("c:valAx"));
            value(writer, QStringLiteral("c:axId"), i == 0 ? QStringLiteral("100") : QStringLiteral("200"));
            writer.writeStartElement(QStringLiteral("c:scaling"));
            value(writer, QStringLiteral("c:orientation"), QStringLiteral("minMax"));
            writer.writeEndElement();
            value(writer, QStringLiteral("c:axPos"), i == 0 ? QStringLiteral("b") : QStringLiteral("l"));
            const auto axes = plot.elementsByTagNameNS(ChartNs, QStringLiteral("axis"));
            for (int axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
                const QDomElement axis = axes.at(axisIndex).toElement();
                if (attribute(axis, QStringLiteral("dimension")) == (i == 0 ? QLatin1String("x") : QLatin1String("y"))) {
                    title(writer, child(axis, ChartNs, QStringLiteral("title")));
                    break;
                }
            }
            value(writer, QStringLiteral("c:crossAx"), i == 0 ? QStringLiteral("200") : QStringLiteral("100"));
            value(writer, QStringLiteral("c:crosses"), QStringLiteral("autoZero"));
            writer.writeEndElement();
        }
    }
    writer.writeEndElement(); // plotArea
    if (!first(chart, ChartNs, QStringLiteral("legend")).isNull()) {
        writer.writeStartElement(QStringLiteral("c:legend"));
        value(writer, QStringLiteral("c:legendPos"), QStringLiteral("r"));
        writer.writeEndElement();
    }
    value(writer, QStringLiteral("c:plotVisOnly"), QStringLiteral("1"));
    writer.writeEndElement(); // chart
    writer.writeStartElement(QStringLiteral("c:externalData"));
    writer.writeAttribute(QStringLiteral("r:id"), workbookRelationshipId);
    value(writer, QStringLiteral("c:autoUpdate"), QStringLiteral("0"));
    writer.writeEndElement();
    writer.writeEndElement(); // chartSpace
    writer.writeEndDocument();
    *workbook = workbookPackage(columns);
    return workbook->isEmpty() ? QByteArray() : result;
}
