// SPDX-FileCopyrightText: 2024 Carl Schwan <carl@carlschwan.eu>
// SPDX-License-Identifier: LGPL-2.0-or-later

#include "KoTabBarProxyStyle.h"

#include <KColorUtils>

#include <QApplication>
#include <QPainter>
#include <QStyleOptionTab>
#include <qnamespace.h>

KoTabBarProxyStyle::KoTabBarProxyStyle()
    : QProxyStyle()
{
    _viewHoverBrush = KStatefulBrush(KColorScheme::View, KColorScheme::HoverColor);
}

QColor KoTabBarProxyStyle::alphaColor(QColor color, qreal alpha) const
{
    if (alpha >= 0 && alpha < 1.0) {
        color.setAlphaF(alpha * color.alphaF());
    }
    return color;
}

int KoTabBarProxyStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_TabBarTabShiftVertical:
        return 0;
    case PM_TabBarTabShiftHorizontal:
        return 0;
    case PM_TabBarTabOverlap:
        return 0;
    case PM_TabBarBaseOverlap:
        return 0;
    case PM_TabBarTabHSpace:
        return 2 * TabBar_TabMarginWidth;
    case PM_TabBarTabVSpace:
        return 2 * TabBar_TabMarginHeight;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

QSize KoTabBarProxyStyle::sizeFromContents(ContentsType element, const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    if (element == CT_TabBarTab) {
        const auto tabOption(qstyleoption_cast<const QStyleOptionTab *>(option));
        const bool hasText(tabOption && !tabOption->text.isEmpty());
        const bool hasIcon(tabOption && !tabOption->icon.isNull());
        const bool hasLeftButton(tabOption && !tabOption->leftButtonSize.isEmpty());
        const bool hasRightButton(tabOption && !tabOption->leftButtonSize.isEmpty());

        // calculate width increment for horizontal tabs
        int widthIncrement = 0;
        if (hasIcon && !(hasText || hasLeftButton || hasRightButton)) {
            widthIncrement -= 4;
        }
        if (hasText && hasIcon) {
            widthIncrement += TabBar_TabItemSpacing;
        }
        if (hasLeftButton && (hasText || hasIcon)) {
            widthIncrement += TabBar_TabItemSpacing;
        }
        if (hasRightButton && (hasText || hasIcon || hasLeftButton)) {
            widthIncrement += TabBar_TabItemSpacing;
        }

        // add margins
        QSize contentSize(size);

        // compare to minimum size
        contentSize.rheight() += widthIncrement;
        if (hasIcon && !hasText) {
            contentSize = contentSize.expandedTo(QSize(TabBar_TabMinHeight, 0));
        } else {
            contentSize = contentSize.expandedTo(QSize(TabBar_TabMinHeight, TabBar_TabMinWidth));
        }

        return size;
    }
    return QProxyStyle::sizeFromContents(element, option, size, widget);
}

void KoTabBarProxyStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    if (element == PE_FrameTabWidget || element == PE_FrameTabBarBase) {
        // don't draw frame
        return;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

qreal KoTabBarProxyStyle::devicePixelRatio(QPainter *painter) const
{
    return painter->device() ? painter->device()->devicePixelRatioF() : qApp->devicePixelRatio();
}

QRectF KoTabBarProxyStyle::strokedRect(const QRectF &rect, const qreal penWidth) const
{
    /* With a pen stroke width of 1, the rectangle should have each of its
     * sides moved inwards by half a pixel. This allows the stroke to be
     * pixel perfect instead of blurry from sitting between pixels and
     * prevents the rectangle with a stroke from becoming larger than the
     * original size of the rectangle.
     */
    qreal adjustment = 0.5 * penWidth;
    return rect.adjusted(adjustment, adjustment, -adjustment, -adjustment);
}

void KoTabBarProxyStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    if (element == CE_TabBarTabShape) {
        const auto tabOption(qstyleoption_cast<const QStyleOptionTab *>(option));
        if (!tabOption) {
            return;
        }

        // palette and state
        const bool enabled = option->state & State_Enabled;
        const bool hovered = option->state & State_MouseOver;
        const bool selected = option->state & State_Selected;
        const bool north = tabOption->shape == QTabBar::RoundedNorth || tabOption->shape == QTabBar::TriangularNorth;
        const bool south = tabOption->shape == QTabBar::RoundedSouth || tabOption->shape == QTabBar::TriangularSouth;
        const bool east = tabOption->shape == QTabBar::RoundedEast || tabOption->shape == QTabBar::TriangularEast;
        const bool verticalTabs(isVerticalTab(tabOption));
        // A little breathing room between tabs makes their rounded shape
        // visible in both the ribbon's horizontal and tool area's vertical layout.
        QRectF frameRect(option->rect.adjusted(3, 2, -3, -2));
        if (frameRect.isEmpty()) {
            return;
        }

        // setup painter
        painter->setRenderHint(QPainter::Antialiasing, true);
        const auto palette = option->palette;
        QColor fill(Qt::transparent);
        if (selected) {
            // Office's current ribbon treatment uses a quiet accent tint for
            // the active tab and a stronger brand-colored edge for focus.
            fill = alphaColor(palette.color(QPalette::Highlight), 0.14);
        } else if (enabled && hovered) {
            fill = alphaColor(hoverColor(palette), 0.32);
        }

        if (fill.alpha() > 0) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(fill);
            painter->drawRoundedRect(frameRect, 8, 8);
        }

        if (selected) {
            const qreal edgeSize = 3;
            QRectF accentRect(frameRect);
            if (north || south) {
                accentRect.setHeight(edgeSize);
                if (south) {
                    accentRect.moveBottom(frameRect.bottom());
                }
            } else if (verticalTabs) {
                accentRect.setWidth(edgeSize);
                if (east) {
                    accentRect.moveRight(frameRect.right());
                }
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(palette.color(QPalette::Highlight));
            painter->drawRoundedRect(accentRect, 1.5, 1.5);
        }
        return;
    }

    QProxyStyle::drawControl(element, option, painter, widget);
}

