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
#include "KWPageManager.h"
#include "KWView.h"
#include "Words.h"
#include "frames/KWFrame.h"
#include "frames/KWFrameSet.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KoCanvasControllerWidget.h>
#include <KoCanvasResourceManager.h>
#include <KoProperties.h>
#include <KoShapeController.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeRegistry.h>
#include <KoDockerManager.h>
#include <KoFlake.h>
#include <KoModeBoxFactory.h>
#include <KoParameterShape.h>
#include <KoParameterToPathCommand.h>
#include <KoRuler.h>
#include <KoRibbonLayout.h>
#include <KoZoomController.h>
#include <KoZoomMode.h>
#include <KoRulerController.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeManager.h>
#include <KoShapeTransformCommand.h>
#include <KoToolManager.h>
#include <KoToolBase.h>
#include <KoText.h>
#include <KoTextEditor.h>
#include <KoUnit.h>

#include <KoMainWindow.h>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QCheckBox>
#include <QFrame>
#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStringList>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QTextDocument>
#include <QTransform>
#include <QVector>

#include <functional>
#include <memory>
#include <algorithm>

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

static void populateRibbonSettingsLater(QToolButton *button, QMainWindow *window, int attemptsLeft)
{
    if (QMenu *menu = findMenu(window, QStringLiteral("settings"), i18n("Settings"))) {
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setEnabled(true);
    } else if (attemptsLeft > 0) {
        QTimer::singleShot(100, button, [button, window, attemptsLeft] {
            populateRibbonSettingsLater(button, window, attemptsLeft - 1);
        });
    }
}

namespace
{
/// A ribbon group: a title plus the actions to show in it (action name, button label).
struct RibbonGroup {
    enum Layout { LargeButtons, LargeAndStacked, Stacked, CheckBoxes, PreviewNavigation, ZoomButtons };
    QString title;
    QList<QPair<QString, QString>> actions;
    Layout layout = LargeButtons;
};

using ActionFinder = std::function<QAction *(const QString &)>;

// Tool activation replaces identically named actions in the view's action
// collection. A ribbon button must follow that replacement, rather than keep
// a disabled action from the tool that happened to be active during startup.
class RibbonActionButton final : public QToolButton
{
public:
    RibbonActionButton(const QString &name, const QString &label, const ActionFinder &findAction, QWidget *parent)
        : QToolButton(parent), m_name(name), m_label(label), m_findAction(findAction)
    {
        bindAction();
        if (defaultAction() && qobject_cast<KoToolBase *>(defaultAction()->parent())) {
            connect(KoToolManager::instance(), &KoToolManager::changedTool, this, [this] { bindAction(); });
        }
    }

private:
    void bindAction()
    {
        QAction *action = m_findAction(m_name);
        if (!action) {
            setEnabled(false);
            return;
        }
        if (action != defaultAction()) {
            disconnect(m_presentationConnection);
            setDefaultAction(action);
            m_presentationConnection = connect(action, &QAction::changed, this, [this] { updatePresentation(); });
        }
        updatePresentation();
    }

    void updatePresentation()
    {
        setText(m_label);
        if (defaultAction() && defaultAction()->icon().isNull()) {
            const QIcon fallback = property("ribbonFallbackIcon").value<QIcon>();
            if (!fallback.isNull()) {
                setIcon(fallback);
            }
        }
        if (defaultAction() && defaultAction()->menu()) {
            setPopupMode(m_name == QLatin1String("add_shape") || defaultAction()->property("ribbonMenuOnly").toBool()
                             ? QToolButton::InstantPopup : QToolButton::MenuButtonPopup);
        }
    }

    QString m_name;
    QString m_label;
    ActionFinder m_findAction;
    QMetaObject::Connection m_presentationConnection;
};

// Follow the selected text document as the cursor moves between body text,
// headers and footers. The page keeps its controls while its tab is hidden.
class TableRibbonContext final : public QObject
{
public:
    TableRibbonContext(KWCanvas *canvas, QWidget *page)
        : QObject(page), m_canvas(canvas), m_page(page)
    {
        connect(canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, [this] { update(); });
        connect(canvas->resourceManager(), &KoCanvasResourceManager::canvasResourceChanged, this, [this](int key, const QVariant &) {
            if (key == KoText::CurrentTextPosition || key == KoText::CurrentTextAnchor || key == KoText::CurrentTextDocument) {
                update();
            }
        });
        QTimer::singleShot(0, this, [this] { update(); });
    }

private:
    void update()
    {
        KoTextEditor *editor = m_canvas ? KoTextEditor::getTextEditorFromCanvas(m_canvas) : nullptr;
        if (editor != m_editor) {
            for (const auto &connection : std::as_const(m_connections)) {
                disconnect(connection);
            }
            m_connections.clear();
            m_editor = editor;
            if (editor) {
                m_connections.append(connect(editor, &KoTextEditor::cursorPositionChanged, this, [this] { update(); }));
                m_connections.append(connect(editor, &KoTextEditor::textFormatChanged, this, [this] { update(); }));
                m_connections.append(connect(editor->document(), &QTextDocument::contentsChanged, this, [this] { update(); }));
                m_connections.append(connect(editor, &QObject::destroyed, this, [this] {
                    m_editor = nullptr;
                    m_page->setProperty("ribbonContextActive", false);
                }));
            }
        }
        const bool tableActive = editor && editor->currentTable();
        const QVariant current = m_page->property("ribbonContextActive");
        if (!current.isValid() || current.toBool() != tableActive) {
            m_page->setProperty("ribbonContextActive", tableActive);
        }
    }

    QPointer<KWCanvas> m_canvas;
    QWidget *m_page;
    QPointer<KoTextEditor> m_editor;
    QList<QMetaObject::Connection> m_connections;
};
}

