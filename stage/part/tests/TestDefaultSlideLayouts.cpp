/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "KPrDocument.h"
#include "KPrPage.h"
#include "KPrView.h"

#include <KoDocumentEntry.h>
#include <KoColorBackground.h>
#include <KoDocumentResourceManager.h>
#include <KoPatternBackground.h>
#include <KoGlobal.h>
#include <KoMainWindow.h>
#include <KoPart.h>
#include <KoShapeContainer.h>
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
        document->setModified(false);
        delete window;
        app.processEvents();
        qInfo() << "PASS default layouts, insertion, undo/redo, reload and existing blank slides:" << name;
    }
    return 0;
}
