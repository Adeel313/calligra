/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2010 C. Boemann <cbo@boemann.dk>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "SimpleTableWidget.h"
#include <KoRibbonLayout.h>
#include "TextTool.h"

#include <KoColor.h>
#include <KoColorPopupAction.h>
#include <KoIcon.h>
#include <KoRibbonIcons.h>
#include <KoTableCellStyle.h>
#include <KoTextLayoutCellHelper.h>
#include <KoZoomHandler.h>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QColorDialog>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QVBoxLayout>
#include <QToolButton>

#include <KoStyleManager.h>
#include <KoTableCellStyle.h>
#include <KoTableStyle.h>
#include <KoTextTableTemplate.h>

#include <QPixmap>
#include <QWidget>

SimpleTableWidget::SimpleTableWidget(TextTool *tool, QWidget *parent)
    : QWidget(parent)
    , m_blockSignals(false)
    , m_tool(tool)
    , m_lastStyleEmitted(2)
{
    widget.setupUi(this);
    widget.addRowAbove->setDefaultAction(tool->action("insert_tablerow_above"));
    widget.addRowBelow->setDefaultAction(tool->action("insert_tablerow_below"));
    widget.addColumnLeft->setDefaultAction(tool->action("insert_tablecolumn_left"));
    widget.addColumnRight->setDefaultAction(tool->action("insert_tablecolumn_right"));
    widget.deleteRow->setDefaultAction(tool->action("delete_tablerow"));
    widget.deleteColumn->setDefaultAction(tool->action("delete_tablecolumn"));
    widget.mergeCells->setDefaultAction(tool->action("merge_tablecells"));
    widget.splitCells->setDefaultAction(tool->action("split_tablecells"));

    connect(tool->action("activate_borderpainter"), &QAction::triggered, this, &SimpleTableWidget::restartPainting);
    widget.border->setDefaultAction(tool->action("activate_borderpainter"));

    fillBorderButton(QColor(0, 0, 0));

    KoColorPopupAction *actionBorderColor = new KoColorPopupAction(this);
    actionBorderColor->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("format-fill-color"),
                                                           koIcon("format-fill-color")));
    actionBorderColor->setText(i18n("Set Border Color..."));
    widget.border->addAction(actionBorderColor);
    connect(actionBorderColor, &KoColorPopupAction::colorChanged, this, &SimpleTableWidget::setBorderColor);

    connect(widget.addRowAbove, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.addRowBelow, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.addColumnLeft, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.addColumnRight, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.deleteRow, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.deleteColumn, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.mergeCells, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.splitCells, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.border, &FormattingButton::itemTriggered, this, &SimpleTableWidget::emitTableBorderDataUpdated);
    connect(widget.border, &QAbstractButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    connect(widget.border, &FormattingButton::doneWithFocus, this, &SimpleTableWidget::doneWithFocus);
    connect(tool, &TextTool::tableStateChanged, this, &SimpleTableWidget::updateTableState);
}

void SimpleTableWidget::restartPainting()
{
    emitTableBorderDataUpdated(m_lastStyleEmitted);
}

