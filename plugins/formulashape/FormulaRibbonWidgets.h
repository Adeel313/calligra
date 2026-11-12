/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2007 Martin Pfeiffer <hubipete@gmx.net>
                 2009 Jeremias Epperlein <jeeree@web.de>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef FORMULARIBBONWIDGETS_H
#define FORMULARIBBONWIDGETS_H

#include <QList>
#include <QString>
#include <QWidget>

class QGridLayout;
class QHBoxLayout;
class QToolButton;
class KoFormulaTool;

/**
 * @short The "Structures" group of the formula ribbon
 *
 * Offers the structure templates as large buttons; the variants of a template
 * are in the arrow of its button.
 */
class FormulaStructuresWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FormulaStructuresWidget(KoFormulaTool *tool, QWidget *parent = nullptr);

private:
    void addTemplate(QHBoxLayout *layout, const QString &caption, const QString &action,
                     const QStringList &variants = QStringList());

    KoFormulaTool *m_tool;
};

/**
 * @short The "Symbols" group of the formula ribbon
 *
 * Shows two rows of symbols; the arrow buttons page through the sets of
 * common, set, Greek, arrow and miscellaneous symbols.
 */
class FormulaSymbolsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FormulaSymbolsWidget(KoFormulaTool *tool, QWidget *parent = nullptr);

private:
    void populatePage();

    KoFormulaTool *m_tool;
    QGridLayout *m_grid;
    QList<QList<QString>> m_pages;
    QList<QToolButton *> m_symbolButtons;
    int m_page;
};

#endif // FORMULARIBBONWIDGETS_H
