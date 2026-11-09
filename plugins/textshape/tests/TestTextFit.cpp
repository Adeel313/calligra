// SPDX-License-Identifier: LGPL-2.0-or-later
#include "TextShape.h"
#include <FrameIterator.h>
#include <KoStyleManager.h>
#include <KoTextDocumentLayout.h>
#include <KoTextLayoutRootArea.h>
#include <KoTosContainer.h>
#include <QApplication>
#include <QTextCursor>
#include <memory>

// Minimal shape that carries text, like a custom shape or a callout does.
class TextOnShape : public KoTosContainer
{
public:
    bool loadOdf(const KoXmlElement &, KoShapeLoadingContext &) override
    {
        return true;
    }

    void saveOdf(KoShapeSavingContext &) const override
    {
    }
};

static qreal firstFontSize(QTextDocument *document)
{
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.fontPointSize() > 0)
                return format.fontPointSize();
        }
    }
    return document->defaultFont().pointSizeF();
}

// True when the layout reached the end of the text (trailing empty paragraphs
// aside), i.e. nothing was left outside the frame.
static bool laysOutEverything(KoTextShapeData *data)
{
    KoTextLayoutRootArea *rootArea = data->rootArea();
    if (!rootArea)
        return false;
    FrameIterator *next = rootArea->nextStartOfArea();
    if (!next || next->it.atEnd())
        return true;
    QTextBlock last = data->document()->lastBlock();
    while (last.isValid() && last.text().trimmed().isEmpty() && !last.textList())
        last = last.previous();
    if (!last.isValid())
        return true;
    return next->it.currentBlock().isValid()
        && next->it.currentBlock().position() >= last.position() + last.length();
}

