// SPDX-License-Identifier: LGPL-2.0-or-later
#include <KoCharacterStyle.h>
#include <KoTextDocument.h>
#include <QFontDatabase>
#include <QTest>

class TestFontMetrics : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void externalLeadingIsOptional()
    {
        if (!QFontDatabase::families().contains(QStringLiteral("Times New Roman")))
            QSKIP("Times New Roman is not installed");
        QTextDocument document;
        KoTextDocument textDocument(&document);
        QVERIFY(!textDocument.addExternalLeading());
        textDocument.setAddExternalLeading(true);
        QVERIFY(textDocument.addExternalLeading());
        KoCharacterStyle style;
        style.setFontFamily(QStringLiteral("Times New Roman"));
        const qreal withoutLeading = style.value(KoCharacterStyle::FontYStretchWithoutLeading).toDouble();
        QVERIFY(qAbs(12.0 * withoutLeading * 1.16 - 12.0 * 2268.0 / 2048.0) < 0.01);
        QVERIFY(qAbs(12.0 * (style.fontYStretch() - withoutLeading) * 1.16 - 12.0 * 87.0 / 2048.0) < 0.01);
    }

    void inlineMathUsesTypographicMetrics()
    {
        if (!QFontDatabase::families().contains(QStringLiteral("Cambria Math")))
            QSKIP("Cambria Math is not installed");
        KoCharacterStyle style;
        style.setFontFamily(QStringLiteral("Cambria Math"));
        // Cambria Math's Windows clipping bounds span more than five ems.
        // Its USE_TYPO_METRICS flag requests ordinary text line spacing.
        QVERIFY2(style.fontYStretch() > 0.5 && style.fontYStretch() < 2.0,
                 qPrintable(QStringLiteral("Math line-height stretch: %1").arg(style.fontYStretch())));
    }
    void ordinaryFontKeepsWindowsMetrics()
    {
        if (!QFontDatabase::families().contains(QStringLiteral("Times New Roman")))
            QSKIP("Times New Roman is not installed");
        KoCharacterStyle style;
        style.setFontFamily(QStringLiteral("Times New Roman"));
        // Times New Roman: Windows ascent 1825, descent 443, hhea gap 87,
        // 2048 units/em. Include the layout's leading factor exactly once.
        // At 11 pt this is 12.649 pt, not the old estimated 13.312 pt.
        const qreal lineHeight = 11.0 * style.fontYStretch() * 1.16;
        QVERIFY2(qAbs(lineHeight - 11.0 * 2355.0 / 2048.0) < 0.01,
                 qPrintable(QStringLiteral("Times New Roman line height: %1").arg(lineHeight)));
    }
};
QTEST_MAIN(TestFontMetrics)
#include "TestFontMetrics.moc"
