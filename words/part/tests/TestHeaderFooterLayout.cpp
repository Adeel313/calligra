/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "TestHeaderFooterLayout.h"
#include "MockPart.h"

#include <KWDocument.h>
#include <KWPage.h>
#include <KWPageManager.h>
#include <KWRootAreaProviderBase.h>
#include <frames/KWFrameLayout.h>
#include <frames/KWTextFrameSet.h>
#include <commands/KWShapeCreateCommand.h>
#include <kundo2stack.h>

#include <KoShape.h>
#include <KoShapeRegistry.h>
#include <KoShapeFactoryBase.h>
#include <KoProperties.h>
#include <KoAnchorInlineObject.h>
#include <KoShapeAnchor.h>
#include <KoSelection.h>
#include <KoSnapGuide.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
#include <commands/ChangeAnchorPropertiesCommand.h>
#include <KoToolBase.h>
#include <KoViewConverter.h>
#include <MockShapes.h>
#include "../../../plugins/defaultTools/defaulttool/ShapeMoveStrategy.h"
#include <KoTextDocumentLayout.h>
#include <KoTextLayoutRootArea.h>
#include <KoTextShapeData.h>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest>

class HeaderCanvas : public MockCanvas
{
public:
    KoViewConverter *viewConverter() const override { return const_cast<KoViewConverter *>(&converter); }
    bool snapToGrid() const override { return false; }
    KoViewConverter converter;
};

class HeaderTool : public KoToolBase
{
public:
    explicit HeaderTool(KoCanvasBase *canvas) : KoToolBase(canvas) {}
    void paint(QPainter &, const KoViewConverter &) override {}
    void mousePressEvent(KoPointerEvent *) override {}
    void mouseMoveEvent(KoPointerEvent *) override {}
    void mouseReleaseEvent(KoPointerEvent *) override {}
    void activate(KoToolBase::ToolActivation, const QSet<KoShape *> &) override {}
};

void TestHeaderFooterLayout::testTypingBesideNewImage_data()
{
    testStableHeight_data();
}

