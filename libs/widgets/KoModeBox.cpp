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
#include "KoRibbonLayout.h"

#include <KoCanvasBase.h>
#include <KoCanvasControllerWidget.h>
#include <KoSelection.h>
#include <KoShapeLayer.h>
#include <KoShapeManager.h>
#include <KoShapePropertyWidget.h>
#include <KoToolManager.h>
#include <KoToolBase.h>
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
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QProxyStyle>
#include <QRect>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSet>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionTabWidgetFrame>
#include <QTabBar>
#include <QTextLayout>
#include <QToolButton>
#include <QAction>
#include <QDynamicPropertyChangeEvent>
#include <QWheelEvent>
#include <QWidgetAction>

class KoModeBox::Private
{
public:
    Private(KoCanvasController *c)
        : canvas(c->canvas())
        , orientation(Qt::Vertical)
        , activeId(-1)
        , iconTextFitted(true)
        , fittingIterations(0)
        , iconMode(IconAndText)
        , horizontalTabsSide(LeftSide)
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
        bool afterToolTabs;
        /// tool tab after which this page is placed when both flags above are false
        QString afterToolId;
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
    QWidget *titleWidget = nullptr;
    QToolButton *collapseButton = nullptr;
    bool ribbonCollapsed = false;
    Qt::Orientation orientation;
    QList<KoToolAction *> toolActions; // toolActions maintained by toolmanager
    QList<KoToolAction *> addedToolActions; // tools in the order added to QToolBox
    QMap<int, QWidget *> addedWidgets;
    QMap<int, QSet<QWidget *>> pageAuxWidgets;
    QMap<int, QList<QPointer<QWidget>>> pageOptionWidgets;
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
    /// Set when the ribbon's structure (tabs, custom pages, icon mode) has
    /// changed and the next updateShownTools() call must rebuild it.
    bool ribbonDirty = true;
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

// Shape handling, Connect Shapes, and Draw Path share the Draw ribbon page in
// Words. Keep their tool ids and activation behavior separate while mapping
// them to one tab.
static bool isCombinedShapeTool(const QString &toolId)
{
    return toolId == QLatin1String("InteractionTool") || toolId == QLatin1String("ConnectionTool") || toolId == QLatin1String("CreatePathTool");
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

// Stage and Sheets own a fixed set of ribbon pages. Tool options are merged
// into those pages instead of creating a second set of legacy tabs.
static bool usesCustomRibbonPages(const QString &applicationName)
{
    return applicationName.contains(QLatin1String("stage"), Qt::CaseInsensitive)
        || applicationName.contains(QLatin1String("sheets"), Qt::CaseInsensitive);
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
    d->stack->setObjectName(QStringLiteral("ribbonCommands"));

    d->header = new QWidget(this);
    d->header->setObjectName(QStringLiteral("ribbonTabsRow"));
    d->header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    d->headerLayout = new QHBoxLayout(d->header);
    d->headerLayout->setContentsMargins({});
    d->headerLayout->setSpacing({});

    d->tabBar = new QTabBar;
    d->tabBar->setObjectName(QStringLiteral("ribbonTabs"));
    d->tabBar->setUsesScrollButtons(true);
    d->tabBar->setElideMode(Qt::ElideNone);
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

    if (applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive)) {
        const auto ribbonOrder = [](const KoToolAction *toolAction) {
            if (toolAction->id() == QLatin1String("TextToolFactory_ID")) {
                return 0;
            }
            if (toolAction->id() == QLatin1String("PageToolFactory_ID")) {
                return 10;
            }
            if (toolAction->id() == QLatin1String("ReferencesToolFactory_ID")) {
                return 20;
            }
            if (toolAction->id() == QLatin1String("ReviewToolFactory_ID")) {
                return 30;
            }
            return 40;
        };
        std::stable_sort(d->toolActions.begin(), d->toolActions.end(), [&ribbonOrder](const KoToolAction *a, const KoToolAction *b) {
            const int orderA = ribbonOrder(a);
            const int orderB = ribbonOrder(b);
            return orderA == orderB ? compareToolActions(a, b) : orderA < orderB;
        });
    } else {
        std::sort(d->toolActions.begin(), d->toolActions.end(), compareToolActions);
    }

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
    const bool wordRibbon = horizontal && (applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive)
        || usesCustomRibbonPages(applicationName));
    setObjectName(wordRibbon ? QStringLiteral("wordsRibbon") : QStringLiteral("modeBox"));
    if (wordRibbon) {
        d->stack->setMinimumHeight(128);
        QPalette ribbonPalette = palette();
        ribbonPalette.setColor(QPalette::Window, ribbonPalette.color(QPalette::Base));
        setPalette(ribbonPalette);
        setAutoFillBackground(true);
        // Words, Stage and Sheets share the Office treatment; other applications keep
        // their native tool docker appearance and palette.
        setStyleSheet(QStringLiteral(
            "#wordsRibbon, #ribbonTabsRow { background: palette(base); }"
            "#ribbonCommands QWidget { background-color: palette(base); }"
            "QWidget[ribbonRoutedGroup=\"true\"] { border-right: 1px solid palette(midlight); }"
            "#ribbonTabs::tab { padding: 7px 12px; border: 0; background: transparent; }"
            "#ribbonTabs::tab:selected { color: palette(highlight); border-bottom: 3px solid palette(highlight); padding-bottom: 4px; }"
            "#ribbonTabs::tab:hover { background: palette(alternate-base); }"
            "#ribbonCommands QToolButton { border: 1px solid transparent; border-radius: 3px; padding: 3px; }"
            "#ribbonCommands QToolButton:hover { background: palette(alternate-base); border-color: palette(mid); }"
            "#ribbonCommands QToolButton:checked { background: palette(highlight); color: palette(highlighted-text); }"
            "#ribbonCommands QComboBox, #ribbonCommands QAbstractSpinBox { min-height: 22px; }"
            "QLabel[ribbonGroupTitle=\"true\"] { color: palette(text); font-size: 11px; padding-top: 3px; }"));
        if (!d->collapseButton) {
            d->collapseButton = new QToolButton(d->header);
            d->collapseButton->setObjectName(QStringLiteral("ribbonCollapseButton"));
            d->collapseButton->setAutoRaise(true);
            auto *collapse = new QAction(i18n("Collapse Ribbon"), this);
            collapse->setCheckable(true);
            collapse->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F1));
            collapse->setShortcutContext(Qt::WindowShortcut);
            addAction(collapse);
            d->collapseButton->setDefaultAction(collapse);
            d->collapseButton->setArrowType(Qt::UpArrow);
            connect(collapse, &QAction::toggled, this, &KoModeBox::setRibbonCollapsed);
            connect(d->tabBar, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
                if (index >= 0) {
                    setRibbonCollapsed(!d->ribbonCollapsed);
                }
            });
            connect(d->tabBar, &QTabBar::tabBarClicked, this, [this](int index) {
                if (index >= 0 && d->ribbonCollapsed) {
                    setRibbonCollapsed(false);
                }
            });
            d->headerLayout->addWidget(d->collapseButton);
        }
    } else {
        setStyleSheet(QString());
    }
    if (d->collapseButton) {
        d->collapseButton->setVisible(wordRibbon);
    }
    if (d->titleWidget) {
        d->titleWidget->setVisible(wordRibbon);
    }

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

    d->ribbonDirty = true;
    updateShownTools(d->lastShownCodes);
    if (!d->lastOptionWidgets.isEmpty()) {
        setOptionWidgets(d->lastOptionWidgets);
    }
}

