/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KO_RIBBON_LAYOUT_H
#define KO_RIBBON_LAYOUT_H

#include <QGridLayout>
#include <QPainter>
#include <QMenu>
#include <QToolButton>
#include <QStyle>
#include <QWidget>

// Reuse a tool's existing buttons and menus when presenting a sidebar panel
// in the Words ribbon. Connections and action state stay with the controls.
namespace KoRibbonLayout
{
// Use logical pixels and the active style's size hint, which also reserves
// space for split-menu arrows and themed button margins.
inline void configureButton(QToolButton *button, Qt::ToolButtonStyle presentation)
{
    button->setProperty("ribbonPresentationStyle", int(presentation));
    button->setProperty("ribbonCompactable", !button->icon().isNull() && (presentation == Qt::ToolButtonTextUnderIcon || presentation == Qt::ToolButtonTextBesideIcon));
    if (button->accessibleName().isEmpty()) {
        button->setAccessibleName(button->text().remove(QLatin1Char('&')));
    }
    if (button->toolTip().isEmpty()) {
        button->setToolTip(button->text());
    }
    button->setToolButtonStyle(presentation);
    const bool large = presentation == Qt::ToolButtonTextUnderIcon;
    const int icon = qMax(large ? 36 : 24,
                         button->style()->pixelMetric(large ? QStyle::PM_LargeIconSize : QStyle::PM_SmallIconSize, nullptr, button));
    const QSize preferredIcon = button->property("ribbonPreferredIconSize").toSize();
    button->setIconSize(preferredIcon.isValid() ? preferredIcon : QSize(icon, icon));
    button->setAutoRaise(true);
    button->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    button->ensurePolished();
    const QSize hint = button->sizeHint();
    button->setMinimumSize(hint.expandedTo(QSize(large ? 72 : 34,
                                               large ? qMax(76, icon + button->fontMetrics().lineSpacing() + 16) : icon + 10))
                               .expandedTo(button->property("ribbonPreferredMinimumSize").toSize()));
    button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

// Only resize controls in the ribbon itself. A gallery inside a popup menu
// keeps its own layout even when its ribbon page becomes narrow.
inline QList<QToolButton *> pageButtons(QWidget *page)
{
    QList<QToolButton *> result;
    for (QToolButton *button : page->findChildren<QToolButton *>()) {
        bool inPage = true;
        for (QWidget *ancestor = button; ancestor && ancestor != page; ancestor = ancestor->parentWidget()) {
            if (qobject_cast<QMenu *>(ancestor)
                || (ancestor->isHidden() && ancestor->testAttribute(Qt::WA_WState_ExplicitShowHide))) {
                inPage = false;
                break;
            }
        }
        if (inPage) {
            result.append(button);
        }
    }
    return result;
}

inline void setButtonCompact(QToolButton *button, bool compact)
{
    if (!button->property("ribbonCompactable").toBool()
        || button->property("ribbonIsCompact").toBool() == compact) {
        return;
    }
    if (compact) {
        button->setProperty("ribbonExpandedStyle", int(button->toolButtonStyle()));
        button->setProperty("ribbonExpandedMinimum", button->minimumSize());
        button->setProperty("ribbonExpandedMaximum", button->maximumSize());
        button->setProperty("ribbonExpandedIcon", button->iconSize());
        const int expandedHeight = button->minimumHeight();
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setMinimumSize(0, 0);
        const QSize hint = button->sizeHint();
        button->setMinimumSize(hint.width(), qMax(expandedHeight, hint.height()));
    } else {
        button->setToolButtonStyle(static_cast<Qt::ToolButtonStyle>(button->property("ribbonExpandedStyle").toInt()));
        button->setIconSize(button->property("ribbonExpandedIcon").toSize());
        button->setMaximumSize(button->property("ribbonExpandedMaximum").toSize());
        button->setMinimumSize(button->property("ribbonExpandedMinimum").toSize());
    }
    button->setProperty("ribbonIsCompact", compact);
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
    // Two stacked rows keep every panel inside the one ribbon band height.
    for (int i = 0; i < stacked.size(); ++i) {
        QToolButton *button = stacked[i];
        configureButton(button, Qt::ToolButtonTextBesideIcon);
        button->show();
        layout->addWidget(button, i % 2, large.size() + i / 2, Qt::AlignLeft);
    }
}
}

// Container for a ribbon group. The separator is painted rather than set with a
// stylesheet border so the group's size hint never changes when the ribbon is
// repolished; a hint that changes mid-layout makes the ribbon dock and the
// canvas below it resize each other in an endless loop.
class KoRibbonGroupWidget : public QWidget
{
public:
    explicit KoRibbonGroupWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (width() < 3 || height() < 4)
            return;
        QPainter painter(this);
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawLine(width() - 1, 5, width() - 1, height() - 6);
    }
};

#endif