void TestHeaderFooterLayout::testTypingBesideNewImage()
{
    QFETCH(bool, header);
    MockPart part;
    KWDocument document(&part);
    document.initEmpty();
    auto style = document.pageManager()->page(1).pageStyle();
    if (header) style.setHeaderPolicy(Words::HFTypeUniform);
    else style.setFooterPolicy(Words::HFTypeUniform);
    document.relayout();
    KWTextFrameSet *frames = nullptr;
    const auto type = header ? Words::OddPagesHeaderTextFrameSet : Words::OddPagesFooterTextFrameSet;
    for (auto *candidate : document.frameSets()) {
        auto *text = qobject_cast<KWTextFrameSet *>(candidate);
        if (text && text->textFrameSetType() == type) frames = text;
    }
    QVERIFY(frames);
    auto *container = dynamic_cast<KoShapeContainer *>(frames->shapes().first());
    QVERIFY(container);
    auto *editor = KoTextDocument(frames->document()).textEditor();
    QImage pixels(80, 100, QImage::Format_ARGB32);
    pixels.fill(Qt::magenta);
    KoProperties params;
    params.setProperty(QStringLiteral("qimage"), pixels);
    auto *factory = KoShapeRegistry::instance()->value(QStringLiteral("PictureShape"));
    QVERIFY(factory);
    auto *image = factory->createShape(&params, document.resourceManager());
    QVERIFY(image);
    image->setSize(QSizeF(80, 100));
    image->setTextRunAroundSide(KoShape::BothRunAroundSide);
    auto *anchor = new KoShapeAnchor(image);
    image->setAnchor(anchor);
    anchor->setAnchorType(KoShapeAnchor::AnchorPage);
    KoShapeAnchor properties(nullptr);
    properties.setAnchorType(KoShapeAnchor::AnchorToCharacter);
    properties.setHorizontalRel(KoShapeAnchor::HFrame);
    properties.setHorizontalPos(KoShapeAnchor::HFromLeft);
    properties.setVerticalRel(KoShapeAnchor::VFrame);
    properties.setVerticalPos(KoShapeAnchor::VFromTop);
    const int beforeInsert = document.undoStack()->index();
    auto *insert = editor->beginEditBlock(kundo2_noi18n("Insert image"));
    editor->addCommand(new KWShapeCreateCommand(&document, image, insert));
    editor->addCommand(new ChangeAnchorPropertiesCommand(anchor, properties, container, insert));
    editor->endEditBlock();
    QCOMPARE(document.undoStack()->index(), beforeInsert + 1);
    QCOMPARE(editor->position(), 0);
    QCOMPARE(frames->document()->toPlainText(), QString());
    auto *layout = qobject_cast<KoTextDocumentLayout *>(frames->document()->documentLayout());
    layout->layout();
    const auto *data = qobject_cast<KoTextShapeData *>(container->userData());
    const auto emptyLine = frames->document()->firstBlock().layout()->lineAt(0);
    QVERIFY(emptyLine.y() - data->documentOffset() < image->size().height() / 2);
    QVERIFY(emptyLine.cursorToX(0) >= image->position().x() + image->size().width());
    editor->insertText(QStringLiteral("First line\nSecond line"));
    layout->layout();
    const QRectF imageRect(image->position(), image->size());
    for (auto block = frames->document()->begin(); block.isValid(); block = block.next()) {
        const auto line = block.layout()->lineAt(0);
        const auto textRect = line.naturalTextRect().translated(0, -data->documentOffset());
        QVERIFY(textRect.top() < imageRect.bottom() / 2);
        QVERIFY(textRect.left() >= imageRect.right());
        QVERIFY(!textRect.intersects(imageRect));
    }
    HeaderCanvas canvas;
    canvas.shapeManager()->addShape(container);
    const auto logoIsPainted = [&] {
        image->waitUntilReady(canvas.converter, false);
        QImage rendered(1000, 1000, QImage::Format_ARGB32);
        rendered.fill(Qt::white);
        QPainter painter(&rendered);
        painter.setClipRect(rendered.rect());
        canvas.shapeManager()->paint(painter, canvas.converter, false);
        painter.end();
        const QPoint sample = image->absoluteTransformation(nullptr).map(QPointF(40, 50)).toPoint();
        const QColor color = rendered.pixelColor(sample);
        return color.red() > 200 && color.green() < 50 && color.blue() > 200;
    };
    QVERIFY(logoIsPainted());
    editor->setPosition(0);
    editor->setPosition(frames->document()->characterCount() - 1, QTextCursor::KeepAnchor);
    QVERIFY(logoIsPainted());
    editor->setTextColor(Qt::red);
    layout->layout();
    QVERIFY(image->isVisible(true));
    QCOMPARE(image->parent(), container);
    QCOMPARE(image->position(), imageRect.topLeft());
    QCOMPARE(anchor->textLocation()->position(), 0);
    QVERIFY(layout->textAnchors().contains(anchor));
    QVERIFY(logoIsPainted());
    document.undoStack()->undo();
    layout->layout();
    QVERIFY(image->isVisible(true));
    QVERIFY(layout->textAnchors().contains(anchor));
    document.undoStack()->redo();
    layout->layout();
    QVERIFY(image->isVisible(true));
    QVERIFY(layout->textAnchors().contains(anchor));
    document.undoStack()->setIndex(beforeInsert);
    layout->layout();
    QVERIFY(!image->parent());
    document.undoStack()->redo();
    layout->layout();
    QCOMPARE(anchor->anchorType(), KoShapeAnchor::AnchorToCharacter);
    QCOMPARE(editor->position(), 0);
    QVERIFY(image->parent() == container);
}

void TestHeaderFooterLayout::testFloatingImageBounds_data()
{
    testStableHeight_data();
}

