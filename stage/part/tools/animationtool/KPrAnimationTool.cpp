/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2008 C. Boemann <cbo@boemann.dk>
   SPDX-FileCopyrightText: 2012 Paul Mendez <paulestebanms@gmail.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "KPrAnimationTool.h"
#include <KoMainWindow.h>
#include <KoRibbonIcons.h>
#include <KoRibbonLayout.h>
#include <KActionCollection>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include "KPrView.h"


// Qt Headers
#include <QLabel>
#include <QList>
#include <QPainter>

// KF5 Headers
#include <KLocalizedString>

// Calligra Headers
#include <KoPACanvas.h>
#include <KoPAPageBase.h>
#include <KoPAViewBase.h>
#include <KoPathShape.h>
#include <KoPointerEvent.h>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include <KoShapePaintingContext.h>
#include <KoShapeStroke.h>
#include <KoViewConverter.h>
#include <KoZoomController.h>
#include <KoZoomHandler.h>

// internal Headers
#include "KPrClickActionDocker.h"
#include "KPrPageData.h"
#include "KPrShapeAnimationDocker.h"
#include "KPrShapeApplicationData.h"
#include "animations/KPrAnimateMotion.h"
#include <KPrPageEffectDocker.h>

const int HANDLE_DISTANCE = 10;

KPrAnimationTool::KPrAnimationTool(KoCanvasBase *canvas)
    : KoPathTool(canvas)
    , m_currentMotionPathSelected(nullptr)
    , m_pathShapeManager(nullptr)
    , m_initializeTool(true)
    , m_shapeAnimationWidget(nullptr)
{
}

void KPrAnimationTool::configureRibbon(QWidget *transitions, QWidget *animations, QWidget *insert)
{
    if (property("ribbonConfigured").toBool()) {
        return;
    }
    setProperty("ribbonConfigured", true);
    auto *view = dynamic_cast<KPrView *>(static_cast<KoPACanvas *>(canvas())->koPAView());
    if (!view || !view->mainWindow()) {
        return;
    }
    auto *window = view->mainWindow();
    // The ribbon pages are created empty by KoAppRibbon; a layout only exists once
    // at least one action group could be added to the page. Reuse it when present
    // and create one otherwise, so the dockers are actually shown and pages without
    // any groups cannot crash us.
    const auto addToRibbon = [](QWidget *page, QWidget *child) {
        if (auto *box = qobject_cast<QBoxLayout *>(page->layout())) {
            if (page->objectName() == QLatin1String("stageInsertPage")) {
                int position = 0;
                const int order = child->property("ribbonGroupOrder").toInt();
                while (position < box->count() && box->itemAt(position)->widget()
                       && box->itemAt(position)->widget()->property("ribbonGroupOrder").toInt() <= order) {
                    ++position;
                }
                box->insertWidget(position, child, 0, Qt::AlignLeft | Qt::AlignTop);
            } else {
                box->addWidget(child, 0, Qt::AlignLeft | Qt::AlignTop);
            }
        } else if (page->layout()) {
            page->layout()->addWidget(child);
        } else {
            auto *layout = new QHBoxLayout(page);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(child, 0, Qt::AlignLeft | Qt::AlignTop);
        }
    };
    for (QWidget *widget : optionWidgets()) {
        if (!widget) {
            continue;
        }
        widget->setProperty("ribbonManagedExternally", true);
        if (auto *effects = qobject_cast<KPrPageEffectDocker *>(widget)) {
            effects->configureRibbon();
            addToRibbon(transitions, effects);
        } else if (auto *shapeAnimations = qobject_cast<KPrShapeAnimationDocker *>(widget)) {
            auto *pane = new QDockWidget(i18n("Animation Pane"), window);
            pane->setObjectName(QStringLiteral("stageAnimationPane"));
            pane->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
            pane->setWidget(shapeAnimations);
            pane->setMinimumWidth(280);
            window->addDockWidget(Qt::RightDockWidgetArea, pane);
            pane->hide();
            QObject::connect(view, &QObject::destroyed, pane, &QObject::deleteLater);
            view->actionCollection()->addAction(QStringLiteral("stage_animation_pane"), pane->toggleViewAction());
            addToRibbon(animations, shapeAnimations->createRibbonControls(pane));
        } else if (qobject_cast<KPrClickActionDocker *>(widget)) {
            auto *group = new QWidget(insert);
            group->setProperty("ribbonGroupOrder", 50);
            group->setProperty("ribbonRoutedGroup", true);
            auto *layout = new QVBoxLayout(group);
            layout->setContentsMargins(6, 0, 6, 0);
            layout->setSpacing(2);
            auto *button = new QToolButton(group);
            button->setText(i18n("Action"));
            button->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("system-run")));
            KoRibbonLayout::configureButton(button, Qt::ToolButtonTextUnderIcon);
            button->setProperty("ribbonPreferredIconSize", QSize(36, 36));
            button->setProperty("ribbonPreferredMinimumSize", QSize(72, 76));
            button->setIconSize(QSize(36, 36));
            button->setMinimumSize(button->sizeHint().expandedTo(QSize(72, 76)));
            auto *menu = new QMenu(button);
            auto *action = new QWidgetAction(menu);
            widget->setMinimumWidth(280);
            action->setDefaultWidget(widget);
            menu->addAction(action);
            button->setMenu(menu);
            button->setPopupMode(QToolButton::InstantPopup);
            layout->addWidget(button);
            auto *label = new QLabel(i18n("Links"), group);
            label->setAlignment(Qt::AlignCenter);
            label->setProperty("ribbonGroupTitle", true);
            layout->addWidget(label);
            layout->addStretch();
            addToRibbon(insert, group);
        }
    }
}