void KoModeBox::addToolAction(KoToolAction *toolAction)
{
    d->toolActions.append(toolAction);
}

// Populate a passive ribbon page with the live controls of an existing tool.
// The tool is not activated: only its option widgets are created and handed
// to the page, so a page can show a tool's controls without switching the
// active tool on the canvas.
void KoModeBox::primeToolOptions(const QString &toolId)
{
    if (toolId.isEmpty() || !d->canvas) {
        return;
    }
    KoToolBase *tool = KoToolManager::instance()->toolById(d->canvas, toolId);
    if (!tool) {
        return;
    }
    const QList<QPointer<QWidget>> widgets = tool->optionWidgets();
    if (widgets.isEmpty()) {
        return;
    }
    if (d->orientation == Qt::Horizontal && usesCustomRibbonPages(applicationName)) {
        routeOfficeOptions(toolId, widgets);
        return;
    }
    // Route the tool's controls to the page that asked for them, so they are
    // laid out by setOptionWidgets() like any other option widget.
    for (const QPointer<QWidget> &widget : widgets) {
        if (widget) {
            widget->setProperty("ribbonTargetPage", QString());
        }
    }
    d->lastOptionWidgets = widgets;
    d->pageOptionWidgets[d->activeId] = widgets;
}

// Show a ribbon page by its object name and make it the current tab.
void KoModeBox::showRibbonPage(const QString &objectName)
{
    if (objectName.isEmpty()) {
        return;
    }
    for (const Private::CustomPage &customPage : std::as_const(d->customPages)) {
        if (!customPage.page || customPage.page->objectName() != objectName) {
            continue;
        }
        const int index = d->stack->indexOf(customPage.wrapper);
        if (index < 0) {
            break;
        }
        d->tabBar->setCurrentIndex(index);
        d->stack->setCurrentIndex(index);
        // A page can name the tool whose controls it shows by default.
        const QString defaultTool = customPage.page->property("ribbonDefaultTool").toString();
        if (!defaultTool.isEmpty()) {
            primeToolOptions(defaultTool);
        }
        break;
    }
}

