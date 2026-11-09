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
#include <KoRibbonIcons.h>
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
#include <QHash>
#include <QLabel>
#include <QImageReader>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QSet>
#include <QStandardPaths>
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
        // A button does not pick the drop-down up from its default action, so a
        // command family (Fill, function categories, ...) needs it set here.
        setMenu(action->menu());
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
    if (page->objectName() == QLatin1String("sheetsHomePage")) {
        // Clipboard, then the tool option groups (Font 20, Alignment 30,
        // Number 40), then the Styles, Cells and Editing groups.
        int order = 100;
        if (title == i18n("Clipboard"))
            order = 10;
        else if (title == i18n("Styles"))
            order = 50;
        else if (title == i18n("Cells"))
            order = 60;
        else if (title == i18n("Editing"))
            order = 70;
        group->setProperty("ribbonGroupOrder", order);
    }
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(6, 0, 6, 0);
    vertical->setSpacing(2);
    // A squeezed caption is worse than a scrollbar: report the real minimum.
    vertical->setSizeConstraint(QLayout::SetMinimumSize);
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(3);
    // Sheets keeps the ribbon compact: large commands span both rows, while
    // the Function Library uses three stacked category rows like the Office
    // formula ribbon.
    const bool compact = page->objectName().startsWith(QLatin1String("sheets"));
    const bool formulaLibrary = compact && page->objectName() == QLatin1String("sheetsFormulasPage")
        && title == i18n("Function Library");
    const int rows = formulaLibrary ? 3 : (compact ? 2 : 3);
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
            const bool iconOnly = action->property("ribbonIconOnly").toBool();
            KoRibbonLayout::configureButton(button, iconOnly ? Qt::ToolButtonIconOnly
                                                            : (large ? Qt::ToolButtonTextUnderIcon
                                                                     : Qt::ToolButtonTextBesideIcon));
            if (iconOnly) {
                button->setToolTip(action->text());
            }
            // Small command buttons drop their labels when the tab would
            // otherwise overflow; the tooltip keeps the full caption.
            else if (!large) {
                button->setProperty("ribbonCompactable", true);
            }
            if (button->icon().isNull()) {
                button->setIcon(QIcon::fromTheme(QStringLiteral("applications-office")));
            }
            control = button;
        }
        if (position < largeCount) {
            grid->addWidget(control, 0, position++, rows, 1, Qt::AlignTop);
        } else {
            const int smallPosition = position - largeCount;
            // Centre the stacked commands against the large command beside
            // them, so their icons line up with its icon.
            grid->addWidget(control, smallPosition % rows, largeCount + smallPosition / rows, Qt::AlignLeft | Qt::AlignVCenter);
            ++position;
        }
    }
    vertical->addLayout(grid);
    vertical->addStretch();
    auto *caption = new QLabel(title, group);
    caption->setAlignment(Qt::AlignCenter);
    caption->setProperty("ribbonGroupTitle", true);
    caption->ensurePolished();
    caption->setMinimumHeight(caption->sizeHint().height());
    vertical->addWidget(caption);
    group->setMinimumHeight(compact ? 0 : 112);
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

// A ribbon button whose drop-down holds the native actions of one command
// family (Fill, Sort & Filter, ...), like the arrow buttons in Office.
QAction *submenuAction(KoView *view, const QString &title, const char *iconName, const QStringList &names)
{
    QMenu *menu = nullptr;
    for (const QString &name : names) {
        QAction *action = findAction(view, name);
        if (!action) {
            continue;
        }
        if (!menu) {
            menu = new QMenu(title, view);
        }
        menu->addAction(action);
    }
    if (!menu) {
        return nullptr;
    }
    // The menu button uses the shipped icon of the command family, or else
    // the icon of its first entry.
    QIcon icon = KoRibbonIcons::forCommand(QLatin1String(iconName));
    if (icon.isNull() && !menu->actions().isEmpty()) {
        icon = menu->actions().constFirst()->icon();
    }
    auto *action = new QAction(icon, title, view);
    action->setMenu(menu);
    return action;
}

void addSheetGroup(KoView *view, QWidget *page, const QString &title, const QStringList &names, int large = 0)
{
    QList<QAction *> actions;
    for (const QString &name : names) {
        if (QAction *action = findAction(view, name)) {
            actions.append(action);
        }
    }
    addGroup(page, title, actions, large);
}

