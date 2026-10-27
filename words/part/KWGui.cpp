/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2001 David Faure <faure@kde.org>
 * SPDX-FileCopyrightText: 2005-2007, 2009 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2010 Boudewijn Rempt <boud@kogmbh.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWGui.h"
#include "KWCanvas.h"
#include "KWDocument.h"
#include "KWPage.h"
#include "KWView.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KoCanvasControllerWidget.h>
#include <KoDockerManager.h>
#include <KoFlake.h>
#include <KoModeBoxFactory.h>
#include <KoRuler.h>
#include <KoRulerController.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeManager.h>
#include <KoToolManager.h>
#include <KoUnit.h>

#include <KoMainWindow.h>
#include <QAction>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScrollBar>
#include <QSizePolicy>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStringList>

static QMenu *findMenu(QMainWindow *mainWindow, const QString &menuName, const QString &menuTitle)
{
    if (!mainWindow) {
        return nullptr;
    }
    if (QMenu *menu = mainWindow->findChild<QMenu *>(menuName)) {
        return menu;
    }
    for (QAction *action : mainWindow->menuBar()->actions()) {
        if (!action->menu()) {
            continue;
        }
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        if (text.compare(menuTitle, Qt::CaseInsensitive) == 0) {
            return action->menu();
        }
    }
    return nullptr;
}

static void populateMenuPage(QWidget *page, QMenu *menu, bool omitUndoRedo = false)
{
    QList<QAction *> actions;
    for (QAction *action : menu->actions()) {
        if (action->isSeparator() || !action->isVisible()) {
            continue;
        }
        if (omitUndoRedo && (action->objectName() == QLatin1String("edit_undo") || action->objectName() == QLatin1String("edit_redo"))) {
            continue;
        }
        actions.append(action);
    }

    auto *pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(2, 1, 2, 1);
    auto *content = new QWidget(page);
    content->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    auto *layout = new QGridLayout(content);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setHorizontalSpacing(3);
    layout->setVerticalSpacing(1);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    const int columns = qMax(1, (actions.size() + 1) / 2);
    for (int i = 0; i < actions.size(); ++i) {
        QAction *action = actions.at(i);
        auto *button = new QToolButton(page);
        button->setDefaultAction(action);
        button->setToolButtonStyle(action->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(16, 16));
        button->setAutoRaise(true);
        if (action->menu()) {
            button->setPopupMode(QToolButton::MenuButtonPopup);
        }
        layout->addWidget(button, i / columns, i % columns, Qt::AlignLeft | Qt::AlignTop);
    }
    pageLayout->addWidget(content, 0, Qt::AlignLeft | Qt::AlignTop);
    pageLayout->addStretch(1);

    // The contents live in a ribbon tab now, so remove the original menu.
    menu->menuAction()->setVisible(false);
}

static QAction *findFileAction(KoMainWindow *mainWindow, KActionCollection *viewActions, const QString &name)
{
    if (viewActions) {
        if (QAction *action = viewActions->action(name)) {
            return action;
        }
    }
    return mainWindow ? mainWindow->actionCollection()->action(name) : nullptr;
}

