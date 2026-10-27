/*
 * SPDX-FileCopyrightText: 2005-2009 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2009 Peter Simonsson <peter.simonsson@gmail.com>
 * SPDX-FileCopyrightText: 2010 Cyrille Berger <cberger@cberger.net>
 * SPDX-FileCopyrightText: 2011 C. Boemann <cbo@boemann.dk>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <algorithm>

#include "KoModeBox_p.h"
#include "KoTabBarProxyStyle.h"

#include <KoCanvasBase.h>
#include <KoCanvasControllerWidget.h>
#include <KoShapeLayer.h>
#include <KoShapePropertyWidget.h>
#include <KoToolManager.h>
#include <WidgetsDebug.h>

#include <KColorUtils>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSelectAction>
#include <KSharedConfig>
#include <KStatefulBrush>
#include <KTitleWidget>

#include <QApplication>
#include <QBoxLayout>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QMap>
#include <QMenu>
#include <QPainter>
#include <QProxyStyle>
#include <QRect>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionTabWidgetFrame>
#include <QTabBar>
#include <QTextLayout>

class KoModeBox::Private
{
public:
    Private(KoCanvasController *c)
        : canvas(c->canvas())
        , activeId(-1)
        , iconTextFitted(true)
        , fittingIterations(0)
        , iconMode(IconAndText)
        , horizontalTabsSide(LeftSide)
        , orientation(Qt::Vertical)
    {
    }

    /// A page that is not bound to a tool, shown as its own tab.
    struct CustomPage {
        QString title;
        QString toolTip;
        QIcon icon;
        QPointer<QWidget> page;
        QPointer<QWidget> wrapper;
        bool beforeToolTabs;
    };

    struct ToolTabWidget {
        QString toolId;
        QPointer<QWidget> widget;
    };

    /// Sentinel value stored as tab data for custom (non-tool) pages.
    static constexpr int customPageMarker = -2;

    KoCanvasBase *canvas;
    QBoxLayout *layout;
    QWidget *header;
    QHBoxLayout *headerLayout;
    QWidget *leadingWidget = nullptr;
    QWidget *trailingWidget = nullptr;
    Qt::Orientation orientation;
    QList<KoToolAction *> toolActions; // toolActions maintained by toolmanager
    QList<KoToolAction *> addedToolActions; // tools in the order added to QToolBox
    QMap<int, QWidget *> addedWidgets;
    QSet<QWidget *> currentAuxWidgets;
    QList<QPointer<QWidget>> lastOptionWidgets;
    QList<QString> lastShownCodes;
    QList<CustomPage> customPages;
    QList<ToolTabWidget> toolTabWidgets;
    int activeId;
    QTabBar *tabBar;
    QStackedWidget *stack;
    QFrame *separator;
    bool iconTextFitted;
    int fittingIterations;
    IconMode iconMode;
    HorizontalTabsSide horizontalTabsSide;
};

QString KoModeBox::applicationName;

static bool compareToolActions(const KoToolAction *b1, const KoToolAction *b2)
{
    int b1Level;
    int b2Level;
    if (b1->section().contains(KoModeBox::applicationName)) {
        b1Level = 0;
    } else if (b1->section().contains("main")) {
        b1Level = 1;
    } else {
        b1Level = 2;
    }

    if (b2->section().contains(KoModeBox::applicationName)) {
        b2Level = 0;
    } else if (b2->section().contains("main")) {
        b2Level = 1;
    } else {
        b2Level = 2;
    }

    if (b1Level == b2Level) {
        return b1->priority() < b2->priority();
    } else {
        return b1Level < b2Level;
    }
}

// Connect Shapes and Draw Path share a single ribbon page in Words. Keep their
// tool ids and activation behavior separate while mapping both to one tab.
static bool isCombinedShapeTool(const QString &toolId)
{
    return toolId == QLatin1String("ConnectionTool") || toolId == QLatin1String("CreatePathTool");
}

static int ribbonGroupId(const KoToolAction *toolAction, const QList<KoToolAction *> &toolActions, bool combineShapeTools)
{
    if (combineShapeTools && isCombinedShapeTool(toolAction->id())) {
        for (KoToolAction *candidate : toolActions) {
            if (candidate->id() == QLatin1String("ConnectionTool")) {
                return candidate->buttonGroupId();
            }
        }
    }
    return toolAction->buttonGroupId();
}

KoModeBox::KoModeBox(KoCanvasControllerWidget *canvas, const QString &appName)
    : QWidget()
    , d(std::make_unique<Private>(canvas))
{
    applicationName = appName;

    KConfigGroup cfg = KSharedConfig::openConfig()->group("calligra");
    d->iconMode = (IconMode)cfg.readEntry("ModeBoxIconMode", (int)IconOnly);
    d->horizontalTabsSide = (HorizontalTabsSide)cfg.readEntry("ModeBoxHorizontalTabsSide", (int)LeftSide);

    setContentsMargins({});

    d->layout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    d->layout->setObjectName("KoModeBoxLayout");
    d->layout->setContentsMargins({});
    d->layout->setSpacing({});
    d->stack = new QStackedWidget;

    d->header = new QWidget(this);
    d->header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    d->headerLayout = new QHBoxLayout(d->header);
    d->headerLayout->setContentsMargins({});
    d->headerLayout->setSpacing({});

    d->tabBar = new QTabBar;
    d->tabBar->setDocumentMode(true);
    d->tabBar->setShape(QTabBar::RoundedWest);
    auto style = new KoTabBarProxyStyle;
    d->tabBar->setStyle(style);
    d->tabBar->setAutoFillBackground(true);
    QPalette palette = d->tabBar->palette();
    palette.setColor(QPalette::Window, palette.color(QPalette::Base));
    d->tabBar->setPalette(palette);
    d->headerLayout->addWidget(d->tabBar);
    d->headerLayout->addStretch(1);
    d->layout->addWidget(d->header);

    d->separator = new QFrame;
    d->separator->setFrameStyle(QFrame::VLine);
    d->separator->setFixedWidth(1);
    d->layout->addWidget(d->separator);

    setIconSize();
    d->tabBar->setExpanding(false);
    d->tabBar->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    d->layout->addWidget(d->stack);

    const auto toolActions = KoToolManager::instance()->toolActionList();
    for (KoToolAction *toolAction : toolActions) {
        addToolAction(toolAction);
    }

    std::sort(d->toolActions.begin(), d->toolActions.end(), compareToolActions);

    // Update visibility of toolActions
    updateShownTools(QList<QString>());

    d->tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(d->tabBar, &QTabBar::currentChanged, this, &KoModeBox::toolSelected);
    connect(d->tabBar, &QWidget::customContextMenuRequested, this, &KoModeBox::slotContextMenuRequested);

    connect(KoToolManager::instance(), &KoToolManager::changedTool, this, &KoModeBox::setActiveTool);
    connect(KoToolManager::instance(), &KoToolManager::currentLayerChanged, this, &KoModeBox::setCurrentLayer);
    connect(KoToolManager::instance(), &KoToolManager::toolCodesSelected, this, &KoModeBox::updateShownTools);
    connect(KoToolManager::instance(), &KoToolManager::addedTool, this, &KoModeBox::toolAdded);

    connect(canvas, &KoCanvasControllerWidget::toolOptionWidgetsChanged, this, &KoModeBox::setOptionWidgets);
}

KoModeBox::~KoModeBox() = default;

void KoModeBox::setOrientation(Qt::Orientation orientation)
{
    if (d->orientation == orientation) {
        return;
    }
    d->orientation = orientation;
    const bool horizontal = orientation == Qt::Horizontal;

    d->layout->setDirection(horizontal ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    d->tabBar->setShape(horizontal ? QTabBar::RoundedNorth : QTabBar::RoundedWest);
    d->tabBar->setSizePolicy(horizontal ? QSizePolicy::Maximum : QSizePolicy::Minimum,
                             horizontal ? QSizePolicy::Minimum : QSizePolicy::Expanding);
    d->headerLayout->setDirection(horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    d->layout->setAlignment(d->header, Qt::Alignment{});

    d->separator->setFrameStyle(horizontal ? QFrame::HLine : QFrame::VLine);
    d->separator->setMinimumWidth(horizontal ? 0 : 1);
    d->separator->setMaximumWidth(horizontal ? QWIDGETSIZE_MAX : 1);
    d->separator->setMinimumHeight(horizontal ? 1 : 0);
    d->separator->setMaximumHeight(horizontal ? 1 : QWIDGETSIZE_MAX);

    for (int i = 0; i < d->stack->count(); ++i) {
        if (auto sa = qobject_cast<ScrollArea *>(d->stack->widget(i))) {
            sa->setHorizontalScrollBarPolicy(horizontal ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
            sa->setVerticalScrollBarPolicy(horizontal ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
        }
    }

    updateShownTools(d->lastShownCodes);
    if (!d->lastOptionWidgets.isEmpty()) {
        setOptionWidgets(d->lastOptionWidgets);
    }
}

void KoModeBox::addToolAction(KoToolAction *toolAction)
{
    d->toolActions.append(toolAction);
}

void KoModeBox::setActiveTool(KoCanvasController *canvas, int id)
{
    if (canvas->canvas() == d->canvas) {
        const bool combineShapeTools = d->orientation == Qt::Horizontal && applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive);
        int pageId = id;
        if (combineShapeTools) {
            for (KoToolAction *toolAction : std::as_const(d->toolActions)) {
                if (toolAction->buttonGroupId() == id) {
                    pageId = ribbonGroupId(toolAction, d->toolActions, true);
                    break;
                }
            }
        }
        // Clear the minimumSize instigated by the previous tool
        // The new minimumSize will be set in updateShownTools()
        int oldPageId = d->activeId;
        if (combineShapeTools) {
            for (KoToolAction *toolAction : std::as_const(d->toolActions)) {
                if (toolAction->buttonGroupId() == d->activeId) {
                    oldPageId = ribbonGroupId(toolAction, d->toolActions, true);
                    break;
                }
            }
        }
        if (d->addedWidgets.contains(oldPageId)) {
            ScrollArea *sa = qobject_cast<ScrollArea *>(d->addedWidgets[oldPageId]->parentWidget()->parentWidget());
            sa->setMinimumWidth(0);
            sa->setMinimumHeight(0);
        }

        d->activeId = id;
        d->tabBar->blockSignals(true);
        for (int i = 0; i < d->tabBar->count(); ++i) {
            if (d->tabBar->tabData(i).toInt() == pageId) {
                d->tabBar->setCurrentIndex(i);
                d->stack->setCurrentIndex(i);
                break;
            }
        }
        d->tabBar->blockSignals(false);
        return;
    }
}

void KoModeBox::addCustomPage(const QString &title, const QIcon &icon, const QString &toolTip, QWidget *page, bool beforeToolTabs)
{
    if (!page) {
        return;
    }
    const bool horizontal = d->orientation == Qt::Horizontal;
    auto *wrapper = new ScrollArea;
    wrapper->setHorizontalScrollBarPolicy(horizontal ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    wrapper->setVerticalScrollBarPolicy(horizontal ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    // Must be resizable, like the tool pages in addItem(): the page's actions
    // are added after this point, and a non-resizable scroll area never grows
    // the widget to its new sizeHint, leaving the tab looking empty.
    wrapper->setWidgetResizable(true);
    wrapper->setContentsMargins(0, 0, 0, 0);
    wrapper->setWidget(page);
    wrapper->setFrameShape(QFrame::NoFrame);
    wrapper->setFocusPolicy(Qt::NoFocus);

    d->customPages.append({title, toolTip, icon, page, wrapper, beforeToolTabs});
    updateShownTools(d->lastShownCodes);
}

void KoModeBox::addWidgetToToolTab(const QString &toolId, QWidget *widget)
{
    if (!widget) {
        return;
    }
    d->toolTabWidgets.append({toolId, widget});
    for (KoToolAction *toolAction : std::as_const(d->toolActions)) {
        if (toolAction->id() != toolId) {
            continue;
        }
        QWidget *page = d->addedWidgets.value(toolAction->buttonGroupId());
        QLayout *layout = page ? page->layout() : nullptr;
        if (auto gridLayout = qobject_cast<QGridLayout *>(layout)) {
            gridLayout->addWidget(widget, 0, 0);
        } else if (auto boxLayout = qobject_cast<QBoxLayout *>(layout)) {
            boxLayout->insertWidget(0, widget);
        }
        break;
    }
}

void KoModeBox::setLeadingWidget(QWidget *widget)
{
    if (!widget || d->leadingWidget == widget) {
        return;
    }
    d->leadingWidget = widget;
    widget->setParent(d->header);
    d->headerLayout->insertWidget(0, widget, 0, Qt::AlignVCenter);
}

void KoModeBox::setTrailingWidget(QWidget *widget)
{
    if (!widget || d->trailingWidget == widget) {
        return;
    }
    d->trailingWidget = widget;
    widget->setParent(d->header);
    d->headerLayout->addWidget(widget, 0, Qt::AlignVCenter);
}


void KoModeBox::appendCustomPages(bool beforeToolTabs)
{
    const bool wasBlocked = d->tabBar->blockSignals(true);
    for (const Private::CustomPage &customPage : std::as_const(d->customPages)) {
        if (!customPage.wrapper || customPage.beforeToolTabs != beforeToolTabs) {
            continue;
        }
        const int index = d->tabBar->addTab(customPage.icon, customPage.title);
        d->tabBar->setTabToolTip(index, customPage.toolTip);
        d->tabBar->setTabData(index, Private::customPageMarker);
        d->stack->addWidget(customPage.wrapper);
    }
    d->tabBar->blockSignals(wasBlocked);
}

QIcon KoModeBox::createTextIcon(KoToolAction *toolAction) const
{
    QSize iconSize = d->tabBar->iconSize();
    QFont smallFont = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    qreal pointSize = QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont).pointSizeF();
    smallFont.setPointSizeF(pointSize);
    // This must be a QImage, as drawing to a QPixmap outside the
    // UI thread will cause sporadic crashes.
    QImage pm(iconSize, QImage::Format_ARGB32_Premultiplied);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.rotate(90);
    p.translate(0, -iconSize.width());

    QIcon::fromTheme(toolAction->iconName()).paint(&p, 0, 0, iconSize.height(), 22);

    QTextLayout textLayout(toolAction->iconText(), smallFont, p.device());
    QTextOption option = QTextOption(Qt::AlignTop | Qt::AlignHCenter);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    textLayout.setTextOption(option);
    textLayout.beginLayout();
    qreal height = 0;
    while (1) {
        QTextLine line = textLayout.createLine();
        if (!line.isValid())
            break;

        line.setLineWidth(iconSize.height());
        line.setPosition(QPointF(0, height));
        height += line.height();
    }
    textLayout.endLayout();

    if (textLayout.lineCount() > 2) {
        iconSize.setHeight(iconSize.height() + 8);
        d->tabBar->setIconSize(iconSize);
        d->iconTextFitted = false;
    } else if (height > iconSize.width() - 22) {
        iconSize.setWidth(22 + height);
        d->tabBar->setIconSize(iconSize);
        d->iconTextFitted = false;
    }

    p.setFont(smallFont);
    p.setPen(palette().text().color());
    textLayout.draw(&p, QPoint(0, 22));
    p.end();

    return QIcon(QPixmap::fromImage(pm));
}

QIcon KoModeBox::createSimpleIcon(KoToolAction *toolAction) const
{
    QSize iconSize = d->tabBar->iconSize();

    // This must be a QImage, as drawing to a QPixmap outside the
    // UI thread will cause sporadic crashes.
    QImage pm(iconSize, QImage::Format_ARGB32_Premultiplied);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    if (d->horizontalTabsSide == LeftSide) {
        p.rotate(90);
        p.translate(0, -iconSize.width());
    } else {
        p.rotate(-90);
        p.translate(-iconSize.height(), 0);
    }

    QIcon::fromTheme(toolAction->iconName()).paint(&p, 0, 0, iconSize.height(), iconSize.width());

    return QIcon(QPixmap::fromImage(pm));
}

void KoModeBox::addItem(KoToolAction *toolAction)
{
    const bool combineShapeTools = d->orientation == Qt::Horizontal && applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive);
    const int groupId = ribbonGroupId(toolAction, d->toolActions, combineShapeTools);
    if (combineShapeTools && isCombinedShapeTool(toolAction->id())) {
        for (int i = 0; i < d->tabBar->count(); ++i) {
            if (d->tabBar->tabData(i).toInt() == groupId) {
                d->addedToolActions.append(toolAction);
                return;
            }
        }
    }

    QWidget *oldwidget = d->addedWidgets[groupId];
    QWidget *widget;

    // We need to create a new widget in all cases as QToolBox seems to crash if we reuse
    // a widget (even though the item had been removed)
    QLayout *layout = oldwidget ? oldwidget->layout() : nullptr;
    if (!qobject_cast<QBoxLayout *>(layout)) {
        layout = new QBoxLayout(d->orientation == Qt::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    } else {
        qobject_cast<QBoxLayout *>(layout)->setDirection(d->orientation == Qt::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    }
    widget = new QWidget();
    widget->setLayout(layout);
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    d->addedWidgets[groupId] = widget;

    for (const Private::ToolTabWidget &toolTabWidget : std::as_const(d->toolTabWidgets)) {
        if (toolTabWidget.toolId != toolAction->id() || !toolTabWidget.widget) {
            continue;
        }
        if (auto boxLayout = qobject_cast<QBoxLayout *>(layout); boxLayout && boxLayout->indexOf(toolTabWidget.widget) < 0) {
            boxLayout->insertWidget(0, toolTabWidget.widget);
        }
    }

    if (d->orientation == Qt::Horizontal) {
        QString tabTitle = toolAction->iconText();
        if (applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive)) {
            if (toolAction->id() == QLatin1String("TextToolFactory_ID")) {
                tabTitle = i18n("Home");
            } else if (toolAction->id() == QLatin1String("PageToolFactory_ID")) {
                tabTitle = i18n("Layout");
            } else if (combineShapeTools && isCombinedShapeTool(toolAction->id())) {
                tabTitle = i18n("Shapes");
            }
        }
        const QString iconName = combineShapeTools && isCombinedShapeTool(toolAction->id()) ? QStringLiteral("draw-freehand") : toolAction->iconName();
        const int index = d->tabBar->addTab(QIcon::fromTheme(iconName), tabTitle);
        d->tabBar->setTabToolTip(index, toolAction->toolTip());
        d->tabBar->setTabData(index, groupId);
    } else if (d->iconMode == IconAndText) {
        const int index = d->tabBar->addTab(createTextIcon(toolAction), QString());
        d->tabBar->setTabToolTip(index, toolAction->toolTip());
        d->tabBar->setTabData(index, groupId);
    } else {
        const int index = d->tabBar->addTab(createSimpleIcon(toolAction), QString());
        d->tabBar->setTabToolTip(index, toolAction->toolTip());
        d->tabBar->setTabData(index, groupId);
    }
    d->tabBar->blockSignals(false);
    ScrollArea *sa = new ScrollArea();
    sa->setHorizontalScrollBarPolicy(d->orientation == Qt::Horizontal ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    sa->setVerticalScrollBarPolicy(d->orientation == Qt::Horizontal ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    sa->setWidgetResizable(true);
    sa->setContentsMargins(0, 0, 0, 0);
    sa->setWidget(widget);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setFocusPolicy(Qt::NoFocus);
    d->stack->addWidget(sa);
    d->addedToolActions.append(toolAction);
}

void KoModeBox::updateShownTools(const QList<QString> &codes)
{
    d->lastShownCodes = codes;
    if (d->iconTextFitted) {
        d->fittingIterations = 0;
    }
    d->iconTextFitted = true;

    d->tabBar->blockSignals(true);

    while (d->tabBar->count()) {
        d->tabBar->removeTab(0);
        d->stack->removeWidget(d->stack->widget(0));
    }

    d->addedToolActions.clear();

    appendCustomPages(true);

    bool customPagesInserted = false;
    foreach (KoToolAction *toolAction, d->toolActions) {
        const QString toolCodes = toolAction->visibilityCode();
        if (toolAction->section().contains(applicationName)) {
            addItem(toolAction);
            // In Words, put the View page directly after the Text Editing tab.
            if (!customPagesInserted && toolAction->id() == QLatin1String("TextToolFactory_ID")) {
                appendCustomPages(false);
                customPagesInserted = true;
            }
            continue;
        } else if (!toolAction->section().contains("dynamic") && !toolAction->section().contains("main")) {
            continue;
        }
        if (toolCodes.startsWith(QLatin1String("flake/"))) {
            addItem(toolAction);
            continue;
        }

        if (toolCodes.endsWith(QLatin1String("/always"))) {
            addItem(toolAction);
            continue;
        } else if (toolCodes.isEmpty() && codes.count() != 0) {
            addItem(toolAction);
            continue;
        } else {
            foreach (const QString &shapeCode, codes) {
                if (toolCodes.contains(shapeCode)) {
                    addItem(toolAction);
                    break;
                }
            }
        }
    }
    if (!customPagesInserted) {
        appendCustomPages(false);
    }

    // Restore the active tool's tab
    int activePageId = d->activeId;
    if (d->orientation == Qt::Horizontal && applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive)) {
        for (KoToolAction *toolAction : std::as_const(d->toolActions)) {
            if (toolAction->buttonGroupId() == d->activeId) {
                activePageId = ribbonGroupId(toolAction, d->toolActions, true);
                break;
            }
        }
    }
    for (int i = 0; i < d->tabBar->count(); ++i) {
        if (d->tabBar->tabData(i).toInt() == activePageId) {
            d->tabBar->setCurrentIndex(i);
            d->stack->setCurrentIndex(i);
            break;
        }
    }
    d->tabBar->blockSignals(false);

    if (!d->iconTextFitted && d->fittingIterations++ < 8) {
        updateShownTools(codes);
    }
    d->iconTextFitted = true;
}

void KoModeBox::setOptionWidgets(const QList<QPointer<QWidget>> &optionWidgetList)
{
    const bool combineShapeTools = d->orientation == Qt::Horizontal && applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive);
    QString activeToolId;
    int pageId = d->activeId;
    for (KoToolAction *toolAction : std::as_const(d->toolActions)) {
        if (toolAction->buttonGroupId() == d->activeId) {
            activeToolId = toolAction->id();
            pageId = ribbonGroupId(toolAction, d->toolActions, combineShapeTools);
            break;
        }
    }
    if (!d->addedWidgets.contains(pageId))
        return;

    QWidget *page = d->addedWidgets[pageId];

    for (const QPointer<QWidget> &widget : std::as_const(d->lastOptionWidgets)) {
        if (widget) {
            widget->setParent(page);
        }
    }
    for (const Private::ToolTabWidget &toolTabWidget : std::as_const(d->toolTabWidgets)) {
        if ((toolTabWidget.toolId == activeToolId || (combineShapeTools && isCombinedShapeTool(toolTabWidget.toolId) && isCombinedShapeTool(activeToolId))) && toolTabWidget.widget) {
            toolTabWidget.widget->setParent(page);
        }
    }
    d->lastOptionWidgets = optionWidgetList;

    // For some reason we need to set some attr on our placeholder widget here
    // even though these settings should be default
    // Otherwise Sheets' celltool's optionwidget looks ugly
    page->setAutoFillBackground(false);
    page->setBackgroundRole(QPalette::NoRole);

    auto layout = qobject_cast<QBoxLayout *>(page->layout());
    Q_ASSERT(layout);
    layout->setDirection(d->orientation == Qt::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    layout->setSpacing(2);
    while (QLayoutItem *item = layout->takeAt(0)) {
        delete item;
    }
    qDeleteAll(d->currentAuxWidgets);
    d->currentAuxWidgets.clear();

    if (d->orientation == Qt::Horizontal) {
        QList<QWidget *> ribbonWidgets;
        for (const Private::ToolTabWidget &toolTabWidget : std::as_const(d->toolTabWidgets)) {
            if (toolTabWidget.toolId == activeToolId && toolTabWidget.widget) {
                ribbonWidgets.append(toolTabWidget.widget);
            }
        }
        for (QWidget *widget : optionWidgetList) {
            if (widget) {
                ribbonWidgets.append(widget);
            }
        }

        bool first = true;
        for (QWidget *widget : std::as_const(ribbonWidgets)) {
            if (!first) {
                auto separator = new QFrame(page);
                separator->setFrameStyle(QFrame::VLine);
                separator->setFixedWidth(1);
                layout->addWidget(separator);
                d->currentAuxWidgets.insert(separator);
            }
            first = false;

            auto group = new QWidget(page);
            auto groupLayout = new QVBoxLayout(group);
            groupLayout->setContentsMargins(1, 0, 1, 0);
            groupLayout->setSpacing(1);
            groupLayout->setSizeConstraint(QLayout::SetMinAndMaxSize);

            QLabel *sectionTitle = nullptr;
            if (!widget->windowTitle().isEmpty()) {
                sectionTitle = new QLabel(widget->windowTitle(), group);
                sectionTitle->setAlignment(Qt::AlignHCenter);
                QFont sectionFont = sectionTitle->font();
                sectionFont.setPointSizeF(qMax(7.0, sectionFont.pointSizeF() - 1.0));
                sectionTitle->setFont(sectionFont);
                sectionTitle->setMaximumHeight(14);
                QPalette palette = sectionTitle->palette();
                auto foregroundColor = palette.color(QPalette::WindowText);
                foregroundColor.setAlphaF(0.8);
                palette.setColor(QPalette::WindowText, foregroundColor);
                sectionTitle->setPalette(palette);
            }
            groupLayout->addWidget(widget);
            widget->show();

            if (sectionTitle) {
                groupLayout->addWidget(sectionTitle);
            }

            layout->addWidget(group, 0, Qt::AlignTop);
            d->currentAuxWidgets.insert(group);
        }
        layout->addStretch();
    } else {
        auto title = new KTitleWidget;
        title->setText(d->tabBar->tabToolTip(d->tabBar->currentIndex()));
        layout->addWidget(title);
        d->currentAuxWidgets.insert(title);

        for (QWidget *widget : optionWidgetList) {
            if (widget && !widget->windowTitle().isEmpty()) {
                if (widget != optionWidgetList.constFirst()) {
                    layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
                }
                auto sectionTitle = new QLabel(widget->windowTitle());
                QPalette palette = sectionTitle->palette();
                auto foregroundColor = palette.color(QPalette::WindowText);
                foregroundColor.setAlphaF(0.8);
                palette.setColor(QPalette::WindowText, foregroundColor);
                sectionTitle->setPalette(palette);
                layout->addWidget(sectionTitle);
                d->currentAuxWidgets.insert(sectionTitle);

                auto separator = new QFrame;
                separator->setFrameStyle(QFrame::HLine);
                separator->setFixedHeight(1);
                separator->setPalette(palette);
                layout->addWidget(separator);
                d->currentAuxWidgets.insert(separator);

                layout->addSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
            }
            if (widget) {
                layout->addWidget(widget);
                widget->show();
            }
        }
        layout->addStretch();
    }
}

void ScrollArea::showEvent(QShowEvent *e)
{
    QScrollArea::showEvent(e);
    if (horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff) {
        setMinimumWidth(widget()->minimumSizeHint().width() + (verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 0));
    } else {
        setMinimumHeight(widget()->minimumSizeHint().height() + (horizontalScrollBar()->isVisible() ? horizontalScrollBar()->height() : 0));
    }
}

void KoModeBox::setCurrentLayer(const KoCanvasController *canvas, const KoShapeLayer *layer)
{
    Q_UNUSED(canvas);
    Q_UNUSED(layer);
    // Since targeted application don't use this we won't bother implementing
}

void KoModeBox::setCanvas(KoCanvasBase *canvas)
{
    KoCanvasControllerWidget *ccwidget;

    if (d->canvas) {
        ccwidget = dynamic_cast<KoCanvasControllerWidget *>(d->canvas->canvasController());
        disconnect(ccwidget, &KoCanvasControllerWidget::toolOptionWidgetsChanged, this, &KoModeBox::setOptionWidgets);
    }

    d->canvas = canvas;

    ccwidget = dynamic_cast<KoCanvasControllerWidget *>(d->canvas->canvasController());
    connect(ccwidget, &KoCanvasControllerWidget::toolOptionWidgetsChanged, this, &KoModeBox::setOptionWidgets);
}

void KoModeBox::unsetCanvas()
{
    d->canvas = nullptr;
}

void KoModeBox::toolAdded(KoToolAction *toolAction, KoCanvasController *canvas)
{
    if (canvas->canvas() == d->canvas) {
        addToolAction(toolAction);

        std::stable_sort(d->toolActions.begin(), d->toolActions.end(), compareToolActions);

        updateShownTools(QList<QString>());
    }
}

void KoModeBox::toolSelected(int index)
{
    if (index < 0) {
        return;
    }
    const int id = d->tabBar->tabData(index).toInt();
    if (id == Private::customPageMarker) {
        // a custom page has no tool to activate; just show its content
        d->stack->setCurrentIndex(index);
    } else {
        // find the tool action with this buttonGroupId and trigger it
        for (KoToolAction *toolAction : std::as_const(d->addedToolActions)) {
            if (toolAction->buttonGroupId() == id) {
                toolAction->trigger();
                return;
            }
        }
    }
}

void KoModeBox::slotContextMenuRequested(const QPoint &pos)
{
    QMenu menu;
    KSelectAction *textAction = new KSelectAction(i18n("Text"), &menu);
    connect(textAction, &KSelectAction::indexTriggered, this, &KoModeBox::switchIconMode);
    menu.addAction(textAction);
    textAction->addAction(i18n("Icon and Text"));
    textAction->addAction(i18n("Icon only"));
    textAction->setCurrentItem(d->iconMode);

    KSelectAction *buttonPositionAction = new KSelectAction(i18n("Sidebar side"), &menu);
    connect(buttonPositionAction, &KSelectAction::indexTriggered, this, [this](int side) {
        d->horizontalTabsSide = static_cast<HorizontalTabsSide>(side);
        switchTabsSide(d->horizontalTabsSide);
        KConfigGroup cfg = KSharedConfig::openConfig()->group("calligra");
        cfg.writeEntry("ModeBoxHorizontalTabsSide", (int)d->horizontalTabsSide);
        updateShownTools({});
    });
    menu.addAction(buttonPositionAction);
    buttonPositionAction->addAction(i18n("Left side"));
    buttonPositionAction->addAction(i18n("Right side"));
    buttonPositionAction->setCurrentItem(d->horizontalTabsSide);

    menu.exec(d->tabBar->mapToGlobal(pos));
}

void KoModeBox::switchIconMode(int mode)
{
    d->iconMode = static_cast<IconMode>(mode);
    setIconSize();
    updateShownTools(QList<QString>());

    KConfigGroup cfg = KSharedConfig::openConfig()->group("calligra");
    cfg.writeEntry("ModeBoxIconMode", (int)d->iconMode);
}

void KoModeBox::setIconSize() const
{
    if (d->iconMode == IconAndText) {
        d->tabBar->setIconSize(QSize(32, 64));
    } else {
        d->tabBar->setIconSize(QSize(32, 32));
    }
}
