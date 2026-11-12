/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2007 Martin Pfeiffer <hubipete@gmx.net>
 * SPDX-FileCopyrightText: 2007 Jan Hambrecht <jaham@gmx.net>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "DefaultToolArrangeWidget.h"

#include <KoInteractionTool.h>
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <KoRibbonLayout.h>
#include <KLocalizedString>

DefaultToolArrangeWidget::DefaultToolArrangeWidget(KoInteractionTool *tool, QWidget *parent)
    : QWidget(parent)
{
    m_tool = tool;

    setupUi(this);

    bringToFront->setDefaultAction(m_tool->action("object_order_front"));
    raiseLevel->setDefaultAction(m_tool->action("object_order_raise"));
    lowerLevel->setDefaultAction(m_tool->action("object_order_lower"));
    sendBack->setDefaultAction(m_tool->action("object_order_back"));

    leftAlign->setDefaultAction(m_tool->action("object_align_horizontal_left"));
    hCenterAlign->setDefaultAction(m_tool->action("object_align_horizontal_center"));
    rightAlign->setDefaultAction(m_tool->action("object_align_horizontal_right"));
    topAlign->setDefaultAction(m_tool->action("object_align_vertical_top"));
    vCenterAlign->setDefaultAction(m_tool->action("object_align_vertical_center"));
    bottomAlign->setDefaultAction(m_tool->action("object_align_vertical_bottom"));

    group->setDefaultAction(m_tool->action("object_group"));
    ungroup->setDefaultAction(m_tool->action("object_ungroup"));
    if (qApp->applicationName().contains(QLatin1String("stage"), Qt::CaseInsensitive)
        || qApp->applicationName().contains(QLatin1String("sheets"), Qt::CaseInsensitive)) {
        SpecialSpacer->hide();
        gridLayout_4->setContentsMargins(2, 0, 2, 0);
        for (QGridLayout *layout : {gridLayout, gridLayout_2, gridLayout_3}) {
            layout->setHorizontalSpacing(3);
            layout->setVerticalSpacing(1);
        }
        for (QToolButton *button : {bringToFront, raiseLevel, lowerLevel, sendBack,
                                   leftAlign, hCenterAlign, rightAlign, topAlign, vCenterAlign,
                                   bottomAlign, group, ungroup}) {
            KoRibbonLayout::configureButton(button, Qt::ToolButtonIconOnly);
            button->setIconSize(QSize(28, 28));
            button->setFixedSize(36, 36);
        }
    }
    if (qApp->applicationName().contains(QLatin1String("words"), Qt::CaseInsensitive)) {
        const auto menuButton = [this](const QString &label, QToolButton *primary, const QList<QToolButton *> &commands) {
            auto *button = new QToolButton(this);
            button->setText(label);
            button->setIcon(primary->icon());
            auto *menu = new QMenu(button);
            for (QToolButton *command : commands) {
                menu->addAction(command->defaultAction());
                command->hide();
            }
            button->setMenu(menu);
            button->setPopupMode(QToolButton::InstantPopup);
            return button;
        };
        auto *forward = menuButton(i18n("Bring Forward"), raiseLevel, {raiseLevel, bringToFront});
        auto *backward = menuButton(i18n("Send Backward"), lowerLevel, {lowerLevel, sendBack});
        auto *align = menuButton(i18n("Align"), leftAlign, {leftAlign, hCenterAlign, rightAlign, topAlign, vCenterAlign, bottomAlign});
        auto *grouping = menuButton(i18n("Group"), group, {group, ungroup});
        KoRibbonLayout::arrange(this, {}, {forward, backward, align, grouping});
    }

}