static void populateFilePage(QWidget *page, KoMainWindow *mainWindow, KActionCollection *viewActions)
{
    struct FileGroup {
        QString title;
        QList<QPair<QString, QString>> actions;
    };
    const QList<FileGroup> groups = {
        {i18n("New"), {{QStringLiteral("file_new"), i18n("New Document")}, {QStringLiteral("extra_template"), i18n("Save as Template")}}},
        {i18n("File"), {{QStringLiteral("file_open"), i18n("Open")},
                          {QStringLiteral("file_save"), i18n("Save")},
                          {QStringLiteral("file_export_file"), i18n("Export")},
                          {QStringLiteral("file_export_pdf"), i18n("Export as PDF")}}},
        {i18n("Print"), {{QStringLiteral("file_print"), i18n("Print")}, {QStringLiteral("file_print_preview"), i18n("Print Preview")}}},
        {i18n("Properties"), {{QStringLiteral("file_documentinfo"), i18n("Properties")}}},
    };

    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(2, 1, 2, 1);
    pageLayout->setSpacing(8);
    for (const FileGroup &groupInfo : groups) {
        auto *group = new QWidget(page);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(6, 0, 6, 0);
        groupLayout->setSpacing(0);
        auto *buttons = new QWidget(group);
        buttons->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        auto *buttonLayout = new QGridLayout(buttons);
        buttonLayout->setContentsMargins(1, 0, 1, 0);
        buttonLayout->setHorizontalSpacing(12);
        buttonLayout->setVerticalSpacing(0);
        const int columns = qMax(1, groupInfo.actions.size());
        for (int column = 0; column < columns; ++column) {
            buttonLayout->setColumnStretch(column, 1);
        }

        int buttonIndex = 0;
        for (const auto &[actionName, label] : groupInfo.actions) {
            QAction *action = findFileAction(mainWindow, viewActions, actionName);
            if (!action) {
                continue;
            }
            auto *button = new QToolButton(buttons);
            button->setDefaultAction(action);
            button->setText(label);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(30, 30));
            button->setMinimumSize(104, 42);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            button->setAutoRaise(true);
            if (action->menu()) {
                button->setPopupMode(QToolButton::MenuButtonPopup);
            }
            buttonLayout->addWidget(button, buttonIndex / columns, buttonIndex % columns, Qt::AlignCenter | Qt::AlignVCenter);
            ++buttonIndex;
        }

        if (buttonIndex == 0) {
            group->deleteLater();
            continue;
        }
        groupLayout->addWidget(buttons, 1);
        auto *title = new QLabel(groupInfo.title, group);
        title->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        title->setStyleSheet(QStringLiteral("color: palette(mid);"));
        groupLayout->addWidget(title);
        pageLayout->addWidget(group, 1);

        if (groupInfo.title != groups.constLast().title) {
            auto *separator = new QFrame(page);
            separator->setFrameStyle(QFrame::VLine);
            separator->setFixedWidth(1);
            pageLayout->addWidget(separator);
        }
    }
    pageLayout->addStretch(1);

    if (QMenu *menu = findMenu(mainWindow, QStringLiteral("file"), i18n("File"))) {
        menu->menuAction()->setVisible(false);
    }
}

static void populateFilePageLater(QWidget *page, KoMainWindow *mainWindow, KActionCollection *viewActions, int attemptsLeft)
{
    if (!page || !mainWindow || attemptsLeft <= 0) {
        return;
    }
    if (findFileAction(mainWindow, viewActions, QStringLiteral("file_new"))
        && findFileAction(mainWindow, viewActions, QStringLiteral("file_print"))) {
        populateFilePage(page, mainWindow, viewActions);
        return;
    }
    QTimer::singleShot(100, page, [page, mainWindow, viewActions, attemptsLeft] {
        populateFilePageLater(page, mainWindow, viewActions, attemptsLeft - 1);
    });
}

// Menus are built by the XMLGUI factory after this widget exists, so keep
// looking for them for a short while before giving up.
static void populateMenuPageLater(QWidget *page, QMainWindow *mainWindow, const QString &menuName, const QString &menuTitle, int attemptsLeft, bool omitUndoRedo = false)
{
    if (!page || !mainWindow || attemptsLeft <= 0) {
        return;
    }
    if (QMenu *menu = findMenu(mainWindow, menuName, menuTitle)) {
        populateMenuPage(page, menu, omitUndoRedo);
        return;
    }
    QTimer::singleShot(100, page, [page, mainWindow, menuName, menuTitle, attemptsLeft, omitUndoRedo] {
        populateMenuPageLater(page, mainWindow, menuName, menuTitle, attemptsLeft - 1, omitUndoRedo);
    });
}