static qreal fitScale(int emptyParagraphs, int authoredScale = 0, int reduction = 0)
{
    auto *shape = new TextShape(nullptr, nullptr);
    auto *data = dynamic_cast<KoTextShapeData *>(shape->userData());
    auto *doc = data->document();
    KoTextDocument(doc).setStyleManager(new KoStyleManager(doc));
    doc->setDefaultFont(QFont(QStringLiteral("Sans Serif"), 12));
    QTextCursor cursor(doc);
    cursor.insertText(QStringLiteral("Readable caption.") + QString(emptyParagraphs, QLatin1Char('\n')));
    shape->setSize(QSizeF(220, 50));
    data->setResizeMethod(KoTextShapeData::ShrinkToFitResize);
    data->setAutofitFontScale(authoredScale);
    data->setAutofitLineSpacingReduction(reduction);
    // The import applies the authored scale to the runs; see TextShape::loadOdfFrameElement().
    if (authoredScale > 0)
        KoTextShapeDataBase::scaleFontSizes(doc, authoredScale / 100000.0);
    auto *layout = qobject_cast<KoTextDocumentLayout *>(doc->documentLayout());
    for (int i = 0; i < 40; ++i) { layout->layout(); QCoreApplication::processEvents(); }
    if (doc->blockCount() != emptyParagraphs + 1) qFatal("Fitting must preserve editing paragraphs");
    return firstFontSize(doc) / 12.0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    const qreal authored = fitScale(0, 60000);
    if (qAbs(authored - 0.6) > 0.001) qFatal("Authored font scale ignored: %f", authored);
    const qreal compressed = fitScale(12, 0, 20000);
    if (compressed < 0.8) qFatal("Compressed trailing blanks changed visible scale: %f", compressed);
    const qreal plain = fitScale(0);
    const qreal trailing = fitScale(12);
    if (plain < 0.8 || trailing < 0.8 || qAbs(plain - trailing) > 0.03)
        qFatal("Trailing blanks changed visible text scale: %f vs %f", plain, trailing);
    {
        auto *wrapped = new TextShape(nullptr, nullptr);
        auto *wrappedData = dynamic_cast<KoTextShapeData *>(wrapped->userData());
        auto *doc = wrappedData->document();
        KoTextDocument(doc).setStyleManager(new KoStyleManager(doc));
        doc->setDefaultFont(QFont(QStringLiteral("Sans Serif"), 16));
        const QString quote = QStringLiteral("A long wrapped quote must keep every last line inside its frame. ").repeated(12);
        doc->setPlainText(quote);
        wrapped->setSize(QSizeF(220, 90));
        wrappedData->setResizeMethod(KoTextShapeData::ShrinkToFitResize);
        auto *wrappedLayout = qobject_cast<KoTextDocumentLayout *>(doc->documentLayout());
        for (int i = 0; i < 60; ++i) { wrappedLayout->layout(); QCoreApplication::processEvents(); }
        const qreal fitted = firstFontSize(doc);
        if (fitted >= 16 || !laysOutEverything(wrappedData))
            qFatal("Wrapped quote still clips: font %f", fitted);
        if (doc->toPlainText() != quote) qFatal("Fitting changed the text");
        // Typing more text has to make the frame fit again.
        QTextCursor cursor(doc);
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(QStringLiteral(" More text typed later makes the frame overflow again. ").repeated(6));
        for (int i = 0; i < 60; ++i) { wrappedLayout->layout(); QCoreApplication::processEvents(); }
        if (firstFontSize(doc) >= fitted || !laysOutEverything(wrappedData))
            qFatal("Edited text was not refitted: font %f after %f", firstFontSize(doc), fitted);
    }
    auto shape = std::make_unique<TextShape>(nullptr, nullptr);
    auto *data = dynamic_cast<KoTextShapeData *>(shape->userData());
    KoTextDocument(data->document()).setStyleManager(new KoStyleManager(data->document()));
    data->document()->setPlainText(QStringLiteral("Align me"));
    shape->setSize(QSizeF(220, 100));
    data->setVerticalAlignment(Qt::AlignVCenter);
    auto *layout = qobject_cast<KoTextDocumentLayout *>(data->document()->documentLayout());
    layout->layout();
    if (!data->rootArea() || data->rootArea()->verticalAlignOffset() <= 0) qFatal("Center alignment was not applied");
    data->setVerticalAlignment(Qt::AlignTop);
    data->setDirty(); layout->layout();
    if (qAbs(data->rootArea()->verticalAlignOffset()) > 0.01) qFatal("Top alignment retained a stale offset");
    {
        // Text on a shape (custom shape, callout) has no ShrinkToFitShapeContainer:
        // the text shape has to keep itself inside the shape, also after resizing.
        auto container = std::make_unique<TextOnShape>();
        auto *onShape = new TextShape(nullptr, nullptr);
        auto *onShapeData = dynamic_cast<KoTextShapeData *>(onShape->userData());
        QTextDocument *doc = onShapeData->document();
        KoTextDocument(doc).setStyleManager(new KoStyleManager(doc));
        doc->setDefaultFont(QFont(QStringLiteral("Sans Serif"), 16));
        doc->setPlainText(QStringLiteral("Text on a shape stays inside. ").repeated(6));
        const qreal authoredFont = doc->defaultFont().pointSizeF();
        container->setSize(QSizeF(220, 70));
        onShape->setSize(QSizeF(220, 70));
        onShapeData->setResizeMethod(KoTextShapeData::ShrinkToFitResize);
        container->addShape(onShape);
        auto *onShapeLayout = qobject_cast<KoTextDocumentLayout *>(doc->documentLayout());
        for (int i = 0; i < 60; ++i) { onShapeLayout->layout(); QCoreApplication::processEvents(); }
        const qreal shrunk = firstFontSize(doc);
        const qreal used = onShapeData->rootArea()->boundingRect().height() + onShapeData->topPadding() + onShapeData->bottomPadding();
        const bool complete = !onShapeData->rootArea()->nextStartOfArea() || onShapeData->rootArea()->nextStartOfArea()->it.atEnd();
        if (shrunk >= authoredFont || !complete || used > onShape->size().height() * 1.05)
            qFatal("Text on shape was not fitted: font %f of %f, height %f of %f, complete %d", shrunk, authoredFont, used, onShape->size().height(), int(complete));
        // The container model resizes its text shape like this when the user
        // resizes the shape.
        container->setSize(QSizeF(220, 600));
        onShape->setSize(QSizeF(220, 600));
        for (int i = 0; i < 25; ++i) { onShapeLayout->layout(); QCoreApplication::processEvents(); }
        if (firstFontSize(doc) < authoredFont * 0.95)
            qFatal("Enlarged shape did not restore the text size: %f of %f", firstFontSize(doc), authoredFont);
    }
    qInfo("PASS: trailing blank paragraphs, wrapped text bounds, preserved content and alignment reset");
}
