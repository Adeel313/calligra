// SPDX-License-Identifier: LGPL-2.0-or-later
#include "MockPart.h"

#include <KWDocument.h>
#include <KWPage.h>
#include <KWPageManager.h>
#include <frames/KWTextFrameSet.h>
#include <KoParagraphStyle.h>
#include <KoStyleManager.h>
#include <KoTextDocument.h>
#include <KoTextDocumentLayout.h>
#include <KoTextEditor.h>
#include <KoTextLayoutRootArea.h>
#include <QTextBlock>
#include <QTextDocument>
#include <QtTest>

class TestHeadingStyle : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void firstParagraphKeepsPage()
    {
        MockPart part;
        KWDocument document(&part);
        document.initEmpty();
        KWPageStyle imported(QStringLiteral("Imported page"));
        imported.setPageLayout(document.pageManager()->defaultPageStyle().pageLayout());
        document.pageManager()->addPageStyle(imported);
        auto page = document.pageManager()->page(1);
        page.setPageStyle(imported);

        auto *text = document.mainFrameSet()->document();
        KoTextDocument textDoc(text);
        auto *editor = textDoc.textEditor();
        QVERIFY(editor);
        editor->insertText(QStringLiteral("Section 3: Logic\nBody text"));
        QTextCursor cursor(text);
        QTextBlockFormat format;
        format.setProperty(KoParagraphStyle::MasterPageName, imported.name());
        format.setProperty(KoParagraphStyle::BreakBefore, KoText::PageBreak);
        cursor.mergeBlockFormat(format);
        document.relayout();
        auto *layout = qobject_cast<KoTextDocumentLayout *>(text->documentLayout());
        QVERIFY(layout);
        layout->layout();
        QCOMPARE(document.pageCount(), 1);

        auto *heading = new KoParagraphStyle;
        heading->setName(QStringLiteral("Heading 1"));
        heading->setOutlineLevel(1);
        textDoc.styleManager()->add(heading);
        editor->setPosition(0);
        editor->setPosition(text->firstBlock().length() - 1, QTextCursor::KeepAnchor);
        editor->setStyle(heading);
        layout->layout();
        QCOMPARE(document.pageCount(), 1);
        QVERIFY(!layout->rootAreas().isEmpty());
        QCOMPARE(layout->rootAreas().first()->page()->pageNumber(), 1);
        QCOMPARE(document.pageManager()->page(1).masterPageName(), imported.name());
    }
};

QTEST_MAIN(TestHeadingStyle)
#include "TestHeadingStyle.moc"
