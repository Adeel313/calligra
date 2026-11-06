// SPDX-License-Identifier: LGPL-2.0-or-later
#include "TextShape.h"
#include "ShrinkToFitShapeContainer.h"
#include <KoStyleManager.h>
#include <KoTextDocumentLayout.h>
#include <KoTextLayoutRootArea.h>
#include <QApplication>
#include <QTextCursor>
#include <memory>

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
    std::unique_ptr<ShrinkToFitShapeContainer> container(ShrinkToFitShapeContainer::wrapShape(shape));
    auto *layout = qobject_cast<KoTextDocumentLayout *>(doc->documentLayout());
    for (int i = 0; i < 15; ++i) { layout->layout(); QCoreApplication::processEvents(); }
    if (doc->blockCount() != emptyParagraphs + 1) qFatal("Fitting must preserve editing paragraphs");
    return shape->transformation().m11();
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
        std::unique_ptr<ShrinkToFitShapeContainer> parent(ShrinkToFitShapeContainer::wrapShape(wrapped));
        auto *wrappedLayout = qobject_cast<KoTextDocumentLayout *>(doc->documentLayout());
        for (int i = 0; i < 25; ++i) { wrappedLayout->layout(); QCoreApplication::processEvents(); }
        const qreal scale = wrapped->transformation().m11();
        const qreal height = wrappedData->rootArea()->boundingRect().height() + wrappedData->topPadding() + wrappedData->bottomPadding();
        if (height * scale > 90.2 || scale <= 0 || scale >= 1) qFatal("Wrapped quote still clips: %f at scale %f", height, scale);
        if (doc->toPlainText() != quote) qFatal("Fitting changed the text");
        doc->setPlainText(quote + quote);
        for (int i = 0; i < 25; ++i) { wrappedLayout->layout(); QCoreApplication::processEvents(); }
        const qreal editedHeight = wrappedData->rootArea()->boundingRect().height() + wrappedData->topPadding() + wrappedData->bottomPadding();
        if (editedHeight * wrapped->transformation().m11() > 90.2) qFatal("Edited text was not refitted");
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
    qInfo("PASS: trailing blank paragraphs, wrapped text bounds, preserved content and alignment reset");
}
