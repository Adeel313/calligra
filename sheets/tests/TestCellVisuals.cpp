/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include <core/Cell.h>
#include <core/CellStorage.h>
#include <core/CellVisuals.h>
#include <core/Map.h>
#include <core/Sheet.h>
#include <engine/Value.h>
#include <ui/SheetView.h>
#include <KoXmlWriter.h>
#include <KoZoomHandler.h>
#include <KLocalizedString>
#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QXmlStreamWriter>
#include <cmath>

using namespace Calligra::Sheets;
static void check(bool value, const char *message) { if (!value) qFatal("%s", message); }
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("calligrasheets");
    Map map;
    auto *sheet = dynamic_cast<Sheet *>(map.addNewSheet());
    sheet->setSheetName(QStringLiteral("Dashboard"));
    for (int row = 1; row <= 3; ++row) sheet->fullCellStorage()->setValue(1, row, Value((row - 1) * 50));
    auto *visuals = sheet->cellVisuals();
    CellVisuals::Rule scale;
    scale.type = QStringLiteral("colorScale"); scale.range = QStringLiteral("A1:A3");
    scale.thresholds = {{QStringLiteral("min"), {}}, {QStringLiteral("percentile"), QStringLiteral("50")}, {QStringLiteral("max"), {}}};
    scale.colors = {QColor(248,105,107), QColor(255,235,132), QColor(99,190,123)};
    visuals->rules.append(scale);
    check(visuals->background(Cell(sheet,1,1)) == scale.colors[0], "minimum scale color");
    check(visuals->background(Cell(sheet,1,2)) == scale.colors[1], "median scale color");
    check(visuals->background(Cell(sheet,1,3)) == scale.colors[2], "maximum scale color");
    sheet->fullCellStorage()->setValue(1, 2, Value(25));
    check(visuals->background(Cell(sheet,1,2)) == scale.colors[1], "percentile updates after editing");
    visuals->rules[0].thresholds[1] = {QStringLiteral("num"), QStringLiteral("50")};
    check(visuals->background(Cell(sheet,1,2)) == QColor(252,170,120), "RGB interpolation");
    CellVisuals::Rule icons;
    icons.type = QStringLiteral("iconSet"); icons.range = QStringLiteral("A1:A3");
    icons.thresholds = {{QStringLiteral("percent"), QStringLiteral("0")}, {QStringLiteral("percent"), QStringLiteral("33")}, {QStringLiteral("percent"), QStringLiteral("67")}};
    visuals->rules.append(icons);
    check(visuals->trafficLight(Cell(sheet,1,1)) == 0, "red traffic light");
    check(visuals->trafficLight(Cell(sheet,1,3)) == 2, "green traffic light");
    sheet->fullCellStorage()->setValue(1, 2, Value(50));
    check(visuals->trafficLight(Cell(sheet,1,2)) == 1, "yellow traffic light");
    visuals->rules[1].reverse = true;
    check(visuals->trafficLight(Cell(sheet,1,1)) == 2, "reverse traffic lights");
    visuals->rules[1].reverse = false;
    visuals->rules[1].showValue = false;
    bool show = true; visuals->trafficLight(Cell(sheet,1,1), &show);
    check(!show, "icon-only rule");
    visuals->rules[1].thresholds[1] = {QStringLiteral("num"), QStringLiteral("50"), false};
    check(visuals->trafficLight(Cell(sheet,1,2)) == 0, "exclusive icon boundary");
    CellVisuals::Sparkline spark;
    spark.source = QStringLiteral("Dashboard!A1:A3"); spark.target = QStringLiteral("B5");
    spark.options[QStringLiteral("type")] = QStringLiteral("column");
    spark.colors[QStringLiteral("colorSeries")] = QColor(79,129,189);
    visuals->sparklines.append(spark);
    check(visuals->sparklineValues(spark, sheet) == QVector<double>({0,50,100}), "sparkline references current data");
    sheet->fullCellStorage()->setValue(1, 3, Value(-25));
    check(visuals->sparklineValues(spark, sheet).last() == -25, "sparkline source edits");
    check(sheet->usedArea(true).bottom() >= 5, "empty sparkline target contributes to used area");
    QImage picture(160,30,QImage::Format_ARGB32); picture.fill(Qt::white);
    { QPainter painter(&picture); visuals->paint(painter, QRectF(0,0,160,30), Cell(sheet,2,5)); }
    int colored = 0; for(int y=0;y<picture.height();++y) for(int x=0;x<picture.width();++x) if(picture.pixelColor(x,y) != Qt::white) ++colored;
    check(colored > 50, "sparkline bars render");
    QBuffer buffer; buffer.open(QIODevice::WriteOnly); { KoXmlWriter writer(&buffer); visuals->saveOdf(writer); }
    KoXmlDocument document; check(document.setContent(buffer.data(), true), "visual ODF is well-formed");
    CellVisuals restored; restored.loadOdf(document.documentElement());
    check(restored.rules.size()==2 && restored.sparklines.size()==1, "visual ODF round trip");
    check(restored.rules[1].thresholds[1].inclusive == false && !restored.rules[1].showValue, "icon options survive ODF");
    check(restored.sparklines[0].source == spark.source && restored.sparklines[0].colors == spark.colors, "sparkline source and colors survive ODF");
    QByteArray xlsx; QXmlStreamWriter xml(&xlsx); xml.writeStartElement(QStringLiteral("worksheet")); restored.saveXlsxRules(xml); restored.saveXlsxSparklines(xml); xml.writeEndElement();
    check(xlsx.contains("sparklineGroups") && xlsx.contains("colorScale") && xlsx.contains("3TrafficLights1"), "native XLSX rules retained");
    // Exercise the cell renderer, including an otherwise empty sparkline cell.
    SheetView view(sheet); KoZoomHandler zoom; view.setViewConverter(&zoom); view.setPaintCellRange(QRect(1,1,3,6));
    QImage cells(600,220,QImage::Format_ARGB32); cells.fill(Qt::white);
    { QPainter painter(&cells); painter.setClipRect(cells.rect()); view.paintCells(painter, QRectF(0,0,600,220), QPointF()); }
    check(cells.pixelColor(40, 8) != QColor(Qt::white), "scale background reaches the cell renderer");
    int sparkPixels = 0;
    const QRect target = sheet->cellCoordinatesToDocument(QRect(2,5,1,1)).toAlignedRect();
    for (int y = target.top(); y < target.bottom() && y < cells.height(); ++y)
        for (int x = target.left(); x < target.right() && x < cells.width(); ++x)
            if (cells.pixelColor(x,y).blue() > cells.pixelColor(x,y).red() + 30) ++sparkPixels;
    check(sparkPixels > 0, "sparkline reaches an empty target in the cell renderer");
    if (argc > 1) cells.save(QString::fromLocal8Bit(argv[1]));
    qInfo("PASS: live scales, traffic lights, sparkline values/rendering and ODF/XLSX persistence");
    return 0;
}
