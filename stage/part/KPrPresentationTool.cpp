/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2007-2009 Thorsten Zachmann <zachmann@kde.org>
 * SPDX-FileCopyrightText: 2008 Jim Courtiau <jeremy.courtiau@gmail.com>
 * SPDX-FileCopyrightText: 2009 Alexia Allanic <alexia_allanic@yahoo.fr>
 * SPDX-FileCopyrightText: 2009 Jean-Nicolas Artaud <jeannicolasartaud@gmail.com>
 * SPDX-FileCopyrightText: 2009 Jérémy Lugagne <jejewindsurf@hotmail.com>
 * SPDX-FileCopyrightText: 2009 Johann Hingue <yoan1703@hotmail.fr>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KPrPresentationTool.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QActionGroup>
#ifdef WITH_QTDBUS
#include <QDBusConnection>
#endif
#include <QCursor>
#include <QDesktopServices>
#include <QFrame>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include <KLocalizedString>

#include <KoEventAction.h>
#include <KoPACanvasBase.h>
#include <KoPointedAt.h>
#include <KoPointerEvent.h>
#include <KoShape.h>
#include <KoShapeManager.h>
#include <KoTextLayoutRootArea.h>
#include <KoTextShapeData.h>

#include "KPrPresentationBlackStrategy.h"
#include "KPrPresentationDrawStrategy.h"
#include "KPrPresentationHighlightStrategy.h"
#include "KPrDocument.h"
#include "KPrPresentationStrategy.h"
#include "KPrViewModePresentation.h"
#include "ui/KPrPresentationToolWidget.h"

#include <KoPAPageBase.h>

#ifdef WITH_QTDBUS
#include "KPrPresentationToolAdaptor.h"
#endif

KPrPresentationTool::KPrPresentationTool(KPrViewModePresentation &viewMode)
    : KoToolBase(viewMode.canvas())
    , m_viewMode(viewMode)
    , m_strategy(new KPrPresentationStrategy(this))
#ifdef WITH_QTDBUS
    , m_bus(new KPrPresentationToolAdaptor(this))
#endif
{
#ifdef WITH_QTDBUS
    QDBusConnection::sessionBus().registerObject("/kpresenter/PresentationTools", this);
#endif

    // tool box
    m_frame = new QFrame(m_viewMode.canvas()->canvasWidget());

    QVBoxLayout *frameLayout = new QVBoxLayout();

    m_presentationToolWidget = new KPrPresentationToolWidget(m_viewMode.canvas()->canvasWidget());
    frameLayout->addWidget(m_presentationToolWidget, 0, Qt::AlignLeft | Qt::AlignBottom);
    m_frame->setLayout(frameLayout);
    m_frame->show();

    m_presentationToolWidget->raise();
    m_presentationToolWidget->setVisible(false);
    m_presentationToolWidget->installEventFilter(this);

    // Connections of button clicked to slots
    connect(m_presentationToolWidget->presentationToolUi().penButton, &QAbstractButton::clicked, this, &KPrPresentationTool::drawOnPresentation);
    connect(m_presentationToolWidget->presentationToolUi().highLightButton, &QAbstractButton::clicked, this, &KPrPresentationTool::highlightPresentation);
    connect(m_presentationToolWidget->presentationToolUi().blackButton, &QAbstractButton::clicked, this, &KPrPresentationTool::blackPresentation);
}

KPrPresentationTool::~KPrPresentationTool()
{
    delete m_strategy;
}

bool KPrPresentationTool::wantsAutoScroll() const
{
    return false;
}

void KPrPresentationTool::paint(QPainter &painter, const KoViewConverter &converter)
{
    Q_UNUSED(painter);
    Q_UNUSED(converter);
}

