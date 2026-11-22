/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2009 Thomas Zander <zander@kde.org>
   SPDX-FileCopyrightText: 2006-2007 Stefan Nikolaus <stefan.nikolaus@kdemail.net>
   SPDX-FileCopyrightText: 2006 Robert Knight <robertknight@gmail.com>
   SPDX-FileCopyrightText: 2006 Inge Wallin <inge@lysator.liu.se>
   SPDX-FileCopyrightText: 1999-2002, 2004 Laurent Montel <montel@kde.org>
   SPDX-FileCopyrightText: 2002-2005 Ariya Hidayat <ariya@kde.org>
   SPDX-FileCopyrightText: 1999-2004 David Faure <faure@kde.org>
   SPDX-FileCopyrightText: 2004-2005 Meni Livne <livne@kde.org>
   SPDX-FileCopyrightText: 2001-2003 Philipp Mueller <philipp.mueller@gmx.de>
   SPDX-FileCopyrightText: 2002-2003 Norbert Andres <nandres@web.de>
   SPDX-FileCopyrightText: 2003 Hamish Rodda <rodda@kde.org>
   SPDX-FileCopyrightText: 2003 Joseph Wenninger <jowenn@kde.org>
   SPDX-FileCopyrightText: 2003 Lukas Tinkl <lukas@kde.org>
   SPDX-FileCopyrightText: 2000-2002 Werner Trobin <trobin@kde.org>
   SPDX-FileCopyrightText: 2002 Harri Porten <porten@kde.org>
   SPDX-FileCopyrightText: 2002 John Dailey <dailey@vt.edu>
   SPDX-FileCopyrightText: 2002 Daniel Naber <daniel.naber@t-online.de>
   SPDX-FileCopyrightText: 1999-2000 Torben Weis <weis@kde.org>
   SPDX-FileCopyrightText: 1999-2000 Stephan Kulow <coolo@kde.org>
   SPDX-FileCopyrightText: 2000 Bernd Wuebben <wuebben@kde.org>
   SPDX-FileCopyrightText: 2000 Wilco Greven <greven@kde.org>
   SPDX-FileCopyrightText: 2000 Simon Hausmann <hausmann@kde.org
   SPDX-FileCopyrightText: 1999 Michael Reiher <michael.reiher@gmx.de>
   SPDX-FileCopyrightText: 1999 Boris Wedl <boris.wedl@kfunigraz.ac.at>
   SPDX-FileCopyrightText: 1999 Reginald Stadlbauer <reggie@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Local
#include "Canvas.h"
#include "CanvasBase_p.h"
#include "Doc.h"
#include "HeaderWidgets.h"
#include "View.h"

// Qt
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>

// KF5
#include <kxmlguifactory.h>

// Calligra
#include <KoCanvasController.h>
#include <KoChartInterface.h>
#include <KoShapeContainer.h>
#include <KoShapeManager.h>
#include <KoToolManager.h>
#include <KoToolProxy.h>
#include <KoZoomHandler.h>

// Sheets
#include "core/Cell.h"
#include "core/Map.h"
#include "core/Sheet.h"
#include "engine/SheetsDebug.h"
#include "ui/Selection.h"

#include <algorithm>

#define MIN_SIZE 10

using namespace Calligra::Sheets;