void TestHeaderFooterLayout::testFloatingImageBounds()
{
    QFETCH(bool, header);
    MockPart part;
    KWDocument document(&part);
    document.initEmpty();
    auto style = document.pageManager()->page(1).pageStyle();
    if (header) style.setHeaderPolicy(Words::HFTypeUniform);
    else style.setFooterPolicy(Words::HFTypeUniform);
    document.relayout();
    KWTextFrameSet *frames = nullptr;
    const auto type = header ? Words::OddPagesHeaderTextFrameSet : Words::OddPagesFooterTextFrameSet;
    for (auto *candidate : document.frameSets()) {
        auto *text = qobject_cast<KWTextFrameSet *>(candidate);
        if (text && text->textFrameSetType() == type) frames = text;
    }
    QVERIFY(frames);
    auto *editor = KoTextDocument(frames->document()).textEditor();
    auto *image = new MockShape;
    image->setSize(QSizeF(60, 50));
    image->setTextRunAroundSide(KoShape::BothRunAroundSide);
    auto *anchor = new KoShapeAnchor(image);
    image->setAnchor(anchor);
    anchor->setAnchorType(KoShapeAnchor::AnchorAsCharacter);
    anchor->setVerticalRel(KoShapeAnchor::VBaseline);
    anchor->setVerticalPos(KoShapeAnchor::VTop);
    document.addShape(image);
    editor->insertInlineObject(new KoAnchorInlineObject(anchor));
    auto *layout = qobject_cast<KoTextDocumentLayout *>(frames->document()->documentLayout());
    layout->layout();
    HeaderCanvas canvas;
    canvas.snapGuide()->enableSnapping(false);
    canvas.shapeManager()->addShape(image);
    canvas.shapeManager()->selection()->select(image);
    HeaderTool tool(&canvas);
    const QPointF start = image->absolutePosition();
    ShapeMoveStrategy move(&tool, start);
    for (int i = 1; i <= 3; ++i) {
        move.handleMouseMove(start + QPointF(20 * i, 10 * i), Qt::NoModifier);
        layout->layout();
        QVERIFY2(image->position().y() + image->size().height() <= image->parent()->size().height() + 0.01,
                 "Floating header/footer image must remain inside its frame");
    }
    // Imported or previously dragged images can have an offset beyond the
    // frame, while layout has clamped the visible image back onto the page.
    anchor->setOffset(QPointF(7674, image->position().y()));
    frames->document()->markContentsDirty(0, frames->document()->characterCount());
    layout->layout();
    const QPointF visiblePosition = image->position();
    const QPointF nextStart = image->absolutePosition();
    ShapeMoveStrategy nextMove(&tool, nextStart);
    nextMove.handleMouseMove(nextStart - QPointF(100, 0), Qt::NoModifier);
    const QPointF draggedPosition = image->position();
    QCOMPARE(draggedPosition.x(), visiblePosition.x() - 100);
    layout->layout();
    QCOMPARE(image->position(), draggedPosition);
    QCOMPARE(anchor->offset(), draggedPosition);
    document.addCommand(nextMove.createCommand());
    document.undoStack()->undo();
    layout->layout();
    QCOMPARE(image->position(), visiblePosition);
    document.undoStack()->redo();
    layout->layout();
    QCOMPARE(image->position(), draggedPosition);
    // Put the floating logo at the left edge; new text must use the space
    // beside it rather than being forced into a line below the logo.
    anchor->setOffset(QPointF());
    frames->document()->markContentsDirty(0, frames->document()->characterCount());
    layout->layout();
    editor->setPosition(0);
    editor->insertText(QStringLiteral("Header text"));
    layout->layout();
    const auto block = frames->document()->firstBlock();
    const auto line = block.layout()->lineAt(0);
    const auto *data = qobject_cast<KoTextShapeData *>(image->parent()->userData());
    const auto textRect = line.naturalTextRect().translated(0, -data->documentOffset());
    const QRectF imageRect(image->position(), image->size());
    QVERIFY(!textRect.intersects(imageRect));
    QVERIFY(textRect.top() < imageRect.bottom());
    QVERIFY(textRect.left() >= imageRect.right());
    canvas.shapeManager()->selection()->deselectAll();
    canvas.shapeManager()->remove(image);
}

void TestHeaderFooterLayout::testImageDrag_data()
{
    QTest::addColumn<bool>("header");
    QTest::addColumn<bool>("undoInsertion");
    QTest::newRow("header") << true << true;
    QTest::newRow("footer") << false << true;
    QTest::newRow("close-floating-header") << true << false;
    QTest::newRow("close-floating-footer") << false << false;
}

