/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2006 Thorsten Zachmann <zachmann@kde.org>
   SPDX-FileCopyrightText: 2006-2007 Thomas Zander <zander@kde.org>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "ShapeMoveStrategy.h"
#include "SelectionDecorator.h"

#include <KLocalizedString>

#include <QApplication>

#include <KoCanvasBase.h>
#include <KoPointerEvent.h>
#include <KoSelection.h>
#include <KoShapeAnchor.h>
#include <KoShapeContainer.h>
#include <KoShapeContainerModel.h>
#include <KoShapeManager.h>
#include <KoSnapGuide.h>
#include <KoToolBase.h>
#include <commands/KoShapeMoveCommand.h>

ShapeMoveStrategy::ShapeMoveStrategy(KoToolBase *tool, const QPointF &clicked)
    : KoInteractionStrategy(tool)
    , m_start(clicked)
    , m_canvas(tool->canvas())
    , m_dragStarted(false)
{
    // FIXME: This test is also done in DefaultTool, so it should also be responsible for setting the shapes to operate on
    QList<KoShape *> selectedShapes = m_canvas->shapeManager()->selection()->selectedShapes(KoFlake::StrippedSelection);
    QRectF boundingRect;
    foreach (KoShape *shape, selectedShapes) {
        if (shape->allowedInteraction(KoShape::MoveAllowed, false)) {
            m_selectedShapes << shape;
            m_previousPositions << shape->position();
            m_newPositions << shape->position();
            boundingRect = boundingRect.united(shape->boundingRect());
            if (shape->anchor()) {
                m_previousOffsets << shape->anchor()->offset();
                m_newOffsets << shape->anchor()->offset();
            } else {
                m_previousOffsets << QPointF();
                m_newOffsets << QPointF();
            }
        }
    }
    KoSelection *selection = m_canvas->shapeManager()->selection();
    m_initialOffset = selection->absolutePosition(SelectionDecorator::hotPosition()) - m_start;
    m_initialSelectionPosition = selection->position();
    m_canvas->snapGuide()->setIgnoredShapes(selection->selectedShapes(KoFlake::FullSelection));

    tool->setStatusText(i18n("Press ALT to hold x- or y-position."));
}

void ShapeMoveStrategy::handleMouseMove(const QPointF &point, Qt::KeyboardModifiers modifiers)
{
    if (m_selectedShapes.isEmpty())
        return;

    if (!m_dragStarted) {
        const QPointF viewDelta = m_canvas->viewConverter()->documentToView(point - m_start);
        if (viewDelta.manhattanLength() < QApplication::startDragDistance()) {
            return;
        }
        m_dragStarted = true;
    }

    QPointF diff = point - m_start;

    if (modifiers & (Qt::AltModifier | Qt::ControlModifier)) {
        // keep x or y position unchanged
        if (qAbs(diff.x()) < qAbs(diff.y()))
            diff.setX(0);
        else
            diff.setY(0);
    } else {
        QPointF positionToSnap = point + m_initialOffset;
        tool()->canvas()->updateCanvas(tool()->canvas()->snapGuide()->boundingRect());
        QPointF snappedPosition = tool()->canvas()->snapGuide()->snap(positionToSnap, modifiers);
        tool()->canvas()->updateCanvas(tool()->canvas()->snapGuide()->boundingRect());
        diff = snappedPosition - m_initialOffset - m_start;
    }

    if (diff.x() != 0 || diff.y() != 0) {
        // The user really started dragging: images that are glued to the
        // text flow (as-character anchors) become floating images here,
        // starting at their current position.  A click without movement
        // keeps the image inline.
        promoteInlineAnchors();
    }

    m_diff = diff;

    moveSelection();
}

void ShapeMoveStrategy::handleCustomEvent(KoPointerEvent *event)
{
    QPointF diff = tool()->canvas()->viewConverter()->viewToDocument(event->position());

    if (event->modifiers() & (Qt::AltModifier | Qt::ControlModifier)) {
        // keep x or y position unchanged
        if (qAbs(diff.x()) < qAbs(diff.y()))
            diff.setX(0);
        else
            diff.setY(0);
    }

    if (diff.x() != 0 || diff.y() != 0) {
        promoteInlineAnchors();
    }
    m_diff += 0.1 * diff;

    moveSelection();
}