namespace
{
/// The colours the Shading and Pen Color palettes offer.
QList<QColor> tablePaletteColors()
{
    return {QColor(0, 0, 0), QColor(68, 68, 68), QColor(128, 128, 128), QColor(191, 191, 191),
            QColor(255, 255, 255), QColor(47, 84, 150), QColor(68, 114, 196), QColor(91, 155, 213),
            QColor(217, 226, 243), QColor(192, 0, 0), QColor(237, 125, 49), QColor(255, 192, 0),
            QColor(112, 173, 71), QColor(0, 176, 80), QColor(112, 48, 160)};
}

/// Draws the preview of a table look: a four by three table with a header band.
QIcon tableLookIcon(const QColor &header, const QColor &body, const QColor &band,
                    const QColor &border, bool bandedRows, bool lastRow)
{
    QPixmap pm(48, 34);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    const qreal cellWidth = pm.width() / 4.0;
    const qreal headerHeight = 10.0;
    const qreal bodyHeight = (pm.height() - headerHeight) / 2.0;
    for (int row = 0; row < 3; ++row) {
        const qreal top = row == 0 ? 0.0 : headerHeight + (row - 1) * bodyHeight;
        const qreal height = row == 0 ? headerHeight : bodyHeight;
        for (int column = 0; column < 4; ++column) {
            const QRectF cellRect(column * cellWidth, top, cellWidth, height);
            QColor fill = body;
            if (row == 0) {
                fill = header;
            } else if (row == 2 && lastRow) {
                fill = header;
            } else if (bandedRows && row == 1) {
                fill = band;
            }
            painter.fillRect(cellRect, fill.isValid() ? fill : QColor(Qt::white));
        }
    }

    // Two mock text lines in the header and one in each body row.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(120, 120, 120));
    painter.drawRect(QRectF(3, 3, 24, 1.6));
    painter.drawRect(QRectF(3, 6, 16, 1.6));
    for (int row = 1; row < 3; ++row) {
        const qreal top = headerHeight + (row - 1) * bodyHeight + 4;
        for (int column = 0; column < 4; ++column) {
            painter.drawRect(QRectF(column * cellWidth + 2, top, cellWidth - 6, 1.4));
        }
    }
    painter.setBrush(Qt::NoBrush);

    // The grid: the look's own border colour, or a faint one for a plain look.
    painter.setPen(QPen(border.isValid() ? border : QColor(210, 210, 210), border.isValid() ? 1.0 : 0.6));
    for (int column = 1; column < 4; ++column) {
        painter.drawLine(QPointF(column * cellWidth, 0), QPointF(column * cellWidth, pm.height()));
    }
    painter.drawLine(QPointF(0, headerHeight), QPointF(pm.width(), headerHeight));
    painter.drawLine(QPointF(0, headerHeight + bodyHeight), QPointF(pm.width(), headerHeight + bodyHeight));
    painter.drawRect(QRectF(0.5, 0.5, pm.width() - 1, pm.height() - 1));
    return QIcon(pm);
}

/// Draws the preview of a named table style from the cell styles its template
/// provides for the header, the body and banded rows.
QIcon namedTableStyleIcon(KoStyleManager *styleManager, KoTableStyle *style)
{
    if (!styleManager || !style) {
        return tableLookIcon(QColor(), QColor(), QColor(), QColor(166, 166, 166), false, false);
    }
    KoTextTableTemplate *tableTemplate = nullptr;
    if (style->hasProperty(int(KoTableStyle::TableTemplate))) {
        tableTemplate = styleManager->tableTemplate(style->value(int(KoTableStyle::TableTemplate)).toInt());
    }
    const auto backgroundFor = [styleManager, tableTemplate](int styleId, const QBrush &fallback) {
        if (styleId <= 0) {
            return fallback;
        }
        if (KoTableCellStyle *cellStyle = styleManager->tableCellStyle(styleId)) {
            return cellStyle->background();
        }
        return fallback;
    };
    QColor header;
    QColor body;
    QColor band;
    if (tableTemplate) {
        header = backgroundFor(tableTemplate->firstRow(), QBrush()).color();
        body = backgroundFor(tableTemplate->body(), QBrush()).color();
        band = backgroundFor(tableTemplate->oddRows(), QBrush()).color();
    }
    const QBrush styleBackground = style->background();
    if (styleBackground.style() != Qt::NoBrush && styleBackground.color().isValid() && !body.isValid()) {
        body = styleBackground.color();
    }
    return tableLookIcon(header, body, band, QColor(120, 120, 120), band.isValid(), false);
}
}

namespace
{
/// A small icon only button for the compact grids of the table tab.
QToolButton *tableIconButton(QWidget *parent, QAction *action, const QString &name)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(name);
    if (action) {
        button->setDefaultAction(action);
        button->setAccessibleName(action->text());
        if (!action->toolTip().isEmpty()) {
            button->setToolTip(action->toolTip());
        }
    }
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(22, 22));
    button->setAutoRaise(true);
    button->setMinimumSize(QSize(26, 26));
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return button;
}

/// A placeholder for a command this editor does not offer yet. It is greyed
/// out instead of pretending to work.
QToolButton *disabledButton(QWidget *parent, const QString &text, const QString &icon)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setIcon(QIcon::fromTheme(icon));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIconSize(QSize(16, 16));
    button->setAutoRaise(true);
    button->setEnabled(false);
    return button;
}

/// A caption under a group, italic to match the reference ribbons.
QWidget *tableGroup(const QString &title, bool italicTitle = true)
{
    auto *group = new QWidget;
    group->setWindowTitle(title);
    group->setProperty("ribbonTargetPage", QStringLiteral("tableRibbonPage"));
    if (italicTitle) {
        group->setProperty("ribbonItalicTitle", true);
    }
    group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    return group;
}

/// A sample of a border line, used by the border style and width buttons.
QIcon borderLineIcon(Qt::PenStyle style, const QColor &color, qreal width)
{
    QPixmap pixmap(46, 16);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    QPen pen(color.isValid() ? color : QColor(Qt::black));
    pen.setStyle(style);
    pen.setWidthF(qMax<qreal>(1.0, width));
    painter.setPen(pen);
    painter.drawLine(3, 8, 43, 8);
    return QIcon(pixmap);
}

}

