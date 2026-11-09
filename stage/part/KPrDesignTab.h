/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Calligra developers
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef KPRDESIGNTAB_H
#define KPRDESIGNTAB_H

#include <QObject>
#include <QSharedPointer>

class QHBoxLayout;
class QLabel;
class QToolButton;
class QWidget;

class KPrView;
class KoPAMasterPage;
class KoShapeBackground;

/**
 * Builds the Design tab of the Stage ribbon.
 *
 * The tab follows the PowerPoint layout: a horizontal gallery of the document
 * themes (its master slides), a strip of slide background variants, the slide
 * size and background commands, and the design ideas gallery.
 */
class KPrDesignTab : public QObject
{
    Q_OBJECT
public:
    KPrDesignTab(KPrView *view, QWidget *page);

private Q_SLOTS:
    void rebuildGalleries();

private:
    QWidget *createThemesGroup();
    QWidget *createVariantsGroup();
    QWidget *createCustomizeGroup();
    QWidget *createDesignerGroup();

    QToolButton *addGalleryItem(QHBoxLayout *stripLayout, const QPixmap &pixmap, const QString &toolTip);

    void applyTheme(KoPAMasterPage *master);
    void applyVariant(int index);

    KPrView *m_view;
    QWidget *m_page;
    QHBoxLayout *m_themesLayout;
    QHBoxLayout *m_variantsLayout;
};

#endif // KPRDESIGNTAB_H
