// SPDX-License-Identifier: LGPL-2.0-or-later
#include "defaulttool/DefaultTool.h"
#include "MockShapes.h"
#include <KoPointerEvent.h>
#include <KoSelection.h>
#include <KoSnapGuide.h>
#include <KoViewConverter.h>
#include <kundo2command.h>
#include <QApplication>
#include <QMouseEvent>
#include <memory>
#include <vector>

class DragCanvas : public MockCanvas
{
public:
    KoViewConverter *viewConverter() const override { return const_cast<KoViewConverter *>(&converter); }
    bool snapToGrid() const override { return false; }
    void addCommand(std::unique_ptr<KUndo2Command> &&command) override
    {
        command->redo();
        commands.push_back(std::move(command));
    }
    KoViewConverter converter;
    std::vector<std::unique_ptr<KUndo2Command>> commands;
};

static void check(bool condition, const char *message)
{
    if (!condition) qFatal("%s", message);
}

static void drag(DefaultTool &tool, const QPointF &from, const QPointF &to, bool move = true)
{
    QMouseEvent press(QEvent::MouseButtonPress, from, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    KoPointerEvent p(&press, from);
    tool.mousePressEvent(&p);
    if (move) {
        QMouseEvent motion(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        KoPointerEvent m(&motion, to);
        tool.mouseMoveEvent(&m);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, to, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    KoPointerEvent r(&release, to);
    tool.mouseReleaseEvent(&r);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    DragCanvas canvas;
    canvas.snapGuide()->enableSnapping(false);
    MockShape back, front;
    back.setSize(QSizeF(400, 400));
    front.setSize(QSizeF(100, 100));
    front.setPosition(QPointF(80, 80));
    front.setZIndex(1);
    canvas.shapeManager()->addShape(&back);
    canvas.shapeManager()->addShape(&front);
    DefaultTool tool(&canvas);
    KoSelection *selection = canvas.shapeManager()->selection();

    // A click selects without moving or creating an undo command.
    drag(tool, QPointF(130, 130), QPointF(130, 130), false);
    check(selection->isSelected(&front), "First click must select the front shape");
    check(front.position() == QPointF(80, 80), "Click must not move the shape");
    check(canvas.commands.empty(), "Click must not create an undo entry");

    // One real motion event must move an initially unselected shape.
    selection->deselectAll();
    drag(tool, QPointF(130, 130), QPointF(160, 150));
    check(front.position() == QPointF(110, 100), "First motion event must drag the newly selected shape");
    check(canvas.commands.size() == 1, "Drag must create one undo entry");
    canvas.commands.back()->undo();
    check(front.position() == QPointF(80, 80), "Undo must restore the position");
    canvas.commands.back()->redo();
    check(front.position() == QPointF(110, 100), "Redo must restore the moved position");
    canvas.commands.back()->undo();
    canvas.commands.clear();

    // A selected object behind the clicked object must not steal its drag.
    selection->deselectAll();
    selection->select(&back);
    drag(tool, QPointF(130, 130), QPointF(160, 150));
    check(selection->isSelected(&front) && !selection->isSelected(&back), "Click must replace the selection with the front object");
    check(back.position() == QPointF(), "The selected object behind must stay in place");
    check(front.position() == QPointF(110, 100), "The front object must move on the first drag");

    // Small mouse jitter must not move an object, including at another zoom.
    canvas.commands.clear();
    canvas.converter.setZoom(2);
    const QPointF before = front.position();
    drag(tool, QPointF(160, 150), QPointF(160.1, 150.1));
    check(front.position() == before, "Motion below the drag threshold must not move the shape");
    check(canvas.commands.empty(), "Mouse jitter must not create an undo entry");
    selection->deselectAll();
    canvas.shapeManager()->remove(&front);
    canvas.shapeManager()->remove(&back);
    qInfo("PASS: first-click selection, first-motion drag, overlap, undo/redo, zoomed mouse jitter");
}