QList<QWidget *> SimpleTableWidget::configureRibbon()
{
    if (qApp->applicationName().contains(QLatin1String("words"), Qt::CaseInsensitive)) {
        for (QWidget *child : findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
            child->hide();
        }
        setWindowTitle(i18n("Table"));
        setProperty("ribbonGroupOrder", 10);
        if (QWidget *spacer = findChild<QWidget *>(QStringLiteral("SpecialSpacer"))) {
            spacer->hide();
        }
        auto *select = tableIconButton(this, m_tool->action(QStringLiteral("select_table")), QStringLiteral("selectWholeTable"));
        KoRibbonLayout::arrange(this, {select}, {});

        auto *rows = tableGroup(i18n("Rows & Columns"), false);
        rows->setProperty("ribbonGroupOrder", 20);
        KoRibbonLayout::arrange(rows, {}, {widget.addRowAbove, widget.addRowBelow, widget.addColumnLeft,
                                           widget.addColumnRight, widget.deleteRow, widget.deleteColumn});
        auto *merge = tableGroup(i18n("Merge"), false);
        merge->setProperty("ribbonGroupOrder", 30);
        KoRibbonLayout::arrange(merge, {widget.mergeCells}, {widget.splitCells});
        auto *alignment = tableGroup(i18n("Alignment"), false);
        alignment->setProperty("ribbonGroupOrder", 40);
        QList<QToolButton *> alignButtons;
        for (const QString &name : {QStringLiteral("table_align_left"), QStringLiteral("table_align_center"), QStringLiteral("table_align_right")}) {
            alignButtons.append(tableIconButton(alignment, m_tool->action(name), name + QStringLiteral("RibbonButton")));
        }
        KoRibbonLayout::arrange(alignment, {}, alignButtons);
        auto *properties = createTablePropertiesGroup();
        properties->setProperty("ribbonItalicTitle", false);
        properties->setProperty("ribbonGroupOrder", 50);

        auto *styles = createTableStylesGroup();
        styles->setWindowTitle(i18n("Table Styles"));
        styles->setProperty("ribbonTargetPage", QStringLiteral("tableDesignRibbonPage"));
        styles->setProperty("ribbonItalicTitle", false);
        styles->setProperty("ribbonGroupOrder", 10);
        auto *borders = createBordersGroup();
        borders->setProperty("ribbonTargetPage", QStringLiteral("tableDesignRibbonPage"));
        borders->setProperty("ribbonItalicTitle", false);
        borders->setProperty("ribbonGroupOrder", 20);
        return {rows, merge, alignment, properties, styles, borders};
    }

    // Layout: caption, the row/column commands and the cell selection and
    // merging commands, two rows of icons, as in the reference ribbon.
    setWindowTitle(i18n("Layout"));
    setProperty("ribbonItalicTitle", true);
    if (QWidget *spacer = findChild<QWidget *>(QStringLiteral("SpecialSpacer"))) {
        spacer->hide();
    }
    delete layout();
    auto *layoutGroup = new QGridLayout(this);
    layoutGroup->setContentsMargins(4, 2, 4, 2);
    layoutGroup->setHorizontalSpacing(4);
    layoutGroup->setVerticalSpacing(4);
    layoutGroup->setAlignment(Qt::AlignVCenter);

    auto *caption = new QToolButton(this);
    caption->setText(i18n("Caption"));
    caption->setIcon(QIcon::fromTheme(QStringLiteral("insert-text-frame")));
    caption->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    caption->setIconSize(QSize(18, 18));
    caption->setAutoRaise(true);
    caption->setEnabled(false);
    layoutGroup->addWidget(caption, 0, 0, 2, 1, Qt::AlignVCenter);

    // Two rows of icons: inserting rows and columns on the first, deleting
    // them and selecting the table on the second.
    const QList<QToolButton *> rowButtons = {widget.addRowAbove, widget.addRowBelow, widget.addColumnLeft,
                                             widget.addColumnRight, widget.deleteRow, widget.deleteColumn};
    int index = 0;
    for (QToolButton *button : rowButtons) {
        button->setParent(this);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(22, 22));
        button->setAutoRaise(true);
        button->setMinimumSize(QSize(26, 26));
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (QAction *action = button->defaultAction()) {
            button->setAccessibleName(action->text());
            if (!action->toolTip().isEmpty()) {
                button->setToolTip(action->toolTip());
            }
        }
        button->show();
        layoutGroup->addWidget(button, index / 4, 1 + index % 4, Qt::AlignVCenter);
        ++index;
    }
    auto *selectCell = tableIconButton(this, m_tool->action(QStringLiteral("select_table")), QStringLiteral("selectWholeTable"));
    selectCell->setIconSize(QSize(18, 18));
    selectCell->setMinimumSize(QSize(22, 22));
    selectCell->setToolTip(i18n("Select the whole table the cursor is in"));
    layoutGroup->addWidget(selectCell, 1, 3, Qt::AlignVCenter);
    auto *mergeSelection = tableIconButton(this, m_tool->action(QStringLiteral("merge_tablecells")), QStringLiteral("mergeSelection"));
    mergeSelection->setIconSize(QSize(18, 18));
    mergeSelection->setMinimumSize(QSize(22, 22));
    layoutGroup->addWidget(mergeSelection, 1, 4, Qt::AlignVCenter);

    // The text buttons the reference shows on the right of this group.
    for (QToolButton *button : {widget.mergeCells, widget.splitCells}) {
        button->setParent(this);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize(QSize(16, 16));
        button->setAutoRaise(true);
        button->show();
    }
    layoutGroup->addWidget(widget.mergeCells, 0, 5, Qt::AlignVCenter);
    layoutGroup->addWidget(widget.splitCells, 0, 6, Qt::AlignVCenter);
    auto *splitTable = disabledButton(this, i18n("Split Table"), QStringLiteral("edit-table-cell-split"));
    splitTable->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    layoutGroup->addWidget(splitTable, 1, 5, 1, 2, Qt::AlignVCenter);

    // Style: the table style gallery and the two colour dropdowns.
    auto *style = createTableStylesGroup();
    style->setWindowTitle(i18n("Style"));

    // Alignment: the table alignment, and the size commands the reference has
    // next to it.
    auto *alignment = tableGroup(i18n("Alignment"));
    auto *alignmentLayout = new QGridLayout(alignment);
    alignmentLayout->setContentsMargins(4, 2, 4, 2);
    alignmentLayout->setHorizontalSpacing(2);
    alignmentLayout->setVerticalSpacing(2);
    alignmentLayout->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    const QList<QPair<QString, QString>> alignments = {
        {QStringLiteral("table_align_left"), QStringLiteral("alignTableLeft")},
        {QStringLiteral("table_align_center"), QStringLiteral("alignTableCenter")},
        {QStringLiteral("table_align_right"), QStringLiteral("alignTableRight")},
    };
    int alignmentIndex = 0;
    for (const auto &[actionName, objectName] : alignments) {
        alignmentLayout->addWidget(tableIconButton(alignment, m_tool->action(actionName), objectName),
                                   alignmentIndex / 3, alignmentIndex % 3, Qt::AlignVCenter);
        ++alignmentIndex;
    }
    auto *optimize = new QToolButton(alignment);
    optimize->setIcon(QIcon::fromTheme(QStringLiteral("zoom-fit-best")));
    optimize->setToolTip(i18n("Optimize Size"));
    optimize->setToolButtonStyle(Qt::ToolButtonIconOnly);
    optimize->setIconSize(QSize(22, 22));
    optimize->setAutoRaise(true);
    optimize->setEnabled(false);
    alignmentLayout->addWidget(optimize, 0, 3, 2, 1, Qt::AlignVCenter);

    // Functions and Numbers exist in the reference ribbon but this editor has
    // no table formulas, so they are shown greyed out.
    auto *functions = tableGroup(i18n("Functions"));
    auto *functionsLayout = new QGridLayout(functions);
    functionsLayout->setContentsMargins(4, 2, 4, 2);
    functionsLayout->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    functionsLayout->addWidget(disabledButton(functions, i18n("Sort"), QStringLiteral("view-sort-ascending")), 0, 0);
    functionsLayout->addWidget(disabledButton(functions, i18n("Sum"), QStringLiteral("math-symbol")), 1, 0);

    auto *numbers = tableGroup(i18n("Numbers"));
    auto *numbersLayout = new QGridLayout(numbers);
    numbersLayout->setContentsMargins(4, 2, 4, 2);
    numbersLayout->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    numbersLayout->addWidget(disabledButton(numbers, i18n("Edit Formula"), QStringLiteral("insert-object")), 0, 0);
    auto *numberFormat = new QToolButton(numbers);
    numberFormat->setIcon(QIcon::fromTheme(QStringLiteral("format-number-percent")));
    numberFormat->setToolTip(i18n("Number Format"));
    numberFormat->setToolButtonStyle(Qt::ToolButtonIconOnly);
    numberFormat->setIconSize(QSize(22, 22));
    numberFormat->setAutoRaise(true);
    numberFormat->setEnabled(false);
    numbersLayout->addWidget(numberFormat, 0, 1);

    layoutGroup->setColumnMinimumWidth(5, 70);
    layoutGroup->setColumnMinimumWidth(6, 58);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QWidget *borders = createBordersGroup();

    // The reference ribbon packs these groups into the available width with a
    // smaller face than the rest of the interface, so do the same here.
    QFont ribbonFont = font();
    ribbonFont.setPointSizeF(qMax(7.0, ribbonFont.pointSizeF() - 1.5));
    setFont(ribbonFont);
    for (QWidget *group : {borders, style, alignment, functions, numbers}) {
        group->setFont(ribbonFont);
    }
    return {borders, style, alignment, functions, numbers};
}