void ShapeMoveStrategy::promoteInlineAnchors()
{
    for (int i = 0; i < m_selectedShapes.count(); ++i) {
        KoShape *shape = m_selectedShapes.at(i);
        KoShapeAnchor *anchor = shape->anchor();
        if (!anchor || anchor->anchorType() != KoShapeAnchor::AnchorAsCharacter) {
            continue;
        }
        // An as-character image is glued to the text flow: the layout keeps
        // recomputing its position, so a position-only drag would be undone
        // at the next relayout.  Turn it into a floating image anchored to
        // the page and take the current position as the new offset so it
        // does not visually jump.  From here on the generic move code
        // (position + offset kept in sync) applies.
        anchor->setAnchorType(KoShapeAnchor::AnchorParagraph);
        anchor->setHorizontalPos(KoShapeAnchor::HFromLeft);
        anchor->setVerticalPos(KoShapeAnchor::VFromTop);
        anchor->setHorizontalRel(KoShapeAnchor::HPage);
        anchor->setVerticalRel(KoShapeAnchor::VPage);
        anchor->setOffset(shape->position());
        // The old offset belonged to the as-character anchor; record the
        // new one so undo restores a consistent state.
        m_previousOffsets[i] = anchor->offset();
        m_newOffsets[i] = anchor->offset();
    }
}

void ShapeMoveStrategy::moveSelection()
{
    Q_ASSERT(m_newPositions.count());

    int i = 0;
    foreach (KoShape *shape, m_selectedShapes) {
        QPointF delta = m_previousPositions.at(i) + m_diff - shape->position();
        if (shape->parent()) {
            shape->parent()->model()->proposeMove(shape, delta);
        }
        tool()->canvas()->clipToDocument(shape, delta);
        QPointF newPos(shape->position() + delta);
        m_newPositions[i] = newPos;
        shape->update();
        shape->setPosition(newPos);
        shape->update();
        if (shape->anchor()) {
            // The layout re-derives the position of an anchored shape from
            // its anchor offset, so a position-only change would be undone
            // on the next relayout (and be lost on save).  Apply the same
            // delta to the offset; KoShapeMoveCommand already stores and
            // restores both values for undo/redo.
            shape->anchor()->setOffset(shape->anchor()->offset() + delta);
            m_newOffsets[i] = shape->anchor()->offset();
        }
        i++;
    }
    tool()->canvas()->shapeManager()->selection()->setPosition(m_initialSelectionPosition + m_diff);
}

KUndo2Command *ShapeMoveStrategy::createCommand()
{
    if (m_diff.x() == 0 && m_diff.y() == 0) {
        return nullptr;
    }
    // get the shapes that has actually been moved
    QVector<QPointF> oldPositions;
    QVector<QPointF> newPositions;
    QVector<QPointF> newOffsets;
    QVector<QPointF> oldOffsets;
    QList<KoShape *> movedShapes;
    for (int i = 0; i < m_selectedShapes.count(); ++i) {
        KoShape *shape = m_selectedShapes.at(i);
        if (shape->position() != m_previousPositions.at(i)) {
            movedShapes << shape;
            oldPositions << m_previousPositions.at(i);
            newPositions << m_newPositions.at(i);
            if (shape->anchor()) {
                oldOffsets << m_previousOffsets.at(i);
                newOffsets << m_newOffsets.at(i);
            }
        }
    }
    if (movedShapes.isEmpty()) {
        return nullptr;
    }
    tool()->canvas()->snapGuide()->reset();
    return new KoShapeMoveCommand(movedShapes, oldPositions, newPositions, oldOffsets, newOffsets);
}

void ShapeMoveStrategy::finishInteraction(Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(modifiers);
    tool()->canvas()->updateCanvas(tool()->canvas()->snapGuide()->boundingRect());
}

void ShapeMoveStrategy::paint(QPainter &painter, const KoViewConverter &converter)
{
    SelectionDecorator decorator(KoFlake::NoHandle, false, false);
    decorator.setSelection(tool()->canvas()->shapeManager()->selection());
    decorator.setHandleRadius(handleRadius());
    decorator.paint(painter, converter);
}