KPrAnimationTool::~KPrAnimationTool()
{
    cleanMotionPathManager();
    delete m_pathShapeManager;
}

void KPrAnimationTool::paint(QPainter &painter, const KoViewConverter &converter)
{
    foreach (KoShape *shape, canvas()->shapeManager()->selection()->selectedShapes()) {
        painter.save();
        // save the original painter transformation
        QTransform painterMatrix = painter.worldTransform();
        painter.setPen(QPen(Qt::green, 0));
        // apply the shape transformation on top of the old painter transformation
        painter.setWorldTransform(shape->absoluteTransformation(&converter) * painterMatrix);
        // apply the zoom factor
        KoShape::applyConversion(painter, converter);
        // draw the shape bounding rect
        painter.drawRect(QRectF(QPointF(), shape->size()));
        painterMatrix = painter.worldTransform();
        painter.restore();
    }
    // Paint motion paths
    QMapIterator<KoPathShape *, KPrAnimateMotion *> i(m_animateMotionMap);
    while (i.hasNext()) {
        i.next();
        QSizeF pageSize = getPageSize();
        if (i.value()->currentPageSize() != pageSize) {
            i.value()->getPath(1, pageSize);
        }
    }
    if (m_pathShapeManager) {
        m_pathShapeManager->paint(painter, converter, false);
    }
    KoPathTool::paint(painter, converter);
}