namespace {

struct ChartSourceRegion {
    int dataSet = -1;
    QString role;
    QString sheet;
    QRect rect;
};

QVariantMap chartSourceRangesToVariant(const QMap<QString, QList<QRect>> &sourceRanges)
{
    QVariantMap result;
    for (auto it = sourceRanges.cbegin(); it != sourceRanges.cend(); ++it) {
        QVariantList ranges;
        ranges.reserve(it.value().size());
        for (const QRect &range : it.value()) {
            ranges.append(QVariant(range));
        }
        result.insert(it.key(), ranges);
    }
    return result;
}

QVariantList chartSourceRegionsToVariant(const QList<ChartSourceRegion> &regions)
{
    QVariantList result;
    result.reserve(regions.size());
    for (const ChartSourceRegion &region : regions) {
        QVariantMap entry;
        entry.insert(QStringLiteral("dataSet"), region.dataSet);
        entry.insert(QStringLiteral("role"), region.role);
        entry.insert(QStringLiteral("sheet"), region.sheet);
        entry.insert(QStringLiteral("rect"), region.rect);
        result.append(entry);
    }
    return result;
}

QList<ChartSourceRegion> chartSourceRegionsFromVariant(const QVariantList &values)
{
    QList<ChartSourceRegion> result;
    result.reserve(values.size());
    for (const QVariant &value : values) {
        const QVariantMap entry = value.toMap();
        ChartSourceRegion region;
        bool dataSetOk = false;
        region.dataSet = entry.value(QStringLiteral("dataSet")).toInt(&dataSetOk);
        if (!dataSetOk) {
            region.dataSet = -1;
        }
        region.role = entry.value(QStringLiteral("role")).toString();
        region.sheet = entry.value(QStringLiteral("sheet")).toString();
        region.rect = entry.value(QStringLiteral("rect")).toRect();
        if (!region.sheet.isEmpty() && region.rect.isValid()) {
            result.append(region);
        }
    }
    return result;
}

QList<ChartSourceRegion> chartSourceRegionsFromVariantMap(const QVariantMap &sourceRanges)
{
    QList<ChartSourceRegion> result;
    for (auto it = sourceRanges.cbegin(); it != sourceRanges.cend(); ++it) {
        const QVariantList values = it.value().toList();
        for (const QVariant &value : values) {
            if (!value.canConvert<QRect>()) {
                continue;
            }
            ChartSourceRegion region;
            region.sheet = it.key();
            region.rect = value.toRect();
            if (region.rect.isValid()) {
                result.append(region);
            }
        }
    }
    return result;
}

QMap<QString, QList<QRect>> chartSourceRangesFromRegions(const QList<ChartSourceRegion> &regions)
{
    QMap<QString, QList<QRect>> result;
    for (const ChartSourceRegion &region : regions) {
        auto &ranges = result[region.sheet];
        if (!ranges.contains(region.rect)) {
            ranges.append(region.rect);
        }
    }
    return result;
}

QRectF chartRangeDocumentRect(CanvasBase *canvas, Sheet *sheet, const QRect &range)
{
    QRectF area = sheet->cellCoordinatesToDocument(range);
    if (sheet->layoutDirection() == Qt::RightToLeft) {
        const QRectF paintRect(canvas->viewConverter()->viewToDocument(canvas->canvasWidget()->rect()).translated(canvas->offset()));
        area.moveLeft(paintRect.width() - area.right());
    }
    return area;
}

QRectF chartRangeHandleRect(CanvasBase *canvas, Sheet *sheet, const QRect &range)
{
    qreal zoomX = 1.0;
    qreal zoomY = 1.0;
    canvas->viewConverter()->zoom(&zoomX, &zoomY);
    const QRectF area = chartRangeDocumentRect(canvas, sheet, range);
    const QSizeF handleSize(10.0 / zoomX, 10.0 / zoomY);
    return QRectF(area.right() - handleSize.width() / 2, area.bottom() - handleSize.height() / 2, handleSize.width(), handleSize.height());
}

} // namespace

class Q_DECL_HIDDEN Canvas::Private
{
public:
    View *view;
    QList<ChartSourceRegion> chartSourceRegions;
    bool draggingChartSourceRegion = false;
    int draggedSourceRegionIndex = -1;
    QRect draggedOriginalRect;
};

/****************************************************************
 *
 * Canvas
 *
 ****************************************************************/

Canvas::Canvas(View *view)
    : QWidget(view)
    , CanvasBase(view ? view->doc() : nullptr)
    , cd(new Private)
{
    cd->view = view;

    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_StaticContents);
    setBackgroundRole(QPalette::Base);
    QWidget::setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    installEventFilter(this); // for TAB key processing, otherwise focus change
    setAcceptDrops(true);
    setAttribute(Qt::WA_InputMethodEnabled, true); // ensure using the InputMethod
}

Canvas::~Canvas()
{
    foreach (QAction *action, actions()) {
        removeAction(action);
    }

    delete cd;
}

View *Canvas::view() const
{
    return cd->view;
}

void Canvas::validateSelection()
{
    Sheet *const sheet = activeSheet();
    if (!sheet)
        return;

    if (selection()->isSingular()) {
        const Cell cell = Cell(sheet, selection()->cursor()).masterCell();
        Validity validity = cell.validity();
        if (validity.displayValidationInformation()) {
            const QString title = validity.titleInfo();
            QString message = validity.messageInfo();
            if (title.isEmpty() && message.isEmpty())
                return;

            QString resultText("<html><body>");
            if (!title.isEmpty()) {
                resultText += "<h2>" + title + "</h2>";
            }
            if (!message.isEmpty()) {
                message.replace(QChar('\n'), QString("<br>"));
                resultText += "<p>" + message + "</p>";
            }
            resultText += "</body></html>";

            const double xpos = sheet->columnPosition(cell.column()) + cell.width();
            const double ypos = sheet->rowPosition(cell.row()) + cell.height();
            const QPointF position = QPointF(xpos, ypos) - offset();
            QToolTip::showText(view()->mapToGlobal(QPoint(position.x(), position.y())), resultText);
        } else {
            QToolTip::hideText();
        }
    } else {
        QToolTip::hideText();
    }
}

