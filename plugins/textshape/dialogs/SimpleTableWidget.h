/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2010 C. Boemann <cbo@boemann.dk>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef SIMPLETABLEWIDGET_H
#define SIMPLETABLEWIDGET_H

#include <KoBorder.h>
#include <KoListStyle.h>
#include <ui_SimpleTableWidget.h>

#include <QTextBlock>
#include <QWidget>

class TextTool;
class KoStyleManager;
class KoTableCellStyle;
class KoColor;
class QComboBox;
class QToolButton;

class SimpleTableWidget : public QWidget
{
    Q_OBJECT
public:
    QList<QWidget *> configureRibbon();
    explicit SimpleTableWidget(TextTool *tool, QWidget *parent = nullptr);

public Q_SLOTS:
    void setStyleManager(KoStyleManager *sm);
    void emitTableBorderDataUpdated(int i = 0);
    void restartPainting();
    void setBorderColor(const KoColor &);
    /// Fills the table style options and the style gallery from the table the
    /// cursor is in.
    void updateTableState();

Q_SIGNALS:
    void doneWithFocus();
    void tableBorderDataUpdated(const KoBorder::BorderData &);

private:
    void fillBorderButton(const QColor &color);
    QWidget *createTableStylesGroup();
    QToolButton *createShadingButton(QWidget *parent);
    QWidget *createBordersGroup();
    QWidget *createTablePropertiesGroup();
    void fillStyleGallery();

    Ui::SimpleTableWidget widget;
    QList<QToolButton *> m_styleButtons;
    QWidget *m_styleButtonContainer = nullptr;
    QComboBox *m_borderWeight = nullptr;
    QToolButton *m_penColor = nullptr;
    QToolButton *m_bordersButton = nullptr;
    KoStyleManager *m_styleManager;
    bool m_blockSignals;
    bool m_comboboxHasBidiItems;
    QTextBlock m_currentBlock;
    TextTool *m_tool;
    QList<KoTableCellStyle *> m_cellStyles; // we only fill out the top borderdata for the previews
    int m_lastStyleEmitted;
};

#endif
