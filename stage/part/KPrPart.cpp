/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2012 C. Boemann <cbo@kogmbh.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "KPrPart.h"

#include "KPrDocument.h"
#include "KPrFactory.h"
#include "KPrView.h"

#include "KPrPageSelectStrategyActive.h"
#include "KPrShapeManagerDisplayMasterStrategy.h"
#include <KoCanvasBase.h>
#include <KoComponentData.h>
#include <KoPACanvasItem.h>
#include <KoPAPageBase.h>
#include <KoShapeManager.h>
#include <KoShapeRegistry.h>

#include <KMessageBox>

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

KPrPart::KPrPart(QObject *parent)
    : KoPart(KPrFactory::componentData(), parent)
{
    setTemplatesResourcePath(QLatin1String("calligrastage/templates/"));
}

KPrPart::~KPrPart() = default;

void KPrPart::setDocument(KPrDocument *document)
{
    KoPart::setDocument(document);
    m_document = document;
}

KoView *KPrPart::createViewInstance(KoDocument *document, QWidget *parent)
{
    auto prDocument = qobject_cast<KPrDocument *>(document);
    KPrView *view = new KPrView(this, prDocument, parent);
    connect(prDocument, &KPrDocument::replaceActivePage, view, &KPrView::replaceActivePage);
    return view;
}

QGraphicsItem *KPrPart::createCanvasItem(KoDocument *document)
{
    KoPACanvasItem *canvasItem = new KoPACanvasItem(qobject_cast<KoPADocument *>(document));
    canvasItem->masterShapeManager()->setPaintingStrategy(
        new KPrShapeManagerDisplayMasterStrategy(canvasItem->masterShapeManager(), new KPrPageSelectStrategyActive(canvasItem)));
    return canvasItem;
}

KoMainWindow *KPrPart::createMainWindow()
{
    KoMainWindow *mainWindow = new KoMainWindow(STAGE_MIME_TYPE, componentData());
    // RestoreGeometry() can leave a small "normal geometry" behind even when
    // the saved state is maximized.  Some window systems apply that geometry
    // after the window is mapped, shrinking it again.  Reset the normal
    // geometry to the whole screen before maximizing.
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        mainWindow->setGeometry(screen->availableGeometry());
    }
    QTimer::singleShot(0, mainWindow, [mainWindow]() {
        mainWindow->showMaximized();
    });
    // Wayland can apply the restored geometry after the first maximize call.
    QTimer::singleShot(1000, mainWindow, [mainWindow]() {
        mainWindow->showMaximized();
    });
    return mainWindow;
}

void KPrPart::showStartUpWidget(KoMainWindow *parent, bool alwaysShow)
{
    // Go through all (optional) plugins we require and quit if necessary
    bool error = false;
    KoShapeFactoryBase *factory;

    factory = KoShapeRegistry::instance()->value("TextShapeID");
    if (!factory) {
        m_errorMessage = i18n("Can not find needed text component, Calligra Stage will quit now.");
        error = true;
    }
    factory = KoShapeRegistry::instance()->value("PictureShape");
    if (!factory) {
        m_errorMessage = i18n("Can not find needed picture component, Calligra Stage will quit now.");
        error = true;
    }

    if (error) {
        QTimer::singleShot(0, this, &KPrPart::showErrorAndDie);
    } else {
        KoPart::showStartUpWidget(parent, alwaysShow);
    }
}

void KPrPart::showErrorAndDie()
{
    KMessageBox::error(nullptr, m_errorMessage, i18n("Installation Error"));
    // This means "the environment is incorrect" on Windows
    // FIXME: Is this uniform on all platforms?
    QCoreApplication::exit(10);
}