void TestHeaderFooterLayout::testImageDrag()
{
    QFETCH(bool, header);
    QFETCH(bool, undoInsertion);
    MockPart part;
    KWDocument document(&part);
    document.initEmpty();
    auto style = document.pageManager()->page(1).pageStyle();
    if (header) style.setHeaderPolicy(Words::HFTypeUniform);
    else style.setFooterPolicy(Words::HFTypeUniform);
    document.relayout();
    KWTextFrameSet *frames = nullptr;
    const auto type = header ? Words::OddPagesHeaderTextFrameSet : Words::OddPagesFooterTextFrameSet;
    for (auto *candidate : document.frameSets()) {
        auto *text = qobject_cast<KWTextFrameSet *>(candidate);
        if (text && text->textFrameSetType() == type) frames = text;
    }
    QVERIFY(frames);
    auto *editor = KoTextDocument(frames->document()).textEditor();
    QVERIFY(editor);
    editor->insertText(QStringLiteral("Header before image "));
    auto *image = new MockShape;
    image->setSize(QSizeF(60, 50));
    image->setTextRunAroundSide(KoShape::NoRunAround);
    auto *anchor = new KoShapeAnchor(image);
    image->setAnchor(anchor);
    anchor->setAnchorType(KoShapeAnchor::AnchorAsCharacter);
    anchor->setVerticalRel(KoShapeAnchor::VBaseline);
    anchor->setVerticalPos(KoShapeAnchor::VTop);
    const int beforeInsert = document.undoStack()->index();
    auto *insert = editor->beginEditBlock(kundo2_noi18n("Insert image"));
    editor->addCommand(new KWShapeCreateCommand(&document, image, insert));
    editor->insertInlineObject(new KoAnchorInlineObject(anchor), insert);
    editor->endEditBlock();
    QCOMPARE(document.undoStack()->index(), beforeInsert + 1);
    editor->insertText(QStringLiteral(" text after image"));
    auto *layout = qobject_cast<KoTextDocumentLayout *>(frames->document()->documentLayout());
    QVERIFY(layout);
    layout->layout();
    QVERIFY(image->parent());
    const QPointF before = image->position();
    QVERIFY(before.x() > 0);

    HeaderCanvas canvas;
    canvas.snapGuide()->enableSnapping(false);
    canvas.shapeManager()->addShape(image);
    canvas.shapeManager()->selection()->select(image);
    HeaderTool tool(&canvas);
    ShapeMoveStrategy move(&tool, image->absolutePosition());
    move.handleMouseMove(image->absolutePosition() + QPointF(20, 0), Qt::NoModifier);
    QCOMPARE(anchor->anchorType(), KoShapeAnchor::AnchorToCharacter);
    QCOMPARE(anchor->textLocation()->document(), frames->document());
    const QPointF moved = image->position();
    QCOMPARE(moved.x(), before.x() + 20);
    document.addCommand(move.createCommand());
    layout->layout();
    QCOMPARE(image->position(), moved);
    // Editing must not add the character position to the frame offset again.
    editor->setPosition(0);
    editor->insertText(QStringLiteral("More "));
    layout->layout();
    QCOMPARE(image->position(), moved);
    image->setSize(QSizeF(70, 40));
    layout->layout();
    QCOMPARE(image->position(), moved);
    image->setSize(QSizeF(60, 50));
    layout->layout();
    // Undo the text edit, drag, and conversion, laying out between each step.
    document.undoStack()->undo();
    layout->layout();
    document.undoStack()->undo();
    layout->layout();
    document.undoStack()->undo();
    layout->layout();
    QCOMPARE(anchor->anchorType(), KoShapeAnchor::AnchorAsCharacter);
    QCOMPARE(image->position(), before);
    document.undoStack()->redo();
    layout->layout();
    document.undoStack()->redo();
    layout->layout();
    QCOMPARE(image->position(), moved);
    canvas.shapeManager()->selection()->deselectAll();
    canvas.shapeManager()->remove(image);
    if (!undoInsertion) return;
    // One undo of insertion removes both the image frame and its character.
    document.undoStack()->setIndex(beforeInsert);
    layout->layout();
    QCOMPARE(frames->document()->toPlainText(), QStringLiteral("Header before image "));
    QVERIFY(!image->parent());
    document.undoStack()->redo();
    layout->layout();
    QVERIFY(image->parent());
    QVERIFY(frames->document()->toPlainText().contains(QChar::ObjectReplacementCharacter));
}

void TestHeaderFooterLayout::testStableHeight_data()
{
    QTest::addColumn<bool>("header");
    QTest::newRow("header") << true;
    QTest::newRow("footer") << false;
}