void KPrPresentationTool::mousePressEvent(KoPointerEvent *event)
{
    if (event->button() & Qt::LeftButton) {
        event->accept();
        finishEventActions();
        KoShape *shapeClicked = canvas()->shapeManager()->shapeAt(event->point);
        if (shapeClicked) {
            QString link;
            if (checkHyperlink(event, shapeClicked, link)) {
                runHyperlink(link);
                return;
            }

            m_eventActions = shapeClicked->eventActions();
            if (!m_eventActions.isEmpty()) {
                foreach (KoEventAction *eventAction, m_eventActions) {
                    eventAction->start();
                }
                // don't do next step if a action was executed
                return;
            }
        }
        m_viewMode.navigate(KPrAnimationDirector::NextStep);
    } else if (event->button() & Qt::RightButton) {
        event->accept();
        finishEventActions();
        showContextMenu(event->globalPosition().toPoint());
    }
}

void KPrPresentationTool::mouseDoubleClickEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KPrPresentationTool::mouseMoveEvent(KoPointerEvent *event)
{
    KoShape *shape = canvas()->shapeManager()->shapeAt(event->point);

    QString link;
    if (checkHyperlink(event, shape, link)) {
        canvas()->setCursor(Qt::PointingHandCursor);
        return;
    }

    canvas()->setCursor(Qt::ArrowCursor);
}

void KPrPresentationTool::mouseReleaseEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KPrPresentationTool::showContextMenu(const QPoint &globalPos)
{
    QWidget *canvasWidget = m_viewMode.canvas()->canvasWidget();
    QMenu *menu = new QMenu(canvasWidget);
    menu->setAttribute(Qt::WA_DeleteOnClose);

    QAction *previous = menu->addAction(i18n("&Previous"));
    connect(previous, &QAction::triggered, this, [this]() {
        m_viewMode.navigate(KPrAnimationDirector::PreviousStep);
    });

    QAction *next = menu->addAction(i18n("&Next"));
    connect(next, &QAction::triggered, this, [this]() {
        m_viewMode.navigate(KPrAnimationDirector::NextStep);
    });

    menu->addSeparator();

    QAction *first = menu->addAction(i18n("&First Slide"));
    connect(first, &QAction::triggered, this, [this]() {
        m_viewMode.navigate(KPrAnimationDirector::FirstPage);
    });

    QAction *last = menu->addAction(i18n("&Last Slide"));
    connect(last, &QAction::triggered, this, [this]() {
        m_viewMode.navigate(KPrAnimationDirector::LastPage);
    });

    menu->addSeparator();

    QMenu *goToSlide = menu->addMenu(i18n("Go to &Slide"));
    KPrDocument *document = qobject_cast<KPrDocument *>(m_viewMode.canvas()->document());
    if (document) {
        const QList<KoPAPageBase *> slides = document->slideShow();
        for (int i = 0; i < slides.size(); ++i) {
            QString title = slides.at(i)->name();
            if (title.isEmpty()) {
                title = i18n("Slide %1", i + 1);
            }
            QAction *slideAction = goToSlide->addAction(QStringLiteral("%1. %2").arg(i + 1).arg(title));
            slideAction->setCheckable(true);
            slideAction->setChecked(i == m_viewMode.currentPage());
            connect(slideAction, &QAction::triggered, this, [this, i]() {
                m_viewMode.navigateToPage(i);
            });
        }
    }
    goToSlide->setEnabled(!goToSlide->isEmpty());

    menu->addSeparator();

    QMenu *screen = menu->addMenu(i18n("&Screen"));
    QAction *blackScreen = screen->addAction(i18n("&Black Screen"));
    blackScreen->setCheckable(true);
    blackScreen->setChecked(dynamic_cast<KPrPresentationBlackStrategy *>(m_strategy) != nullptr);
    connect(blackScreen, &QAction::triggered, this, &KPrPresentationTool::blackPresentation);

    QMenu *pointer = menu->addMenu(i18n("&Pointer Options"));
    QActionGroup *pointerGroup = new QActionGroup(menu);
    pointerGroup->setExclusive(true);

    QAction *arrow = pointer->addAction(i18n("&Arrow"));
    arrow->setCheckable(true);
    arrow->setChecked(dynamic_cast<KPrPresentationStrategy *>(m_strategy) != nullptr);
    pointerGroup->addAction(arrow);
    connect(arrow, &QAction::triggered, this, &KPrPresentationTool::normalPresentation);

    QAction *pen = pointer->addAction(i18n("&Pen"));
    pen->setCheckable(true);
    pen->setChecked(dynamic_cast<KPrPresentationDrawStrategy *>(m_strategy) != nullptr);
    pointerGroup->addAction(pen);
    connect(pen, &QAction::triggered, this, &KPrPresentationTool::drawOnPresentation);

    QAction *highlighter = pointer->addAction(i18n("&Highlighter"));
    highlighter->setCheckable(true);
    highlighter->setChecked(dynamic_cast<KPrPresentationHighlightStrategy *>(m_strategy) != nullptr);
    pointerGroup->addAction(highlighter);
    connect(highlighter, &QAction::triggered, this, &KPrPresentationTool::highlightPresentation);

    menu->addSeparator();

    QAction *help = menu->addAction(i18n("&Help"));
    connect(help, &QAction::triggered, this, [canvasWidget]() {
        QMessageBox::information(canvasWidget, i18n("Slide Show Help"),
                                 i18n("<p><b>Next:</b> Right arrow, Space, Page Down or Down arrow<br>"
                                      "<b>Previous:</b> Left arrow, Backspace, Page Up or Up arrow<br>"
                                      "<b>First/Last slide:</b> Home / End<br>"
                                      "<b>End show:</b> Esc<br>"
                                      "<b>Context menu:</b> Right-click or Shift+F10</p>"));
    });

    menu->addSeparator();

    QAction *endShow = menu->addAction(i18n("&End Show"));
    connect(endShow, &QAction::triggered, this, [this]() {
        m_viewMode.activateSavedViewMode();
    });

    menu->popup(globalPos);
}

