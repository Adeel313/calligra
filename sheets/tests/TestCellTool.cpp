// SPDX-License-Identifier: LGPL-2.0-or-later
#include <QTest>
#include <QMouseEvent>
#include <QClipboard>
#include <QMimeData>
#include <memory>

#include <KoInteractionTool.h>
#include <KoPointerEvent.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeManager.h>
#include <KoShapeRegistry.h>
#include <KoToolManager.h>
#include <KoToolProxy.h>
#include <kundo2stack.h>

#include "core/Map.h"
#include "core/Sheet.h"
#include "part/Canvas.h"
#include "part/Doc.h"
#include "part/Part.h"
#include "part/View.h"

using namespace Calligra::Sheets;

class TestCellTool : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void chartDragAndDelete();
};

void TestCellTool::chartDragAndDelete()
{
    // The offscreen platform has no initial clipboard MIME data; chart option
    // panels contain color controls that inspect it when the tool opens.
    QApplication::clipboard()->setMimeData(new QMimeData);
    Part part;
    Doc document(&part);
    part.setDocument(&document);
    document.initEmpty();
    std::unique_ptr<View> view(qobject_cast<View *>(part.createView(&document, nullptr)));
    QVERIFY(view);
    Canvas *canvas = view->canvasWidget();
    Sheet *sheet = canvas->activeSheet();
    QVERIFY(sheet);
    auto *factory = KoShapeRegistry::instance()->value(QStringLiteral("ChartShape"));
    QVERIFY(factory);
    KoShape *chart = factory->createDefaultShape(document.resourceManager());
    QVERIFY(chart);
    chart->setPosition(QPointF(100, 100));
    chart->setSize(QSizeF(200, 150));
    sheet->addShape(chart);
    canvas->shapeManager()->addShape(chart);

    auto *manager = KoToolManager::instance();
    manager->switchToolRequested(QStringLiteral("KSpreadCellToolId"));
    QCOMPARE(manager->activeToolId(), QStringLiteral("KSpreadCellToolId"));
    const QPointF position = chart->position();
    const QPointF pressPoint = position + QPointF(5, 5);
    const QPointF delta(40, 30);
    QMouseEvent press(QEvent::MouseButtonPress, pressPoint, pressPoint,
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    KoPointerEvent pressEvent(&press, pressPoint);
    canvas->toolProxy()->mousePressEvent(&pressEvent);
    QCOMPARE(manager->activeToolId(), QString(KoInteractionTool_ID));
    QVERIFY(canvas->shapeManager()->selection()->isSelected(chart));

    QMouseEvent move(QEvent::MouseMove, pressPoint + delta, pressPoint + delta,
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    KoPointerEvent moveEvent(&move, pressPoint + delta);
    canvas->toolProxy()->mouseMoveEvent(&moveEvent);
    QMouseEvent release(QEvent::MouseButtonRelease, pressPoint + delta, pressPoint + delta,
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    KoPointerEvent releaseEvent(&release, pressPoint + delta);
    canvas->toolProxy()->mouseReleaseEvent(&releaseEvent);
    QCOMPARE(chart->position(), position + delta);
    document.undoStack()->undo();
    QCOMPARE(chart->position(), position);
    document.undoStack()->redo();
    QCOMPARE(chart->position(), position + delta);

    // A double-click still opens the chart's own editing tool.
    manager->toolById(canvas, KoInteractionTool_ID)->mouseDoubleClickEvent(&releaseEvent);
    QCOMPARE(manager->activeToolId(), QStringLiteral("ChartToolFactory_ID"));
    manager->switchToolRequested(KoInteractionTool_ID);
    canvas->toolProxy()->deleteSelection();
    QVERIFY(!sheet->shapes().contains(chart));
    document.undoStack()->undo();
    QVERIFY(sheet->shapes().contains(chart));
}

QTEST_MAIN(TestCellTool)
#include "TestCellTool.moc"