void TestHeaderFooterLayout::testStableHeight()
{
    QFETCH(bool, header);
    MockPart part;
    KWDocument document(&part);
    document.initEmpty();
    KWPage page = document.pageManager()->page(1);
    auto style = page.pageStyle();
    if (header) {
        style.setHeaderPolicy(Words::HFTypeUniform);
    } else {
        style.setFooterPolicy(Words::HFTypeUniform);
    }
    document.relayout();
    const auto type = header ? Words::OddPagesHeaderTextFrameSet : Words::OddPagesFooterTextFrameSet;
    KWTextFrameSet *frames = nullptr;
    for (auto *candidate : document.frameSets()) {
        auto *textFrames = qobject_cast<KWTextFrameSet *>(candidate);
        if (textFrames && textFrames->textFrameSetType() == type) {
            frames = textFrames;
            break;
        }
    }
    QVERIFY(frames);
    QVERIFY(!frames->shapes().isEmpty());
    auto *shape = frames->shapes().first();
    auto *data = qobject_cast<KoTextShapeData *>(shape->userData());
    QVERIFY(data);
    data->setPadding(0);
    auto *layout = qobject_cast<KoTextDocumentLayout *>(frames->document()->documentLayout());
    auto *provider = frames->rootAreaProvider();
    bool isNew = false;
    auto *area = provider->provide(layout, RootAreaConstraint(), 0, &isNew);
    QVERIFY(area);
    area->setReferenceRect(0, shape->size().width(), 0, 1000);
    area->setBottom(60);
    provider->doPostLayout(area, isNew);
    QCOMPARE(shape->minimumHeight(), qreal(60));
    const qreal bodyHeight = document.mainFrameSet()->shapes().first()->size().height();

    // A smaller measurement of unchanged shared content must not resize the
    // body and start another document-wide reflow.
    area->setBottom(30);
    provider->doPostLayout(area, false);
    QCOMPARE(shape->minimumHeight(), qreal(60));
    QCOMPARE(document.mainFrameSet()->shapes().first()->size().height(), bodyHeight);

    // Actual header/footer editing must still allow the area to shrink.
    QTextCursor cursor(frames->document());
    cursor.insertText(QStringLiteral("edited"));
    area->setBottom(30);
    provider->doPostLayout(area, false);
    QCOMPARE(shape->minimumHeight(), qreal(30));

    // A different available width also invalidates the height measurement.
    shape->setSize(QSizeF(shape->size().width() / 2, shape->size().height()));
    area->setReferenceRect(0, shape->size().width(), 0, 1000);
    area->setBottom(20);
    provider->doPostLayout(area, false);
    QCOMPARE(shape->minimumHeight(), qreal(20));

    // Padding is outside the cached content measurement and can shrink
    // without changing the text document's revision.
    data->setTopPadding(10);
    data->setBottomPadding(10);
    area->setBottom(20);
    provider->doPostLayout(area, false);
    QCOMPARE(shape->minimumHeight(), qreal(40));
    data->setTopPadding(0);
    data->setBottomPadding(0);
    provider->doPostLayout(area, false);
    QCOMPARE(shape->minimumHeight(), qreal(20));
}

void TestHeaderFooterLayout::testRemoveHeaderFooterPolicy_data()
{
    testStableHeight_data();
}

// Removing the header/footer policy deletes the frame shape of the now unused
// header/footer. That shape owns a root area whose back-pointer lives in the
// shape's user data, so the deletion exercises KWTextFrameSet::cleanupShape()
// and the shape/application-data teardown order. This used to free the shape's
// user data first and then write into the already released root area, which
// corrupted the heap (crash while opening DOCX files with many sections).
void TestHeaderFooterLayout::testRemoveHeaderFooterPolicy()
{
    QFETCH(bool, header);
    MockPart part;
    KWDocument document(&part);
    document.initEmpty();
    KWPage page = document.pageManager()->page(1);
    auto style = page.pageStyle();
    if (header) style.setHeaderPolicy(Words::HFTypeUniform);
    else style.setFooterPolicy(Words::HFTypeUniform);
    document.relayout();

    const auto type = header ? Words::OddPagesHeaderTextFrameSet : Words::OddPagesFooterTextFrameSet;
    KWTextFrameSet *frames = nullptr;
    for (auto *candidate : document.frameSets()) {
        auto *text = qobject_cast<KWTextFrameSet *>(candidate);
        if (text && text->textFrameSetType() == type) {
            frames = text;
            break;
        }
    }
    QVERIFY(frames);
    QVERIFY(!frames->shapes().isEmpty());
    KoShape *shape = frames->shapes().first();
    auto *data = qobject_cast<KoTextShapeData *>(shape->userData());
    QVERIFY(data);

    // Run the layout synchronously so the header/footer shape really gets an
    // associated root area before we delete it.
    auto *layout = qobject_cast<KoTextDocumentLayout *>(frames->document()->documentLayout());
    QVERIFY(layout);
    layout->layout();
    QVERIFY(data->rootArea());

    // Switching the policy off makes the next relayout delete the shape.
    if (header) style.setHeaderPolicy(Words::HFTypeNone);
    else style.setFooterPolicy(Words::HFTypeNone);
    document.relayout();
    QVERIFY(frames->shapes().isEmpty());

    // A use-after-free in the cleanup path corrupts the allocator, which is
    // detected by the next allocations.
    for (int i = 0; i < 4096; ++i) {
        auto *scratch = new QList<qreal>;
        for (int j = 0; j < 32; ++j)
            scratch->append(j);
        delete scratch;
    }
    QVERIFY(document.pageCount() >= 1);
}

QTEST_MAIN(TestHeaderFooterLayout)