void KPrPresentationTool::keyPressEvent(QKeyEvent *event)
{
    finishEventActions();

    // Match PowerPoint's Shift+F10 / Menu-key behaviour.
    if (event->key() == Qt::Key_Menu || (event->key() == Qt::Key_F10 && event->modifiers() & Qt::ShiftModifier)) {
        event->accept();
        showContextMenu(QCursor::pos());
        return;
    }

    // first try to handle the event in the strategy if it is done there no need to use the default action
    if (!m_strategy->keyPressEvent(event)) {
        switch (event->key()) {
        case Qt::Key_Escape:
            m_viewMode.activateSavedViewMode();
            break;
        case Qt::Key_Home:
            m_viewMode.navigate(KPrAnimationDirector::FirstPage);
            break;
        case Qt::Key_Up:
        case Qt::Key_PageUp:
            m_viewMode.navigate(KPrAnimationDirector::PreviousPage);
            break;
        case Qt::Key_Backspace:
        case Qt::Key_Left:
            m_viewMode.navigate(KPrAnimationDirector::PreviousStep);
            break;
        case Qt::Key_Right:
        case Qt::Key_Space:
            m_viewMode.navigate(KPrAnimationDirector::NextStep);
            break;
        case Qt::Key_Down:
        case Qt::Key_PageDown:
            m_viewMode.navigate(KPrAnimationDirector::NextPage);
            break;
        case Qt::Key_End:
            m_viewMode.navigate(KPrAnimationDirector::LastPage);
            break;
        default:
            event->ignore();
            break;
        }
    }
}

void KPrPresentationTool::keyReleaseEvent(QKeyEvent *event)
{
    Q_UNUSED(event);
}

void KPrPresentationTool::wheelEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KPrPresentationTool::activate(ToolActivation toolActivation, const QSet<KoShape *> &shapes)
{
    Q_UNUSED(toolActivation);
    Q_UNUSED(shapes);
    m_frame->setGeometry(canvas()->canvasWidget()->geometry());
    m_presentationToolWidget->setVisible(false);
    // redirect event to tool widget
    m_frame->installEventFilter(this);
    // activate tracking for show/hide tool buttons
    m_frame->setMouseTracking(true);
}

