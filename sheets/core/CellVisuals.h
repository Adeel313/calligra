/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef CALLIGRA_SHEETS_CELL_VISUALS_H
#define CALLIGRA_SHEETS_CELL_VISUALS_H

#include "sheets_core_export.h"
#include <QColor>
#include <QMap>
#include <QRect>
#include <QStringList>
#include <QVector>

#include <KoXmlReader.h>
class KoXmlWriter;
class QPainter;
class QXmlStreamWriter;
namespace Calligra { namespace Sheets {
class Cell;
class Sheet;

// Visual rules retain their source ranges; values and thresholds are evaluated
// from the workbook rather than baking imported cached values into cell styles.
class CALLIGRA_SHEETS_CORE_EXPORT CellVisuals
{
public:
    struct Threshold {
        QString type = QStringLiteral("num");
        QString value;
        bool inclusive = true;
    };
    struct Rule {
        QString range;
        QString type;
        QString iconSet = QStringLiteral("3TrafficLights1");
        int priority = 1;
        bool reverse = false;
        bool showValue = true;
        QVector<Threshold> thresholds;
        QVector<QColor> colors;
    };
    struct Sparkline {
        QString source;
        QString target;
        QMap<QString, QString> options;
        QMap<QString, QColor> colors;
    };
    QList<Rule> rules;
    QList<Sparkline> sparklines;
    bool isEmpty() const { return rules.isEmpty() && sparklines.isEmpty(); }
    QColor background(const Cell &cell) const;
    int trafficLight(const Cell &cell, bool *showValue = nullptr) const;
    QVector<double> sparklineValues(const Sparkline &sparkline, Sheet *sheet) const;
    void paint(QPainter &painter, const QRectF &rect, const Cell &cell) const;
    QRect usedArea(Sheet *sheet) const;
    void loadOdf(const KoXmlElement &element);
    void saveOdf(KoXmlWriter &writer) const;
    void saveXlsxRules(QXmlStreamWriter &writer) const;
    void saveXlsxSparklines(QXmlStreamWriter &writer) const;
};
} }
#endif