static void populateInsertPageLater(QWidget *page, QMainWindow *mainWindow, KActionCollection *actions, int attemptsLeft)
{
    if (!page || !mainWindow || !actions || attemptsLeft <= 0) {
        return;
    }

    const QStringList actionNames = {QStringLiteral("insert_table"),
                                     QStringLiteral("insert_picture"),
                                     QStringLiteral("insert_link"),
                                     QStringLiteral("insert_footendnote"),
                                     QStringLiteral("insert_tableofcontents"),
                                     QStringLiteral("insert_header"),
                                     QStringLiteral("insert_footer")};
    QList<QAction *> availableActions;
    for (const QString &name : actionNames) {
        if (QAction *action = actions->action(name); action && action->isVisible()) {
            availableActions.append(action);
        }
    }
    QAction *addShapeAction = actions->action(QStringLiteral("add_shape"));
    QMenu *shapeCollection = addShapeAction ? addShapeAction->menu() : nullptr;
    if (availableActions.isEmpty() || !shapeCollection) {
        QTimer::singleShot(100, page, [page, mainWindow, actions, attemptsLeft] {
            populateInsertPageLater(page, mainWindow, actions, attemptsLeft - 1);
        });
        return;
    }

    auto *pageLayout = new QHBoxLayout(page);
    pageLayout->setContentsMargins(2, 1, 2, 1);
    pageLayout->setSpacing(4);
    auto *content = new QWidget(page);
    auto *layout = new QGridLayout(content);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setHorizontalSpacing(4);
    layout->setVerticalSpacing(1);
    layout->setSizeConstraint(QLayout::SetFixedSize);
    const int columns = qMax(1, (availableActions.size() + 1) / 2);
    for (int i = 0; i < availableActions.size(); ++i) {
        QAction *action = availableActions.at(i);
        auto *button = new QToolButton(page);
        button->setDefaultAction(action);
        button->setToolButtonStyle(action->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
        button->setIconSize(QSize(24, 24));
        button->setAutoRaise(true);
        layout->addWidget(button, i / columns, i % columns, Qt::AlignLeft | Qt::AlignTop);
    }
    pageLayout->addWidget(content, 0, Qt::AlignLeft | Qt::AlignTop);
    auto *separator = new QFrame(page);
    separator->setFrameStyle(QFrame::VLine);
    separator->setFixedWidth(1);
    pageLayout->addWidget(separator);
    shapeCollection->setParent(page, Qt::Widget);
    shapeCollection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    pageLayout->addWidget(shapeCollection, 1);
    shapeCollection->show();
}

KWGui::KWGui(const QString &viewMode, KWView *parent)
    : QWidget(parent)
    , m_view(parent)
{
    QGridLayout *gridLayout = new QGridLayout(this);
    gridLayout->setContentsMargins({});
    gridLayout->setSpacing(0);

    setMouseTracking(true);

    // Ruler
    m_horizontalRuler = new KoRuler(this, Qt::Horizontal, m_view->viewConverter());
    m_horizontalRuler->setShowMousePosition(true);
    m_horizontalRuler->setUnit(m_view->kwdocument()->unit());
    m_verticalRuler = new KoRuler(this, Qt::Vertical, m_view->viewConverter());
    m_verticalRuler->setUnit(m_view->kwdocument()->unit());
    m_verticalRuler->setShowMousePosition(true);

    m_canvas = new KWCanvas(viewMode, static_cast<KWDocument *>(m_view->koDocument()), m_view, this);
    KoCanvasControllerWidget *canvasController = new KoCanvasControllerWidget(m_view->actionCollection(), this);
    m_canvasController = canvasController;
    // We need to set this as QDeclarativeView sets them a bit different from QAbstractScrollArea
    canvasController->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    canvasController->setFocusPolicy(Qt::NoFocus);
    // setScene(0);

    canvasController->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_canvasController->setMargin(10);
    m_canvasController->setCanvas(m_canvas);
    m_canvasController->setCanvasMode(KoCanvasController::AlignTop);
    KoToolManager::instance()->addController(m_canvasController);
    KoToolManager::instance()->registerTools(m_view->actionCollection(), m_canvasController);

    if (m_view->mainWindow()) {
        KoModeBoxFactory modeBoxFactory(canvasController, qApp->applicationName(), i18n("Tools"), KoDockFactoryBase::DockTop);

        auto *filePage = new QWidget;
        filePage->setObjectName(QStringLiteral("fileRibbonPage"));
        filePage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("File"), QIcon::fromTheme("document-open"), i18n("Create, open, save, and print documents"), filePage, true);

        auto *editPage = new QWidget;
        editPage->setObjectName(QStringLiteral("editRibbonPage"));
        editPage->setWindowTitle(i18n("Edit"));
        editPage->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        modeBoxFactory.addWidgetToToolTab(QStringLiteral("TextToolFactory_ID"), editPage);

        auto *shapeToolPicker = new QWidget;
        shapeToolPicker->setWindowTitle(i18n("Shape Tools"));
        auto *shapeToolLayout = new QHBoxLayout(shapeToolPicker);
        shapeToolLayout->setContentsMargins(2, 1, 2, 1);
        shapeToolLayout->setSpacing(3);
        const auto addShapeToolButton = [this, shapeToolPicker, shapeToolLayout](const QString &toolId, const QString &label, const QString &iconName) {
            auto *button = new QToolButton(shapeToolPicker);
            button->setText(label);
            button->setIcon(QIcon::fromTheme(iconName));
            button->setIconSize(QSize(26, 26));
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setAutoRaise(true);
            connect(button, &QToolButton::clicked, this, [toolId] {
                KoToolManager::instance()->switchToolRequested(toolId);
            });
            shapeToolLayout->addWidget(button);
        };
        addShapeToolButton(QStringLiteral("ConnectionTool"), i18n("Connect Shapes"), QStringLiteral("x-shape-connection"));
        addShapeToolButton(QStringLiteral("CreatePathTool"), i18n("Draw Path"), QStringLiteral("createpath"));
        modeBoxFactory.addWidgetToToolTab(QStringLiteral("ConnectionTool"), shapeToolPicker);

        auto *viewPage = new QWidget;
        viewPage->setObjectName(QStringLiteral("viewRibbonPage"));
        viewPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        auto *insertPage = new QWidget;
        insertPage->setObjectName(QStringLiteral("insertRibbonPage"));
        insertPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Insert"), QIcon::fromTheme("insert-image"), i18n("Insert tables, pictures, links and document elements"), insertPage);
        modeBoxFactory.addCustomPage(i18n("View"), QIcon::fromTheme("view-preview"), i18n("Zoom, rulers, grid and other view options"), viewPage);

        QMenuBar *menuBar = m_view->mainWindow()->menuBar();
        menuBar->setNativeMenuBar(false);
        menuBar->hide();

        QAction *settingsMenuAction = nullptr;
        for (QAction *action : menuBar->actions()) {
            QString title = action->text();
            title.remove(QLatin1Char('&'));
            if (title.compare(i18n("Settings"), Qt::CaseInsensitive) == 0 && action->menu()) {
                settingsMenuAction = action;
                menuBar->removeAction(action);
                settingsMenuAction->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
                settingsMenuAction->setToolTip(i18n("Settings"));
                settingsMenuAction->setText(QString());
                break;
            }
        }

        auto *quickAccess = new QToolBar;
        quickAccess->setObjectName(QStringLiteral("ribbonQuickAccess"));
        quickAccess->setMovable(false);
        quickAccess->setFloatable(false);
        quickAccess->setIconSize(QSize(20, 20));
        quickAccess->setToolButtonStyle(Qt::ToolButtonIconOnly);
        quickAccess->setContentsMargins(2, 0, 2, 0);
        const auto findAction = [this](const QString &name) -> QAction * {
            if (QAction *action = m_view->actionCollection()->action(name)) {
                return action;
            }
            return m_view->mainWindow()->actionCollection()->action(name);
        };
        for (const QString &actionName : {QStringLiteral("file_new"),
                                          QStringLiteral("file_open"),
                                          QStringLiteral("file_save"),
                                          QStringLiteral("edit_undo"),
                                          QStringLiteral("edit_redo"),
                                          QStringLiteral("file_print")}) {
            if (QAction *action = findAction(actionName)) {
                quickAccess->addAction(action);
            }
        }

        auto *settingsButton = new QToolButton;
        settingsButton->setDefaultAction(settingsMenuAction);
        settingsButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
        settingsButton->setToolTip(i18n("Settings"));
        settingsButton->setAutoRaise(true);
        settingsButton->setIconSize(QSize(20, 20));
        settingsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        if (settingsMenuAction && settingsMenuAction->menu()) {
            settingsButton->setMenu(settingsMenuAction->menu());
            settingsButton->setPopupMode(QToolButton::InstantPopup);
        }

        for (QToolBar *toolbar : m_view->mainWindow()->findChildren<QToolBar *>()) {
            toolbar->hide();
            // KoPart::showStartUpWidget() looks up "mainToolBar" when the
            // File > New flow opens the template chooser. Keep the XMLGUI
            // container alive even though the ribbon replaces its display.
            if (toolbar->objectName() != QLatin1String("mainToolBar")) {
                m_view->mainWindow()->removeToolBar(toolbar);
                toolbar->deleteLater();
            } else {
                connect(toolbar, &QToolBar::visibilityChanged, toolbar, [toolbar](bool visible) {
                    if (visible) {
                        QTimer::singleShot(0, toolbar, &QWidget::hide);
                    }
                });
            }
        }
        modeBoxFactory.setLeadingWidget(quickAccess);
        modeBoxFactory.setTrailingWidget(settingsButton);

        QDockWidget *modeBox = m_view->mainWindow()->createDockWidget(&modeBoxFactory);
        modeBox->setMaximumHeight(160);
        m_view->mainWindow()->addDockWidget(Qt::TopDockWidgetArea, modeBox);
        modeBox->show();
        m_view->mainWindow()->dockerManager()->removeToolOptionsDocker();
        dynamic_cast<KoCanvasObserverBase *>(modeBox)->setObservedCanvas(m_canvas);

        populateFilePageLater(filePage, m_view->mainWindow(), m_view->actionCollection(), 20);
        populateMenuPageLater(editPage, m_view->mainWindow(), QStringLiteral("edit"), i18n("Edit"), 20, true);
        populateInsertPageLater(insertPage, m_view->mainWindow(), m_view->actionCollection(), 20);
        populateMenuPageLater(viewPage, m_view->mainWindow(), QStringLiteral("view"), i18n("View"), 20);
    }

    gridLayout->addWidget(m_horizontalRuler->tabChooser(), 0, 0);
    gridLayout->addWidget(m_horizontalRuler, 0, 1);
    gridLayout->addWidget(m_verticalRuler, 1, 0);
    gridLayout->addWidget(canvasController, 1, 1);

    new KoRulerController(m_horizontalRuler, m_canvas->resourceManager());

    connect(m_view->kwdocument(), &KoDocument::unitChanged, m_horizontalRuler, &KoRuler::setUnit);
    connect(m_view->kwdocument(), &KoDocument::unitChanged, m_verticalRuler, &KoRuler::setUnit);
    connect(m_view->kwdocument(), &KWDocument::pageSetupChanged, this, &KWGui::pageSetupChanged);

    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::canvasOffsetXChanged, m_horizontalRuler, &KoRuler::setOffset);
    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::canvasOffsetYChanged, m_verticalRuler, &KoRuler::setOffset);
    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::canvasOffsetYChanged, parent, &KWView::offsetInDocumentMoved);
    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::canvasMousePositionChanged, this, &KWGui::updateMousePos);
    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::moveDocumentOffset, m_canvas, &KWCanvas::setDocumentOffset);

    connect(m_canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, &KWGui::shapeSelectionChanged);

    m_verticalRuler->createGuideToolConnection(m_canvas);
    m_horizontalRuler->createGuideToolConnection(m_canvas);

    pageSetupChanged();

    QTimer::singleShot(0, this, &KWGui::setupUnitActions);
}