QWidget *SimpleTableWidget::createBordersGroup()
{
    QWidget *group = tableGroup(i18n("Borders"));
    auto *layout = new QGridLayout(group);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(2);
    layout->setAlignment(Qt::AlignVCenter);

    const auto presetMenu = [this, group]() {
        auto *menu = new QMenu(group);
        const QList<QPair<QString, int>> presets = {{i18n("All Borders"), TextTool::AllBorders},
                                                    {i18n("Outside Borders"), TextTool::OutsideBorders},
                                                    {i18n("Inside Borders"), TextTool::InsideBorders},
                                                    {i18n("No Border"), TextTool::NoBorders}};
        for (const auto &[label, preset] : presets) {
            QAction *entry = menu->addAction(label);
            connect(entry, &QAction::triggered, this, [this, preset] {
                m_tool->applyTableBorderPreset(preset);
                Q_EMIT doneWithFocus();
            });
        }
        return menu;
    };

    KoBorder::BorderData pen = m_tool->tableBorderPen();
    if (pen.outerPen.widthF() <= 0) {
        pen.outerPen.setWidthF(0.5);
    }
    if (!pen.outerPen.color().isValid()) {
        pen.outerPen.setColor(Qt::black);
    }
    const QColor penColor = pen.outerPen.color();
    const qreal penWidth = pen.outerPen.widthF();

    const auto makeButton = [group](const QString &name, const QString &text) {
        auto *button = new QToolButton(group);
        button->setObjectName(name);
        button->setText(text);
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setAutoRaise(true);
        button->setIconSize(QSize(20, 20));
        return button;
    };

    // Two rows of icons with their captions below, as in the reference group.
    auto *styles = makeButton(QStringLiteral("tableBorderStyles"), i18n("Border Styles"));
    styles->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("border_all"), koIcon("border_all")));
    styles->setPopupMode(QToolButton::InstantPopup);
    styles->setMenu(presetMenu());
    layout->addWidget(styles, 0, 0, Qt::AlignVCenter);

    auto *borders = makeButton(QStringLiteral("tableBorders"), i18n("Borders"));
    borders->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("border_all"), koIcon("border_all")));
    borders->setPopupMode(QToolButton::InstantPopup);
    borders->setMenu(presetMenu());
    layout->addWidget(borders, 1, 0, Qt::AlignVCenter);

    // The line style, above the line width.
    auto *lineStyle = makeButton(QStringLiteral("tableBorderStyle"), i18n("Solid"));
    lineStyle->setPopupMode(QToolButton::InstantPopup);
    lineStyle->setIconSize(QSize(34, 14));
    lineStyle->setIcon(borderLineIcon(Qt::SolidLine, penColor, penWidth));
    lineStyle->setToolTip(i18n("Style of the border line the border tools draw"));
    layout->addWidget(lineStyle, 0, 1, Qt::AlignVCenter);

    auto *lineWidth = makeButton(QStringLiteral("tableBorderWidth"),
                                 i18nc("border width in points", "%1 pt", QString::number(penWidth)));
    lineWidth->setPopupMode(QToolButton::InstantPopup);
    lineWidth->setIconSize(QSize(34, 14));
    lineWidth->setIcon(borderLineIcon(Qt::SolidLine, penColor, penWidth));
    lineWidth->setToolTip(i18n("Width of the border line the border tools draw"));
    layout->addWidget(lineWidth, 1, 1, Qt::AlignVCenter);

    auto *styleMenu = new QMenu(lineStyle);
    const QList<QPair<QString, Qt::PenStyle>> stylesList = {{i18n("Solid"), Qt::SolidLine},
                                                            {i18n("Dashed"), Qt::DashLine},
                                                            {i18n("Dotted"), Qt::DotLine},
                                                            {i18n("Dash Dot"), Qt::DashDotLine}};
    for (const auto &[label, line] : stylesList) {
        QAction *entry = styleMenu->addAction(borderLineIcon(line, penColor, penWidth), label);
        connect(entry, &QAction::triggered, this, [this, line] {
            KoBorder::BorderData data = m_tool->tableBorderPen();
            data.style = line == Qt::DashLine      ? KoBorder::BorderDashed
                         : line == Qt::DotLine     ? KoBorder::BorderDotted
                         : line == Qt::DashDotLine ? KoBorder::BorderDashDot
                                                   : KoBorder::BorderSolid;
            data.outerPen.setStyle(line);
            m_tool->setTableBorderPen(data);
        });
        connect(entry, &QAction::triggered, lineStyle, [lineStyle, label, line, penColor, penWidth] {
            lineStyle->setText(label);
            lineStyle->setIcon(borderLineIcon(line, penColor, penWidth));
        });
    }
    lineStyle->setMenu(styleMenu);

    auto *widthMenu = new QMenu(lineWidth);
    for (qreal width : {0.5, 0.75, 1.0, 1.5, 2.25, 3.0}) {
        const QString label = i18nc("border width in points", "%1 pt", QString::number(width));
        QAction *entry = widthMenu->addAction(borderLineIcon(Qt::SolidLine, penColor, width), label);
        connect(entry, &QAction::triggered, this, [this, width] {
            KoBorder::BorderData data = m_tool->tableBorderPen();
            data.outerPen.setWidthF(width);
            data.innerPen.setWidthF(width);
            m_tool->setTableBorderPen(data);
        });
        connect(entry, &QAction::triggered, lineWidth, [lineWidth, label, width, penColor] {
            lineWidth->setText(label);
            lineWidth->setIcon(borderLineIcon(Qt::SolidLine, penColor, width));
        });
    }
    lineWidth->setMenu(widthMenu);

    // Pen Color: the colour the border tools and the border painter draw with.
    auto *penButton = makeButton(QStringLiteral("tableBorderPen"), i18n("Pen Color"));
    penButton->setPopupMode(QToolButton::InstantPopup);
    penButton->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("border_color"), koIcon("border_color")));
    penButton->setToolTip(i18n("Colour of the border line the border tools draw"));
    auto *colorMenu = new QMenu(penButton);
    const QList<QColor> colors = {QColor(Qt::black),       QColor(Qt::white),        QColor(0x7f, 0x7f, 0x7f),
                                  QColor(0xc0, 0x00, 0x00), QColor(0xff, 0x66, 0x00), QColor(0xff, 0xc0, 0x00),
                                  QColor(0x00, 0x80, 0x00), QColor(0x00, 0x70, 0xc0), QColor(0x2f, 0x54, 0x96)};
    for (const QColor &color : colors) {
        QPixmap swatch(22, 22);
        swatch.fill(color);
        QAction *entry = colorMenu->addAction(QIcon(swatch), color.name());
        connect(entry, &QAction::triggered, this, [this, color] {
            KoBorder::BorderData data = m_tool->tableBorderPen();
            data.outerPen.setColor(color);
            data.innerPen.setColor(color);
            m_tool->setTableBorderPen(data);
        });
    }
    colorMenu->addSeparator();
    QAction *moreColors = colorMenu->addAction(i18n("More Colors..."));
    connect(moreColors, &QAction::triggered, this, [this] {
        const QColor color = QColorDialog::getColor(m_tool->tableBorderPen().outerPen.color(), this,
                                                    i18n("Border Pen Color"));
        if (!color.isValid()) {
            return;
        }
        KoBorder::BorderData data = m_tool->tableBorderPen();
        data.outerPen.setColor(color);
        data.innerPen.setColor(color);
        m_tool->setTableBorderPen(data);
    });
    penButton->setMenu(colorMenu);
    layout->addWidget(penButton, 0, 2, Qt::AlignVCenter);

    // Border Painter: painting table cell edges with the pen above.
    auto *painter = makeButton(QStringLiteral("tableBorderPainter"), i18n("Border Painter"));
    painter->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("borderpainter"), koIcon("borderpainter")));
    painter->setCheckable(true);
    painter->setChecked(false);
    painter->setFocusPolicy(Qt::NoFocus);
    painter->setToolTip(i18n("Paint the pen's border on the table cells you click"));
    connect(painter, &QToolButton::toggled, this, [this](bool checked) {
        m_tool->setTableBorderPainterActive(checked);
        if (checked) {
            Q_EMIT doneWithFocus();
        }
    });
    layout->addWidget(painter, 1, 2, Qt::AlignVCenter);

    // Keep the captions whole; the ribbon squeezes groups when space is short.
    for (QToolButton *button : {styles, borders, lineStyle, lineWidth, penButton, painter}) {
        // Leave room for the menu arrow too, otherwise the caption is elided.
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + 30);
    }
    for (int column = 0; column < 3; ++column) {
        layout->setColumnStretch(column, 1);
    }

    group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return group;
}