void KoModeBox::setActiveTool(KoCanvasController *canvas, int id)
{
    if (canvas->canvas() == d->canvas) {
        if (d->orientation == Qt::Horizontal && usesCustomRibbonPages(applicationName)) {
            // Selecting a canvas tool updates its controls without replacing
            // the ribbon page the user is currently browsing.
            d->activeId = id;
            return;
        }
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

void KoModeBox::addCustomPage(const QString &title,
                              const QIcon &icon,
                              const QString &toolTip,
                              QWidget *page,
                              bool beforeToolTabs,
                              bool afterToolTabs,
                              const QString &afterToolId)
{
    if (!page) {
        return;
    }
    // Historical default: pages that are neither before nor after the tool
    // tabs go directly after the Home tab of Words.
    QString anchor = afterToolId;
    if (!beforeToolTabs && !afterToolTabs && anchor.isEmpty()) {
        anchor = QLatin1String("TextToolFactory_ID");
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

    d->customPages.append({title, toolTip, icon, page, wrapper, beforeToolTabs, afterToolTabs, anchor});
    page->installEventFilter(this);
    d->ribbonDirty = true;
    updateShownTools(d->lastShownCodes);
}

bool KoModeBox::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::DynamicPropertyChange
        && static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName() == "ribbonContextActive") {
        for (const auto &customPage : std::as_const(d->customPages)) {
            if (customPage.page != watched || !customPage.wrapper) {
                continue;
            }
            const int index = d->stack->indexOf(customPage.wrapper);
            if (index < 0) {
                break;
            }
            const QVariant context = watched->property("ribbonContextActive");
            const bool visible = !context.isValid() || context.toBool();
            const bool returnToTool = !visible && d->tabBar->currentIndex() == index;
            const QSignalBlocker blocker(d->tabBar);
            d->tabBar->setTabVisible(index, visible);
            if (returnToTool) {
                // Hiding contextual controls must not activate another tool
                // or move the cursor. Return to the current tool's ribbon.
                if (usesCustomRibbonPages(applicationName)) {
                    showRibbonPage(applicationName.contains(QLatin1String("sheets"), Qt::CaseInsensitive)
                        ? QStringLiteral("sheetsHomePage") : QStringLiteral("stageHomePage"));
                }
                for (int i = 0; i < d->tabBar->count(); ++i) {
                    if (d->tabBar->isTabVisible(i) && d->tabBar->tabData(i).toInt() == d->activeId) {
                        d->tabBar->setCurrentIndex(i);
                        break;
                    }
                }
            }
            d->stack->setCurrentIndex(d->tabBar->currentIndex());
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
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
            if (d->orientation == Qt::Horizontal) {
                auto *group = new QWidget(page);
                group->setProperty("ribbonRoutedGroup", true);
                auto *groupLayout = new QVBoxLayout(group);
                groupLayout->setContentsMargins(4, 1, 4, 1);
                groupLayout->addWidget(widget, 0, Qt::AlignTop);
                groupLayout->addStretch();
                auto *title = new QLabel(widget->windowTitle(), group);
                title->setProperty("ribbonGroupTitle", true);
                title->setMinimumHeight(18);
                title->setAlignment(Qt::AlignCenter);
                groupLayout->addWidget(title);
                group->setMinimumHeight(110);
                const bool hasStretch = boxLayout->count() > 0 && boxLayout->itemAt(boxLayout->count() - 1)->spacerItem();
                boxLayout->insertWidget(hasStretch ? boxLayout->count() - 1 : boxLayout->count(), group);
                if (!hasStretch) {
                    boxLayout->addStretch();
                }
                d->pageAuxWidgets[toolAction->buttonGroupId()].insert(group);
            } else {
                boxLayout->insertWidget(0, widget);
            }
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
    const int index = d->collapseButton ? d->headerLayout->indexOf(d->collapseButton) : d->headerLayout->count();
    d->headerLayout->insertWidget(index, widget, 0, Qt::AlignVCenter);
}

void KoModeBox::setTitleWidget(QWidget *widget)
{
    if (!widget || d->titleWidget == widget) {
        return;
    }
    if (d->titleWidget) {
        d->titleWidget->deleteLater();
    }
    d->titleWidget = widget;
    d->layout->insertWidget(0, widget);
    widget->setVisible(d->orientation == Qt::Horizontal);
}

void KoModeBox::setRibbonCollapsed(bool collapsed)
{
    if (d->orientation != Qt::Horizontal || !d->collapseButton) {
        return;
    }
    d->ribbonCollapsed = collapsed;
    d->stack->setVisible(!collapsed);
    d->collapseButton->defaultAction()->setChecked(collapsed);
    d->collapseButton->defaultAction()->setText(collapsed ? i18n("Expand Ribbon") : i18n("Collapse Ribbon"));
    d->collapseButton->setArrowType(collapsed ? Qt::DownArrow : Qt::UpArrow);
    // A dock retains its previous height unless both limits are updated.
    if (auto *dock = qobject_cast<QDockWidget *>(parentWidget())) {
        dock->setMinimumHeight(0);
        dock->setMaximumHeight(collapsed ? sizeHint().height() : QWIDGETSIZE_MAX);
        if (auto *window = qobject_cast<QMainWindow *>(dock->parentWidget())) {
            window->resizeDocks({dock}, {sizeHint().height()}, Qt::Vertical);
        }
    }
}


void KoModeBox::appendCustomPages(bool beforeToolTabs, bool afterToolTabs, const QString &afterToolId)
{
    const bool wasBlocked = d->tabBar->blockSignals(true);
    for (const Private::CustomPage &customPage : std::as_const(d->customPages)) {
        if (!customPage.page || !customPage.wrapper || customPage.beforeToolTabs != beforeToolTabs || customPage.afterToolTabs != afterToolTabs) {
            continue;
        }
        if (!beforeToolTabs && !afterToolTabs && customPage.afterToolId != afterToolId) {
            continue;
        }
        const bool wordRibbon = d->orientation == Qt::Horizontal && applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive);
        const int index = d->tabBar->addTab(wordRibbon ? QIcon() : customPage.icon, customPage.title);
        d->tabBar->setTabToolTip(index, customPage.toolTip);
        d->tabBar->setTabData(index, Private::customPageMarker);
        d->stack->addWidget(customPage.wrapper);
        const QVariant context = customPage.page->property("ribbonContextActive");
        d->tabBar->setTabVisible(index, !context.isValid() || context.toBool());
    }
    d->tabBar->blockSignals(wasBlocked);
}

void KoModeBox::appendLeftoverCustomPages()
{
    const bool wasBlocked = d->tabBar->blockSignals(true);
    for (const Private::CustomPage &customPage : std::as_const(d->customPages)) {
        if (!customPage.page || !customPage.wrapper || customPage.beforeToolTabs || customPage.afterToolTabs) {
            continue;
        }
        if (d->stack->indexOf(customPage.wrapper) >= 0) {
            continue; // already placed after its anchor tool tab
        }
        const bool wordRibbon = d->orientation == Qt::Horizontal && applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive);
        const int index = d->tabBar->addTab(wordRibbon ? QIcon() : customPage.icon, customPage.title);
        d->tabBar->setTabToolTip(index, customPage.toolTip);
        d->tabBar->setTabData(index, Private::customPageMarker);
        d->stack->addWidget(customPage.wrapper);
        const QVariant context = customPage.page->property("ribbonContextActive");
        d->tabBar->setTabVisible(index, !context.isValid() || context.toBool());
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
        if (auto boxLayout = qobject_cast<QBoxLayout *>(layout); boxLayout && !widget->isAncestorOf(toolTabWidget.widget)) {
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
                // Drawing tools and shape formatting share one tab.
                tabTitle = i18n("Draw");
            }
        }
        const QString iconName = combineShapeTools && isCombinedShapeTool(toolAction->id()) ? QStringLiteral("draw-freehand") : toolAction->iconName();
        const int index = d->tabBar->addTab(combineShapeTools ? QIcon() : QIcon::fromTheme(iconName), tabTitle);
        const QString tabToolTip = combineShapeTools && isCombinedShapeTool(toolAction->id())
            ? i18n("Draw, connect, and format shapes")
            : toolAction->toolTip();
        d->tabBar->setTabToolTip(index, tabToolTip);
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
    // Selecting text or moving the cursor in a large document emits
    // toolCodesSelected() very frequently.  Rebuilding every tab, scroll area
    // and custom page on each of those signals made the whole ribbon flicker.
    // Only rebuild when the visible tool set can actually have changed; the
    // active tab is kept in sync by setActiveTool() in the meantime.
    QList<QString> sortedCodes = codes;
    QList<QString> sortedLastCodes = d->lastShownCodes;
    std::sort(sortedCodes.begin(), sortedCodes.end());
    std::sort(sortedLastCodes.begin(), sortedLastCodes.end());
    if (!d->ribbonDirty && sortedCodes == sortedLastCodes && d->iconTextFitted
        && (!d->addedToolActions.isEmpty() || !d->customPages.isEmpty())) {
        return;
    }
    d->ribbonDirty = false;

    const QPointer<QWidget> selectedCustomPage = d->tabBar->currentIndex() >= 0
            && d->tabBar->tabData(d->tabBar->currentIndex()).toInt() == Private::customPageMarker
        ? d->stack->currentWidget() : nullptr;
    d->lastShownCodes = codes;
    if (d->iconTextFitted) {
        d->fittingIterations = 0;
    }
    d->iconTextFitted = true;

    const bool wasUpdatesEnabled = updatesEnabled();
    if (wasUpdatesEnabled) {
        setUpdatesEnabled(false);
    }

    d->tabBar->blockSignals(true);

    while (d->tabBar->count()) {
        d->tabBar->removeTab(0);
        d->stack->removeWidget(d->stack->widget(0));
    }

    d->addedToolActions.clear();

    appendCustomPages(true);

    const bool officeWordRibbon = d->orientation == Qt::Horizontal && applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive);
    QList<KoToolAction *> contextualShapeTools;
    if (officeWordRibbon) {
        for (KoToolAction *toolAction : std::as_const(d->toolActions)) {
            if (isCombinedShapeTool(toolAction->id())) {
                contextualShapeTools.append(toolAction);
            }
        }
        std::stable_sort(contextualShapeTools.begin(), contextualShapeTools.end(), [](const KoToolAction *a, const KoToolAction *b) {
            const auto toolOrder = [](const QString &toolId) {
                if (toolId == QLatin1String("ConnectionTool")) {
                    return 0;
                }
                if (toolId == QLatin1String("InteractionTool")) {
                    return 1;
                }
                return 2;
            };
            return toolOrder(a->id()) < toolOrder(b->id());
        });
    }
    const auto appendToolAction = [this, officeWordRibbon](KoToolAction *toolAction) {
        if (d->orientation == Qt::Horizontal && usesCustomRibbonPages(applicationName)) {
            // Named ribbon pages own the native tool controls.
            // Do not append a second tab for each legacy tool.
            return;
        }
        if (officeWordRibbon && isCombinedShapeTool(toolAction->id())) {
            // The combined Draw tab is inserted immediately after Insert.
        } else {
            addItem(toolAction);
        }
    };

    foreach (KoToolAction *toolAction, d->toolActions) {
        const QString toolCodes = toolAction->visibilityCode();
        if (officeWordRibbon && toolAction->id() == QLatin1String("PathToolFactoryId")) {
            // The Words ribbon hides the path editing tab completely; its
            // useful "Convert to Path" action lives in the Draw tab instead.
            continue;
        }
        if (toolAction->section().contains(applicationName)) {
            appendToolAction(toolAction);
            // Insert the custom pages that are anchored after this tool tab,
            // e.g. Insert directly after Home in Words.
            if (!(officeWordRibbon && isCombinedShapeTool(toolAction->id()))) {
                appendCustomPages(false, false, toolAction->id());
            }
            if (officeWordRibbon && toolAction->id() == QLatin1String("TextToolFactory_ID")) {
                // Word's ribbon order is Home, Insert, Draw, Design, Layout.
                // Shape tools stay grouped into one tab, with the Design page
                // anchored immediately after that combined tab.
                for (KoToolAction *shapeTool : std::as_const(contextualShapeTools)) {
                    addItem(shapeTool);
                }
                appendCustomPages(false, false, QStringLiteral("ConnectionTool"));
            }
            continue;
        } else if (!toolAction->section().contains("dynamic") && !toolAction->section().contains("main")) {
            continue;
        }
        if (toolCodes.startsWith(QLatin1String("flake/"))) {
            appendToolAction(toolAction);
            continue;
        }

        if (toolCodes.endsWith(QLatin1String("/always"))) {
            appendToolAction(toolAction);
            continue;
        } else if (toolCodes.isEmpty() && codes.count() != 0) {
            appendToolAction(toolAction);
            continue;
        } else {
            foreach (const QString &shapeCode, codes) {
                if (toolCodes.contains(shapeCode)) {
                    appendToolAction(toolAction);
                    break;
                }
            }
        }
    }
    // Custom pages whose anchor tool tab never appeared are not lost.
    appendLeftoverCustomPages();
    // Keep View and Help after the standard tabs.
    appendCustomPages(false, true);

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
    const int customIndex = selectedCustomPage ? d->stack->indexOf(selectedCustomPage) : -1;
    if (customIndex >= 0 && d->tabBar->isTabVisible(customIndex)) {
        d->tabBar->setCurrentIndex(customIndex);
        d->stack->setCurrentIndex(customIndex);
    }
    d->tabBar->blockSignals(false);

    if (wasUpdatesEnabled) {
        setUpdatesEnabled(true);
    }

    if (!d->iconTextFitted && d->fittingIterations++ < 8) {
        updateShownTools(codes);
    }
    d->iconTextFitted = true;
}