// Show these commands as compact icon-only buttons in the next group that
// contains them, instead of hiding them in a drop-down.
void markRibbonIconOnly(KoView *view, const QStringList &names)
{
    for (const QString &name : names) {
        if (QAction *action = findAction(view, name)) {
            action->setProperty("ribbonIconOnly", true);
        }
    }
}

void addMenu(QWidget *page, QMenu *menu);

QWidget *sheetPage(const QList<Page> &pages, const char *id)
{
    for (const Page &entry : pages) {
        if (entry.widget->objectName() == QLatin1String(id)) {
            return entry.widget;
        }
    }
    return nullptr;
}

void populateSheetsFile(KoView *view, QWidget *page)
{
    if (!page)
        return;
    addSheetGroup(view, page, i18n("File"), {"file_new", "file_open", "file_save", "file_save_as", "file_print"}, 1);
    addSheetGroup(view, page, i18n("Template"), {"createTemplate"}, 1);
    addSheetGroup(view, page, i18n("Security"), {"file_encrypt_doc", "file_uncompress_doc"}, 1);
    // Recent files, export and the remaining standard commands stay one click away.
    if (QMenu *menu = findMenu(view->mainWindow(), QStringLiteral("file"))) {
        addGroup(page, i18n("More"), {menu->menuAction()}, 0);
    }
}

void populateSheetsInsert(KoView *view, QWidget *page)
{
    if (!page)
        return;
    addSheetGroup(view, page, i18n("Cells"), {"insertCell", "insertRow", "insertColumn", "insertSheet"}, 1);
    addSheetGroup(view, page, i18n("Shapes"), {"add_shape"}, 1);
    addSheetGroup(view, page, i18n("Illustrations"), {"insert_chart", "insert_picture"}, 0);
    addSheetGroup(view, page, i18n("Text"),
                  {"insertFormula", "insertMathExpr", "insertSeries", "insertSpecialChar", "insertHyperlink", "comment"});
    addSheetGroup(view, page, i18n("External Data"),
                  {"insertFromClipboard", "insertFromTextfile", "insertFromDatabase"});
}

void populateSheetsLayout(KoView *view, QWidget *page)
{
    if (!page)
        return;

    // Page Setup follows the office layout: the three main commands are tall
    // buttons, and the print-area and break actions are individual icons
    // beside them instead of hidden in drop-downs.
    addSheetGroup(view, page, i18n("Page Setup"),
                  {"pageMarginsMenu", "pageOrientationMenu", "pageSizeMenu", "setPrintArea",
                   "clearPrintArea", "breakRow", "breakColumn", "printTitles"}, 3);
    addSheetGroup(view, page, i18n("Scale to Fit"), {"pageScaleFit"});
    addSheetGroup(view, page, i18n("Sheet Options"), {"pageSheetOptions"});
    // Arrange shows every shape operation as its own button; the compact
    // alignment and rotation icons keep the group narrow.
    addSheetGroup(view, page, i18n("Arrange"),
                  {"shapeBringForward", "shapeSendBackward", "shapeAlignLeft", "shapeAlignCenter",
                   "shapeAlignRight", "shapeAlignTop", "shapeAlignMiddle", "shapeAlignBottom",
                   "shapeGroup", "shapeRotateLeft", "shapeRotateRight"}, 2);
}

void populateSheetsData(KoView *view, QWidget *page)
{
    if (!page)
        return;
    // Follow the office Data tab: importing, then the sort and filter families
    // as medium buttons with their variants in drop-downs, then the tools,
    // forecast and outline groups.
    addSheetGroup(view, page, i18n("Get & Transform Data"),
                  {"insertFromTextfile", "insertFromDatabase", "insertFromClipboard"}, 1);

    markRibbonIconOnly(view, {"sort", "sortInc", "sortDesc", "autoFilter", "customFilter"});
    addSheetGroup(view, page, i18n("Sort & Filter"), {"sort", "sortInc", "sortDesc", "autoFilter", "customFilter"});

    addSheetGroup(view, page, i18n("Data Tools"),
                  {"textToColumns", "validity", "consolidate", "namedAreaDialog"}, 1);
    addSheetGroup(view, page, i18n("Forecast"), {"goalSeek"}, 1);
    addSheetGroup(view, page, i18n("Outline"), {"subtotals", "pivot"}, 1);
}