void KPrAnimationTool::activate(ToolActivation toolActivation, const QSet<KoShape *> &shapes)
{
    Q_UNUSED(toolActivation);
    Q_UNUSED(shapes);
    useCursor(m_selectCursor);
    repaintDecorations();

    if (!m_pathShapeManager) {
        m_pathShapeManager = new KoShapeManager(canvas());
    }
    if (m_initializeTool) {
        reloadMotionPaths();
        connect((static_cast<KoPACanvas *>(canvas()))->koPAView()->proxyObject,
                &KoPAViewProxyObject::activePageChanged,
                this,
                &KPrAnimationTool::reloadMotionPaths);
        if (m_shapeAnimationWidget) {
            connect((static_cast<KoPACanvas *>(canvas()))->koPAView()->proxyObject,
                    &KoPAViewProxyObject::activePageChanged,
                    m_shapeAnimationWidget,
                    &KPrShapeAnimationDocker::slotActivePageChanged);
        }
    }

    // Init parent tool if motion path shape is selected
    QList<KoPathShape *> selectedShapes;
    foreach (KoShape *shape, shapes) {
        KoPathShape *pathShape = dynamic_cast<KoPathShape *>(shape);
        if (shape->isEditable() && pathShape && !shape->isPrintable()) {
            if (m_currentMotionPathSelected == pathShape) {
                return;
            }
            selectedShapes.append(pathShape);
        }
    }
    if (!selectedShapes.isEmpty()) {
        KoPathTool::activate(toolActivation, shapes);
    }
}

void KPrAnimationTool::deactivate()
{
    // Clean shape manager of motion paths
    cleanMotionPathManager();
    disconnect((static_cast<KoPACanvas *>(canvas()))->koPAView()->proxyObject,
               &KoPAViewProxyObject::activePageChanged,
               this,
               &KPrAnimationTool::reloadMotionPaths);
    disconnect((static_cast<KoPACanvas *>(canvas()))->koPAView()->proxyObject,
               &KoPAViewProxyObject::activePageChanged,
               m_shapeAnimationWidget,
               &KPrShapeAnimationDocker::slotActivePageChanged);
    m_initializeTool = true;
    delete m_pathShapeManager;
    m_pathShapeManager = nullptr;
    KoPathTool::deactivate();
}

void KPrAnimationTool::mousePressEvent(KoPointerEvent *event)
{
    // If no shape was click deselect all
    KoSelection *selection = canvas()->shapeManager()->selection();
    foreach (KoShape *shape, selection->selectedShapes()) {
        shape->update();
    }
    selection->deselectAll();
    // Select clicked shape
    KoShape *shape = canvas()->shapeManager()->shapeAt(event->point);
    if (shape) {
        selection->select(shape);
        selection->update();
        shape->update();
    }
    // Init Path tool if motion shape is selected
    shape = m_pathShapeManager->shapeAt(event->point);
    if (KoPathShape *pathShape = dynamic_cast<KoPathShape *>(shape)) {
        if (!pathShape->isPrintable()) {
            QSet<KoShape *> shapes;
            shapes << pathShape;
            m_initializeTool = false;
            activate(DefaultActivation, shapes);
            m_currentMotionPathSelected = pathShape;
        }
    }
    KoPathTool::mousePressEvent(event);
}

void KPrAnimationTool::repaintDecorations()
{
    if (canvas()->shapeManager()->selection()->count() > 0) {
        canvas()->updateCanvas(handlesSize());
    }
    KoPathTool::repaintDecorations();
}

QRectF KPrAnimationTool::handlesSize()
{
    QRectF bound = canvas()->shapeManager()->selection()->boundingRect();

    // expansion Border
    if (!canvas() || !canvas()->viewConverter())
        return bound;

    QPointF border = canvas()->viewConverter()->viewToDocument(QPointF(HANDLE_DISTANCE, HANDLE_DISTANCE));
    bound.adjust(-border.x(), -border.y(), border.x(), border.y());
    return bound;
}