QWidget *SimpleTableWidget::createTableStylesGroup()
{
    auto *group = new QWidget;
    group->setWindowTitle(i18n("Table Styles"));
    group->setProperty("ribbonTargetPage", QStringLiteral("tableRibbonPage"));
    auto *layout = new QHBoxLayout(group);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(1);
    layout->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_styleButtonContainer = new QWidget(group);
    auto *stylesLayout = new QGridLayout(m_styleButtonContainer);
    stylesLayout->setContentsMargins(0, 0, 0, 0);
    stylesLayout->setHorizontalSpacing(2);
    stylesLayout->setVerticalSpacing(2);
    stylesLayout->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    layout->addWidget(m_styleButtonContainer);
    group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    group->setProperty("ribbonItalicTitle", true);
    for (int column = 0; column < 3; ++column) {
        stylesLayout->setColumnStretch(column, 1);
    }
    fillStyleGallery();

    // The two colour dropdowns under the gallery, as in the reference tab.
    auto *tileHolder = layout->takeAt(0)->widget();
    layout->addWidget(tileHolder);
    auto *colors = new QWidget(group);
    auto *colorsLayout = new QHBoxLayout(colors);
    colorsLayout->setContentsMargins(0, 0, 0, 0);
    colorsLayout->setSpacing(2);

    QToolButton *shading = createShadingButton(colors);
    shading->setToolButtonStyle(Qt::ToolButtonIconOnly);
    shading->setIconSize(QSize(22, 22));
    colorsLayout->addWidget(shading);
    layout->addWidget(colors, 0, Qt::AlignLeft);
    layout->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    return group;
}