void Canvas::mousePressEvent(QMouseEvent *event)
{
    QPointF highlightPosition;
    if (layoutDirection() == Qt::LeftToRight) {
        highlightPosition = viewConverter()->viewToDocument(event->position().toPoint()) + offset();
    } else {
        const QPoint position(QWidget::width() - event->position().x(), event->position().y());
        highlightPosition = viewConverter()->viewToDocument(position) + offset();
    }

    if (event->button() == Qt::LeftButton && !cd->chartSourceRegions.isEmpty()) {
        Sheet *sheet = activeSheet();
        if (sheet) {
            qreal zoomX = 1.0;
            qreal zoomY = 1.0;
            viewConverter()->zoom(&zoomX, &zoomY);
            const qreal slackX = 4.0 / zoomX;
            const qreal slackY = 4.0 / zoomY;
            for (int i = 0; i < cd->chartSourceRegions.size(); ++i) {
                const ChartSourceRegion &region = cd->chartSourceRegions.at(i);
                if (region.sheet != sheet->sheetName()) {
                    continue;
                }
                const QRectF handle = chartRangeHandleRect(this, sheet, region.rect)
                                          .adjusted(-slackX, -slackY, slackX, slackY);
                if (!handle.contains(highlightPosition)) {
                    continue;
                }
                cd->draggingChartSourceRegion = true;
                cd->draggedSourceRegionIndex = i;
                cd->draggedOriginalRect = region.rect;
                setCursor(Qt::SizeFDiagCursor);
                event->accept();
                return;
            }
        }
    }

    if (!d->chartSourceRanges.isEmpty()) {
        d->chartSourceRanges.clear();
        cd->chartSourceRegions.clear();
        update();
    }

    // KoPointerEvent pev(event, QPointF());
    // mousePressed(&pev);

    QMouseEvent *const origEvent = event;
    QPointF documentPosition;
    if (layoutDirection() == Qt::LeftToRight) {
        documentPosition = viewConverter()->viewToDocument(event->position().toPoint()) + offset();
    } else {
        const QPoint position(QWidget::width() - event->position().x(), event->position().y());
        const QPointF offset(this->offset().x(), this->offset().y());
        documentPosition = viewConverter()->viewToDocument(position) + offset;
        debugSheets << "----------------------------";
        debugSheets << "event->position().toPoint():" << event->position().toPoint();
        debugSheets << "event->globalPosition().toPoint():" << event->globalPosition().toPoint();
        debugSheets << "position:" << position;
        debugSheets << "offset:" << offset;
        debugSheets << "documentPosition:" << documentPosition;
        event = new QMouseEvent(QEvent::MouseButtonPress, position, mapToGlobal(position), event->button(), event->buttons(), event->modifiers());
        debugSheets << "newEvent->pos():" << event->position().toPoint();
        debugSheets << "newEvent->globalPosition().toPoint():" << event->globalPosition().toPoint();
    }

#if 0 // This is disabled for now as per irc, as it blocks resize.
    // If the celltool is not active and we initiate a click on something that is not a flake element, we need
    // to reactivate the cell tool. THis is a temporary solution, pending the flake updates.
    if (KoToolManager::instance()->activeToolId() != "KSpreadCellToolId")
        if (!shapeManager()->shapeAt (documentPosition, KoFlake::ShapeOnTop))
            KoToolManager::instance()->switchToolRequested("KSpreadCellToolId");
#endif

    // flake
    if (d->toolProxy) {
        d->toolProxy->mousePressEvent(event, documentPosition);

        if (!event->isAccepted() && event->button() == Qt::RightButton) {
            showContextMenu(origEvent->globalPosition().toPoint());
            origEvent->setAccepted(true);
        }
    }
    if (layoutDirection() == Qt::RightToLeft) {
        delete event;
    }
}