KWGui::~KWGui() = default;

int KWGui::visibleWidth() const
{
    return m_canvasController->visibleWidth();
}

int KWGui::visibleHeight() const
{
    return m_canvasController->visibleHeight();
}

QSize KWGui::viewportSize() const
{
    return m_canvasController->viewportSize();
}

bool KWGui::horizontalScrollBarVisible()
{
    return static_cast<KoCanvasControllerWidget *>(m_canvasController)->horizontalScrollBar()
        && static_cast<KoCanvasControllerWidget *>(m_canvasController)->horizontalScrollBar()->isVisible();
}

void KWGui::pageSetupChanged()
{
    const KWPageManager *pm = m_view->kwdocument()->pageManager();
    const KWPage firstPage = pm->begin();
    const KWPage lastPage = pm->last();
    int height = 0;
    if (lastPage.isValid())
        height = lastPage.offsetInDocument() + lastPage.height();
    m_verticalRuler->setRulerLength(height);
    updateRulers();
    int width = 0;
    if (firstPage.isValid())
        width = firstPage.width();
    m_horizontalRuler->setRulerLength(width);
    m_horizontalRuler->setActiveRange(0, width);
    m_verticalRuler->setActiveRange(0, height);
    updateRulers();
}

void KWGui::updateMousePos(const QPoint &point)
{
    QPoint canvasOffset(m_canvasController->canvasOffsetX(), m_canvasController->canvasOffsetY());
    // the offset is positive it the canvas is shown fully visible
    canvasOffset.setX(canvasOffset.x() < 0 ? canvasOffset.x() : 0);
    canvasOffset.setY(canvasOffset.y() < 0 ? canvasOffset.y() : 0);
    QPoint viewPos = point - canvasOffset;
    m_horizontalRuler->updateMouseCoordinate(viewPos.x());
    m_verticalRuler->updateMouseCoordinate(viewPos.y());
}

void KWGui::updateRulers() const
{
    m_verticalRuler->setVisible(m_view->kwdocument()->config().viewRulers());
    m_horizontalRuler->setVisible(m_view->kwdocument()->config().viewRulers());
}

void KWGui::shapeSelectionChanged()
{
}

void KWGui::setupUnitActions()
{
    QList<QAction *> unitActions = m_view->createChangeUnitActions();
    QAction *separator = new QAction(this);
    separator->setSeparator(true);
    unitActions.append(separator);
    unitActions.append(m_view->actionCollection()->action("format_page"));
    m_horizontalRuler->setPopupActionList(unitActions);
}

void KWGui::mouseMoveEvent(QMouseEvent *e)
{
    m_view->viewMouseMoveEvent(e);
}