void SimpleTableWidget::fillStyleGallery()
{
    if (!m_styleButtonContainer) {
        return;
    }
    auto *layout = qobject_cast<QGridLayout *>(m_styleButtonContainer->layout());
    if (!layout) {
        return;
    }
    qDeleteAll(m_styleButtons);
    m_styleButtons.clear();

    struct Look {
        int look;
        QString name;
        QColor header;
        QColor body;
        QColor band;
        QColor border;
        bool banded;
        bool lastRow;
    };
    // These mirror the looks TextTool::applyTableLook knows about.
    const QList<Look> looks = {
        {TextTool::PlainLook, i18n("Plain Table"), QColor(), QColor(), QColor(), QColor(), false, false},
        {TextTool::GridLook, i18n("Grid Table"), QColor(), QColor(), QColor(), QColor(0, 0, 0), false, false},
        {TextTool::LightGridLook, i18n("Light Grid"), QColor(), QColor(), QColor(), QColor(166, 166, 166), false, false},
        {TextTool::BandedLook, i18n("Banded Rows"), QColor(217, 226, 243), QColor(), QColor(242, 242, 242), QColor(), true, false},
        {TextTool::HeaderLook, i18n("Dark Header"), QColor(47, 84, 150), QColor(), QColor(217, 226, 243), QColor(), true, false},
        {TextTool::EmphasisLook, i18n("Emphasis"), QColor(214, 220, 228), QColor(), QColor(), QColor(166, 166, 166), false, true},
    };
    for (const Look &look : looks) {
        auto *button = new QToolButton(m_styleButtonContainer);
        button->setObjectName(QStringLiteral("tableLook%1").arg(look.look));
        button->setIcon(tableLookIcon(look.header, look.body, look.band, look.border, look.banded, look.lastRow));
        button->setIconSize(QSize(38, 26));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setAutoRaise(true);
        button->setCheckable(false);
        button->setToolTip(look.name);
        button->setAccessibleName(look.name);
        button->setProperty("tableLook", look.look);
        connect(button, &QToolButton::clicked, this, [this, look] {
            m_tool->applyTableLook(look.look);
            Q_EMIT doneWithFocus();
            updateTableState();
        });
        layout->addWidget(button, m_styleButtons.size() / 3, m_styleButtons.size() % 3, Qt::AlignVCenter);
        m_styleButtons.append(button);
    }

    // The table styles the document itself defines follow the built in looks.
    if (m_styleManager) {
        const QList<KoTableStyle *> tableStyles = m_styleManager->tableStyles();
        for (KoTableStyle *style : tableStyles) {
            if (!style || style->name().isEmpty()) {
                continue;
            }
            auto *button = new QToolButton(m_styleButtonContainer);
            button->setObjectName(QStringLiteral("tableStyle%1").arg(style->styleId()));
            button->setIcon(namedTableStyleIcon(m_styleManager, style));
            button->setIconSize(QSize(48, 34));
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
            button->setAutoRaise(true);
            button->setToolTip(style->name());
            button->setAccessibleName(style->name());
            button->setProperty("tableStyleId", style->styleId());
            connect(button, &QToolButton::clicked, this, [this, style] {
                m_tool->applyTableStyle(style);
                Q_EMIT doneWithFocus();
                updateTableState();
            });
            layout->addWidget(button, m_styleButtons.size() / 3, m_styleButtons.size() % 3, Qt::AlignVCenter);
            m_styleButtons.append(button);
        }
    }
}