// Keep native controls alive in Stage and Sheets pages, just as Words
// routes insert/table controls into its custom ribbon pages. Repeated tool
// notifications reuse the same groups instead of duplicating their widgets.
void KoModeBox::routeOfficeOptions(const QString &toolId, const QList<QPointer<QWidget>> &widgets)
{
    const bool spreadsheet = applicationName.contains(QLatin1String("sheets"), Qt::CaseInsensitive);
    const QString prefix = spreadsheet ? QStringLiteral("sheets") : QStringLiteral("stage");
    QString defaultPage = prefix + QStringLiteral("ShapeFormatPage");
    if (toolId == QLatin1String("KSpreadCellToolId")
        || (!spreadsheet && toolId == QLatin1String("TextToolFactory_ID")))
        defaultPage = prefix + QStringLiteral("HomePage");
    else if (toolId == QLatin1String("KoPABackgroundTool"))
        defaultPage = prefix + QStringLiteral("DesignPage");
    else if (toolId == QLatin1String("ConnectionTool") || toolId == QLatin1String("CreatePathTool")
             || toolId == QLatin1String("KoPencilTool") || toolId == QLatin1String("PathToolFactoryId"))
        defaultPage = prefix + QStringLiteral("DrawPage");

    for (QWidget *widget : widgets) {
        if (!widget || widget->property("ribbonManagedExternally").toBool())
            continue;
        const QString target = widget->property("ribbonTargetPage").toString();
        for (const auto &customPage : std::as_const(d->customPages)) {
            if (!customPage.page || customPage.page->objectName() != (target.isEmpty() ? defaultPage : target))
                continue;
            // QWidgetAction may temporarily detach its gallery from the menu.
            // Remember its destination as well as checking widget ancestry.
            if (customPage.page->isAncestorOf(widget)
                || widget->property("ribbonRoutedPage").toString() == customPage.page->objectName())
                break;
            widget->setProperty("ribbonRoutedPage", customPage.page->objectName());
            if (widget->objectName() == QLatin1String("Slide Layouts")) {
                auto *slides = customPage.page->findChild<QWidget *>(QStringLiteral("stageSlidesGroup"));
                auto *grid = slides ? slides->findChild<QGridLayout *>() : nullptr;
                if (!grid) {
                    widget->setProperty("ribbonRoutedPage", QString());
                    break; // Populate the gallery once the Slides group exists.
                }
                auto *button = new QToolButton(slides);
                button->setObjectName(QStringLiteral("stageSlideLayout"));
                button->setText(i18n("Layout"));
                button->setIcon(QIcon::fromTheme(QStringLiteral("view-grid")));
                auto *menu = new QMenu(button);
                auto *action = new QWidgetAction(menu);
                widget->setMinimumSize(320, 240);
                action->setDefaultWidget(widget);
                menu->addAction(action);
                button->setMenu(menu);
                button->setPopupMode(QToolButton::InstantPopup);
                KoRibbonLayout::configureButton(button, Qt::ToolButtonTextBesideIcon);
                grid->addWidget(button, 2, 1, Qt::AlignLeft);
                break;
            }
            auto *layout = qobject_cast<QHBoxLayout *>(customPage.page->layout());
            if (!layout) {
                layout = new QHBoxLayout(customPage.page);
                layout->setContentsMargins(4, 4, 4, 2);
                layout->addStretch();
            }
            auto *group = new QWidget(customPage.page);
            group->setProperty("ribbonRoutedGroup", true);
            group->setProperty("ribbonGroupOrder", widget->property("ribbonGroupOrder"));
            auto *vertical = new QVBoxLayout(group);
            vertical->setContentsMargins(8, 0, 8, 0);
            vertical->addWidget(widget, 0, Qt::AlignTop);
            vertical->addStretch();
            auto *caption = new QLabel(widget->windowTitle(), group);
            caption->setProperty("ribbonGroupTitle", true);
            caption->setAlignment(Qt::AlignCenter);
            caption->setMinimumHeight(18);
            vertical->addWidget(caption);
            const int order = widget->property("ribbonGroupOrder").isValid()
                ? widget->property("ribbonGroupOrder").toInt() : 1000;
            int position = 0;
            while (position < layout->count() && layout->itemAt(position)->widget()) {
                const QVariant other = layout->itemAt(position)->widget()->property("ribbonGroupOrder");
                if ((other.isValid() ? other.toInt() : 1000) > order)
                    break;
                ++position;
            }
            layout->insertWidget(position, group);
            widget->show();
            break;
        }
    }
}

