/* SPDX-License-Identifier: LGPL-2.0-or-later */
#pragma once
#include <QByteArray>
#include <QString>

// Convert an embedded ODF chart, including its local data, to DrawingML.
// An empty result means the chart cannot be exported without losing its data.
QByteArray writeDocxChart(const QByteArray &odfChart, const QString &workbookRelationshipId, QByteArray *workbook);
