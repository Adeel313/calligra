/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KO_RIBBON_LAYOUT_H
#define KO_RIBBON_LAYOUT_H

#include <QGridLayout>
#include <QToolButton>
#include <QStyle>

// Reuse a tool's existing buttons and menus when presenting a sidebar panel
// in the Words ribbon. Connections and action state stay with the controls.
namespace KoRibbonLayout
{
// Use logical pixels and the active style's size hint, which also reserves
// space for split-menu arrows and themed button margins.
inline void configureButton(QToolButton *button, Qt::ToolButtonStyle presentation)
{
    button->setToolButtonStyle(presentation);
    const bool large = presentation == Qt::ToolButtonTextUnderIcon;
    const int icon = qMax(large ? 32 : 20,
                         button->style()->pixelMetric(large ? QStyle::PM_LargeIconSize : QStyle::PM_SmallIconSize, nullptr, button));
    button->setIconSize(QSize(icon, icon));
    button->setAutoRaise(true);
    button->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    button->ensurePolished();
    const QSize hint = button->sizeHint();
    button->setMinimumSize(hint.expandedTo(QSize(large ? 64 : 30,
                                               large ? icon + button->fontMetrics().lineSpacing() + 16 : icon + 10)));
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

inline void arrange(QWidget *panel, const QList<QToolButton *> &large, const QList<QToolButton *> &stacked)
{
    for (QToolButton *button : large + stacked) {
        button->setParent(panel);
    }
    if (QWidget *spacer = panel->findChild<QWidget *>(QStringLiteral("SpecialSpacer"))) {
        spacer->hide();
    }
    delete panel->layout();
    auto *layout = new QGridLayout(panel);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(2);
    layout->setAlignment(Qt::AlignTop);
    for (int i = 0; i < large.size(); ++i) {
        QToolButton *button = large[i];
        configureButton(button, Qt::ToolButtonTextUnderIcon);
        button->show();
        layout->addWidget(button, 0, i, 3, 1, Qt::AlignTop);
    }
    for (int i = 0; i < stacked.size(); ++i) {
        QToolButton *button = stacked[i];
        configureButton(button, Qt::ToolButtonTextBesideIcon);
        button->show();
        layout->addWidget(button, i % 3, large.size() + i / 3, Qt::AlignLeft);
    }
}
}
#endif