void KoModeBox::setOptionWidgets(const QList<QPointer<QWidget>> &optionWidgetList)
{
    if (d->orientation == Qt::Horizontal && usesCustomRibbonPages(applicationName)) {
        for (const KoToolAction *toolAction : std::as_const(d->toolActions)) {
            if (toolAction->buttonGroupId() == d->activeId) {
                routeOfficeOptions(toolAction->id(), optionWidgetList);
                break;
            }
        }
        return;
    }
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

    // The option widgets are often re-announced while the same tool stays
    // active (every cursor/selection change can trigger it).  Rebuilding the
    // page layout in that case makes the ribbon flash and steals focus from
    // the canvas, so keep the existing layout when nothing really changed.
    bool optionWidgetsUnchanged = d->pageOptionWidgets.contains(pageId)
        && d->pageOptionWidgets[pageId] == optionWidgetList;
    if (optionWidgetsUnchanged) {
        for (const QPointer<QWidget> &widget : optionWidgetList) {
            if (!widget || page->isAncestorOf(widget)) {
                continue;
            }
            const QString targetPage = widget->property("ribbonTargetPage").toString();
            bool routed = false;
            if (!targetPage.isEmpty()) {
                for (const auto &customPage : std::as_const(d->customPages)) {
                    if (customPage.page && customPage.page->objectName() == targetPage
                        && customPage.page->isAncestorOf(widget)) {
                        routed = true;
                        break;
                    }
                }
            }
            if (!routed) {
                optionWidgetsUnchanged = false;
                break;
            }
        }
    }
    if (optionWidgetsUnchanged) {
        d->lastOptionWidgets = optionWidgetList;
        return;
    }

    for (const QPointer<QWidget> &widget : std::as_const(d->pageOptionWidgets[pageId])) {
        if (widget && (!combineShapeTools || widget->property("ribbonTargetPage").toString().isEmpty())) {
            widget->setParent(page);
        }
    }
    for (const Private::ToolTabWidget &toolTabWidget : std::as_const(d->toolTabWidgets)) {
        if ((toolTabWidget.toolId == activeToolId || (combineShapeTools && isCombinedShapeTool(toolTabWidget.toolId) && isCombinedShapeTool(activeToolId))) && toolTabWidget.widget) {
            toolTabWidget.widget->setParent(page);
        }
    }
    d->lastOptionWidgets = optionWidgetList;
    d->pageOptionWidgets[pageId] = optionWidgetList;

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
    qDeleteAll(d->pageAuxWidgets[pageId]);
    d->pageAuxWidgets[pageId].clear();

    if (d->orientation == Qt::Horizontal) {
        const bool expandShapeOptions = combineShapeTools && isCombinedShapeTool(activeToolId) && !optionWidgetList.isEmpty();
        QList<QWidget *> ribbonWidgets;
        for (const Private::ToolTabWidget &toolTabWidget : std::as_const(d->toolTabWidgets)) {
            if ((toolTabWidget.toolId == activeToolId
                 || (combineShapeTools && isCombinedShapeTool(toolTabWidget.toolId) && isCombinedShapeTool(activeToolId)))
                && toolTabWidget.widget) {
                ribbonWidgets.append(toolTabWidget.widget);
            }
        }
        for (QWidget *widget : optionWidgetList) {
            if (combineShapeTools && widget && !widget->property("ribbonTargetPage").toString().isEmpty()) {
                const QString target = widget->property("ribbonTargetPage").toString();
                bool routed = false;
                for (const auto &customPage : std::as_const(d->customPages)) {
                    if (!customPage.page || customPage.page->objectName() != target) {
                        continue;
                    }
                    routed = true;
                    if (!customPage.page->isAncestorOf(widget)) {
                        auto *targetLayout = qobject_cast<QHBoxLayout *>(customPage.page->layout());
                        if (!targetLayout) {
                            targetLayout = new QHBoxLayout(customPage.page);
                            targetLayout->setContentsMargins(4, 4, 4, 2);
                            targetLayout->addStretch();
                        }
                        auto *group = new QWidget(customPage.page);
                        group->setProperty("ribbonRoutedGroup", true);
                        auto *groupLayout = new QVBoxLayout(group);
                        groupLayout->setContentsMargins(8, 0, 8, 0);
                        groupLayout->addWidget(widget, 0, Qt::AlignTop);
                        groupLayout->addStretch();
                        auto *title = new QLabel(widget->windowTitle(), group);
                        title->setProperty("ribbonGroupTitle", true);
                        title->setMinimumHeight(18);
                        title->setAlignment(Qt::AlignCenter);
                        groupLayout->addWidget(title);
                        targetLayout->insertWidget(qMax(0, targetLayout->count() - 1), group);
                        widget->show();
                    }
                    break;
                }
                if (routed) {
                    continue;
                }
            }
            if (widget && !ribbonWidgets.contains(widget)) {
                ribbonWidgets.append(widget);
            }
        }

        // Order the ribbon groups like the Microsoft Office ribbon when the
        // widgets declare a "ribbonGroupOrder" property; widgets without the
        // property keep their relative order at the end.
        const auto ribbonGroupOrder = [](QWidget *widget) {
            bool ok = false;
            const int order = widget->property("ribbonGroupOrder").toInt(&ok);
            return ok ? order : 1000;
        };
        std::stable_sort(ribbonWidgets.begin(), ribbonWidgets.end(), [&ribbonGroupOrder](QWidget *a, QWidget *b) {
            return ribbonGroupOrder(a) < ribbonGroupOrder(b);
        });

        // Give every ribbon group the same vertical track. Without this, a
        // short option panel (such as Paragraph) puts its group label higher
        // than a taller panel (such as Page Setup), making one tab look like
        // its controls are split across different rows.
        int ribbonGroupContentHeight = 0;
        for (QWidget *widget : std::as_const(ribbonWidgets)) {
            ribbonGroupContentHeight = qMax(ribbonGroupContentHeight,
                                             qMax(widget->sizeHint().height(), widget->minimumSizeHint().height()));
        }

        bool first = true;
        for (QWidget *widget : std::as_const(ribbonWidgets)) {
            if (!first) {
                auto separator = new QFrame(page);
                separator->setFrameStyle(QFrame::VLine);
                separator->setFixedWidth(1);
                layout->addWidget(separator);
                d->pageAuxWidgets[pageId].insert(separator);
            }
            first = false;

            auto group = new QWidget(page);
            auto groupLayout = new QVBoxLayout(group);
            groupLayout->setContentsMargins(1, 0, 1, 0);
            groupLayout->setSpacing(1);
            groupLayout->setSizeConstraint(QLayout::SetMinimumSize);

            QLabel *sectionTitle = nullptr;
            if (!widget->windowTitle().isEmpty()) {
                sectionTitle = new QLabel(widget->windowTitle(), group);
                sectionTitle->setProperty("ribbonGroupTitle", true);
                sectionTitle->setAlignment(Qt::AlignHCenter);
                QFont sectionFont = sectionTitle->font();
                sectionFont.setPointSizeF(qMax(7.0, sectionFont.pointSizeF() - 1.0));
                sectionTitle->setFont(sectionFont);
                sectionTitle->setMinimumHeight(18);
                QPalette palette = sectionTitle->palette();
                auto foregroundColor = palette.color(QPalette::WindowText);
                foregroundColor.setAlphaF(0.8);
                palette.setColor(QPalette::WindowText, foregroundColor);
                sectionTitle->setPalette(palette);
            }
            groupLayout->addWidget(widget, 0, Qt::AlignTop);
            widget->show();

            if (sectionTitle) {
                groupLayout->addStretch(1);
                groupLayout->addWidget(sectionTitle);
            }
            group->setMinimumHeight(ribbonGroupContentHeight + (sectionTitle ? 18 : 0));

            const bool isShapeOption = expandShapeOptions && optionWidgetList.contains(widget);
            if (isShapeOption) {
                QSizePolicy sizePolicy = widget->sizePolicy();
                sizePolicy.setHorizontalPolicy(QSizePolicy::Expanding);
                widget->setSizePolicy(sizePolicy);
                group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            }
            if (expandShapeOptions) {
                group->setMinimumWidth(group->sizeHint().width());
            }
            layout->addWidget(group, isShapeOption ? 1 : 0);
            d->pageAuxWidgets[pageId].insert(group);
        }
        if (!expandShapeOptions) {
            layout->addStretch();
        } else {
            layout->activate();
            page->setMinimumWidth(layout->minimumSize().width());
        }
    } else {
        auto title = new KTitleWidget;
        title->setText(d->tabBar->tabToolTip(d->tabBar->currentIndex()));
        layout->addWidget(title);
        d->pageAuxWidgets[pageId].insert(title);

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
                d->pageAuxWidgets[pageId].insert(sectionTitle);

                auto separator = new QFrame;
                separator->setFrameStyle(QFrame::HLine);
                separator->setFixedHeight(1);
                separator->setPalette(palette);
                layout->addWidget(separator);
                d->pageAuxWidgets[pageId].insert(separator);

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
    if (!widget()) {
        return;
    }
    if (horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff) {
        setMinimumWidth(widget()->minimumSizeHint().width() + (verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 0));
    } else {
        if (widget()->layout()) {
            widget()->layout()->activate();
            const int contentWidth = qMax(widget()->minimumSizeHint().width(), widget()->layout()->minimumSize().width());
            widget()->setMinimumWidth(contentWidth);
        }
        setMinimumHeight(qMax(0, widget()->minimumSizeHint().height()) + (horizontalScrollBar()->isVisible() ? horizontalScrollBar()->height() : 0));
    }
}

void ScrollArea::wheelEvent(QWheelEvent *event)
{
    const QPoint delta = event->pixelDelta().isNull() ? event->angleDelta() : event->pixelDelta();
    int horizontalDelta = delta.x();
    if (!horizontalDelta && event->modifiers().testFlag(Qt::ShiftModifier)) {
        horizontalDelta = delta.y();
    }
    if (horizontalDelta && horizontalScrollBar()->maximum() > 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - horizontalDelta);
        event->accept();
        return;
    }
    QScrollArea::wheelEvent(event);
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

        d->ribbonDirty = true;
        updateShownTools(QList<QString>());
    }
}

