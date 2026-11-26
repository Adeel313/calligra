// SPDX-License-Identifier: LGPL-2.0-or-later
#include <KoCanvasBase.h>
#include <KoCanvasController.h>
#include <KoDocument.h>
#include <KoDocumentEntry.h>
#include <KoMainWindow.h>
#include <KoPart.h>
#include <KoProperties.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeController.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeManager.h>
#include <KoShapeRegistry.h>
#include <KoToolManager.h>
#include <KAboutData>
#include <KLocalizedString>
#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QMimeData>
#include <QImage>
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
    app.setApplicationName(QStringLiteral("calligrasheets"));
    KAboutData::setApplicationData(KAboutData(QStringLiteral("calligrasheets"), QStringLiteral("Sheets Shape Ribbon Test"), QStringLiteral("1")));
    KLocalizedString::setApplicationDomain("calligrasheets");
    if (!app.clipboard()->mimeData())
        app.clipboard()->setMimeData(new QMimeData);
    const auto settle = [] {
        QEventLoop loop;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
    };
    QString error;
    KoPart *part = KoDocumentEntry::queryByMimeType(QStringLiteral("application/vnd.oasis.opendocument.spreadsheet")).createKoPart(&error);
    require(part, "Sheets part unavailable");
    part->document()->initEmpty();
    part->document()->setAutoSave(0);
    KoMainWindow *window = part->createMainWindow();
    window->setRootDocument(part->document(), part);
    window->show();
    settle();
    auto *ribbon = window->findChild<QWidget *>(QStringLiteral("wordsRibbon"));
    require(ribbon, "Sheets ribbon unavailable");
    KoCanvasController *controller = KoToolManager::instance()->activeCanvasController();
    require(controller, "Sheets canvas controller unavailable");
    KoCanvasBase *canvas = controller->canvas();
    auto *selection = canvas->shapeManager()->selection();
    const auto requireHome = [&] {
        bool home = false;
        for (QTabBar *tabs : ribbon->findChildren<QTabBar *>())
            home |= QString(tabs->tabText(tabs->currentIndex())).remove(QLatin1Char('&')) == QStringLiteral("Home");
        require(home, "deselecting a Sheets object must return Home instead of File");
    };
    for (const QString &shapeId : {QStringLiteral("TextShapeID"), QStringLiteral("FormulaShapeID"), QStringLiteral("PictureShape")}) {
        auto *factory = KoShapeRegistry::instance()->value(shapeId);
        require(factory, "shape factory unavailable");
        KoProperties properties;
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::green);
        properties.setProperty("qimage", image);
        KoShape *shape = shapeId == QLatin1String("PictureShape")
            ? factory->createShape(&properties, canvas->shapeController()->resourceManager())
            : factory->createDefaultShape(canvas->shapeController()->resourceManager());
        canvas->addCommand(canvas->shapeController()->addShapeDirect(shape));
        for (int repetition = 0; repetition < 3; ++repetition) {
            QMetaObject::invokeMethod(ribbon, "showRibbonPage", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("sheetsHomePage")));
            selection->select(shape);
            settle();
            const QString pageName = shapeId == QLatin1String("FormulaShapeID") ? QStringLiteral("sheetsEquationPage") : QStringLiteral("sheetsShapeFormatPage");
            auto *page = window->findChild<QWidget *>(pageName);
            require(page && page->property("ribbonContextActive").toBool(), "selecting an object must show its contextual page");
            KoToolManager::instance()->switchToolRequested(KoToolManager::instance()->preferredToolForSelection({shape}));
            settle();
            KoToolManager::instance()->switchToolRequested(QStringLiteral("InteractionTool"));
            selection->deselectAll();
            settle();
            require(!page->property("ribbonContextActive").toBool(), "deselecting an object must hide its contextual page");
            requireHome();
        }
    }
    part->document()->setModified(false);
    delete window;
    qInfo("PASS Sheets shape, equation and picture deselection returns Home");
}