QToolButton *SimpleTableWidget::createShadingButton(QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("tableShading"));
    button->setText(i18n("Shading"));
    button->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("format-fill-color"), koIcon("format-fill-color")));
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(28, 28));
    button->setAutoRaise(true);
    button->setPopupMode(QToolButton::InstantPopup);
    button->setToolTip(i18n("Change the background colour of the selected cells"));

    auto *menu = new QMenu(button);
    for (const QColor &color : tablePaletteColors()) {
        QPixmap swatch(16, 16);
        swatch.fill(color);
        QAction *entry = menu->addAction(QIcon(swatch), color.name(QColor::HexRgb));
        entry->setToolTip(color.name(QColor::HexRgb));
        connect(entry, &QAction::triggered, this, [this, color] {
            m_tool->setTableCellBackground(QBrush(color));
            Q_EMIT doneWithFocus();
        });
    }
    menu->addSeparator();
    QAction *noColor = menu->addAction(i18n("No Color"));
    connect(noColor, &QAction::triggered, this, [this] {
        m_tool->setTableCellBackground(QBrush());
        Q_EMIT doneWithFocus();
    });
    button->setMenu(menu);
    KoRibbonLayout::configureButton(button, Qt::ToolButtonTextUnderIcon);
    return button;
}


QWidget *SimpleTableWidget::createTablePropertiesGroup()
{
    auto *group = new QWidget;
    group->setWindowTitle(i18n("Properties"));
    group->setProperty("ribbonTargetPage", QStringLiteral("tableRibbonPage"));
    group->setProperty("ribbonItalicTitle", true);
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(0);
    layout->setAlignment(Qt::AlignVCenter);

    auto *button = new QToolButton(group);
    button->setObjectName(QStringLiteral("tableProperties"));
    button->setDefaultAction(m_tool->action(QStringLiteral("format_table")));
    if (button->icon().isNull()) {
        button->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("insert-table"), koIcon("insert-table")));
    }
    button->setText(i18n("Table Properties"));
    KoRibbonLayout::configureButton(button, Qt::ToolButtonTextUnderIcon);
    connect(button, &QToolButton::clicked, this, &SimpleTableWidget::doneWithFocus);
    layout->addWidget(button, 0, Qt::AlignVCenter);
    return group;
}