// The formula tab follows the office layout: a function library with the
// engine's categories, the defined names, the calculation commands and the
// review commands Calligra keeps on the same tab.
void populateSheetsFormulas(KoView *view, QWidget *page)
{
    if (!page)
        return;
    QList<QAction *> library;
    if (QAction *insertFunction = findAction(view, QStringLiteral("insertFormula")))
        library.append(insertFunction);
    if (QAction *autoSum = findAction(view, QStringLiteral("autoSum")))
        library.append(autoSum);

    // The sheets app registers one drop-down per function category.
    QList<QAction *> categories;
    const auto actions = view->actionCollection()->actions();
    for (QAction *action : actions) {
        if (!action->objectName().startsWith(QLatin1String("insertFunction")))
            continue;
        const QString category = action->text();
        const char *icon = "functions";
        if (category.contains(QLatin1String("Financ"), Qt::CaseInsensitive))
            icon = "attach_money";
        else if (category.contains(QLatin1String("Logic"), Qt::CaseInsensitive))
            icon = "rule";
        else if (category.contains(QLatin1String("Text"), Qt::CaseInsensitive))
            icon = "text_fields";
        else if (category.contains(QLatin1String("Date"), Qt::CaseInsensitive))
            icon = "calendar_month";
        else if (category.contains(QLatin1String("Lookup"), Qt::CaseInsensitive))
            icon = "manage_search";
        else if (category.contains(QLatin1String("Math"), Qt::CaseInsensitive))
            icon = "calculate";
        const QIcon categoryIcon = KoRibbonIcons::forCommand(QLatin1String(icon));
        if (!categoryIcon.isNull())
            action->setIcon(categoryIcon);
        categories.append(action);
    }
    std::stable_sort(categories.begin(), categories.end(), [](const QAction *a, const QAction *b) {
        return a->property("ribbonCommandOrder").toInt() < b->property("ribbonCommandOrder").toInt();
    });
    // One compact drop-down per available function category. The menu is
    // attached to its own tool button so it survives ribbon-page rebuilds.
    const auto menuButton = [page](const QString &name, const QString &title, const QIcon &icon, QMenu *menu) {
        auto *button = new QToolButton;
        button->setObjectName(QStringLiteral("ribbon") + name);
        button->setText(title);
        button->setAccessibleName(title);
        button->setIcon(icon);
        button->setProperty("ribbonCompactable", true);
        button->setMenu(menu);
        // Use a split-style menu indicator like Excel's function-category
        // buttons, while also opening the category menu when its label/icon is
        // clicked (these category buttons have no single default command).
        button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setToolTip(title);
        KoRibbonLayout::configureButton(button, Qt::ToolButtonTextBesideIcon);
        QObject::connect(button, &QToolButton::clicked, button, [button, menu] {
            if (menu) {
                menu->popup(button->mapToGlobal(QPoint(0, button->height())));
            }
        });
        auto *widgetAction = new QWidgetAction(page);
        widgetAction->setDefaultWidget(button);
        return widgetAction;
    };
    auto *recentMenu = new QMenu(i18n("Recently Used"), view);
    const QStringList recentFunctionNames = {QStringLiteral("SUM"), QStringLiteral("AVERAGE"), QStringLiteral("IF"),
                                             QStringLiteral("COUNT"), QStringLiteral("MIN"), QStringLiteral("MAX")};
    for (const QString &functionName : recentFunctionNames) {
        for (QAction *category : std::as_const(categories)) {
            if (!category->menu())
                continue;
            for (QAction *function : category->menu()->actions()) {
                if (function->text().compare(functionName, Qt::CaseInsensitive) == 0) {
                    recentMenu->addAction(function);
                    break;
                }
            }
        }
    }
    if (!recentMenu->isEmpty()) {
        library.append(menuButton(QStringLiteral("RecentlyUsed"), i18n("Recently Used"),
                                  KoRibbonIcons::forCommand(QStringLiteral("functions")), recentMenu));
    }

    QSet<QAction *> addedCategories;
    for (QAction *source : std::as_const(categories)) {
        if (addedCategories.contains(source))
            continue;
        const QString groupName = source->text();
        QString buttonTitle = groupName;
        QMenu *menu = source->menu();
        QIcon icon = source->icon();
        if (groupName == QLatin1String("Math") || groupName == QLatin1String("Trigonometric")) {
            QAction *math = nullptr;
            QAction *trigonometric = nullptr;
            for (QAction *candidate : std::as_const(categories)) {
                if (candidate->text() == QLatin1String("Math"))
                    math = candidate;
                else if (candidate->text() == QLatin1String("Trigonometric"))
                    trigonometric = candidate;
            }
            buttonTitle = i18n("Math & Trig");
            icon = KoRibbonIcons::forCommand(QStringLiteral("calculate"));
            menu = new QMenu(buttonTitle, view);
            if (math && math->menu())
                menu->addActions(math->menu()->actions());
            if (trigonometric && trigonometric->menu()) {
                if (math && math->menu() && !math->menu()->isEmpty())
                    menu->addSeparator();
                menu->addActions(trigonometric->menu()->actions());
            }
            if (math)
                addedCategories.insert(math);
            if (trigonometric)
                addedCategories.insert(trigonometric);
        } else {
            addedCategories.insert(source);
            if (groupName == QLatin1String("Lookup & Reference"))
                buttonTitle = i18n("Lookup & Reference");
        }
        library.append(menuButton(source->objectName(), buttonTitle, icon, menu));
    }
    addGroup(page, i18n("Function Library"), library, 1);

    addSheetGroup(view, page, i18n("Defined Names"), {"namedAreaDialog", "setAreaName"}, 1);
    addSheetGroup(view, page, i18n("Formula Auditing"), {"showFormulas", "RecalcWorkSheet", "RecalcWorkBook"}, 1);
    QList<QAction *> calculation;
    if (QAction *automatic = findAction(view, QStringLiteral("automaticCalculation")))
        calculation.append(automatic);
    if (QAction *options = submenuAction(view, i18n("Calculation Options"), "calculate",
                                         {"menu_none", "menu_sum", "menu_min", "menu_max", "menu_average", "menu_count", "menu_counta"}))
        calculation.append(options);
    addGroup(page, i18n("Calculation"), calculation, 1);
}

