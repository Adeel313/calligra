/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Calligra developers
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef KPRFORMATTAB_H
#define KPRFORMATTAB_H

#include <QColor>
#include <QList>
#include <QObject>

class QToolButton;
class QWidget;

class KPrView;
class KoProperties;
class KoUnitDoubleSpinBox;

/**
 * Fills the contextual Shape Format tab of Stage.
 *
 * The tab follows the PowerPoint "Drawing Tools" layout: a gallery that inserts
 * preset shapes and a gallery of shape styles, in front of the arrange, size and
 * line controls that come from the shape tool itself.
 */
class KPrFormatTab : public QObject
{
    Q_OBJECT
public:
    KPrFormatTab(KPrView *view, QWidget *page);

private:
    QWidget *createInsertShapesGroup();
    QWidget *createShapeStylesGroup();
    QWidget *createSizeGroup();
    void insertShape(const QString &preset);
    void applyStyle(const QColor &fill, const QColor &stroke, bool filled);
    void updateSizeFields();
    void applySize();

    KPrView *m_view;
    QWidget *m_page;
    KoUnitDoubleSpinBox *m_heightBox = nullptr;
    KoUnitDoubleSpinBox *m_widthBox = nullptr;
    bool m_updatingSize = false;
    // Kept alive for the shape tool, which does not take ownership.
    QList<KoProperties *> m_presetProperties;
};

#endif // KPRFORMATTAB_H