// Make sure the selection contains a shape the tool can work on.  Tools like
// the text-family tools (References, Review, ...) only activate on a text
// shape; with an empty or object selection they immediately hand control back,
// which used to make the ribbon jump to the shape tools (Draw) tab when their
// own tab was clicked.  Selecting a valid target first (e.g. the page text
// frame) keeps the tool active so the clicked tab stays open.
static bool selectToolTarget(KoCanvasBase *canvas, KoToolAction *toolAction)
{
    if (!canvas || !toolAction) {
        return false;
    }
    const QString codes = toolAction->visibilityCode();
    if (codes.isEmpty() || codes.startsWith(QLatin1String("flake/"))) {
        // The tool works with any (or no) selection.
        return false;
    }
    KoShapeManager *manager = canvas->shapeManager();
    if (!manager) {
        return false;
    }
    const QStringList wanted = codes.split(QLatin1Char(','));
    KoSelection *selection = manager->selection();
    for (KoShape *shape : selection->selectedShapes()) {
        QSet<KoShape *> delegates = shape->toolDelegates();
        if (delegates.isEmpty()) {
            delegates << shape;
        }
        for (KoShape *delegate : delegates) {
            if (wanted.contains(delegate->shapeId())) {
                return true;
            }
        }
    }
    // Nothing usable selected: select the first top level shape the tool can
    // work on; for text tools that is the page text frame.
    for (KoShape *shape : manager->topLevelShapes()) {
        if (wanted.contains(shape->shapeId())) {
            selection->deselectAll();
            selection->select(shape);
            return true;
        }
    }
    return false;
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
        if (d->orientation == Qt::Horizontal && usesCustomRibbonPages(applicationName)) {
            for (const auto &customPage : std::as_const(d->customPages)) {
                if (customPage.wrapper != d->stack->currentWidget() || !customPage.page)
                    continue;
                const QString toolId = customPage.page->property("ribbonActivateTool").toString();
                if (!toolId.isEmpty())
                    KoToolManager::instance()->switchToolRequested(toolId);
                const QString defaultTool = customPage.page->property("ribbonDefaultTool").toString();
                if (!defaultTool.isEmpty())
                    primeToolOptions(defaultTool);
                break;
            }
        }
    } else {
        // Set the page before triggering the tool. If this tab's tool is
        // already active, trigger() emits no activation change and otherwise
        // leaves the last selected custom page visible.
        d->stack->setCurrentIndex(index);
        const bool combinedShapeToolsTab = std::any_of(d->toolActions.cbegin(), d->toolActions.cend(), [this, id](const KoToolAction *toolAction) {
            return isCombinedShapeTool(toolAction->id()) && ribbonGroupId(toolAction, d->toolActions, true) == id;
        });
        const bool layoutToolTab = std::any_of(d->toolActions.cbegin(), d->toolActions.cend(), [id](const KoToolAction *toolAction) {
            return toolAction->id() == QLatin1String("PageToolFactory_ID") && toolAction->buttonGroupId() == id;
        });
        const bool passiveRibbonToolTab = d->orientation == Qt::Horizontal && applicationName.contains(QLatin1String("words"), Qt::CaseInsensitive)
            && (combinedShapeToolsTab || layoutToolTab);
        if (passiveRibbonToolTab) {
            // Opening the Draw tab auto-selects its "Select" tool so that
            // clicking a shape selects it.  Layout shows its controls without
            // applying a tool to the document; its individual controls remain
            // active.
            if (combinedShapeToolsTab) {
                for (KoToolAction *toolAction : std::as_const(d->addedToolActions)) {
                    if (toolAction->id() == QLatin1String("InteractionTool")) {
                        toolAction->trigger();
                        break;
                    }
                }
            }
            return;
        }
        // find the tool action with this buttonGroupId and trigger it
        for (KoToolAction *toolAction : std::as_const(d->addedToolActions)) {
            if (toolAction->buttonGroupId() == id) {
                selectToolTarget(d->canvas, toolAction);
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
        d->ribbonDirty = true;
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
    d->ribbonDirty = true;
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