void Canvas::showContextMenu(const QPoint &globalPos)
{
    view()->unplugActionList("toolproxy_action_list");
    view()->plugActionList("toolproxy_action_list", toolProxy()->popupActionList());
    if (KXMLGUIFactory *factory = view()->factory()) {
        QMenu *menu = dynamic_cast<QMenu *>(factory->container("default_canvas_popup", view()));
        // Only show the menu, if there are items. The plugged action list counts as one action.
        if (menu && menu->actions().size() > 1) {
            menu->exec(globalPos);
        }
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    //    KoPointerEvent pev(event, QPointF());
    //    mouseReleased(&pev);

    QPointF documentPosition;
    if (layoutDirection() == Qt::LeftToRight) {
        documentPosition = viewConverter()->viewToDocument(event->position().toPoint()) + offset();
    } else {
        const QPoint position(QWidget::width() - event->position().x(), event->position().y());
        const QPointF offset(this->offset().x(), this->offset().y());
        documentPosition = viewConverter()->viewToDocument(position) + offset;
        event = new QMouseEvent(QEvent::MouseButtonRelease, position, mapToGlobal(position), event->button(), event->buttons(), event->modifiers());
    }

    if (cd->draggingChartSourceRegion) {
        cd->draggingChartSourceRegion = false;
        cd->draggedSourceRegionIndex = -1;
        unsetCursor();
        Q_EMIT chartSourceRangesEdited(chartSourceRegionsToVariant(cd->chartSourceRegions));
        event->accept();
        if (layoutDirection() == Qt::RightToLeft) {
            delete event;
        }
        return;
    }

    // flake
    if (d->toolProxy)
        d->toolProxy->mouseReleaseEvent(event, documentPosition);

    if (layoutDirection() == Qt::RightToLeft) {
        delete event;
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    //    KoPointerEvent pev(event, QPointF());
    //    mouseMoved(&pev);

    QPointF documentPosition;
    if (layoutDirection() == Qt::LeftToRight) {
        documentPosition = viewConverter()->viewToDocument(event->position().toPoint()) + offset();
    } else {
        const QPoint position(QWidget::width() - event->position().x(), event->position().y());
        const QPointF offset(this->offset().x(), this->offset().y());
        documentPosition = viewConverter()->viewToDocument(position) + offset;
        event = new QMouseEvent(QEvent::MouseMove, position, mapToGlobal(position), event->button(), event->buttons(), event->modifiers());
    }

    if (cd->draggingChartSourceRegion && (event->buttons() & Qt::LeftButton)) {
        Sheet *sheet = activeSheet();
        const int index = cd->draggedSourceRegionIndex;
        if (sheet && index >= 0 && index < cd->chartSourceRegions.size()) {
            const QRect cell = sheet->documentToCellCoordinates(QRectF(documentPosition, QSizeF(1.0, 1.0)));
            QRect resized = cd->draggedOriginalRect;
            resized.setRight(qMax(resized.right(), cell.left()));
            resized.setBottom(qMax(resized.bottom(), cell.top()));
            if (resized != cd->chartSourceRegions[index].rect) {
                cd->chartSourceRegions[index].rect = resized;
                d->chartSourceRanges = chartSourceRangesFromRegions(cd->chartSourceRegions);
                update();
            }
        }
        event->accept();
        if (layoutDirection() == Qt::RightToLeft) {
            delete event;
        }
        return;
    }

    // flake
    if (d->toolProxy)
        d->toolProxy->mouseMoveEvent(event, documentPosition);

    if (layoutDirection() == Qt::RightToLeft) {
        delete event;
    }
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    QMouseEvent *originalEvent = event;
    QPointF documentPosition;
    if (layoutDirection() == Qt::LeftToRight) {
        documentPosition = viewConverter()->viewToDocument(event->position().toPoint()) + offset();
    } else {
        const QPoint position(QWidget::width() - event->position().x(), event->position().y());
        const QPointF offset(this->offset().x(), this->offset().y());
        documentPosition = viewConverter()->viewToDocument(position) + offset;
        event = new QMouseEvent(QEvent::MouseButtonDblClick, position, mapToGlobal(position), event->button(), event->buttons(), event->modifiers());
    }

    QMap<QString, QList<QRect>> sourceRanges;
    if (event->button() == Qt::LeftButton) {
        for (KoShape *shape = shapeManager()->shapeAt(documentPosition, KoFlake::ShapeOnTop); shape; shape = shape->parent()) {
            if (auto *chart = qobject_cast<KoChart::ChartInterface *>(dynamic_cast<QObject *>(shape))) {
                sourceRanges = chart->worksheetSourceRanges();
                break;
            }
        }
    }

    // flake
    if (d->toolProxy) {
        // If the celltool is not active and the double click is on something that is not a flake element, we need
        // to reactivate the cell tool. Normally flake would handle this, but the main app is not a shape, so we have to.
        // TODO: figure out a way to make the flake's functionality work. It'd likely require turning the main app
        // widget into a KoShape or something along those lines.
        if (KoToolManager::instance()->activeToolId() != "KSpreadCellToolId") {
            if (!shapeManager()->shapeAt(documentPosition, KoFlake::ShapeOnTop)) {
                KoToolManager::instance()->switchToolRequested("KSpreadCellToolId");
                if (event != originalEvent) {
                    delete event;
                }
                return;
            }
        }

        d->toolProxy->mouseDoubleClickEvent(event, documentPosition);
    }

    if (event != originalEvent) {
        originalEvent->setAccepted(event->isAccepted());
        delete event;
    }
    if (sourceRanges.isEmpty()) {
        return;
    }

    highlightChartSourceRanges(chartSourceRangesToVariant(sourceRanges));
}

void Canvas::highlightChartSourceRanges(const QVariantMap &sourceRanges)
{
    cd->chartSourceRegions = chartSourceRegionsFromVariantMap(sourceRanges);
    finishChartSourceHighlight();
}

void Canvas::highlightChartSourceRegions(const QVariantList &sourceRegions)
{
    cd->chartSourceRegions = chartSourceRegionsFromVariant(sourceRegions);
    finishChartSourceHighlight();
}

void Canvas::finishChartSourceHighlight()
{
    d->chartSourceRanges = chartSourceRangesFromRegions(cd->chartSourceRegions);
    if (d->chartSourceRanges.isEmpty()) {
        update();
        return;
    }

    Sheet *sheet = activeSheet();
    if (!sheet) {
        update();
        return;
    }
    if (!d->chartSourceRanges.contains(sheet->sheetName())) {
        for (auto it = d->chartSourceRanges.cbegin(); it != d->chartSourceRanges.cend(); ++it) {
            auto *source = static_cast<Sheet *>(doc()->map()->findSheet(it.key()));
            if (source && !source->isHidden()) {
                view()->setActiveSheet(source);
                sheet = source;
                break;
            }
        }
    }

    const auto ranges = d->chartSourceRanges.value(sheet->sheetName());
    const QRect visible = visibleCells();
    if (!ranges.isEmpty() && std::none_of(ranges.cbegin(), ranges.cend(), [&](const QRect &range) { return visible.intersects(range); })) {
        const QRectF firstCell = sheet->cellCoordinatesToDocument(QRect(ranges.first().topLeft(), QSize(1, 1)));
        if (canvasController()) {
            canvasController()->ensureVisible(viewConverter()->documentToView(firstCell), true);
        }
    }
    update();
}

bool Canvas::event(QEvent *e)
{
    if (toolProxy()) {
        toolProxy()->processEvent(e);
    }
    return QWidget::event(e);
}

void Canvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    paint(&painter, event->rect());
    event->accept();
}

void Canvas::dragEnterEvent(QDragEnterEvent *event)
{
    if (CanvasBase::dragEnter(event->mimeData())) {
        event->acceptProposedAction();
    }
}

void Canvas::dragMoveEvent(QDragMoveEvent *event)
{
    if (CanvasBase::dragMove(event->mimeData(), event->position().toPoint(), event->source())) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void Canvas::dragLeaveEvent(QDragLeaveEvent *)
{
    CanvasBase::dragLeave();
}

void Canvas::dropEvent(QDropEvent *event)
{
    if (CanvasBase::drop(event->mimeData(), event->position().toPoint(), event->source())) {
        event->setAccepted(true);
    } else {
        event->ignore();
    }
}

void Canvas::setVertScrollBarPos(qreal pos)
{
    if (pos < 0)
        pos = view()->vertScrollBar()->maximum() - pos;
    view()->vertScrollBar()->setValue((int)pos);
}

void Canvas::setHorizScrollBarPos(qreal pos)
{
    if (pos < 0)
        pos = view()->horzScrollBar()->maximum() - pos;
    view()->horzScrollBar()->setValue((int)pos);
}

KoZoomHandler *Canvas::zoomHandler() const
{
    return view()->zoomHandler();
}

Sheet *Canvas::activeSheet() const
{
    return view()->activeSheet();
}

bool Canvas::isViewLoading() const
{
    return view()->isLoading();
}

SheetView *Canvas::sheetView(Sheet *sheet) const
{
    return view()->sheetView(sheet);
}

Calligra::Sheets::Selection *Canvas::selection() const
{
    return view()->selection();
}

ColumnHeader *Canvas::columnHeader() const
{
    return view()->columnHeader();
}

RowHeader *Canvas::rowHeader() const
{
    return view()->rowHeader();
}

void Canvas::enableAutoScroll()
{
    view()->enableAutoScroll();
}

void Canvas::disableAutoScroll()
{
    view()->disableAutoScroll();
}

void Canvas::setCursor(const QCursor &cursor)
{
    QWidget::setCursor(cursor);
}
