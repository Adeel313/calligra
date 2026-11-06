/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "TestHeaderFooterLayout.h"
#include "MockPart.h"

#include <KWDocument.h>
#include <KWPage.h>
#include <KWPageManager.h>
#include <KWRootAreaProviderBase.h>
#include <frames/KWFrameLayout.h>
#include <frames/KWTextFrameSet.h>

#include <KoShape.h>
#include <KoTextDocumentLayout.h>
#include <KoTextLayoutRootArea.h>
#include <KoTextShapeData.h>
#include <QTextCursor>
#include <QTextDocument>
#include <QtTest>

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

QTEST_MAIN(TestHeaderFooterLayout)