void SimpleTableWidget::updateTableState()
{
    // The style options are checkable actions, so their state is refreshed with
    // the rest of the tool's actions; the gallery just needs its highlight.
    const QTextTableFormat format = m_tool->currentTableFormat();
    const bool inTable = m_tool->cursorInTable();
    for (QToolButton *button : std::as_const(m_styleButtons)) {
        if (!button) {
            continue;
        }
        button->setEnabled(inTable);
        const QVariant styleId = button->property("tableStyleId");
        if (styleId.isValid()) {
            button->setDown(inTable && format.hasProperty(KoTableStyle::StyleId)
                            && format.intProperty(KoTableStyle::StyleId) == styleId.toInt());
        } else {
            button->setDown(false);
        }
    }
}

void SimpleTableWidget::emitTableBorderDataUpdated(int i)
{
    m_lastStyleEmitted = i;
    Q_EMIT tableBorderDataUpdated(m_cellStyles[i - 1]->getEdge(KoBorder::TopBorder));
}

void SimpleTableWidget::setStyleManager(KoStyleManager *sm)
{
    m_styleManager = sm;
    fillStyleGallery();
}

void SimpleTableWidget::setBorderColor(const KoColor &koColor)
{
    fillBorderButton(koColor.toQColor());
}

void SimpleTableWidget::fillBorderButton(const QColor &color)
{
    ItemChooserAction *chooser = widget.border->addItemChooser(9);

    qDeleteAll(m_cellStyles);
    m_cellStyles.clear();

    qreal thickness[9] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.25, 3.0, 4.5, 6.0};

    KoTableCellStyle cellStyle;
    qDeleteAll(m_cellStyles);
    m_cellStyles.append(KoTableCellStyle().clone());
    for (int i = 8; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDoubleWave, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 6; i < 8; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderWave, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 4; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDouble, thickness[i] * 1.5, color);
        cellStyle.setEdgeDoubleBorderValues(KoBorder::TopBorder, thickness[i], thickness[i] / 4);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 6; i < 7; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderSlash, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }
    m_cellStyles.append(nullptr);
    for (int i = 0; i < 7; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDouble, thickness[i] * 3, color);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 0; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDouble, thickness[i] * 2, color);
        cellStyle.setEdgeDoubleBorderValues(KoBorder::TopBorder, thickness[i] / 2, thickness[i] / 2);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 0; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderSolid, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 0; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDotted, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 0; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDashed, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 0; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDashedLong, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 0; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDashDot, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }
    for (int i = 0; i < 9; i++) {
        cellStyle.setEdge(KoBorder::TopBorder, KoBorder::BorderDashDotDot, thickness[i], color);
        m_cellStyles.append(cellStyle.clone());
    }

    int i = 1;
    KoZoomHandler zoomHandler;
    foreach (KoTableCellStyle *style, m_cellStyles) {
        if (style == nullptr) {
            widget.border->addBlanks(chooser, 1);
            i++;
            continue;
        }
        QPixmap pm(48, 16);

        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        if (style->hasBorders()) {
            p.scale(zoomHandler.zoomedResolutionX(), zoomHandler.zoomedResolutionY());
            KoTextLayoutCellHelper cellStyleHelper(*style);
            qreal width = style->topBorderWidth();
            cellStyleHelper.drawTopHorizontalBorder(p,
                                                    0,
                                                    8 / zoomHandler.zoomedResolutionY() - width / 2,
                                                    pm.width() / zoomHandler.zoomedResolutionX(),
                                                    nullptr);

            widget.border->addItem(chooser, pm, i, KoUnit().toUserStringValue(style->topBorderWidth()) + "pt");
        } else {
            p.drawText(0, 0, 48, 16, Qt::AlignCenter, i18nc("No border - has to fit in 48pixels", "None"));
            widget.border->addItem(chooser, pm, i, i18n("No Border"));
        }
        i++;
    }
    widget.border->setItemsBackground(chooser, QColor(Qt::white));

    // widget.borderType->addItem("None");

    widget.border->addSeparator();

    /*
        //TODO: Uncomment the below line when the string freeze is over
        //action->setToolTip(i18n("Change the level the list is at"));

        QMenu *listLevelMenu = new QMenu();
        const int levelIndent = 13;
        for (int level = 0; level < 10; ++level) {
            QWidgetAction *wa = new QWidgetAction(listLevelMenu);
            ListLevelChooser *chooserWidget = new ListLevelChooser((levelIndent * level) + 5);
            wa->setDefaultWidget(chooserWidget);
            listLevelMenu->addAction(wa);
            m_mapper->setMapping(wa,level + 1);
            connect(chooserWidget, SIGNAL(clicked()), wa, SLOT(trigger()));
            connect(wa, SIGNAL(triggered()), m_mapper, SLOT(map()));
        }

        action->setMenu(listLevelMenu);
        */
}