QList<QPointer<QWidget>> KPrAnimationTool::createOptionWidgets()
{
    KPrPageEffectDocker *effectWidget = new KPrPageEffectDocker();
    effectWidget->setView((static_cast<KoPACanvas *>(canvas()))->koPAView());

    KPrClickActionDocker *clickActionWidget = new KPrClickActionDocker();
    clickActionWidget->setView((static_cast<KoPACanvas *>(canvas()))->koPAView());

    m_shapeAnimationWidget = new KPrShapeAnimationDocker();
    m_shapeAnimationWidget->setView((static_cast<KoPACanvas *>(canvas()))->koPAView());
    connect(m_shapeAnimationWidget, &KPrShapeAnimationDocker::shapeAnimationsChanged, this, &KPrAnimationTool::verifyMotionPathChanged);
    connect(m_shapeAnimationWidget, &KPrShapeAnimationDocker::motionPathAddedRemoved, this, &KPrAnimationTool::reloadMotionPaths);
    connect((static_cast<KoPACanvas *>(canvas()))->koPAView()->proxyObject,
            &KoPAViewProxyObject::activePageChanged,
            m_shapeAnimationWidget,
            &KPrShapeAnimationDocker::slotActivePageChanged);

    QList<QPointer<QWidget>> widgets;
    effectWidget->setWindowTitle(i18n("Slide Transitions"));
    widgets.append(effectWidget);
    clickActionWidget->setWindowTitle(i18n("Shape Click Actions"));
    widgets.append(clickActionWidget);
    m_shapeAnimationWidget->setWindowTitle(i18n("Shape Animations"));
    widgets.append(m_shapeAnimationWidget);
    return widgets;
}

void KPrAnimationTool::initMotionPathShapes()
{
    cleanMotionPathManager();
    // Load motion paths Data
    KPrPageData *pageData = dynamic_cast<KPrPageData *>((static_cast<KoPACanvas *>(canvas()))->koPAView()->activePage());
    Q_ASSERT(pageData);
    KPrShapeAnimations *animations = &(pageData->animations());
    for (int j = 0; j < animations->rowCount(); j++) {
        KPrShapeAnimation *anim = animations->animationByRow(j);
        if (anim->presetClass() == KPrShapeAnimation::MotionPath) {
            for (int i = 0; i < anim->animationCount(); i++) {
                if (KPrAnimateMotion *motion = dynamic_cast<KPrAnimateMotion *>(anim->animationAt(i))) {
                    // Load motion path
                    QSizeF pageSize = getPageSize();
                    KoPathShape *path = motion->getPath(1, pageSize);
                    m_animateMotionMap.insert(path, motion);
                    m_shapesMap.insert(path, anim->shape());
                    // change stroke appearance
                    KoShapeStroke *stroke = new KoShapeStroke();
                    QVector<qreal> dashes;
                    qreal space = 8;
                    dashes << 1 << space << 3 << space;
                    stroke->setLineStyle(Qt::DashLine, dashes);
                    stroke->setLineWidth(1);
                    stroke->setColor(Qt::gray);
                    path->setStroke(stroke);
                    addPathShape(path);
                }
            }
        }
    }
}

void KPrAnimationTool::addPathShape(KoPathShape *pathShape)
{
    if (!m_pathShapeManager) {
        return;
    }
    pathShape->setPrintable(false);
    m_pathShapeManager->addShape(pathShape);
}

QSizeF KPrAnimationTool::getPageSize()
{
    QSizeF pageSize = static_cast<KoPACanvas *>(canvas())->koPAView()->zoomController()->pageSize();
    return pageSize;
}

void KPrAnimationTool::cleanMotionPathManager()
{
    if (!m_pathShapeManager) {
        return;
    }
    foreach (KoShape *shape, m_pathShapeManager->shapes()) {
        m_pathShapeManager->remove(shape);
    }
    m_animateMotionMap.clear();
    m_shapesMap.clear();
    m_currentMotionPathSelected = nullptr;
}

void KPrAnimationTool::reloadMotionPaths()
{
    // Remove handles
    m_pointSelection.clear();
    m_pointSelection.setSelectedShapes(QList<KoPathShape *>());
    m_pointSelection.update();
    // Load motion paths
    initMotionPathShapes();
}

void KPrAnimationTool::verifyMotionPathChanged(KoShape *shape)
{
    QMapIterator<KoPathShape *, KoShape *> i(m_shapesMap);
    while (i.hasNext()) {
        i.next();
        if (i.value() == shape) {
            reloadMotionPaths();
        }
    }
}
