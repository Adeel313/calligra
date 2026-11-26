/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "KPrDocument.h"
#include "KPrPage.h"
#include "KPrView.h"
#include "KPrMasterPage.h"

#include <KoDocumentEntry.h>
#include <KoColorBackground.h>
#include <KoDocumentResourceManager.h>
#include <KoPatternBackground.h>
#include <KoGlobal.h>
#include <KoMainWindow.h>
#include <KoPart.h>
#include <KoShapeContainer.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeRegistry.h>
#include <KoShapeController.h>
#include <KoShapeManager.h>
#include <KoSelection.h>
#include <KoPACanvasBase.h>
#include <KoToolManager.h>
#include <KoPageLayout.h>
#include <kundo2stack.h>
#include <KAboutData>
#include <KActionCollection>
#include <QAction>
#include <KLocalizedString>
#include <QApplication>
#include <QDebug>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTabBar>
#include <QClipboard>
#include <QMimeData>
#include <QToolButton>
#include <QMenu>
#include <QWidgetAction>
#include <cstdlib>

static void require(bool condition, const char *message)
{
    if (!condition) {
        qCritical() << message;
        std::exit(1);
    }
}

static QStringList placeholderClasses(KoShapeContainer *container)
{
    QStringList classes;
    for (KoShape *shape : container->shapes()) {
        const QString presentationClass = shape->additionalAttribute("presentation:class");
        if (!presentationClass.isEmpty())
            classes.append(presentationClass);
        if (auto *child = dynamic_cast<KoShapeContainer *>(shape))
            classes.append(placeholderClasses(child));
    }
    classes.sort();
    return classes;
}