// Lay out ribbon groups in the Microsoft Office style: large buttons with the
// text under the icon, the group label centred underneath and thin separators
// between the groups.
static void populateRibbonGroups(QWidget *page, const QList<RibbonGroup> &groups, const ActionFinder &findAction, bool showGroupTitles)
{
    auto *pageLayout = qobject_cast<QHBoxLayout *>(page->layout());
    if (!pageLayout) {
        pageLayout = new QHBoxLayout(page);
    }
    pageLayout->setContentsMargins(4, 4, 4, 2);
    pageLayout->setSpacing(0);
    bool firstGroup = true;
    int groupPosition = 0;
    for (const RibbonGroup &groupInfo : groups) {
        auto *group = new QWidget(page);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(8, 0, 8, 0);
        groupLayout->setSpacing(0);
        auto *buttons = new QWidget(group);
        buttons->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        auto *buttonLayout = new QGridLayout(buttons);
        buttonLayout->setContentsMargins(1, 0, 1, 0);
        buttonLayout->setAlignment(Qt::AlignTop);
        buttonLayout->setHorizontalSpacing(4);
        buttonLayout->setVerticalSpacing(1);

        int buttonIndex = 0;
        for (const auto &[actionName, label] : groupInfo.actions) {
            QAction *action = findAction(actionName);
            if (!action) {
                continue;
            }
            if (groupInfo.layout == RibbonGroup::CheckBoxes) {
                auto *check = new QCheckBox(label, buttons);
                check->setObjectName(actionName + QStringLiteral("RibbonCheck"));
                check->setChecked(action->isChecked());
                check->setEnabled(action->isEnabled());
                check->setToolTip(action->toolTip());
                QObject::connect(check, &QCheckBox::clicked, action, &QAction::trigger);
                QObject::connect(action, &QAction::changed, check, [check, action] {
                    check->setChecked(action->isChecked());
                    check->setEnabled(action->isEnabled());
                });
                buttonLayout->addWidget(check, buttonIndex % 3, buttonIndex / 3);
                ++buttonIndex;
                continue;
            }
            const QString displayLabel = actionName == QLatin1String("edit_select_all") ? i18n("Select") : label;
            auto *button = new RibbonActionButton(actionName, displayLabel, findAction, buttons);
            button->setObjectName(actionName + QStringLiteral("RibbonButton"));
            if (action->icon().isNull()) {
                static const QMap<QString, QString> commandIcons = {
                    {QStringLiteral("format_stylist"), QStringLiteral("format-text-bold")},
                    {QStringLiteral("format_font"), QStringLiteral("preferences-desktop-font")},
                    {QStringLiteral("format_paragraph"), QStringLiteral("format-justify-left")},
                    {QStringLiteral("insert_annotation"), QStringLiteral("comment-new")},
                    {QStringLiteral("insert_header"), QStringLiteral("insert-text-frame")},
                    {QStringLiteral("insert_footer"), QStringLiteral("insert-text-frame")},
                    {QStringLiteral("insert_variable"), QStringLiteral("insert-text")},
                };
                button->setIcon(QIcon::fromTheme(commandIcons.value(actionName, QStringLiteral("document-edit"))));
                button->setProperty("ribbonFallbackIcon", button->icon());
            }
            // A single prominent command beside compact stacks of three
            // mirrors Office's groups without making every action oversized.
            const int largeCount = groupInfo.layout == RibbonGroup::ZoomButtons ? 3
                : ((groupInfo.layout == RibbonGroup::LargeAndStacked || groupInfo.layout == RibbonGroup::PreviewNavigation) ? 1 : 0);
            const bool large = groupInfo.layout == RibbonGroup::LargeButtons || buttonIndex < largeCount;
            if (action->menu()) {
                button->setPopupMode(actionName == QLatin1String("add_shape") || action->property("ribbonMenuOnly").toBool()
                                         ? QToolButton::InstantPopup : QToolButton::MenuButtonPopup);
            }
            if (actionName == QLatin1String("edit_find") || actionName == QLatin1String("edit_select_all")) {
                auto *menu = new QMenu(button);
                const QStringList names = actionName == QLatin1String("edit_find")
                    ? QStringList{QStringLiteral("edit_find"), QStringLiteral("edit_findnext"), QStringLiteral("edit_findprevious")}
                    : QStringList{QStringLiteral("edit_select_all"), QStringLiteral("edit_selectcurrentframe"), QStringLiteral("edit_delete")};
                for (const QString &name : names) {
                    if (QAction *command = findAction(name)) {
                        menu->addAction(command);
                    }
                }
                button->setMenu(menu);
                button->setPopupMode(QToolButton::MenuButtonPopup);
            }
            KoRibbonLayout::configureButton(button, large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
            const int compactIndex = buttonIndex - largeCount;
            if (large) {
                buttonLayout->addWidget(button, 0, buttonIndex, 3, 1, Qt::AlignTop);
            } else if (groupInfo.layout == RibbonGroup::PreviewNavigation) {
                KoRibbonLayout::configureButton(button, Qt::ToolButtonIconOnly);
                button->setAccessibleName(label);
                buttonLayout->addWidget(button, 1, compactIndex + 1);
            } else {
                buttonLayout->addWidget(button, compactIndex % 3, compactIndex / 3 + largeCount, Qt::AlignLeft);
            }
            ++buttonIndex;
        }

        if (buttonIndex == 0) {
            group->deleteLater();
            continue;
        }
        groupLayout->addWidget(buttons, 1);
        if (showGroupTitles) {
            auto *title = new QLabel(groupInfo.title, group);
            title->setProperty("ribbonGroupTitle", true);
            title->setMinimumHeight(18);
            title->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
            groupLayout->addWidget(title);
        }

        if (!firstGroup) {
            auto *separator = new QFrame(page);
            separator->setFrameStyle(QFrame::VLine);
            separator->setFixedWidth(1);
            pageLayout->insertWidget(groupPosition++, separator);
        }
        firstGroup = false;
        pageLayout->insertWidget(groupPosition++, group);
    }
    if (pageLayout->count() == 0 || !pageLayout->itemAt(pageLayout->count() - 1)->spacerItem()) {
        pageLayout->addStretch(1);
    }
}

// Ribbon pages are filled once the XMLGUI factory has created the actions.
static void populateRibbonGroupsLater(QWidget *page,
                                      const QList<RibbonGroup> &groups,
                                      const ActionFinder &findAction,
                                      bool showGroupTitles,
                                      int attemptsLeft)
{
    if (!page || attemptsLeft <= 0) {
        return;
    }
    bool allFound = true;
    for (const RibbonGroup &groupInfo : groups) {
        for (const auto &[actionName, label] : groupInfo.actions) {
            if (!findAction(actionName)) {
                allFound = false;
                break;
            }
        }
        if (!allFound) {
            break;
        }
    }
    if (allFound || attemptsLeft == 1) {
        populateRibbonGroups(page, groups, findAction, showGroupTitles);
        return;
    }
    QTimer::singleShot(100, page, [page, groups, findAction, showGroupTitles, attemptsLeft] {
        populateRibbonGroupsLater(page, groups, findAction, showGroupTitles, attemptsLeft - 1);
    });
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

static void showRibbonCommandSearch(KWView *view)
{
    QDialog dialog(view);
    dialog.setWindowTitle(i18n("Search Commands"));
    dialog.resize(540, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit(&dialog);
    search->setPlaceholderText(i18n("Search for a command…"));
    search->setClearButtonEnabled(true);
    search->setAccessibleName(i18n("Search Commands"));
    layout->addWidget(search);
    auto *results = new QListWidget(&dialog);
    results->setIconSize(QSize(20, 20));
    results->setAlternatingRowColors(true);
    layout->addWidget(results);

    QList<QAction *> actions = view->actionCollection()->actions();
    if (view->mainWindow()) {
        actions.append(view->mainWindow()->actionCollection()->actions());
    }
    std::stable_sort(actions.begin(), actions.end(), [](QAction *a, QAction *b) {
        return QString::localeAwareCompare(a->text(), b->text()) < 0;
    });
    QList<QPointer<QAction>> commands;
    QSet<QAction *> seen;
    for (QAction *action : actions) {
        if (seen.contains(action) || action->isSeparator() || action->text().isEmpty() || action->menu()) {
            continue;
        }
        seen.insert(action);
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        if (!action->shortcut().isEmpty()) {
            text += QStringLiteral("    ") + action->shortcut().toString(QKeySequence::NativeText);
        }
        auto *item = new QListWidgetItem(action->icon(), text, results);
        item->setToolTip(action->toolTip());
        if (!action->isEnabled()) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        commands.append(action);
    }
    const auto filter = [results](const QString &query) {
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < results->count(); ++i) {
            auto *item = results->item(i);
            const bool match = item->text().contains(query, Qt::CaseInsensitive);
            item->setHidden(!match);
            if (!first && match && (item->flags() & Qt::ItemIsEnabled)) {
                first = item;
            }
        }
        results->setCurrentItem(first);
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, filter);
    QPointer<QAction> selected;
    const auto activate = [&] {
        const int row = results->currentRow();
        if (row >= 0 && !results->item(row)->isHidden() && commands.value(row) && commands[row]->isEnabled()) {
            selected = commands[row];
            dialog.accept();
        }
    };
    QObject::connect(search, &QLineEdit::returnPressed, &dialog, activate);
    QObject::connect(results, &QListWidget::itemActivated, &dialog, activate);
    filter(QString());
    search->setFocus();
    if (dialog.exec() == QDialog::Accepted && selected && selected->isEnabled()) {
        selected->trigger();
    }
}

static void populateFilePage(QWidget *page, KoMainWindow *mainWindow, KActionCollection *viewActions)
{
    // Microsoft Office style File groups.
    const QList<RibbonGroup> groups = {
        {i18n("New"), {{QStringLiteral("file_new"), i18n("Blank Document")}}},
        {i18n("Open"), {{QStringLiteral("file_open"), i18n("Open")}}},
        {i18n("Save"),
         {{QStringLiteral("file_save"), i18n("Save")},
          {QStringLiteral("file_save_as"), i18n("Save As")},
          {QStringLiteral("extra_template"), i18n("Save as Template")}}},
        {i18n("Print"), {{QStringLiteral("file_print"), i18n("Print")}, {QStringLiteral("file_print_preview"), i18n("Print Preview")}}},
        {i18n("Export"), {{QStringLiteral("file_export_file"), i18n("Export")}, {QStringLiteral("file_export_pdf"), i18n("Export as PDF")}}},
        {i18n("Info"), {{QStringLiteral("file_documentinfo"), i18n("Properties")}, {QStringLiteral("file_versions_file"), i18n("Versions")}}},
    };
    const ActionFinder findAction = [mainWindow, viewActions](const QString &name) {
        return findFileAction(mainWindow, viewActions, name);
    };
    populateRibbonGroups(page, groups, findAction, true);

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
    canvasController->setFrameStyle(QFrame::NoFrame);
    QPalette workspacePalette = canvasController->viewport()->palette();
    workspacePalette.setColor(QPalette::Window, workspacePalette.color(QPalette::Midlight));
    canvasController->viewport()->setPalette(workspacePalette);
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

        // Layout is a passive ribbon tab, so create its controls up front and
        // keep them on the tab without activating PageTool when the tab opens.
        if (KoToolBase *pageTool = KoToolManager::instance()->toolById(m_canvas, QStringLiteral("PageToolFactory_ID"))) {
            for (const QPointer<QWidget> &widget : pageTool->optionWidgets()) {
                if (widget) {
                    modeBoxFactory.addWidgetToToolTab(QStringLiteral("PageToolFactory_ID"), widget);
                }
            }
        }

        auto *filePage = new QWidget;
        filePage->setObjectName(QStringLiteral("fileRibbonPage"));
        filePage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("File"), QIcon::fromTheme("document-open"), i18n("Create, open, save, and print documents"), filePage, true);

        // Microsoft Office style Home tab groups that the text tool itself
        // does not provide: Clipboard, Styles and Editing.
        auto *goTo = new QAction(QIcon::fromTheme(QStringLiteral("go-jump")), i18n("Go to"), this);
        m_view->actionCollection()->addAction(QStringLiteral("ribbon_go_to_page"), goTo);
        connect(goTo, &QAction::triggered, this, [this] {
            const KWPageManager *pages = m_view->kwdocument()->pageManager();
            if (pages->pageCount() == 0) {
                return;
            }
            bool accepted = false;
            const int number = QInputDialog::getInt(this, i18n("Go to Page"), i18n("Page:"),
                                                    m_view->currentPage().pageNumber(),
                                                    pages->begin().pageNumber(), pages->last().pageNumber(), 1, &accepted);
            if (accepted) {
                m_view->goToPage(pages->page(number));
            }
        });
        const QPointer<KWView> ribbonView = m_view;
        const ActionFinder findRibbonAction = [ribbonView](const QString &name) {
            return ribbonView ? findFileAction(ribbonView->mainWindow(), ribbonView->actionCollection(), name) : nullptr;
        };
        const auto addHomeGroup = [&modeBoxFactory, &findRibbonAction](const QString &title, const QList<QPair<QString, QString>> &actions, int order, RibbonGroup::Layout layout) {
            auto *groupWidget = new QWidget;
            groupWidget->setWindowTitle(title);
            groupWidget->setProperty("ribbonGroupOrder", order);
            groupWidget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            modeBoxFactory.addWidgetToToolTab(QStringLiteral("TextToolFactory_ID"), groupWidget);
            populateRibbonGroupsLater(groupWidget, {{title, actions, layout}}, findRibbonAction, false, 20);
        };
        addHomeGroup(i18n("Edit"),
                     {{QStringLiteral("edit_paste"), i18n("Paste")},
                      {QStringLiteral("edit_cut"), i18n("Cut")},
                      {QStringLiteral("edit_copy"), i18n("Copy")},
                      {QStringLiteral("format_painter"), i18n("Format painter")}},
                     10, RibbonGroup::LargeAndStacked);
        addHomeGroup(i18n("Search"),
                     {{QStringLiteral("edit_find"), i18n("Search")},
                      {QStringLiteral("edit_replace"), i18n("Replace")},
                      {QStringLiteral("edit_findnext"), i18n("Search again")},
                      {QStringLiteral("ribbon_go_to_page"), i18n("Go to")}},
                     50, RibbonGroup::LargeAndStacked);
        addHomeGroup(i18n("Selection"),
                     {{QStringLiteral("edit_select_all"), i18n("Select all")}},
                     60, RibbonGroup::LargeButtons);

        // Microsoft Office 2026 style ribbon pages: Insert, Draw and Design
        // after Home, Mailings after References, and View and Help at the end.
        auto *insertPage = new QWidget;
        insertPage->setObjectName(QStringLiteral("insertRibbonPage"));
        insertPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Insert"),
                                    QIcon::fromTheme("insert-image"),
                                    i18n("Insert pages, tables, pictures, links and document elements"),
                                    insertPage,
                                    false,
                                    false,
                                    QStringLiteral("TextToolFactory_ID"));

        // Actions that switch to the drawing tools, used by the Draw page.
        const auto localRibbonActions = std::make_shared<QMap<QString, QAction *>>();
        const QList<KoToolAction *> toolActionList = KoToolManager::instance()->toolActionList();
        struct ToolSwitchEntry {
            QString toolId;
            QString label;
            QString iconName;
        };
        const QList<ToolSwitchEntry> drawTools = {
            {QStringLiteral("KoPencilTool"), i18n("Freehand"), QStringLiteral("draw-freehand")},
            {QStringLiteral("CreatePathTool"), i18n("Draw Path"), QStringLiteral("createpath")},
            {QStringLiteral("ConnectionTool"), i18n("Connect Shapes"), QStringLiteral("x-shape-connection")},
            {QStringLiteral("InteractionTool"), i18n("Select"), QStringLiteral("arrow")},
        };
        auto *drawToolGroup = new QActionGroup(this);
        drawToolGroup->setExclusive(true);
        for (const ToolSwitchEntry &entry : drawTools) {
            bool available = false;
            for (KoToolAction *toolAction : toolActionList) {
                if (toolAction->id() == entry.toolId) {
                    available = true;
                    break;
                }
            }
            if (!available) {
                continue;
            }
            auto *action = new QAction(QIcon::fromTheme(entry.iconName), entry.label, this);
            action->setCheckable(true);
            drawToolGroup->addAction(action);
            const QString toolId = entry.toolId;
            connect(action, &QAction::triggered, this, [toolId] {
                KoToolManager::instance()->switchToolRequested(toolId);
            });
            localRibbonActions->insert(entry.toolId, action);
        }
        // Keep the Draw tab's tool buttons in sync with the active tool: the
        // "Select" option shows as selected exactly when shape selection is
        // active, and none of the buttons is checked for other tools.
        connect(KoToolManager::instance(), &KoToolManager::changedTool, this, [this, localRibbonActions, toolActionList](KoCanvasController *controller, int buttonGroupId) {
            if (controller != m_canvas->canvasController()) {
                return;
            }
            QString changedToolId;
            for (KoToolAction *toolAction : toolActionList) {
                if (toolAction->buttonGroupId() == buttonGroupId && localRibbonActions->contains(toolAction->id())) {
                    localRibbonActions->value(toolAction->id())->setChecked(true);
                    changedToolId = toolAction->id();
                    break;
                }
            }
            if (changedToolId.isEmpty()) {
                for (const QString &toolId : {QStringLiteral("KoPencilTool"), QStringLiteral("CreatePathTool"), QStringLiteral("ConnectionTool"), QStringLiteral("InteractionTool")}) {
                    if (localRibbonActions->contains(toolId)) {
                        localRibbonActions->value(toolId)->setChecked(false);
                    }
                }
            } else if (changedToolId == QLatin1String("InteractionTool")) {
                // The body/header/footer text frames are only in the selection
                // as the text tool's editing target.  When the shape-handling
                // tool takes over they must not show up as selected objects
                // around the whole page.
                KoSelection *selection = m_canvas->shapeManager()->selection();
                const QList<KoShape *> selected = selection->selectedShapes();
                for (KoShape *shape : selected) {
                    const KWFrame *frame = m_view->kwdocument()->frameOfShape(shape);
                    if (frame && Words::isAutoGenerated(KWFrameSet::from(frame->shape()))) {
                        selection->deselect(shape);
                    }
                }
            }
        });

        // "Convert to Path" normally lives in the path editing tool's option
        // panel.  The Words ribbon hides that tab completely, so expose the
        // action in the Draw tab and run it against the canvas selection
        // directly (same behavior as KoPathTool::convertToPath).
        const auto collectParametricShapes = [this] {
            QList<KoParameterShape *> shapes;
            for (KoShape *shape : m_canvas->shapeManager()->selection()->selectedShapes()) {
                if (auto *parameterShape = dynamic_cast<KoParameterShape *>(shape); parameterShape && parameterShape->isParametricShape()) {
                    shapes.append(parameterShape);
                }
            }
            return shapes;
        };
        auto *convertToPathAction = new QAction(QIcon::fromTheme(QStringLiteral("format-convert-to-path")), i18n("Convert to Path"), this);
        const auto updateConvertToPathAction = [collectParametricShapes, convertToPathAction] {
            convertToPathAction->setEnabled(!collectParametricShapes().isEmpty());
        };
        updateConvertToPathAction();
        connect(convertToPathAction, &QAction::triggered, this, [this, collectParametricShapes, updateConvertToPathAction] {
            const QList<KoParameterShape *> shapes = collectParametricShapes();
            if (!shapes.isEmpty()) {
                m_canvas->addCommand(new KoParameterToPathCommand(shapes));
                updateConvertToPathAction();
            }
        });
        connect(m_canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, updateConvertToPathAction);
        localRibbonActions->insert(QStringLiteral("convert-to-path"), convertToPathAction);

        // Rotate the selected images and shapes in 90 degree steps.  Only
        // objects that actually allow rotation and are not the document's
        // auto-generated page frames take part.
        const auto collectRotatableShapes = [this] {
            QList<KoShape *> shapes;
            for (KoShape *shape : m_canvas->shapeManager()->selection()->selectedShapes(KoFlake::StrippedSelection)) {
                if (!shape->allowedInteraction(KoShape::RotationAllowed, false)) {
                    continue;
                }
                const KWFrame *frame = m_view->kwdocument()->frameOfShape(shape);
                if (frame && Words::isAutoGenerated(KWFrameSet::from(frame->shape()))) {
                    continue;
                }
                shapes.append(shape);
            }
            return shapes;
        };
        const auto rotateSelection = [this, collectRotatableShapes](qreal angle) {
            const QList<KoShape *> shapes = collectRotatableShapes();
            if (shapes.isEmpty()) {
                return;
            }
            QVector<QTransform> oldTransforms;
            QVector<QTransform> newTransforms;
            oldTransforms.reserve(shapes.count());
            newTransforms.reserve(shapes.count());
            for (KoShape *shape : shapes) {
                shape->update();
                oldTransforms << shape->transformation();
                shape->rotate(angle);
                newTransforms << shape->transformation();
                shape->update();
            }
            auto *command = new KoShapeTransformCommand(shapes, oldTransforms, newTransforms);
            command->setText(angle < 0.0 ? kundo2_i18n("Rotate Left") : kundo2_i18n("Rotate Right"));
            m_canvas->addCommand(command);
        };
        auto *rotateLeftAction = new QAction(QIcon::fromTheme(QStringLiteral("object-rotate-left")), i18n("Rotate Left"), this);
        rotateLeftAction->setObjectName(QStringLiteral("rotate-left"));
        rotateLeftAction->setToolTip(i18n("Rotate the selected objects 90° counterclockwise"));
        rotateLeftAction->setStatusTip(rotateLeftAction->toolTip());
        auto *rotateRightAction = new QAction(QIcon::fromTheme(QStringLiteral("object-rotate-right")), i18n("Rotate Right"), this);
        rotateRightAction->setObjectName(QStringLiteral("rotate-right"));
        rotateRightAction->setToolTip(i18n("Rotate the selected objects 90° clockwise"));
        rotateRightAction->setStatusTip(rotateRightAction->toolTip());
        const auto updateRotateActions = [collectRotatableShapes, rotateLeftAction, rotateRightAction] {
            const bool enabled = !collectRotatableShapes().isEmpty();
            rotateLeftAction->setEnabled(enabled);
            rotateRightAction->setEnabled(enabled);
        };
        updateRotateActions();
        connect(rotateLeftAction, &QAction::triggered, this, [rotateSelection] {
            rotateSelection(-90.0);
        });
        connect(rotateRightAction, &QAction::triggered, this, [rotateSelection] {
            rotateSelection(90.0);
        });
        connect(m_canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, updateRotateActions);
        localRibbonActions->insert(QStringLiteral("rotate-left"), rotateLeftAction);
        localRibbonActions->insert(QStringLiteral("rotate-right"), rotateRightAction);

        const ActionFinder findDrawAction = [findRibbonAction, localRibbonActions](const QString &name) {
            if (localRibbonActions->contains(name)) {
                return localRibbonActions->value(name);
            }
            return findRibbonAction(name);
        };

        // The Draw tab is the combined shape tool tab: its drawing groups are
        // shown together with the shape formatting options instead of adding a
        // second, separate "Shape Format" tab.
        auto *drawPage = new QWidget;
        drawPage->setObjectName(QStringLiteral("drawRibbonPage"));
        drawPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        drawPage->setProperty("ribbonGroupOrder", 10);
        modeBoxFactory.addWidgetToToolTab(QStringLiteral("ConnectionTool"), drawPage);

        auto *designPage = new QWidget;
        designPage->setObjectName(QStringLiteral("designRibbonPage"));
        designPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Design"),
                                    QIcon::fromTheme("document-edit"),
                                    i18n("Styles and page background"),
                                    designPage,
                                    false,
                                    false,
                                    QStringLiteral("ConnectionTool"));

        auto *mailingsPage = new QWidget;
        mailingsPage->setObjectName(QStringLiteral("mailingsRibbonPage"));
        mailingsPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Mailings"),
                                    QIcon::fromTheme("mail-message-new"),
                                    i18n("Insert fields, variables and bookmarks"),
                                    mailingsPage,
                                    false,
                                    false,
                                    QStringLiteral("ReferencesToolFactory_ID"));

        auto *viewPage = new QWidget;
        viewPage->setObjectName(QStringLiteral("viewRibbonPage"));
        viewPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("View"), QIcon::fromTheme("view-preview"), i18n("Zoom, rulers, grid and other view options"), viewPage, false, true);

        auto *helpPage = new QWidget;
        helpPage->setObjectName(QStringLiteral("helpRibbonPage"));
        helpPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Help"), QIcon::fromTheme("help-contents"), i18n("Documentation, feedback and about"), helpPage, false, true);

        auto *tablePage = new QWidget;
        tablePage->setObjectName(QStringLiteral("tableRibbonPage"));
        tablePage->setProperty("ribbonContextActive", false);
        modeBoxFactory.addCustomPage(i18n("Table Layout"), QIcon::fromTheme("insert-table"), i18n("Table rows, columns and borders"), tablePage, false, true);
        new TableRibbonContext(m_canvas, tablePage);

        QMenuBar *menuBar = m_view->mainWindow()->menuBar();
        menuBar->setNativeMenuBar(false);
        menuBar->hide();
        // XMLGUI can show the menubar again while merging a document's
        // actions. Keep its commands in the ribbon and Settings menu.
        menuBar->setFixedHeight(0);

        auto *quickAccess = new QToolBar;
        quickAccess->setObjectName(QStringLiteral("ribbonQuickAccess"));
        quickAccess->setMovable(false);
        quickAccess->setFloatable(false);
        quickAccess->setIconSize(QSize(20, 20));
        quickAccess->setToolButtonStyle(Qt::ToolButtonIconOnly);
        quickAccess->setContentsMargins(2, 0, 2, 0);
        quickAccess->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        quickAccess->setStyleSheet(QStringLiteral(
            "#ribbonQuickAccess { color: palette(text); background: palette(base); border: 0; border-radius: 4px; spacing: 2px; }"));
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
        settingsButton->setObjectName(QStringLiteral("ribbonSettings"));
        settingsButton->setEnabled(false);
        settingsButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
        settingsButton->setToolTip(i18n("Settings"));
        settingsButton->setAutoRaise(true);
        settingsButton->setIconSize(QSize(20, 20));
        settingsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        populateRibbonSettingsLater(settingsButton, m_view->mainWindow(), 20);

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
        // The document header bar (brand, document title and the search box)
        // has been removed.  The quick access toolbar now sits directly in the
        // tab bar row, in front of the ribbon tabs, and the commands help
        // (search) button moved next to the settings button behind them.
        auto *searchAction = new QAction(QIcon::fromTheme(QStringLiteral("edit-find")), i18n("Search Commands"), this);
        searchAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Q));
        m_view->addAction(searchAction);
        connect(searchAction, &QAction::triggered, this, [this] { showRibbonCommandSearch(m_view); });
        auto *searchButton = new QToolButton;
        searchButton->setObjectName(QStringLiteral("ribbonSearch"));
        searchButton->setDefaultAction(searchAction);
        searchButton->setText(i18n("Search Commands  (Alt+Q)"));
        searchButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        searchButton->setStyleSheet(QStringLiteral(
            "#ribbonSearch { color: palette(text); background: palette(base); border: 0; border-radius: 4px; padding: 3px 10px; }"
            "#ribbonSearch:hover { background: palette(alternate-base); }"));

        // Trailing group: commands help followed by the settings button.
        auto *trailingControls = new QWidget;
        trailingControls->setObjectName(QStringLiteral("ribbonTrailingControls"));
        auto *trailingLayout = new QHBoxLayout(trailingControls);
        trailingLayout->setContentsMargins(6, 0, 6, 0);
        trailingLayout->setSpacing(6);
        trailingLayout->addWidget(searchButton, 0, Qt::AlignVCenter);
        trailingLayout->addWidget(settingsButton, 0, Qt::AlignVCenter);

        modeBoxFactory.setLeadingWidget(quickAccess);
        modeBoxFactory.setTrailingWidget(trailingControls);

        QDockWidget *modeBox = m_view->mainWindow()->createDockWidget(&modeBoxFactory);
        modeBox->setAllowedAreas(Qt::TopDockWidgetArea);
        m_view->mainWindow()->addDockWidget(Qt::TopDockWidgetArea, modeBox);
        modeBox->show();
        QTimer::singleShot(0, modeBox, [this, modeBox] {
            m_view->mainWindow()->resizeDocks({modeBox}, {modeBox->sizeHint().height()}, Qt::Vertical);
        });
        m_view->mainWindow()->dockerManager()->removeToolOptionsDocker();
        dynamic_cast<KoCanvasObserverBase *>(modeBox)->setObservedCanvas(m_canvas);

        populateFilePageLater(filePage, m_view->mainWindow(), m_view->actionCollection(), 20);

        auto *insertImage = new QAction(QIcon::fromTheme(QStringLiteral("insert-image")), i18n("Insert Image"), this);
        insertImage->setToolTip(i18n("Choose an image to insert into the document"));
        m_view->actionCollection()->addAction(QStringLiteral("insert_picture"), insertImage);
        connect(insertImage, &QAction::triggered, this, [this] {
            QStringList mimeTypes;
            for (const QByteArray &mimeType : QImageReader::supportedMimeTypes()) {
                mimeTypes.append(QString::fromLatin1(mimeType));
            }
            QFileDialog dialog(this, i18n("Insert Image"));
            dialog.setFileMode(QFileDialog::ExistingFile);
            dialog.setAcceptMode(QFileDialog::AcceptOpen);
            dialog.setMimeTypeFilters(mimeTypes);
            if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
                return;
            }
            QImageReader reader(dialog.selectedFiles().first());
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (image.isNull()) {
                QMessageBox::warning(this, i18n("Insert Image"), i18n("Could not load the selected image: %1", reader.errorString()));
                return;
            }
            KoShapeFactoryBase *factory = KoShapeRegistry::instance()->value(QStringLiteral("PictureShape"));
            if (!factory) {
                return;
            }
            KoProperties properties;
            properties.setProperty(QStringLiteral("qimage"), image);
            KoShape *shape = factory->createShape(&properties, m_canvas->shapeController()->resourceManager());
            if (!shape) {
                return;
            }
            const QRectF page = m_view->currentPage().rect().adjusted(36, 36, -36, -36);
            if (page.width() > 0 && page.height() > 0) {
                shape->setSize(shape->size().scaled(page.size(), Qt::KeepAspectRatio).boundedTo(shape->size()));
                shape->setPosition(page.topLeft());
            }
            if (KUndo2Command *command = m_canvas->shapeController()->addShapeDirect(shape)) {
                m_canvas->addCommand(command);
                KoSelection *selection = m_canvas->shapeManager()->selection();
                selection->deselectAll();
                selection->select(shape);
            } else {
                delete shape;
            }
            m_canvas->setFocus(Qt::OtherFocusReason);
        });

        // Microsoft Office 2026 style groups of the Insert tab.
        const QList<RibbonGroup> insertGroups = {
            {i18n("Pages"), {{QStringLiteral("insert_framebreak"), i18n("Page Break")}}},
            {i18n("Tables"), {{QStringLiteral("insert_table"), i18n("Table")}}},
            {i18n("Illustrations"), {{QStringLiteral("add_shape"), i18n("Shapes")}, {QStringLiteral("insert_chart"), i18n("Chart")}, {QStringLiteral("insert_picture"), i18n("Insert Image")}}},
            {i18n("Comments"), {{QStringLiteral("insert_annotation"), i18n("Comment")}}},
            {i18n("Header & Footer"),
             {{QStringLiteral("insert_header"), i18n("Header")},
              {QStringLiteral("insert_footer"), i18n("Footer")},
              {QStringLiteral("insert_variable"), i18n("Page Number")}}},
            {i18n("Symbols"), {{QStringLiteral("insert_specialchar"), i18n("Symbol")}}},
        };
        populateRibbonGroupsLater(insertPage, insertGroups, findRibbonAction, true, 20);

        const QList<RibbonGroup> drawGroups = {
            {i18n("Select"), {{QStringLiteral("InteractionTool"), i18n("Select")}}},
            {i18n("Draw"),
             {{QStringLiteral("KoPencilTool"), i18n("Freehand")},
              {QStringLiteral("CreatePathTool"), i18n("Draw Path")},
              {QStringLiteral("ConnectionTool"), i18n("Connect Shapes")},
              {QStringLiteral("convert-to-path"), i18n("Convert to Path")}}},
            {i18n("Rotate"),
             {{QStringLiteral("rotate-left"), i18n("Rotate Left")},
              {QStringLiteral("rotate-right"), i18n("Rotate Right")}}},
        };
        populateRibbonGroupsLater(drawPage, drawGroups, findDrawAction, true, 20);

        const QList<RibbonGroup> designGroups = {
            {i18n("Document Formatting"),
             {{QStringLiteral("format_stylist"), i18n("Styles")},
              {QStringLiteral("format_font"), i18n("Fonts")},
              {QStringLiteral("format_paragraph"), i18n("Paragraph Spacing")}}},
            {i18n("Page Background"), {{QStringLiteral("border_backgroundcolor"), i18n("Page Color")}}},
        };
        populateRibbonGroupsLater(designPage, designGroups, findRibbonAction, true, 20);

        // Microsoft Office 2026 style groups of the Mailings tab.
        const QList<RibbonGroup> mailingsGroups = {
            {i18n("Start Mail Merge"),
             {{QStringLiteral("mailmerge_select_recipients"), i18n("Select Recipients")},
              {QStringLiteral("mailmerge_edit_recipients"), i18n("Edit Recipient List")}}, RibbonGroup::LargeAndStacked},
            {i18n("Write & Insert Fields"),
             {{QStringLiteral("mailmerge_insert_field"), i18n("Insert Merge Field")},
              {QStringLiteral("mailmerge_greeting_line"), i18n("Greeting Line")},
              {QStringLiteral("mailmerge_highlight"), i18n("Highlight Fields")}}, RibbonGroup::LargeAndStacked},
            {i18n("Preview Results"),
             {{QStringLiteral("mailmerge_preview"), i18n("Preview Results")},
              {QStringLiteral("mailmerge_first"), i18n("First")},
              {QStringLiteral("mailmerge_prev"), i18n("Previous")},
              {QStringLiteral("mailmerge_next"), i18n("Next")},
              {QStringLiteral("mailmerge_last"), i18n("Last")}}, RibbonGroup::PreviewNavigation},
            {i18n("Finish"), {{QStringLiteral("mailmerge_finish"), i18n("Edit Individual Documents")}}},
        };
        populateRibbonGroupsLater(mailingsPage, mailingsGroups, findRibbonAction, true, 20);

        // Microsoft Office 2026 style groups of the View tab.
        const auto viewActions = std::make_shared<QMap<QString, QAction *>>();
        const auto addViewAction = [this, viewActions](const QString &name, const QString &text, const QString &icon) {
            auto *command = new QAction(QIcon::fromTheme(icon), text, this);
            viewActions->insert(name, command);
            return command;
        };
        auto *horizontalRuler = addViewAction(QStringLiteral("ribbon_horizontal_ruler"), i18n("Horiz. ruler"), QStringLiteral("show-ruler"));
        auto *verticalRuler = addViewAction(QStringLiteral("ribbon_vertical_ruler"), i18n("Vert. ruler"), QStringLiteral("show-ruler"));
        for (QAction *ruler : {horizontalRuler, verticalRuler}) {
            ruler->setCheckable(true);
            ruler->setChecked(m_view->kwdocument()->config().viewRulers());
        }
        m_horizontalRuler->setProperty("ribbonRulerVisible", horizontalRuler->isChecked());
        m_verticalRuler->setProperty("ribbonRulerVisible", verticalRuler->isChecked());
        connect(horizontalRuler, &QAction::toggled, this, [this](bool visible) {
            m_horizontalRuler->setProperty("ribbonRulerVisible", visible);
            m_horizontalRuler->setVisible(visible);
        });
        connect(verticalRuler, &QAction::toggled, this, [this](bool visible) {
            m_verticalRuler->setProperty("ribbonRulerVisible", visible);
            m_verticalRuler->setVisible(visible);
        });
        QTimer::singleShot(0, this, [this, findRibbonAction, horizontalRuler, verticalRuler] {
            if (QAction *rulers = findRibbonAction(QStringLiteral("show_ruler"))) {
                connect(rulers, &QAction::toggled, horizontalRuler, &QAction::setChecked);
                connect(rulers, &QAction::toggled, verticalRuler, &QAction::setChecked);
            }
        });
        const auto addZoom = [this, addViewAction](const QString &name, const QString &label, const QString &icon, KoZoomMode::Mode mode, qreal factor) {
            QAction *command = addViewAction(name, label, icon);
            command->setCheckable(true);
            connect(command, &QAction::triggered, this, [this, command, mode, factor] {
                m_view->zoomController()->setZoom(mode, factor);
                command->setChecked(true);
            });
            QTimer::singleShot(0, command, [this, command, mode, factor] {
                const auto sync = [command, mode, factor](KoZoomMode::Mode currentMode, qreal currentFactor) {
                    command->setChecked(currentMode == mode
                                        && (mode != KoZoomMode::ZOOM_CONSTANT || qAbs(currentFactor - factor) < 0.001));
                };
                connect(m_view->zoomController(), &KoZoomController::zoomChanged, command, sync);
                auto *handler = static_cast<KoZoomHandler *>(m_view->viewConverter());
                sync(handler->zoomMode(), handler->zoom());
            });
        };
        auto *setZoom = addViewAction(QStringLiteral("ribbon_set_zoom"), i18n("Set\nzoom"), QStringLiteral("zoom-select"));
        connect(setZoom, &QAction::triggered, this, [this] {
            bool accepted = false;
            const int percent = QInputDialog::getInt(this, i18n("Set Zoom"), i18n("Zoom (%):"),
                                                    qRound(m_view->viewConverter()->zoom() * 100), 10, 500, 10, &accepted);
            if (accepted) {
                m_view->zoomController()->setZoom(KoZoomMode::ZOOM_CONSTANT, percent / 100.0);
            }
        });
        addZoom(QStringLiteral("ribbon_actual_size"), i18n("Actual\nsize"), QStringLiteral("zoom-original"), KoZoomMode::ZOOM_CONSTANT, 1.0);
        addZoom(QStringLiteral("ribbon_fit_width"), i18n("Fit width"), QStringLiteral("zoom-fit-width"), KoZoomMode::ZOOM_WIDTH, 1.0);
        addZoom(QStringLiteral("ribbon_full_page"), i18n("Full page"), QStringLiteral("zoom-fit-best"), KoZoomMode::ZOOM_PAGE, 1.0);
        addZoom(QStringLiteral("ribbon_fit_text"), i18n("Fit margins"), QStringLiteral("zoom-fit-width"), KoZoomMode::ZOOM_TEXT, 1.0);
        auto *zoomMenuAction = addViewAction(QStringLiteral("ribbon_zoom_menu"), i18n("Zoom"), QStringLiteral("zoom-in"));
        auto *zoomMenu = new QMenu(viewPage);
        for (int percent : {50, 75, 100, 125, 150, 200}) {
            auto *preset = zoomMenu->addAction(i18n("%1%", percent));
            connect(preset, &QAction::triggered, this, [this, percent] {
                m_view->zoomController()->setZoom(KoZoomMode::ZOOM_CONSTANT, percent / 100.0);
            });
        }
        zoomMenuAction->setMenu(zoomMenu);
        zoomMenuAction->setProperty("ribbonMenuOnly", true);
        auto *grid = addViewAction(QStringLiteral("ribbon_grid_guides"), i18n("Grid and\nguides"), QStringLiteral("view-grid"));
        auto *gridMenu = new QMenu(viewPage);
        connect(gridMenu, &QMenu::aboutToShow, viewPage, [gridMenu, findRibbonAction] {
            gridMenu->clear();
            for (const QString &name : {QStringLiteral("view_grid"), QStringLiteral("view_snaptogrid"), QStringLiteral("view_show_guides")}) {
                if (QAction *command = findRibbonAction(name)) {
                    gridMenu->addAction(command);
                }
            }
        });
        grid->setMenu(gridMenu);
        grid->setProperty("ribbonMenuOnly", true);
        auto *sidebars = addViewAction(QStringLiteral("ribbon_sidebars"), i18n("Sidebars"), QStringLiteral("view-sidetree"));
        auto *sidebarMenu = new QMenu(viewPage);
        connect(sidebarMenu, &QMenu::aboutToShow, viewPage, [this, sidebarMenu, modeBox] {
            sidebarMenu->clear();
            for (QDockWidget *dock : m_view->mainWindow()->findChildren<QDockWidget *>()) {
                if (dock != modeBox) {
                    sidebarMenu->addAction(dock->toggleViewAction());
                }
            }
        });
        sidebars->setMenu(sidebarMenu);
        sidebars->setProperty("ribbonMenuOnly", true);
        auto *windows = addViewAction(QStringLiteral("ribbon_windows"), i18n("Windows"), QStringLiteral("window-duplicate"));
        auto *windowMenu = new QMenu(viewPage);
        connect(windowMenu, &QMenu::aboutToShow, viewPage, [windowMenu, findRibbonAction] {
            windowMenu->clear();
            if (QAction *newWindow = findRibbonAction(QStringLiteral("view_newview"))) {
                windowMenu->addAction(newWindow);
            }
            windowMenu->addSeparator();
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *window = qobject_cast<KoMainWindow *>(widget)) {
                    if (window->isVisible()) {
                        QAction *activate = windowMenu->addAction(window->windowTitle());
                        const QPointer<KoMainWindow> target(window);
                        connect(activate, &QAction::triggered, windowMenu, [target] {
                            if (target) { target->showNormal(); target->raise(); target->activateWindow(); }
                        });
                    }
                }
            }
        });
        windows->setMenu(windowMenu);
        windows->setProperty("ribbonMenuOnly", true);
        const ActionFinder findViewAction = [viewActions, findDrawAction](const QString &name) {
            return viewActions->contains(name) ? viewActions->value(name) : findDrawAction(name);
        };
        const QList<RibbonGroup> viewGroups = {
            {i18n("Mode"), {{QStringLiteral("InteractionTool"), i18n("Object\nmode")}}},
            {i18n("Views"), {{QStringLiteral("view_fullscreen"), i18n("Full\nscreen")}}},
            {i18n("Fields"),
             {{QStringLiteral("view_fieldshadings"), i18n("Shade fields")},
              {QStringLiteral("view_formattingchars"), i18n("Formatting marks")},
              {QStringLiteral("view_frameborders"), i18n("Text boundaries")}}, RibbonGroup::Stacked},
            {i18n("View"),
             {{QStringLiteral("ribbon_grid_guides"), i18n("Grid and\nguides")},
              {QStringLiteral("ribbon_horizontal_ruler"), i18n("Horiz. ruler")},
              {QStringLiteral("ribbon_vertical_ruler"), i18n("Vert. ruler")}}, RibbonGroup::LargeAndStacked},
            {i18n("Zoom"),
             {{QStringLiteral("ribbon_set_zoom"), i18n("Set\nzoom")},
              {QStringLiteral("ribbon_zoom_menu"), i18n("Zoom")},
              {QStringLiteral("ribbon_actual_size"), i18n("Actual\nsize")},
              {QStringLiteral("ribbon_fit_text"), i18n("Fit margins")},
              {QStringLiteral("ribbon_full_page"), i18n("Full page")},
              {QStringLiteral("ribbon_fit_width"), i18n("Fit width")}}, RibbonGroup::ZoomButtons},
            {i18n("Windows"),
             {{QStringLiteral("ribbon_sidebars"), i18n("Sidebars")},
              {QStringLiteral("ribbon_windows"), i18n("Windows")}}},
        };
        populateRibbonGroupsLater(viewPage, viewGroups, findViewAction, true, 20);

        // Microsoft Office 2026 style groups of the Help tab.
        const QList<RibbonGroup> helpGroups = {
            {i18n("Help"),
             {{QStringLiteral("help_contents"), i18n("Help")},
              {QStringLiteral("help_report_bug"), i18n("Report Bug")},
              {QStringLiteral("help_switch_application_language"), i18n("Switch Language")}}},
            {i18n("About"),
             {{QStringLiteral("help_about_app"), i18n("About Calligra Words")},
              {QStringLiteral("help_about_kde"), i18n("About KDE")}}},
        };
        populateRibbonGroupsLater(helpPage, helpGroups, findRibbonAction, true, 20);
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
    for (KoRuler *ruler : {m_verticalRuler, m_horizontalRuler}) {
        const QVariant individual = ruler->property("ribbonRulerVisible");
        ruler->setVisible(individual.isValid() ? individual.toBool() : m_view->kwdocument()->config().viewRulers());
    }
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
