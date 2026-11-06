/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "KoAppRibbon.h"

#include "KoMainWindow.h"
#include "KoView.h"
#include "KoDocument.h"
#include <KoCanvasBase.h>
#include <KoCanvasResourceManager.h>
#include <KoCreateShapesTool.h>
#include <KoModeBoxFactory.h>
#include <KoProperties.h>
#include <KoRibbonLayout.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeController.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeManager.h>
#include <KoShapeRegistry.h>
#include <KoToolManager.h>
#include <KoToolBase.h>
#include <KoTextEditor.h>
#include <KActionCollection>
#include <KLocalizedString>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace
{
struct Page {
    QWidget *widget;
    QStringList menus;
};

QString label(QString text)
{
    return text.remove(QLatin1Char('&'));
}

QMenu *findMenu(KoMainWindow *window, const QString &name)
{
    for (QAction *action : window->menuBar()->actions()) {
        if (action->menu() && action->menu()->objectName().compare(name, Qt::CaseInsensitive) == 0) {
            return action->menu();
        }
    }
    return nullptr;
}

QAction *findAction(KoView *view, const QString &name)
{
    if (QAction *action = view->actionCollection()->action(name)) {
        return action;
    }
    return view->mainWindow()->actionCollection()->action(name);
}

class RibbonActionButton : public QToolButton
{
public:
    RibbonActionButton(KoView *view, QAction *action, QWidget *parent)
        : QToolButton(parent)
        , m_view(view)
        , m_original(action)
        , m_name(action->objectName())
    {
        bind();
        // Switching between cell, text, and shape tools can replace actions
        // with the same name in the view's collection.
        connect(KoToolManager::instance(), &KoToolManager::changedTool, this, [this] { bind(); });
    }

private:
    void bind()
    {
        QAction *action = m_view && !m_name.isEmpty() ? findAction(m_view, m_name) : nullptr;
        if (!action) {
            action = m_original;
        }
        setDefaultAction(action);
        if (!action) {
            setEnabled(false);
            return;
        }
        setAccessibleName(label(action->text()));
        setPopupMode(action->menu() ? QToolButton::InstantPopup : QToolButton::DelayedPopup);
        if (action->icon().isNull()) {
            setIcon(QIcon::fromTheme(QStringLiteral("applications-office")));
        }
    }

    QPointer<KoView> m_view;
    QPointer<QAction> m_original;
    QString m_name;
};

void addGroup(QWidget *page, const QString &title, const QList<QAction *> &actions, int largeButtons = -1)
{
    if (actions.isEmpty()) {
        return;
    }
    auto *layout = qobject_cast<QHBoxLayout *>(page->layout());
    if (!layout) {
        layout = new QHBoxLayout(page);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(0);
        layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }
    auto *group = new QWidget(page);
    group->setProperty("ribbonRoutedGroup", true);
    if (page->objectName() == QLatin1String("stageHomePage") && title == i18n("Slides"))
        group->setObjectName(QStringLiteral("stageSlidesGroup"));
    if (page->objectName() == QLatin1String("stageHomePage")) {
        const QStringList order = {i18n("Clipboard"), i18n("Slides"), i18n("Font"), i18n("Paragraph"), i18n("Drawing"), i18n("Editing"), i18n("More Editing")};
        group->setProperty("ribbonGroupOrder", (order.indexOf(title) + 1) * 10);
    }
    if (page->objectName() == QLatin1String("sheetsHomePage"))
        group->setProperty("ribbonGroupOrder", title == i18n("Clipboard") ? 10 : 70);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(8, 0, 8, 0);
    vertical->setSpacing(2);
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(3);
    const int largeCount = largeButtons < 0 ? (actions.size() <= 4 ? actions.size() : 0) : largeButtons;
    int position = 0;
    for (QAction *action : actions) {
        if (!action || action->isSeparator()) {
            continue;
        }
        QWidget *control = nullptr;
        if (auto *widgetAction = qobject_cast<QWidgetAction *>(action); widgetAction && !action->menu()) {
            control = widgetAction->requestWidget(group);
        }
        if (!control) {
            auto *view = qobject_cast<KoView *>(page->property("ribbonView").value<QObject *>());
            auto *button = new RibbonActionButton(view, action, group);
            button->setObjectName(QStringLiteral("ribbon_") + action->objectName());
            button->setAccessibleName(label(action->text()));
            if (action->menu()) {
                button->setPopupMode(QToolButton::InstantPopup);
            }
            const bool large = position < largeCount;
            KoRibbonLayout::configureButton(button, large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
            if (button->icon().isNull()) {
                button->setIcon(QIcon::fromTheme(QStringLiteral("applications-office")));
            }
            control = button;
        }
        if (position < largeCount) {
            grid->addWidget(control, 0, position++, 3, 1, Qt::AlignTop);
        } else {
            const int smallPosition = position - largeCount;
            grid->addWidget(control, smallPosition % 3, largeCount + smallPosition / 3, Qt::AlignLeft);
            ++position;
        }
    }
    vertical->addLayout(grid);
    vertical->addStretch();
    auto *caption = new QLabel(title, group);
    caption->setAlignment(Qt::AlignCenter);
    caption->setProperty("ribbonGroupTitle", true);
    vertical->addWidget(caption);
    group->setMinimumHeight(112);
    int positionInPage = 0;
    const QVariant order = group->property("ribbonGroupOrder");
    while (positionInPage < layout->count() && layout->itemAt(positionInPage)->widget()) {
        const QVariant other = layout->itemAt(positionInPage)->widget()->property("ribbonGroupOrder");
        if (order.isValid() && (other.isValid() ? other.toInt() : 1000) > order.toInt())
            break;
        ++positionInPage;
    }
    layout->insertWidget(positionInPage, group);
}

void populateStage(KoView *view, KoCanvasBase *canvas, const QList<Page> &pages)
{
    // Use familiar PowerPoint command names and semantic theme icons while
    // retaining the native actions, shortcuts and enabled/checked state.
    struct Command { const char *id; QString text; const char *icon; };
    const Command commands[] = {
        {"page_insertpage", i18n("New Slide"), "view-presentation"},
        {"page_copypage", i18n("Duplicate Slide"), "edit-copy"},
        {"page_deletepage", i18n("Delete Slide"), "edit-delete"},
        {"insert_table", i18n("Table"), "insert-table"},
        {"insert_picture", i18n("Pictures"), "insert-image"},
        {"insert_pictures", i18n("Photo Album"), "insert-image"},
        {"add_shape", i18n("Shapes"), "draw-rectangle"},
        {"insert_chart", i18n("Chart"), "office-chart-bar"},
        {"stage_insert_textbox", i18n("Text Box"), "draw-text"},
        {"insert_specialchar", i18n("Symbol"), "character-set"},
        {"import_document", i18n("Reuse Slides"), "document-import"},
        {"format_masterpage", i18n("Slide Design"), "view-presentation"},
        {"format_pagelayout", i18n("Slide Size"), "document-properties"},
        {"slideshow_start_from_beginning", i18n("From Beginning"), "view-presentation"},
        {"slideshow_start_from_current", i18n("From Current Slide"), "media-playback-start"},
        {"slideshow_configure", i18n("Set Up Slide Show"), "configure"},
        {"edit_customslideshows", i18n("Custom Slide Show"), "view-list-details"},
        {"slideshow_presenterview", i18n("Presenter View"), "video-display"},
        {"tools_spelling", i18n("Spelling"), "tools-check-spelling"},
        {"insert_annotation", i18n("New Comment"), "edit-comment"},
        {"view_normal", i18n("Normal"), "view-presentation"},
        {"view_notes", i18n("Notes Page"), "document-edit"},
        {"view_slides_sorter", i18n("Slide Sorter"), "view-grid"},
        {"view_masterpages", i18n("Slide Master"), "view-presentation"},
        {"view_newview", i18n("New Window"), "window-new"},
        {"view_rulers", i18n("Ruler"), "measure"},
        {"view_grid", i18n("Gridlines"), "view-grid"},
        {"view_show_guides", i18n("Guides"), "snap-guideline"}
    };
    const Command drawingTools[] = {
        {"InteractionTool", i18n("Select"), "edit-select"},
        {"KoPencilTool", i18n("Pen"), "draw-freehand"},
        {"CreatePathTool", i18n("Freeform"), "draw-path"},
        {"ConnectionTool", i18n("Connector"), "draw-connector"}
    };
    for (const Command &command : drawingTools) {
        if (!findAction(view, QLatin1String(command.id))
            && KoToolManager::instance()->toolById(canvas, QLatin1String(command.id))) {
            auto *action = new QAction(QIcon::fromTheme(QLatin1String(command.icon)), command.text, view);
            const QString toolId = QLatin1String(command.id);
            view->actionCollection()->addAction(toolId, action);
            QObject::connect(action, &QAction::triggered, view, [canvas, toolId] {
                KoToolManager::instance()->switchToolRequested(toolId);
                canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
            });
        }
    }
    for (const Command &command : commands) {
        if (QAction *action = findAction(view, QLatin1String(command.id))) {
            action->setText(command.text);
            action->setIconText(command.text);
            action->setIcon(QIcon::fromTheme(QLatin1String(command.icon), action->icon()));
        }
    }
    const auto page = [&](const char *id) -> QWidget * {
        for (const Page &entry : pages) {
            if (entry.widget->objectName() == QLatin1String(id)) {
                return entry.widget;
            }
        }
        return nullptr;
    };
    const auto group = [&](const char *id, const QString &title, const QStringList &names, int large = 0) {
        QList<QAction *> actions;
        for (const QString &name : names) {
            if (QAction *action = findAction(view, name)) {
                actions.append(action);
            }
        }
        if (QWidget *target = page(id)) {
            addGroup(target, title, actions, large);
        }
    };
    group("stageHomePage", i18n("Clipboard"), {"edit_paste", "edit_cut", "edit_copy"}, 1);
    group("stageHomePage", i18n("Slides"), {"page_insertpage", "page_copypage", "page_deletepage"}, 1);
    group("stageHomePage", i18n("Drawing"), {"add_shape"}, 1);
    group("stageHomePage", i18n("Editing"), {"edit_find", "edit_replace", "edit_select_all"});
    group("stageInsertPage", i18n("Slides"), {"page_insertpage", "import_document"}, 1);
    group("stageInsertPage", i18n("Tables"), {"insert_table"}, 1);
    group("stageInsertPage", i18n("Images"), {"insert_picture", "insert_pictures"}, 1);
    group("stageInsertPage", i18n("Illustrations"), {"add_shape", "insert_chart"}, 2);
    group("stageInsertPage", i18n("Text"), {"stage_insert_textbox", "insert_variable"}, 1);
    group("stageInsertPage", i18n("Symbols"), {"insert_specialchar"}, 1);
    group("stageDrawPage", i18n("Tools"), {"InteractionTool", "KoPencilTool", "CreatePathTool", "ConnectionTool"}, 1);
    group("stageDrawPage", i18n("Shapes"), {"add_shape", "convert-to-path"}, 1);
    group("stageDesignPage", i18n("Themes"), {"format_masterpage"}, 1);
    group("stageDesignPage", i18n("Customize"), {"format_pagelayout"}, 1);
    group("stageSlideShowPage", i18n("Start Slide Show"), {"slideshow_start_from_beginning", "slideshow_start_from_current"}, 2);
    group("stageSlideShowPage", i18n("Set Up"), {"slideshow_configure", "edit_customslideshows", "slideshow_presenterview"}, 1);
    group("stageReviewPage", i18n("Proofing"), {"tools_spelling", "tool_auto_spellcheck", "tool_configure_spellcheck"}, 1);
    group("stageReviewPage", i18n("Comments"), {"insert_annotation"}, 1);
    group("stageViewPage", i18n("Presentation Views"), {"view_normal", "view_notes", "view_slides_sorter"}, 3);
    group("stageViewPage", i18n("Master Views"), {"view_masterpages"}, 1);
    group("stageViewPage", i18n("Show"), {"view_rulers", "view_grid", "view_show_guides", "view_snaptogrid", "view_page_margins"});
    group("stageViewPage", i18n("Zoom"), {"zoom_out", "zoom_in", "view_zoom", "view_fullscreen"}, 1);
    group("stageViewPage", i18n("Window"), {"view_newview"}, 1);

    // Keep the complete native menus accessible without spreading their
    // separators into anonymous ribbon groups.
    if (QMenu *edit = findMenu(view->mainWindow(), QStringLiteral("edit"))) {
        addGroup(page("stageHomePage"), i18n("More Editing"), {edit->menuAction()}, 0);
    }
    if (QMenu *format = findMenu(view->mainWindow(), QStringLiteral("format"))) {
        addGroup(page("stageShapeFormatPage"), i18n("Format"), {format->menuAction()}, 0);
    }
    if (auto *box = view->mainWindow()->findChild<QWidget *>(QStringLiteral("wordsRibbon"))) {
        for (const QString &tool : {QStringLiteral("TextToolFactory_ID"), QStringLiteral("InteractionTool"),
                                    QStringLiteral("KoPABackgroundTool"), QStringLiteral("ConnectionTool"), QStringLiteral("CreatePathTool")}) {
            QMetaObject::invokeMethod(box, "primeToolOptions", Qt::DirectConnection, Q_ARG(QString, tool));
        }
        QMetaObject::invokeMethod(box, "showRibbonPage", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("stageHomePage")));
    }
    auto *shapePage = page("stageShapeFormatPage");
    auto *tablePage = page("stageTableFormatPage");
    const auto updateContext = [=] {
        shapePage->setProperty("ribbonContextActive", !canvas->shapeManager()->selection()->selectedShapes().isEmpty());
        KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(canvas);
        tablePage->setProperty("ribbonContextActive", editor && editor->currentTable());
    };
    QObject::connect(canvas->shapeManager()->selection(), &KoSelection::selectionChanged, shapePage, updateContext);
    QObject::connect(canvas->resourceManager(), &KoCanvasResourceManager::canvasResourceChanged, tablePage, [=](int, const QVariant &) { updateContext(); });
    updateContext();
}

void addMenu(QWidget *page, QMenu *menu)
{
    if (!menu) {
        return;
    }
    QList<QAction *> group;
    const QString title = label(menu->title());
    for (QAction *action : menu->actions()) {
        if (action->isSeparator()) {
            addGroup(page, title, group);
            group.clear();
        } else {
            group.append(action);
        }
    }
    addGroup(page, title, group);
}

void collectActions(QMenu *menu, QList<QAction *> &actions, QSet<QAction *> &seen)
{
    for (QAction *action : menu->actions()) {
        if (action->menu()) {
            collectActions(action->menu(), actions, seen);
        } else if (!action->isSeparator() && !seen.contains(action) && !qobject_cast<QWidgetAction *>(action)) {
            seen.insert(action);
            actions.append(action);
        }
    }
}

void searchCommands(KoView *view)
{
    QDialog dialog(view);
    dialog.setWindowTitle(i18n("Search Commands"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit(&dialog);
    search->setPlaceholderText(i18n("Type a command name"));
    search->setAccessibleName(i18n("Search Commands"));
    auto *list = new QListWidget(&dialog);
    layout->addWidget(search);
    layout->addWidget(list);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QList<QAction *> actions;
    QSet<QAction *> seen;
    for (QAction *action : view->mainWindow()->menuBar()->actions()) {
        if (action->menu()) {
            collectActions(action->menu(), actions, seen);
        }
    }
    for (QAction *action : view->actionCollection()->actions()) {
        if (!seen.contains(action) && !action->isSeparator() && !action->menu() && !qobject_cast<QWidgetAction *>(action)) {
            seen.insert(action);
            actions.append(action);
        }
    }
    QList<QPointer<QAction>> matches;
    const auto filter = [=, &matches] {
        list->clear();
        matches.clear();
        for (QAction *action : actions) {
            if (QAction *current = findAction(view, action->objectName())) {
                action = current;
            }
            const QString text = label(action->text());
            if (!action->isEnabled() || !text.contains(search->text(), Qt::CaseInsensitive)) {
                continue;
            }
            matches.append(action);
            list->addItem(new QListWidgetItem(action->icon(), text));
        }
        if (list->count()) {
            list->setCurrentRow(0);
        }
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, filter);
    QObject::connect(search, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    QObject::connect(list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    filter();
    dialog.resize(520, 420);
    search->setFocus();
    if (dialog.exec() == QDialog::Accepted && list->currentRow() >= 0) {
        const QPointer<QAction> action = matches.value(list->currentRow());
        if (action && action->isEnabled()) {
            action->trigger();
        }
    }
}

void populate(KoView *view, KoCanvasBase *canvas, const QList<Page> &pages, QWidget *home, QToolBar *quick, QToolButton *settings, bool spreadsheet, int attempts)
{
    KoMainWindow *window = view->mainWindow();
    if (!window) {
        return;
    }
    // XMLGUI merges the view's menus after its constructor has returned.
    if (!findMenu(window, QStringLiteral("view")) || !findAction(view, QStringLiteral("file_save"))) {
        if (attempts > 0) {
            QTimer::singleShot(100, view, [=] { populate(view, canvas, pages, home, quick, settings, spreadsheet, attempts - 1); });
        }
        return;
    }
    for (const Page &page : pages) {
        for (const QString &name : page.menus) {
            addMenu(page.widget, findMenu(window, name));
        }
    }
    if (!spreadsheet) {
        populateStage(view, canvas, pages);
    }
    QList<QAction *> clipboard;
    const QStringList clipboardNames = spreadsheet
        ? QStringList{QStringLiteral("paste"), QStringLiteral("cut"), QStringLiteral("copy"), QStringLiteral("specialPaste")}
        : QStringList{QStringLiteral("edit_paste"), QStringLiteral("edit_cut"), QStringLiteral("edit_copy")};
    for (const QString &name : clipboardNames) {
        if (QAction *action = findAction(view, name)) {
            clipboard.append(action);
        }
    }
    if (spreadsheet) {
        addGroup(home, i18n("Clipboard"), clipboard);
    }
    if (QMenu *edit = spreadsheet ? findMenu(window, QStringLiteral("edit")) : nullptr) {
        addGroup(home, i18n("Editing"), {edit->menuAction()});
    }
    if (spreadsheet) {
        if (auto *box = window->findChild<QWidget *>(QStringLiteral("wordsRibbon"))) {
            for (const QString &tool : {QStringLiteral("KSpreadCellToolId"), QStringLiteral("InteractionTool"),
                                        QStringLiteral("ConnectionTool"), QStringLiteral("CreatePathTool")})
                QMetaObject::invokeMethod(box, "primeToolOptions", Qt::DirectConnection, Q_ARG(QString, tool));
            QMetaObject::invokeMethod(box, "showRibbonPage", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("sheetsHomePage")));
        }
        for (const Page &page : pages) {
            if (page.widget->objectName() != QLatin1String("sheetsDrawPage"))
                continue;
            QList<QAction *> actions;
            const QStringList tools = {QStringLiteral("InteractionTool"), QStringLiteral("KoPencilTool"),
                                       QStringLiteral("CreatePathTool"), QStringLiteral("ConnectionTool")};
            const QStringList titles = {i18n("Select"), i18n("Freehand"), i18n("Freeform"), i18n("Connector")};
            const QStringList icons = {QStringLiteral("edit-select"), QStringLiteral("draw-freehand"),
                                       QStringLiteral("draw-path"), QStringLiteral("draw-connector")};
            for (int i = 0; i < tools.size(); ++i) {
                if (!KoToolManager::instance()->toolById(canvas, tools[i]))
                    continue;
                auto *action = new QAction(QIcon::fromTheme(icons[i]), titles[i], view);
                const QString toolId = tools[i];
                action->setObjectName(toolId);
                QObject::connect(action, &QAction::triggered, view, [canvas, toolId] {
                    KoToolManager::instance()->switchToolRequested(toolId);
                    canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
                });
                actions.append(action);
            }
            addGroup(page.widget, i18n("Tools"), actions, 1);
            if (QAction *shapes = findAction(view, QStringLiteral("add_shape")))
                addGroup(page.widget, i18n("Shapes"), {shapes}, 1);
        }
    }
    for (const QString &name : {QStringLiteral("file_new"), QStringLiteral("file_open"), QStringLiteral("file_save"),
                                QStringLiteral("edit_undo"), QStringLiteral("edit_redo"), QStringLiteral("file_print")}) {
        if (QAction *action = findAction(view, name)) {
            quick->addAction(action);
        }
    }
    if (QMenu *menu = findMenu(window, QStringLiteral("settings"))) {
        settings->setMenu(menu);
        settings->setEnabled(true);
    }
    // Keep XMLGUI containers alive: the template chooser uses mainToolBar.
    for (QToolBar *toolbar : window->findChildren<QToolBar *>()) {
        if (toolbar == quick) {
            continue;
        }
        toolbar->hide();
        QObject::connect(toolbar, &QToolBar::visibilityChanged, home, [toolbar](bool visible) {
            if (visible) {
                QTimer::singleShot(0, toolbar, &QWidget::hide);
            }
        });
    }
    window->menuBar()->hide();
}
}

void KoAppRibbon::configure(KoModeBoxFactory &factory, KoView *view, KoCanvasBase *canvas, bool spreadsheet)
{
    auto *imageAction = new QAction(QIcon::fromTheme(QStringLiteral("insert-image")), i18n("Insert Image"), view);
    view->actionCollection()->addAction(QStringLiteral("insert_picture"), imageAction);
    imageAction->setEnabled(view->koDocument()->isReadWrite());
    QObject::connect(imageAction, &QAction::triggered, view, [view, canvas] {
        if (!view->koDocument()->isReadWrite()) {
            return;
        }
        QFileDialog picker(view, i18n("Insert Image"));
        picker.setFileMode(QFileDialog::ExistingFile);
        picker.setAcceptMode(QFileDialog::AcceptOpen);
        QStringList imagePatterns;
        for (const QByteArray &format : QImageReader::supportedImageFormats()) {
            imagePatterns.append(QStringLiteral("*.") + QString::fromLatin1(format));
        }
        picker.setNameFilter(i18n("Images (%1)", imagePatterns.join(QLatin1Char(' '))));
        if (picker.exec() != QDialog::Accepted || picker.selectedFiles().isEmpty()) {
            return;
        }
        QImageReader reader(picker.selectedFiles().first());
        reader.setAutoTransform(true);
        const QImage image = reader.read();
        if (image.isNull()) {
            QMessageBox::warning(view, i18n("Insert Image"), i18n("Could not load the selected image: %1", reader.errorString()));
            return;
        }
        KoShapeFactoryBase *shapeFactory = KoShapeRegistry::instance()->value(QStringLiteral("PictureShape"));
        if (!shapeFactory) {
            return;
        }
        KoProperties properties;
        properties.setProperty(QStringLiteral("qimage"), image);
        KoShape *shape = shapeFactory->createShape(&properties, canvas->shapeController()->resourceManager());
        if (!shape) {
            return;
        }
        // Center new pictures in the visible document area, keeping large
        // images within the viewport while preserving their aspect ratio.
        QRect visible = canvas->canvasWidget()->visibleRegion().boundingRect();
        if (visible.isEmpty()) {
            visible = canvas->canvasWidget()->rect();
        }
        const QRectF area(canvas->viewToDocument(visible.topLeft()), canvas->viewToDocument(visible.bottomRight()));
        if (area.width() > 0 && area.height() > 0) {
            shape->setSize(shape->size().scaled(area.size() * 0.7, Qt::KeepAspectRatio).boundedTo(shape->size()));
            const QPointF position = area.center() - QPointF(shape->size().width() / 2, shape->size().height() / 2);
            shape->setPosition(QPointF(qMax(0.0, position.x()), qMax(0.0, position.y())));
        }
        int top = 0;
        for (KoShape *existing : canvas->shapeManager()->shapes()) {
            top = qMax(top, existing->zIndex());
        }
        shape->setZIndex(top + 1);
        if (KUndo2Command *command = canvas->shapeController()->addShapeDirect(shape)) {
            canvas->addCommand(command);
            canvas->shapeManager()->selection()->deselectAll();
            canvas->shapeManager()->selection()->select(shape);
            canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
        } else {
            delete shape;
        }
    });
    const QString homeTool = QStringLiteral("KSpreadCellToolId");
    QList<Page> pages;
    const auto addPage = [&](const QString &title, const QStringList &menus, bool first = false, bool last = false) {
        auto *page = new QWidget;
        page->setProperty("ribbonView", QVariant::fromValue<QObject *>(view));
        page->setObjectName(QStringLiteral("appRibbon_") + menus.join(QLatin1Char('_')));
        page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        factory.addCustomPage(title, QIcon(), title, page, first, last, homeTool);
        pages.append({page, menus});
        return page;
    };
    QWidget *home = nullptr;
    if (!spreadsheet) {
        auto *textBox = new QAction(QIcon::fromTheme(QStringLiteral("insert-text")), i18n("Text Box"), view);
        view->actionCollection()->addAction(QStringLiteral("stage_insert_textbox"), textBox);
        QObject::connect(textBox, &QAction::triggered, view, [view, canvas] {
            if (!view->koDocument()->isReadWrite()) {
                return;
            }
            if (KoCreateShapesTool *tool = KoToolManager::instance()->shapeCreatorTool(canvas)) {
                tool->setShapeId(QStringLiteral("TextShapeID"));
                tool->setShapeProperties(nullptr);
                KoToolManager::instance()->switchToolRequested(KoCreateShapesTool_ID);
                canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
            }
        });
        const auto stagePage = [&](const QString &title, const QString &id, const QStringList &menus = {}) {
            auto *widget = new QWidget;
            widget->setObjectName(id);
            widget->setProperty("ribbonView", QVariant::fromValue<QObject *>(view));
            factory.addCustomPage(title, QIcon(), title, widget, true);
            pages.append({widget, menus});
            return widget;
        };
        stagePage(i18n("File"), QStringLiteral("stageFilePage"), {QStringLiteral("file")});
        home = stagePage(i18n("Home"), QStringLiteral("stageHomePage"));
        home->setProperty("ribbonDefaultTool", QStringLiteral("InteractionTool"));
        stagePage(i18n("Insert"), QStringLiteral("stageInsertPage"));
        stagePage(i18n("Draw"), QStringLiteral("stageDrawPage"));
        auto *design = stagePage(i18n("Design"), QStringLiteral("stageDesignPage"));
        design->setProperty("ribbonActivateTool", QStringLiteral("KoPABackgroundTool"));
        stagePage(i18n("Transitions"), QStringLiteral("stageTransitionsPage"));
        auto *animations = stagePage(i18n("Animations"), QStringLiteral("stageAnimationsPage"));
        animations->setProperty("ribbonActivateTool", QStringLiteral("Animation Tool"));
        stagePage(i18n("Slide Show"), QStringLiteral("stageSlideShowPage"));
        stagePage(i18n("Review"), QStringLiteral("stageReviewPage"));
        stagePage(i18n("View"), QStringLiteral("stageViewPage"));
        auto *shapeFormat = stagePage(i18n("Shape Format"), QStringLiteral("stageShapeFormatPage"));
        shapeFormat->setProperty("ribbonContextActive", false);
        auto *tableFormat = stagePage(i18n("Table Layout"), QStringLiteral("stageTableFormatPage"));
        tableFormat->setProperty("ribbonContextActive", false);
        stagePage(i18n("Help"), QStringLiteral("stageHelpPage"), {QStringLiteral("help")});
    } else {
        addPage(i18n("File"), {QStringLiteral("file")}, true);
        home = addPage(i18n("Home"), {}, true);
        home->setObjectName(QStringLiteral("sheetsHomePage"));
        home->setProperty("ribbonDefaultTool", homeTool);
        home->setProperty("ribbonActivateTool", homeTool);
        addPage(i18n("Insert"), {QStringLiteral("insert")});
        auto *draw = addPage(i18n("Draw"), {});
        draw->setObjectName(QStringLiteral("sheetsDrawPage"));
        addPage(i18n("Page Layout"), {QStringLiteral("format")});
        addPage(i18n("Data"), {QStringLiteral("data")});
        addPage(i18n("Formulas and Review"), {QStringLiteral("tools")});
        addPage(i18n("Navigate"), {QStringLiteral("go")}, false, true);
        addPage(i18n("View"), {QStringLiteral("view")}, false, true);
        auto *shapeFormat = addPage(i18n("Shape Format"), {}, false, true);
        shapeFormat->setObjectName(QStringLiteral("sheetsShapeFormatPage"));
        shapeFormat->setProperty("ribbonContextActive", false);
        const auto updateContext = [canvas, shapeFormat] {
            shapeFormat->setProperty("ribbonContextActive", !canvas->shapeManager()->selection()->selectedShapes().isEmpty());
        };
        QObject::connect(canvas->shapeManager()->selection(), &KoSelection::selectionChanged, shapeFormat, updateContext);
        updateContext();
        addPage(i18n("Help"), {QStringLiteral("help")}, false, true);
    }

    auto *quick = new QToolBar;
    quick->setObjectName(QStringLiteral("ribbonQuickAccess"));
    quick->setIconSize(QSize(18, 18));
    quick->setMovable(false);
    quick->setFloatable(false);
    factory.setLeadingWidget(quick);

    auto *trailing = new QWidget;
    auto *layout = new QHBoxLayout(trailing);
    layout->setContentsMargins(6, 0, 6, 0);
    auto *search = new QAction(QIcon::fromTheme(QStringLiteral("edit-find")), i18n("Search Commands"), view);
    search->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Q));
    view->addAction(search);
    QObject::connect(search, &QAction::triggered, view, [view] { searchCommands(view); });
    auto *searchButton = new QToolButton(trailing);
    searchButton->setDefaultAction(search);
    searchButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    searchButton->setAutoRaise(true);
    layout->addWidget(searchButton);
    auto *settings = new QToolButton(trailing);
    settings->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    settings->setToolTip(i18n("Settings"));
    settings->setAccessibleName(i18n("Settings"));
    settings->setAutoRaise(true);
    settings->setPopupMode(QToolButton::InstantPopup);
    settings->setEnabled(false);
    layout->addWidget(settings);
    factory.setTrailingWidget(trailing);
    QTimer::singleShot(0, view, [=] { populate(view, canvas, pages, home, quick, settings, spreadsheet, 50); });
}
