// SPDX-License-Identifier: LGPL-2.0-or-later
#include <KWView.h>
#include <KoCanvasBase.h>
#include <KoDocument.h>
#include <KoDocumentEntry.h>
#include <KoMainWindow.h>
#include <KoPart.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeManager.h>
#include <KoShapeRegistry.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeController.h>
#include <KoToolManager.h>
#include <KAboutData>
#include <KActionCollection>
#include <KLocalizedString>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMimeData>
#include <QTabBar>
#include <QTimer>
#include <cstdlib>

static void require(bool condition, const char *message)
{
    if (!condition) {
        qCritical("%s", message);
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const auto settle = [] {
        QEventLoop loop;
        QTimer::singleShot(50, &loop, &QEventLoop::quit);
        loop.exec();
    };
    app.setApplicationName(QStringLiteral("calligrawords"));
    KAboutData::setApplicationData(KAboutData(QStringLiteral("calligrawords"), QStringLiteral("Words Equation Ribbon Test"), QStringLiteral("1")));
    KLocalizedString::setApplicationDomain("calligrawords");
    if (!app.clipboard()->mimeData())
        app.clipboard()->setMimeData(new QMimeData);
    QString error;
    KoPart *part = KoDocumentEntry::queryByMimeType(QStringLiteral("application/vnd.oasis.opendocument.text")).createKoPart(&error);
    require(part, "Words part unavailable");
    KoMainWindow *window = part->createMainWindow();
    require(window->openDocument(part, QUrl::fromLocalFile(QStringLiteral(FILES_DATA_DIR "weekend-hike.odt"))), "Words test document did not open");
    window->show();
    auto *view = qobject_cast<KWView *>(window->rootView());
    require(view, "Words view unavailable");
    part->document()->setAutoSave(0);
    QElapsedTimer wait;
    wait.start();
    while (!view->actionCollection()->action(QStringLiteral("insert_formula")) && wait.elapsed() < 3000)
        app.processEvents();
    QAction *insert = view->actionCollection()->action(QStringLiteral("insert_formula"));
    require(insert, "Insert Equation unavailable");
    insert->trigger();
    settle();

    auto *page = window->findChild<QWidget *>(QStringLiteral("wordsEquationPage"));
    require(page && page->property("ribbonContextActive").toBool(), "inserting an equation must show Equation");
    const auto verifyMathPage = [&] {
        QStringList controls;
        int groups = 0;
        for (QWidget *widget : page->findChildren<QWidget *>()) {
            groups += widget->property("ribbonRoutedGroup").toBool();
            if (!widget->windowTitle().isEmpty())
                controls.append(widget->windowTitle());
        }
        require(groups == 3, "Equation must contain only math groups");
        for (const QString &title : {QStringLiteral("Symbols"), QStringLiteral("Structures"), QStringLiteral("LaTeX")})
            require(controls.contains(title), "Equation is missing math controls");
        int equationTabs = 0;
        bool selected = false;
        for (QTabBar *tabs : window->findChildren<QTabBar *>()) {
            for (int i = 0; i < tabs->count(); ++i)
                equationTabs += QString(tabs->tabText(i)).remove(QLatin1Char('&')) == QStringLiteral("Equation");
            selected |= QString(tabs->tabText(tabs->currentIndex())).remove(QLatin1Char('&')) == QStringLiteral("Equation");
        }
        require(equationTabs == 1 && selected, "Equation must be the active tab without a duplicate tool tab");
    };
    verifyMathPage();
    auto *selection = view->canvasBase()->shapeManager()->selection();
    KoShape *formula = nullptr;
    for (KoShape *shape : selection->selectedShapes())
        if (shape->shapeId() == QLatin1String("FormulaShapeID"))
            formula = shape;
    require(formula, "inserted equation must be selected");
    KoToolManager::instance()->switchToolRequested(QStringLiteral("InteractionTool"));
    selection->deselectAll();
    selection->select(formula);
    settle();
    verifyMathPage();
    KoToolManager::instance()->switchToolRequested(QStringLiteral("KoFormulaToolFactoryId"));
    settle();
    verifyMathPage();
    KoToolManager::instance()->switchToolRequested(QStringLiteral("InteractionTool"));
    selection->deselectAll();
    settle();
    require(!page->property("ribbonContextActive").toBool(), "deselecting math must hide Equation");
    const auto requireHome = [&] {
        bool home = false;
        for (QTabBar *tabs : window->findChild<QWidget *>(QStringLiteral("wordsRibbon"))->findChildren<QTabBar *>())
            home |= QString(tabs->tabText(tabs->currentIndex())).remove(QLatin1Char('&')) == QStringLiteral("Home");
        require(home, "deselecting a Words object must return Home instead of File");
    };
    requireHome();
    auto *textFactory = KoShapeRegistry::instance()->value(QStringLiteral("TextShapeID"));
    require(textFactory, "text shape factory unavailable");
    KoShape *textShape = textFactory->createDefaultShape(view->canvasBase()->shapeController()->resourceManager());
    view->canvasBase()->addCommand(view->canvasBase()->shapeController()->addShapeDirect(textShape));
    for (int repetition = 0; repetition < 3; ++repetition) {
        for (KoShape *shape : {textShape, formula}) {
            selection->deselectAll();
            selection->select(shape);
            settle();
            KoToolManager::instance()->switchToolRequested(QStringLiteral("InteractionTool"));
            selection->deselectAll();
            settle();
            requireHome();
        }
    }
    part->document()->setModified(false);
    delete window;
    qInfo("PASS Words Equation ribbon insertion, selection, editing and deselection");
}