void KPrPresentationTool::deactivate()
{
    switchStrategy(new KPrPresentationStrategy(this));
    finishEventActions();
}

void KPrPresentationTool::finishEventActions()
{
    foreach (KoEventAction *eventAction, m_eventActions) {
        eventAction->finish();
    }
}

void KPrPresentationTool::switchStrategy(KPrPresentationStrategyBase *strategy)
{
    Q_ASSERT(strategy);
    Q_ASSERT(m_strategy != strategy);
    delete m_strategy;
    m_strategy = strategy;
}

// SLOTS
void KPrPresentationTool::highlightPresentation()
{
    KPrPresentationStrategyBase *strategy;
    if (dynamic_cast<KPrPresentationHighlightStrategy *>(m_strategy)) {
        strategy = new KPrPresentationStrategy(this);
    } else {
        strategy = new KPrPresentationHighlightStrategy(this);
    }
    switchStrategy(strategy);
}

void KPrPresentationTool::drawOnPresentation()
{
    KPrPresentationStrategyBase *strategy;
    if (dynamic_cast<KPrPresentationDrawStrategy *>(m_strategy)) {
        strategy = new KPrPresentationStrategy(this);
    } else {
        strategy = new KPrPresentationDrawStrategy(this);
    }
    switchStrategy(strategy);
}

void KPrPresentationTool::blackPresentation()
{
    KPrPresentationStrategyBase *strategy;
    if (dynamic_cast<KPrPresentationBlackStrategy *>(m_strategy)) {
        strategy = new KPrPresentationStrategy(this);
    } else {
        strategy = new KPrPresentationBlackStrategy(this);
    }
    switchStrategy(strategy);
}

bool KPrPresentationTool::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseMove) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        QWidget *source = static_cast<QWidget *>(obj);
        QPoint pos = source->mapFrom(m_viewMode.canvas()->canvasWidget(), mouseEvent->pos());

        QSize buttonSize = m_presentationToolWidget->size() + QSize(20, 20);
        QRect geometry = QRect(0, m_frame->height() - buttonSize.height(), buttonSize.width(), buttonSize.height());
        if (geometry.contains(pos)) {
            m_presentationToolWidget->setVisible(true);
        } else {
            m_presentationToolWidget->setVisible(false);
        }
    }
    return false;
}

bool KPrPresentationTool::checkHyperlink(KoPointerEvent *event, KoShape *shape, QString &hyperLink)
{
    if (!shape) {
        return false;
    }

    KoTextShapeData *textShapeData = qobject_cast<KoTextShapeData *>(shape->userData());
    if (textShapeData) {
        if (!textShapeData->rootArea()) {
            return false; // not layouted yet
        }
        QPointF p = shape->absoluteTransformation(nullptr).inverted().map(event->point);
        p = p + QPointF(0.0, textShapeData->documentOffset());

        KoPointedAt pointedAt = textShapeData->rootArea()->hitTest(p, Qt::ExactHit);

        if (!textShapeData->isDirty() && !pointedAt.externalHRef.isEmpty()) {
            hyperLink = pointedAt.externalHRef;
            return true;
        }
    }
    return false;
}

void KPrPresentationTool::runHyperlink(const QString &hyperLink)
{
    QUrl url = QUrl::fromUserInput(hyperLink);

    QDesktopServices::openUrl(url);
}

KPrPresentationStrategyBase *KPrPresentationTool::strategy()
{
    return m_strategy;
}

KPrViewModePresentation &KPrPresentationTool::viewModePresentation()
{
    return m_viewMode;
}

void KPrPresentationTool::normalPresentation()
{
    switchStrategy(new KPrPresentationStrategy(this));
}
