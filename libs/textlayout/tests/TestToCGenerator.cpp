// SPDX-License-Identifier: LGPL-2.0-or-later
#include "MockRootAreaProvider.h"
#include "ToCGenerator.h"
#include <KoInlineTextObjectManager.h>
#include <KoParagraphStyle.h>
#include <KoStyleManager.h>
#include <KoTableOfContentsGeneratorInfo.h>
#include <KoTextDocument.h>
#include <KoTextDocumentLayout.h>
#include <KoTextLayoutRootArea.h>
#include <KoTextPage.h>
#include <KoTextRangeManager.h>
#include <QImage>
#include <QPainter>
#include <QTest>
#include <QTextCursor>

namespace {
class TestPage : public KoTextPage
{
public:
    int number = 7;
    int pageNumber() const override { return number; }
    int visiblePageNumber(PageSelection = CurrentPage, int = 0) const override { return number; }
    QRectF rect() const override { return QRectF(0, 0, 600, 800); }
};

struct Fixture {
    MockRootAreaProvider provider;
    QTextDocument document;
    QTextDocument toc;
    KoStyleManager *styles = new KoStyleManager(&document);
    KoTextDocumentLayout *layout;
    TestPage *page = new TestPage;
    KoTableOfContentsGeneratorInfo *info = new KoTableOfContentsGeneratorInfo(true);

    Fixture()
    {
        KoTextDocument(&document).setStyleManager(styles);
        KoTextDocument(&document).setTextRangeManager(new KoTextRangeManager(&document));
        KoTextDocument(&document).setInlineTextObjectManager(new KoInlineTextObjectManager(&document));
        layout = new KoTextDocumentLayout(&document, &provider);
        document.setDocumentLayout(layout);
        QTextCursor cursor(&document);
        cursor.insertText(QStringLiteral("TOC\nAlpha\nBeta"));
        QTextBlock heading = document.begin().next();
        for (int level = 1; level <= 2; ++level) {
            QTextCursor c(heading);
            QTextBlockFormat format;
            format.setProperty(KoParagraphStyle::OutlineLevel, level);
            c.setBlockFormat(format);
            auto *style = new KoParagraphStyle;
            style->setName(QStringLiteral("Regression TOC %1").arg(level));
            style->setFontFamily(QStringLiteral("DejaVu Sans"));
            style->setFontPointSize(level == 1 ? 11 : 10.5);
            style->setFontWeight(level == 1 ? QFont::Bold : QFont::Normal);
            style->setLeftMargin(QTextLength(QTextLength::FixedLength, (level - 1) * 20));
            styles->add(style);
            info->m_entryTemplate[level - 1].styleId = style->styleId();
            heading = heading.next();
        }
        info->m_outlineLevel = 2;
        layout->layout();
        provider.area()->setPage(page);
        KoTextDocument(&toc).setStyleManager(styles);
    }
};
}

class TestToCGenerator : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void regenerationAndLinks()
    {
        Fixture f;
        ToCGenerator generator(&f.toc, f.info);
        generator.setBlock(f.document.begin());
        QVERIFY(generator.generate());
        QCOMPARE(f.toc.blockCount(), 2);
        QCOMPARE(f.toc.begin().text(), QStringLiteral("Alpha\t7"));
        QCOMPARE(f.toc.lastBlock().text(), QStringLiteral("Beta\t7"));
        QCOMPARE(f.toc.begin().begin().fragment().charFormat().fontWeight(), int(QFont::Bold));
        QCOMPARE(f.toc.lastBlock().begin().fragment().charFormat().fontWeight(), int(QFont::Normal));
        const auto link = f.toc.begin().begin().fragment().charFormat();
        QVERIFY(link.isAnchor());
        QVERIFY(link.anchorHref().startsWith(QLatin1Char('#')));
        QVERIFY(link.foreground().color() != QColor(Qt::blue));
        const QString target = link.anchorHref();
        QTextCursor cursor(f.document.begin().next());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.insertText(QStringLiteral("Renamed"));
        f.page->number = 9;
        QVERIFY(generator.generate());
        QCOMPARE(f.toc.blockCount(), 2);
        QCOMPARE(f.toc.begin().text(), QStringLiteral("Renamed\t9"));
        QCOMPARE(f.toc.begin().begin().fragment().charFormat().anchorHref(), target);
    }

    void sparseTemplates()
    {
        Fixture f;
        // A single level-2 template must work even though its array index is 0.
        for (int i = f.info->m_entryTemplate.size() - 1; i >= 0; --i) {
            if (i == 1) continue;
            qDeleteAll(f.info->m_entryTemplate[i].indexEntries);
            f.info->m_entryTemplate.removeAt(i);
        }
        ToCGenerator generator(&f.toc, f.info);
        generator.setBlock(f.document.begin());
        QVERIFY(generator.generate());
        QCOMPARE(f.toc.blockCount(), 1);
        QCOMPARE(f.toc.begin().text(), QStringLiteral("Beta\t7"));
    }

    void dottedLeadersArePainted_data()
    {
        QTest::addColumn<int>("dpi");
        QTest::newRow("screen") << 72;
        QTest::newRow("printer") << 600;
    }

    void dottedLeadersArePainted()
    {
        MockRootAreaProvider provider;
        Fixture f;
        ToCGenerator generator(&f.toc, f.info);
        generator.setBlock(f.document.begin());
        QVERIFY(generator.generate());
        KoTextDocument(&f.toc).setInlineTextObjectManager(new KoInlineTextObjectManager(&f.toc));
        auto *layout = new KoTextDocumentLayout(&f.toc, &provider);
        f.toc.setDocumentLayout(layout);
        layout->layout();
        QFETCH(int, dpi);
        const qreal scale = dpi / 72.0;
        QImage image(qCeil(400 * scale), qCeil(200 * scale), QImage::Format_ARGB32_Premultiplied);
        image.setDotsPerMeterX(qRound(dpi / 0.0254));
        image.setDotsPerMeterY(qRound(dpi / 0.0254));
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.scale(scale, scale);
        KoTextDocumentLayout::PaintContext context;
        context.showTableBorders = false;
        provider.area()->paint(&painter, context);
        painter.end();
        image = image.scaled(400, 200);
        int ink = 0;
        for (int y = 100; y < 120; ++y)
            for (int x = 200; x < 260; ++x)
                if (qGray(image.pixel(x, y)) < 180) ++ink;
        QVERIFY2(ink > 10, qPrintable(QStringLiteral("Leader pixels: %1").arg(ink)));
    }
};
QTEST_MAIN(TestToCGenerator)
#include "TestToCGenerator.moc"
