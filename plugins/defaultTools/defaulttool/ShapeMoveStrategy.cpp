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
#include <QTextDocument>
#include <QUrl>
#include <KoShapeContainerModel.h>
#include <KoShapeManager.h>
#include <KoSnapGuide.h>
#include <KoToolBase.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
#include <KoTextShapeDataBase.h>
#include <commands/ChangeAnchorPropertiesCommand.h>
#include <commands/KoShapeMoveCommand.h>

/**
 * An image inserted into a header or footer lives in the text document of that
 * header/footer.  Its anchor points into that document.
 */
static bool isHeaderFooterAnchor(const KoShapeAnchor *anchor)
{
    if (!anchor || !anchor->textLocation()) {
        return false;
    }
    const QTextDocument *document = anchor->textLocation()->document();
    return document && document->resource(QTextDocument::UserResource, KoShapeAnchor::headerFooterTextDocumentResource()).toBool();
}

/**
 * The rectangle a header/footer shape has to stay in: the frame that holds the
 * header/footer text (in the coordinates of that frame, which are the
 * coordinates of its children).
 */
static bool headerFooterLimit(const KoShape *shape, QRectF *limit)
{
    if (!isHeaderFooterAnchor(shape->anchor())) {
        return false;
    }
    const KoShape *parent = shape->parent();
    if (!parent) {
        return false;
    }
    *limit = QRectF(QPointF(), parent->size());
    return true;
}

/**
 * Turn an as-character image of a header/footer into a floating image that is
 * anchored to the same character inside the header/footer.  Keeping the anchor
 * in that text document means the image stays part of the header/footer, while
 * a floating anchor makes it draggable (an as-character image is positioned by
 * the text layout and can not be moved with the mouse).
 */
static bool makeInlineShapeFloating(KoShape *shape, KoShapeAnchor *anchor,
                                    KoShapeAnchor::HorizontalRel horizontalRel, KoShapeAnchor::VerticalRel verticalRel)
{
    KoShapeContainer *container = shape->parent();
    if (!container) {
        return false;
    }
    KoTextShapeDataBase *textData = qobject_cast<KoTextShapeDataBase *>(container->userData());
    if (!textData || !textData->document()) {
        return false;
    }
    KoTextDocument textDocument(textData->document());
    KoTextEditor *editor = textDocument.textEditor();
    if (!editor) {
        return false;
    }

    // The new anchor is put at the character the image used to occupy.
    editor->setPosition(anchor->textLocation()->position());

    KoShapeAnchor anchorProperties(nullptr);
    anchorProperties.setAnchorType(KoShapeAnchor::AnchorToCharacter);
    anchorProperties.setHorizontalRel(horizontalRel);
    anchorProperties.setVerticalRel(verticalRel);
    anchorProperties.setHorizontalPos(KoShapeAnchor::HFromLeft);
    anchorProperties.setVerticalPos(KoShapeAnchor::VFromTop);
    // The position of an as-character shape is relative to the text frame that
    // holds it, which is how a frame relative (HFrame/VFrame) floating anchor is
    // measured as well.  Using it as the offset therefore keeps the image where
    // it is, while a page relative anchor would need the page position here.
    anchorProperties.setOffset(shape->position());

    ChangeAnchorPropertiesCommand *command = new ChangeAnchorPropertiesCommand(anchor, anchorProperties, container, nullptr);
    editor->addCommand(command); // executes the command and makes it undoable
    return true;
}

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
        // An image anchored in a header or footer is part of that header/footer
        // (like in Word) and must not escape it by turning into a page anchored
        // shape.  It still has to be draggable inside the header/footer, so it
        // becomes a floating shape anchored to a character of that same text.
        if (isHeaderFooterAnchor(anchor)) {
            if (makeInlineShapeFloating(shape, anchor, KoShapeAnchor::HChar, KoShapeAnchor::VChar)) {
                m_previousOffsets[i] = anchor->offset();
                m_newOffsets[i] = anchor->offset();
            }
            continue;
        }
        // An as-character image is glued to the text flow: the layout keeps
        // recomputing its position, so a position-only drag would be undone
        // at the next relayout.  Turn it into a floating image that is anchored
        // to the same character and measured from the text frame, which keeps
        // the image at its current position.  From here on the generic move
        // code (and the text shape container) applies the drag.
        if (makeInlineShapeFloating(shape, anchor, KoShapeAnchor::HFrame, KoShapeAnchor::VFrame)) {
            m_previousOffsets[i] = anchor->offset();
            m_newOffsets[i] = anchor->offset();
        }
    }
}

void ShapeMoveStrategy::moveSelection()
{
    Q_ASSERT(m_newPositions.count());

    int i = 0;
    foreach (KoShape *shape, m_selectedShapes) {
        QPointF target = m_previousPositions.at(i) + m_diff;
        // A shape belonging to a header or footer can be dragged around inside
        // it, but it must not leave the header/footer area.
        QRectF limit;
        if (headerFooterLimit(shape, &limit)) {
            const QSizeF size = shape->boundingRect().size();
            target.setX(qBound(limit.left(), target.x(), qMax(limit.left(), limit.right() - size.width())));
            target.setY(qBound(limit.top(), target.y(), qMax(limit.top(), limit.bottom() - size.height())));
        }
        QPointF delta = target - shape->position();
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