QColor KoTabBarProxyStyle::hoverColor(const QPalette &palette) const
{
    return _viewHoverBrush.brush(palette).color();
}

QRect KoTabBarProxyStyle::subElementRect(SubElement element, const QStyleOption *option, const QWidget *widget) const
{
    if (element == SE_TabWidgetTabBar) {
        // cast option and check
        const auto tabOption = qstyleoption_cast<const QStyleOptionTabWidgetFrame *>(option);
        if (!tabOption) {
            return proxy()->subElementRect(SE_TabWidgetTabBar, option, widget);
        }

        const auto tabWidget = qobject_cast<const QTabWidget *>(widget);
        const auto documentMode = tabWidget->documentMode();

        // do nothing if tabbar is hidden
        const QSize tabBarSize(tabOption->tabBarSize);

        auto rect(option->rect);
        QRect tabBarRect(QPoint(0, 0), tabBarSize);

        Qt::Alignment tabBarAlignment(styleHint(SH_TabBar_Alignment, option, widget));

        // horizontal positioning
        const bool verticalTabs(isVerticalTab(tabOption->shape));
        if (verticalTabs) {
            tabBarRect.setHeight(qMin(tabBarRect.height(), rect.height() - (documentMode ? 0 : 2)));
            if (tabBarAlignment == Qt::AlignCenter) {
                tabBarRect.moveTop(rect.top() + (rect.height() - tabBarRect.height()) / 2);
            } else {
                tabBarRect.moveTop(rect.top() + (documentMode ? 0 : 1));
            }

        } else {
            // account for corner rects
            // need to re-run visualRect to remove right-to-left handling, since it is re-added on tabBarRect at the end
            const auto leftButtonRect(visualRect(option, subElementRect(SE_TabWidgetLeftCorner, option, widget)));
            const auto rightButtonRect(visualRect(option, subElementRect(SE_TabWidgetRightCorner, option, widget)));

            rect.setLeft(leftButtonRect.width());
            rect.setRight(rightButtonRect.left());

            tabBarRect.setWidth(qMin(tabBarRect.width(), rect.width() - (documentMode ? 0 : 2)));
            if (tabBarAlignment == Qt::AlignCenter) {
                tabBarRect.moveLeft(rect.left() + (rect.width() - tabBarRect.width()) / 2);
            } else {
                tabBarRect.moveLeft(rect.left() + (documentMode ? 0 : 1));
            }

            tabBarRect = visualRect(option, tabBarRect);
        }

        // expand the tab bar towards the frame to cover the frame's border
        switch (tabOption->shape) {
        case QTabBar::RoundedNorth:
        case QTabBar::TriangularNorth:
            tabBarRect.moveTop(rect.top());
            tabBarRect.setBottom(tabBarRect.bottom() + 1);
            break;

        case QTabBar::RoundedSouth:
        case QTabBar::TriangularSouth:
            tabBarRect.moveBottom(rect.bottom());
            tabBarRect.setTop(tabBarRect.top() - 1);
            break;

        case QTabBar::RoundedWest:
        case QTabBar::TriangularWest:
            tabBarRect.moveLeft(rect.left());
            tabBarRect.setRight(tabBarRect.right() + 1);
            break;

        case QTabBar::RoundedEast:
        case QTabBar::TriangularEast:
            tabBarRect.moveRight(rect.right());
            tabBarRect.setLeft(tabBarRect.left() - 1);
            break;

        default:
            break;
        }

        return tabBarRect;
    }

    return QProxyStyle::subElementRect(element, option, widget);
}