// The review tab mirrors the office one: proofing, comments, protection and
// the application language.
void populateSheetsReview(KoView *view, QWidget *page)
{
    if (!page)
        return;
    addSheetGroup(view, page, i18n("Proofing"), {"tools_spelling", "sortList"}, 1);
    addSheetGroup(view, page, i18n("Comments"), {"comment", "clearComment"}, 1);
    addSheetGroup(view, page, i18n("Protect"), {"protectSheet", "protectDoc"}, 1);
    addSheetGroup(view, page, i18n("Language"), {"switch_application_language"}, 1);
}

void populateSheetsView(KoView *view, QWidget *page)
{
    if (!page)
        return;
    addSheetGroup(view, page, i18n("Window"), {"view_newview"}, 1);
    // Navigation lives here now that its own tab became Review.
    addSheetGroup(view, page, i18n("Navigate"), {"gotoCell", "go_first", "go_previous", "go_next", "go_last"}, 1);
    addSheetGroup(view, page, i18n("Show"), {"showPageOutline", "showColumnHeader", "showRowHeader", "showTabBar", "showStatusBar"});
    addSheetGroup(view, page, i18n("Zoom"), {"zoom_out", "zoom_in", "view_zoom", "view_fullscreen"}, 1);
    if (QMenu *menu = findMenu(view->mainWindow(), QStringLiteral("view"))) {
        addGroup(page, i18n("More"), {menu->menuAction()}, 0);
    }
}

void populateSheetsHelp(KoView *view, QWidget *page)
{
    if (!page)
        return;
    // The help menu is assembled from KDE's standard actions, so mirror it
    // rather than guessing their identifiers.
    addMenu(page, findMenu(view->mainWindow(), QStringLiteral("help")));
}