static KoShape *findPlaceholder(KoShapeContainer *container, const QString &presentationClass)
{
    for (KoShape *shape : container->shapes()) {
        if (shape->additionalAttribute("presentation:class") == presentationClass)
            return shape;
        if (auto *child = dynamic_cast<KoShapeContainer *>(shape)) {
            if (KoShape *found = findPlaceholder(child, presentationClass))
                return found;
        }
    }
    return nullptr;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // KDE color controls inspect the clipboard while building ribbon options.
    // The offscreen platform starts without any MIME data.
    if (!app.clipboard()->mimeData())
        app.clipboard()->setMimeData(new QMimeData);
    app.setApplicationName(QStringLiteral("calligrastage"));
    KAboutData::setApplicationData(KAboutData(QStringLiteral("calligrastage"), QStringLiteral("Stage Layout Test"), QStringLiteral("1")));
    KLocalizedString::setApplicationDomain("calligrastage");
    KoGlobal::initialize();
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary workspace unavailable");
    for (const QString &name : {QStringLiteral("emptyLandscape.otp"), QStringLiteral("emptyLandscapeWide.otp")}) {
        const QString source = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
            QStringLiteral("calligrastage/templates/Screen/.source/") + name);
        require(!source.isEmpty(), "default presentation template missing");
        QString error;
        KoPart *part = KoDocumentEntry::queryByMimeType(QStringLiteral("application/vnd.oasis.opendocument.presentation")).createKoPart(&error);
        require(part, "Stage part unavailable");
        KoMainWindow *window = part->createMainWindow();
        require(window->openDocument(part, QUrl::fromLocalFile(source)), "default presentation did not open");
        window->show();
        app.processEvents();
        auto *document = qobject_cast<KPrDocument *>(part->document());
        auto *view = qobject_cast<KPrView *>(window->rootView());
        require(document && view, "Stage document/view unavailable");
        document->setAutoSave(0);
        document->setAutoErrorHandlingEnabled(false);
        document->setSaveInBatchMode(true);
        require(document->pageCount() == 1, "new presentation must have one slide");
        require(placeholderClasses(document->pages().first()) == QStringList({QStringLiteral("subtitle"), QStringLiteral("title")}),
                "first slide must have title and subtitle");
        window->grab().save(temporary.filePath(name + QStringLiteral("-title.png")));
        const int undoCount = document->undoStack()->count();
        auto *titlePage = dynamic_cast<KPrPage *>(document->pages().first());
        QImage backgroundImage(16, 16, QImage::Format_RGB32);
        backgroundImage.fill(QColor(32, 64, 128));
        QSharedPointer<KoShapeBackground> background;
        if (name == QStringLiteral("emptyLandscapeWide.otp")) {
            auto imageBackground = QSharedPointer<KoPatternBackground>::create(document->resourceManager()->imageCollection());
            imageBackground->setPattern(backgroundImage);
            imageBackground->setRepeat(KoPatternBackground::Stretched);
            background = imageBackground;
        } else {
            background = QSharedPointer<KoColorBackground>::create(QColor(32, 64, 128));
        }
        titlePage->setBackground(background);
        titlePage->setDisplayMasterBackground(false);
        titlePage->setDisplayMasterShapes(false);
        QAction *newSlide = view->actionCollection()->action(QStringLiteral("page_insertpage"));
        require(newSlide && newSlide->isEnabled(), "New Slide command unavailable");
        newSlide->trigger();
        app.processEvents();
        require(document->pageCount() == 2, "New Slide must insert one slide");
        auto *secondPage = dynamic_cast<KPrPage *>(document->pages().last());
        require(secondPage->masterPage() == titlePage->masterPage(), "New Slide must retain the master design");
        require(secondPage->background() == background, "New Slide must retain the slide background");
        require(!secondPage->displayMasterBackground(), "New Slide must retain the background override");
        require(!secondPage->displayMasterShapes(), "New Slide must retain master shape visibility");
        require(placeholderClasses(document->pages().last()) == QStringList({QStringLiteral("outline"), QStringLiteral("title")}),
                "second slide must have title and content");
        KoShape *content = findPlaceholder(document->pages().last(), QStringLiteral("outline"));
        const auto &pageSize = document->pages().last()->pageLayout();
        require(content && content->size().width() * content->size().height() > pageSize.width * pageSize.height * 0.5,
                "content placeholder must occupy most of the slide");
        require(document->undoStack()->count() == undoCount + 1, "New Slide must be one undo step");
        document->undoStack()->undo();
        require(document->pageCount() == 1, "one undo must remove the complete slide");
        document->undoStack()->redo();
        require(document->pageCount() == 2, "redo must restore the slide");
        require(placeholderClasses(document->pages().last()).contains(QStringLiteral("outline")), "redo must restore content placeholder");
        view->insertPage();
        require(placeholderClasses(document->pages().last()) == QStringList({QStringLiteral("outline"), QStringLiteral("title")}),
                "subsequent slides must keep title and content");
        const QString saved = temporary.filePath(name + QStringLiteral(".odp"));
        document->setOutputMimeType("application/vnd.oasis.opendocument.presentation");
        require(document->exportDocument(QUrl::fromLocalFile(saved)), "presentation save failed");
        document->setModified(false);
        require(document->openUrl(QUrl::fromLocalFile(saved)), "presentation reload failed");
        require(document->pageCount() == 3, "saved slide count changed");
        auto *savedPage = dynamic_cast<KPrPage *>(document->pages().last());
        if (name == QStringLiteral("emptyLandscapeWide.otp")) {
            auto *savedBackground = dynamic_cast<KoPatternBackground *>(savedPage->background().data());
            require(savedBackground && savedBackground->pattern() == backgroundImage, "inherited background image must survive reload");
            require(savedBackground->repeat() == KoPatternBackground::Stretched, "background image scaling must survive reload");
        } else {
            auto *savedBackground = dynamic_cast<KoColorBackground *>(savedPage->background().data());
            require(savedBackground && savedBackground->color() == QColor(32, 64, 128), "inherited background must survive reload");
        }
        require(placeholderClasses(document->pages().first()).contains(QStringLiteral("subtitle")), "title slide lost on reload");
        require(placeholderClasses(document->pages().last()).contains(QStringLiteral("outline")), "content slide lost on reload");
        // A deliberately blank slide in an existing file must stay blank.
        auto *first = dynamic_cast<KPrPage *>(document->pages().first());
        first->setLayout(nullptr, document);
        require(placeholderClasses(first).isEmpty(), "blank layout must remove placeholders");
        require(document->exportDocument(QUrl::fromLocalFile(saved)), "blank slide save failed");
        document->setModified(false);
        require(document->openUrl(QUrl::fromLocalFile(saved)), "blank slide reload failed");
        require(placeholderClasses(document->pages().first()).isEmpty(), "loading must preserve existing blank slides");

        // Selecting math exposes only equation controls, even before entering
        // its editing tool. The controls must stay on that page while editing.
        auto *formulaFactory = KoShapeRegistry::instance()->value(QStringLiteral("FormulaShapeID"));
        require(formulaFactory, "formula shape factory unavailable");
        auto *canvas = view->kopaCanvas();
        KoShape *formula = formulaFactory->createDefaultShape(document->resourceManager());
        canvas->addCommand(canvas->shapeController()->addShape(formula));
        QElapsedTimer ribbonWait;
        ribbonWait.start();
        while ((!window->findChild<QWidget *>(QStringLiteral("stageEquationPage"))
                || window->findChild<QWidget *>(QStringLiteral("stageEquationPage"))->findChildren<QWidget *>().isEmpty())
               && ribbonWait.elapsed() < 3000) {
            app.processEvents();
        }
        auto *equationPage = window->findChild<QWidget *>(QStringLiteral("stageEquationPage"));
        auto *shapePage = window->findChild<QWidget *>(QStringLiteral("stageShapeFormatPage"));
        require(equationPage && shapePage, "Stage contextual ribbon pages unavailable");
        auto *selection = canvas->shapeManager()->selection();
        selection->deselectAll();
        auto *ribbon = window->findChild<QWidget *>(QStringLiteral("wordsRibbon"));
        require(ribbon, "Stage ribbon unavailable");
        const auto showHome = [&] {
            QMetaObject::invokeMethod(ribbon, "showRibbonPage", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("stageHomePage")));
        };
        const auto requireHome = [&] {
            bool homeSelected = false;
            for (QTabBar *tabs : ribbon->findChildren<QTabBar *>())
                homeSelected |= QString(tabs->tabText(tabs->currentIndex())).remove(QLatin1Char('&')) == QStringLiteral("Home");
            require(homeSelected, "deselecting a Stage object must return Home instead of File");
        };
        showHome();
        selection->select(formula);
        app.processEvents();
        require(equationPage->property("ribbonContextActive").toBool(), "equation selection must show Equation");
        require(!shapePage->property("ribbonContextActive").toBool(), "equation selection must hide Shape Format");
        const auto verifyEquationControls = [&] {
            QStringList controls;
            int groups = 0;
            for (QWidget *widget : equationPage->findChildren<QWidget *>()) {
                groups += widget->property("ribbonRoutedGroup").toBool();
                if (!widget->windowTitle().isEmpty())
                    controls.append(widget->windowTitle());
            }
            for (const QString &title : {QStringLiteral("Symbols"), QStringLiteral("Structures"), QStringLiteral("LaTeX")})
                require(controls.contains(title), "Equation must contain all math controls");
            require(groups == 3, "Equation must contain only math control groups");
        };
        verifyEquationControls();
        bool equationTabSelected = false;
        for (QTabBar *tabs : window->findChildren<QTabBar *>())
            equationTabSelected |= tabs->tabText(tabs->currentIndex()) == QStringLiteral("Equation");
        require(equationTabSelected, "selecting math must activate Equation tab");
        KoToolManager::instance()->switchToolRequested(QStringLiteral("KoFormulaToolFactoryId"));
        app.processEvents();
        verifyEquationControls();
        KoToolManager::instance()->switchToolRequested(QStringLiteral("InteractionTool"));
        selection->deselectAll();
        app.processEvents();
        require(!equationPage->property("ribbonContextActive").toBool(), "deselecting math must hide Equation");
        requireHome();
        auto *textFactory = KoShapeRegistry::instance()->value(QStringLiteral("TextShapeID"));
        require(textFactory, "text shape factory unavailable");
        KoShape *textShape = textFactory->createDefaultShape(document->resourceManager());
        canvas->addCommand(canvas->shapeController()->addShape(textShape));
        for (int repetition = 0; repetition < 3; ++repetition) {
            showHome();
            selection->select(textShape);
            app.processEvents();
            require(shapePage->property("ribbonContextActive").toBool(), "selecting text must show Shape Format");
            KoToolManager::instance()->switchToolRequested(QStringLiteral("TextToolFactory_ID"));
            app.processEvents();
            KoToolManager::instance()->switchToolRequested(QStringLiteral("InteractionTool"));
            selection->deselectAll();
            app.processEvents();
            requireHome();
        }
        // The inline gallery must expose every master and apply it as one
        // undoable operation, including slides that hid their master artwork.
        auto *themeButton = window->findChild<QToolButton *>(QStringLiteral("stageThemesButton"));
        require(themeButton && themeButton->menu(), "Design must provide a Themes dropdown");
        auto *themeStrip = window->findChild<QWidget *>(QStringLiteral("stageThemeStrip"));
        require(themeStrip, "Design must provide an inline theme gallery");
        auto selectedTheme = [](QWidget *strip) {
            for (auto *tile : strip->findChildren<QToolButton *>()) {
                if (tile->isChecked())
                    return tile->accessibleName();
            }
            return QString{};
        };
        require(!selectedTheme(themeStrip).isEmpty(), "Gallery must identify the active theme");
        auto *theme = new KPrMasterPage(document);
        theme->setName(QStringLiteral("Regression Theme"));
        theme->setPageLayout(document->pages(true).first()->pageLayout());
        theme->setBackground(QSharedPointer<KoShapeBackground>(new KoColorBackground(QColor(30, 60, 100))));
        document->insertPage(theme, -1);
        auto *firstSlide = dynamic_cast<KoPAPage *>(document->pages().first());
        require(firstSlide, "theme test requires a slide");
        firstSlide->setDisplayMasterBackground(false);
        firstSlide->setMasterBackgroundVisible(false);
        firstSlide->setDisplayMasterShapes(false);
        QList<KoPAMasterPage *> previousMasters;
        for (KoPAPageBase *base : document->pages())
            previousMasters.append(dynamic_cast<KoPAPage *>(base)->masterPage());
        themeButton->menu()->popup(QPoint(20, 100));
        app.processEvents();
        require(themeButton->menu()->isVisible(), "Themes dropdown must open");
        require(themeButton->menu()->actions().size() == 1, "Themes dropdown must open directly on the grid");
        auto *gridAction = qobject_cast<QWidgetAction *>(themeButton->menu()->actions().constFirst());
        require(gridAction && gridAction->defaultWidget()
                && gridAction->defaultWidget()->objectName() == QStringLiteral("stageProfessionalThemeGrid"),
                "Themes dropdown must show only the professional grid");
        themeButton->menu()->hide();
        QToolButton *themeChoice = nullptr;
        bool hasOriginal = false;
        for (auto *tile : themeStrip->findChildren<QToolButton *>()) {
            if (tile->accessibleName() == QStringLiteral("Original"))
                hasOriginal = true;
            if (tile->accessibleName() == QStringLiteral("Regression Theme"))
                themeChoice = tile;
        }
        require(hasOriginal, "Inline gallery must offer the Original theme");
        require(themeChoice, "Newly added masters must appear in the inline gallery");
        const int themeUndoIndex = document->undoStack()->index();
        themeChoice->click();
        app.processEvents();
        require(selectedTheme(themeStrip) == QStringLiteral("Regression Theme"), "Gallery must select the applied theme");
        require(document->undoStack()->index() == themeUndoIndex + 1, "applying a theme must be one undo step");
        for (KoPAPageBase *base : document->pages()) {
            auto *slide = dynamic_cast<KoPAPage *>(base);
            require(slide->masterPage() == theme && slide->displayMasterBackground() && slide->isMasterBackgroundVisible() && slide->displayMasterShapes(),
                    "chosen theme must be visible on every slide");
        }
        document->undoStack()->undo();
        app.processEvents();
        for (int index = 0; index < previousMasters.size(); ++index)
            require(dynamic_cast<KoPAPage *>(document->pages().at(index))->masterPage() == previousMasters.at(index), "undo must restore original themes");
        require(!firstSlide->displayMasterBackground() && !firstSlide->isMasterBackgroundVisible() && !firstSlide->displayMasterShapes(),
                "undo must restore master visibility");
        document->undoStack()->redo();
        app.processEvents();
        require(firstSlide->masterPage() == theme && firstSlide->displayMasterBackground(), "redo must apply the theme again");
        document->setModified(false);
        delete window;
        app.processEvents();
        qInfo() << "PASS default layouts, insertion, undo/redo, reload and existing blank slides:" << name;
    }
    return 0;
}
