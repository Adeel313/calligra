/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2011 Marijn Kruisselbrink <mkruisselbrink@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "ActionOptionWidget.h"

// Qt
#include <QDomElement>
#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QToolButton>
#include <QWidgetAction>

// KF5
#include <KLocalizedString>

// Calligra
#include <KoFontComboBox.h>

// Calligra Sheets
#include "CellToolBase.h"

using namespace Calligra::Sheets;

class GroupFlowLayout : public QLayout
{
public:
    GroupFlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    GroupFlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~GroupFlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> itemList;
    int m_hSpace;
    int m_vSpace;
};

ActionOptionWidget::ActionOptionWidget(CellToolBase *cellTool, const QDomElement &e, QWidget *parent)
    : QWidget(parent)
{
    QString name = e.attribute("name");
    setObjectName(name);
    setWindowTitle(i18n(name.toLatin1()));

    const bool ribbon = QCoreApplication::applicationName().contains(QLatin1String("sheets"), Qt::CaseInsensitive);
    QGridLayout *ribbonLayout = ribbon ? new QGridLayout(this) : nullptr;
    QLayout *layout = ribbonLayout ? static_cast<QLayout *>(ribbonLayout) : new GroupFlowLayout(this);
    layout->setContentsMargins({});
    if (ribbonLayout) {
        ribbonLayout->setSpacing(3);
        ribbonLayout->setAlignment(Qt::AlignTop);
        const QStringList order = {QStringLiteral("Font"), QStringLiteral("Alignment"), QStringLiteral("Number format"),
                                   QStringLiteral("Borders"), QStringLiteral("Merge")};
        setProperty("ribbonGroupOrder", 20 + order.indexOf(name) * 10);
    }

    int groupPosition = 0;
    for (QDomElement group = e.firstChildElement("group"); !group.isNull(); group = group.nextSiblingElement("group")) {
        QHBoxLayout *groupLayout = new QHBoxLayout();
        groupLayout->setSpacing(4);
        QWidget *ribbonGroup = nullptr;
        if (ribbonLayout) {
            // Containers keep short, fixed-width icon rows from limiting
            // the grid column that also holds the wider font selectors.
            ribbonGroup = new QWidget(this);
            ribbonGroup->setLayout(groupLayout);
            groupLayout->setContentsMargins(0, 0, 0, 0);
            groupLayout->setAlignment(Qt::AlignLeft);
            ribbonLayout->addWidget(ribbonGroup, groupPosition % 3, groupPosition / 3, Qt::AlignLeft | Qt::AlignTop);
            ++groupPosition;
        } else {
            layout->addItem(groupLayout);
        }

        // In each group there are a number of actions that will be layouted together.
        for (QDomElement action = group.firstChildElement("action"); !action.isNull(); action = action.nextSiblingElement("action")) {
            QString actionName = action.attribute("name");
            QAction *a = cellTool->action(actionName);
            if (!a) {
                warnSheets << "unknown action" << actionName << "in CellToolOptionWidgets.xml";
                continue;
            }
            QWidgetAction *wa = qobject_cast<QWidgetAction *>(a);
            QWidget *w = wa ? wa->requestWidget(this) : nullptr;
            if (w && ribbon && actionName == QLatin1String("font")) {
                w->setFixedWidth(170);
            } else if (w && ribbon && actionName == QLatin1String("fontSize")) {
                w->setFixedWidth(64);
            } else if (w && qobject_cast<KoFontComboBox *>(w)) {
                w->setMinimumWidth(w->minimumWidth() / 2);
            }
            if (!w) {
                QToolButton *b = new QToolButton(this);
                b->setFocusPolicy(Qt::NoFocus);
                b->setDefaultAction(a);
                b->setAutoRaise(true);
                w = b;
            }

            if (w) {
                groupLayout->addWidget(w);
            }
        }
        if (ribbonGroup) {
            ribbonGroup->setMinimumSize(groupLayout->minimumSize());
        }
    }

    // The following widget activates a special feature in the
    // ToolOptionsDocker that makes the components of the widget align
    // to the top if there is extra space.
    if (!ribbon) {
        QWidget *specialSpacer = new QWidget(this);
        specialSpacer->setObjectName("SpecialSpacer");
        layout->addWidget(specialSpacer);
    }
}

GroupFlowLayout::GroupFlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent)
    , m_hSpace(hSpacing)
    , m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

GroupFlowLayout::GroupFlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing)
    , m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

GroupFlowLayout::~GroupFlowLayout()
{
    QLayoutItem *item;
    while ((item = takeAt(0)))
        delete item;
}

void GroupFlowLayout::addItem(QLayoutItem *item)
{
    itemList.append(item);
}

int GroupFlowLayout::horizontalSpacing() const
{
    if (m_hSpace >= 0) {
        return m_hSpace;
    } else {
        return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
    }
}

int GroupFlowLayout::verticalSpacing() const
{
    if (m_vSpace >= 0) {
        return m_vSpace;
    } else {
        return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
    }
}

int GroupFlowLayout::count() const
{
    return itemList.size();
}

QLayoutItem *GroupFlowLayout::itemAt(int index) const
{
    return itemList.value(index);
}

QLayoutItem *GroupFlowLayout::takeAt(int index)
{
    if (index >= 0 && index < itemList.size())
        return itemList.takeAt(index);
    else
        return nullptr;
}

Qt::Orientations GroupFlowLayout::expandingDirections() const
{
    return Qt::Orientations();
}

bool GroupFlowLayout::hasHeightForWidth() const
{
    return true;
}

int GroupFlowLayout::heightForWidth(int width) const
{
    int height = doLayout(QRect(0, 0, width, 0), true);
    return height;
}

void GroupFlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize GroupFlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize GroupFlowLayout::minimumSize() const
{
    QSize size;
    QLayoutItem *item;
    foreach (item, itemList)
        size = size.expandedTo(item->minimumSize());

    size += QSize(contentsMargins().left() + contentsMargins().right(), contentsMargins().top() + contentsMargins().bottom());
    return size;
}

int GroupFlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;

    QLayoutItem *item;
    foreach (item, itemList) {
        QWidget *wid = item->widget();
        int spaceX = horizontalSpacing();
        if (wid && spaceX == -1)
            spaceX = wid->style()->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
        if (spaceX == -1)
            spaceX = 5;
        int spaceY = verticalSpacing();
        if (wid && spaceY == -1)
            spaceY = wid->style()->layoutSpacing(QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);
        if (spaceY == -1)
            spaceX = 5;
        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }

        if (!testOnly) {
            QSize s = item->sizeHint();
            if (nextX - spaceX > effectiveRect.right()) {
                QSize minSize = item->minimumSize();
                int neededW = effectiveRect.right() - x;
                s.setWidth(qMax(neededW, minSize.width()));
            }
            item->setGeometry(QRect(QPoint(x, y), s));
        }

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }
    return y + lineHeight - rect.y() + bottom;
}

int GroupFlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject *parent = this->parent();
    if (!parent) {
        return -1;
    } else if (parent->isWidgetType()) {
        QWidget *pw = static_cast<QWidget *>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    } else {
        return static_cast<QLayout *>(parent)->spacing();
    }
}