void populateSheets(KoView *view, QWidget *home)
{
    // Use the Office name where Calligra's action captions are more verbose.
    struct Label { const char *id; QString text; };
    const Label labels[] = {
        {"specialPaste", i18n("Paste Special")}, {"currency", i18n("Currency")}, {"percent", i18n("Percent")},
        {"conditional", i18n("Conditional Formatting")}, {"autoFormat", i18n("Format as Table")},
        {"styleDialog", i18n("Cell Styles")}, {"insertCell", i18n("Insert")}, {"deleteCell", i18n("Delete")},
        {"cellStyle", i18n("Format")}, {"autoSum", i18n("AutoSum")},
        {"insertFormula", i18n("Insert Function")}, {"namedAreaDialog", i18n("Name Manager")},
        {"setAreaName", i18n("Define Name")}, {"showFormulas", i18n("Show Formulas")},
        {"automaticCalculation", i18n("Automatic Calculation")},
        // Excel names the import commands after their source.
        {"insertFromTextfile", i18n("From &Text/CSV")}, {"insertFromDatabase", i18n("From &Database")},
        {"insertFromClipboard", i18n("From &Clipboard")},
        {"comment", i18n("New Comment")}, {"clearComment", i18n("Delete Comment")},
        {"add_shape", i18n("Insert Shape")}
    };
    for (const Label &label : labels) {
        if (QAction *action = findAction(view, QLatin1String(label.id))) {
            action->setText(label.text);
            action->setIconText(label.text);
        }
    }

    addSheetGroup(view, home, i18n("Clipboard"), {"paste", "cut", "copy", "specialPaste"}, 1);
    addSheetGroup(view, home, i18n("Styles"), {"conditional", "autoFormat", "styleDialog"});
    addSheetGroup(view, home, i18n("Cells"), {"insertCell", "deleteCell", "cellStyle"});

    // The Editing group shows every command as its own icon button; only the
    // fill, sort, find and clear families used to be drop-down menus.
    markRibbonIconOnly(view, {"fillDown", "fillRight", "fillUp", "fillLeft", "sort", "sortInc", "sortDesc",
                              "autoFilter", "customFilter", "edit_find", "edit_find_next",
                              "edit_replace", "selectAll", "clearContents", "clearAll", "clearComment",
                              "clearConditional", "clearHyperlink"});
    addSheetGroup(view, home, i18n("Editing"),
                  {"autoSum", "fillDown", "fillRight", "fillUp", "fillLeft", "sort", "sortInc", "sortDesc",
                   "autoFilter", "customFilter", "edit_find", "edit_find_next", "edit_replace",
                   "selectAll", "clearContents", "clearAll", "clearComment", "clearConditional",
                   "clearHyperlink"}, 1);
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
        {"insert_formula", i18n("Formula"), "functions"},
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
            // Prefer the bundled, colour-tinted ribbon icon and fall back to
            // the desktop theme when the symbol set has no match.
            const QIcon icon = KoAppRibbon::commandIcon(action->objectName());
            action->setIcon(icon.isNull() ? QIcon::fromTheme(QLatin1String(command.icon), action->icon()) : icon);
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
    // Every Edit-menu command has its own button instead of one catch-all menu.
    group("stageHomePage", i18n("Editing"),
          {"edit_delete", "edit_select_all", "edit_deselect_all", "edit_find", "edit_findnext",
           "edit_findprevious", "edit_replace"}, 0);
    group("stageInsertPage", i18n("Slides"), {"page_insertpage", "import_document"}, 1);
    group("stageInsertPage", i18n("Tables"), {"insert_table"}, 1);
    group("stageInsertPage", i18n("Images"), {"insert_picture", "insert_pictures"}, 1);
    group("stageInsertPage", i18n("Illustrations"),
          {"add_shape", "insert_chart", "insert_formula", "insert_wordart",
           "insert_stateshape", "insert_3dscene", "insert_vectorimage", "insert_webshape"}, 3);
    group("stageInsertPage", i18n("Text"), {"stage_insert_textbox", "insert_variable"}, 1);
    group("stageInsertPage", i18n("Symbols"), {"insert_specialchar"}, 1);
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
    QList<QAction *> shapeFormat;
    for (const QString &name : {QStringLiteral("format_font"), QStringLiteral("format_paragraph"),
                                QStringLiteral("format_stylist")}) {
        if (QAction *format = findAction(view, name)) {
            shapeFormat.append(format);
        }
    }
    addGroup(page("stageShapeFormatPage"), i18n("Format"), shapeFormat, -1);
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
        tablePage->setProperty("ribbonContextActive", editor && editor->tableAtCursor());
    };
    QObject::connect(canvas->shapeManager()->selection(), &KoSelection::selectionChanged, shapePage, updateContext);
    QObject::connect(canvas->resourceManager(), &KoCanvasResourceManager::canvasResourceChanged, tablePage, [=](int, const QVariant &) { updateContext(); });
    updateContext();
    // Colour the text and shape formatting actions shown by the pages, which
    // are registered by the tools rather than by this function. The text tool
    // creates its actions lazily, so refresh them when the active tool changes.
    KoAppRibbon::applyCommandIcons(view);
    QObject::connect(KoToolManager::instance(), &KoToolManager::changedTool, view,
                     [view](KoCanvasController *, int) { KoAppRibbon::applyCommandIcons(view); });
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
    // The spreadsheet pages are built from explicit groups below; the other
    // applications mirror their menus.
    if (!spreadsheet) {
        for (const Page &page : pages) {
            for (const QString &name : page.menus) {
                addMenu(page.widget, findMenu(window, name));
            }
        }
        populateStage(view, canvas, pages);
    } else {
        KoAppRibbon::applyCommandIcons(view);
        populateSheets(view, home);
        populateSheetsFile(view, sheetPage(pages, "sheetsFilePage"));
        populateSheetsInsert(view, sheetPage(pages, "sheetsInsertPage"));
        populateSheetsLayout(view, sheetPage(pages, "sheetsLayoutPage"));
        populateSheetsData(view, sheetPage(pages, "sheetsDataPage"));
        populateSheetsFormulas(view, sheetPage(pages, "sheetsFormulasPage"));
        populateSheetsReview(view, sheetPage(pages, "sheetsReviewPage"));
        populateSheetsView(view, sheetPage(pages, "sheetsViewPage"));
        populateSheetsHelp(view, sheetPage(pages, "sheetsHelpPage"));
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
            const QStringList icons = {QStringLiteral("arrow_selector_tool"), QStringLiteral("draw"),
                                       QStringLiteral("polyline"), QStringLiteral("route")};
            const QStringList themeIcons = {QStringLiteral("edit-select"), QStringLiteral("draw-freehand"),
                                            QStringLiteral("draw-path"), QStringLiteral("draw-connector")};
            for (int i = 0; i < tools.size(); ++i) {
                if (!KoToolManager::instance()->toolById(canvas, tools[i]))
                    continue;
                QIcon toolIcon = KoRibbonIcons::forCommand(icons[i]);
                if (toolIcon.isNull()) {
                    toolIcon = QIcon::fromTheme(themeIcons[i]);
                }
                auto *action = new QAction(toolIcon, titles[i], view);
                const QString toolId = tools[i];
                action->setObjectName(toolId);
                QObject::connect(action, &QAction::triggered, view, [canvas, toolId] {
                    KoToolManager::instance()->switchToolRequested(toolId);
                    canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
                });
                actions.append(action);
            }
            // All drawing tools fit one row at large size, which keeps the
            // group from stacking them into a tall single column.
            addGroup(page.widget, i18n("Tools"), actions, -1);
            if (QAction *shapes = findAction(view, QStringLiteral("add_shape")))
                addGroup(page.widget, i18n("Shapes"), {shapes}, -1);
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

namespace KoAppRibbon
{
QIcon commandIcon(const QString &actionName)
{
    return KoRibbonIcons::forCommand(actionName);
}

void applyCommandIcons(KoView *view)
{
    if (!view) {
        return;
    }
    QList<QAction *> actions = view->actionCollection()->actions();
    if (view->mainWindow()) {
        actions += view->mainWindow()->actionCollection()->actions();
    }
    // Tool actions (text formatting, table editing, ...) are created lazily and
    // live with the tool widgets, so collect them from the widget tree as well.
    actions += view->findChildren<QAction *>();
    QSet<QAction *> seen;
    for (QAction *action : actions) {
        if (!action || seen.contains(action) || action->isSeparator() || action->objectName().isEmpty()) {
            continue;
        }
        seen.insert(action);
        const QIcon icon = commandIcon(action->objectName());
        if (!icon.isNull() && icon.cacheKey() != action->icon().cacheKey()) {
            action->setIcon(icon);
        }
    }
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
        // Object types that used to hide at the bottom of the Shapes gallery:
        // each one is a button of its own on the Insert tab.
        struct InsertShape {
            const char *action;
            QString text;
            const char *shapeId;
            const char *icon;
            QString toolTip;
        };
        const InsertShape insertShapes[] = {
            {"insert_formula", i18n("Formula"), "FormulaShapeID", "functions",
             i18n("Insert a formula and edit it with the formula tools")},
            {"insert_wordart", i18n("Word Art"), "ArtisticText", "style",
             i18n("Insert an artistic text shape")},
            {"insert_stateshape", i18n("State"), "StateShape", "fact_check",
             i18n("Insert a state shape")},
            {"insert_3dscene", i18n("3D Scene"), "ThreedShape", "category",
             i18n("Insert a 3D scene")},
            {"insert_vectorimage", i18n("Vector Image"), "VectorShapeID", "polyline",
             i18n("Insert a vector image")},
            {"insert_webshape", i18n("Web Shape"), "WebShape", "link",
             i18n("Insert a web page shape")},
        };
        for (const InsertShape &shape : insertShapes) {
            if (findAction(view, QLatin1String(shape.action))) {
                continue;
            }
            auto *action = new QAction(KoRibbonIcons::forCommand(QLatin1String(shape.icon)), shape.text, view);
            action->setToolTip(shape.toolTip);
            view->actionCollection()->addAction(QLatin1String(shape.action), action);
            const QString shapeId = QLatin1String(shape.shapeId);
            QObject::connect(action, &QAction::triggered, view, [view, canvas, shapeId] {
                if (!view->koDocument()->isReadWrite()) {
                    return;
                }
                if (KoCreateShapesTool *tool = KoToolManager::instance()->shapeCreatorTool(canvas)) {
                    tool->setShapeId(shapeId);
                    tool->setShapeProperties(nullptr);
                    KoToolManager::instance()->switchToolRequested(KoCreateShapesTool_ID);
                    canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
                }
            });
        }
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
        // Chart Tools, shown while a chart is selected. The chart plugin fills
        // the page with both the design commands and the chart options.
        auto *chartDesign = stagePage(i18n("Chart Format / Design"), QStringLiteral("stageChartDesignPage"));
        chartDesign->setProperty("ribbonContextActive", false);
        stagePage(i18n("Help"), QStringLiteral("stageHelpPage"), {QStringLiteral("help")});
    } else {
        auto *filePage = addPage(i18n("File"), {}, true);
        filePage->setObjectName(QStringLiteral("sheetsFilePage"));
        home = addPage(i18n("Home"), {}, true);
        home->setObjectName(QStringLiteral("sheetsHomePage"));
        home->setProperty("ribbonDefaultTool", homeTool);
        home->setProperty("ribbonActivateTool", homeTool);
        addPage(i18n("Insert"), {})->setObjectName(QStringLiteral("sheetsInsertPage"));
        auto *draw = addPage(i18n("Draw"), {}, false, true);
        draw->setObjectName(QStringLiteral("sheetsDrawPage"));
        draw->setProperty("ribbonContextActive", false);
        addPage(i18n("Page Layout"), {})->setObjectName(QStringLiteral("sheetsLayoutPage"));
        addPage(i18n("Data"), {})->setObjectName(QStringLiteral("sheetsDataPage"));
        addPage(i18n("Formulas"), {})->setObjectName(QStringLiteral("sheetsFormulasPage"));
        addPage(i18n("Review"), {}, false, true)->setObjectName(QStringLiteral("sheetsReviewPage"));
        addPage(i18n("View"), {}, false, true)->setObjectName(QStringLiteral("sheetsViewPage"));
        auto *shapeFormat = addPage(i18n("Shape Format"), {}, false, true);
        shapeFormat->setObjectName(QStringLiteral("sheetsShapeFormatPage"));
        shapeFormat->setProperty("ribbonContextActive", false);
        const auto updateContext = [canvas, shapeFormat] {
            shapeFormat->setProperty("ribbonContextActive", !canvas->shapeManager()->selection()->selectedShapes().isEmpty());
        };
        QObject::connect(canvas->shapeManager()->selection(), &KoSelection::selectionChanged, shapeFormat, updateContext);
        QObject::connect(canvas->shapeManager()->selection(), &KoSelection::selectionChanged, draw, [canvas, draw] {
            draw->setProperty("ribbonContextActive", !canvas->shapeManager()->selection()->selectedShapes().isEmpty());
        });
        updateContext();
        addPage(i18n("Help"), {}, false, true)->setObjectName(QStringLiteral("sheetsHelpPage"));
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
