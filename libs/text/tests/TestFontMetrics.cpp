// SPDX-License-Identifier: LGPL-2.0-or-later
#include <KoCharacterStyle.h>
#include <KoTextDocument.h>
#include <QFontDatabase>
#include <QRawFont>
#include <QFontInfo>
#include <QTest>

class TestFontMetrics : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void missingItalicPreservesWeight()
    {
        const QString family = QStringLiteral("Calibri");
        if (!QFontDatabase::families().contains(family))
            QSKIP("Calibri is not installed");
        bool regular = false;
        for (const QString &name : QFontDatabase::styles(family)) {
            regular |= !QFontDatabase::italic(family, name)
                && QFontDatabase::weight(family, name) == QFont::Normal;
        }
        if (!regular)
            QSKIP("Calibri's regular face is not installed");
        KoCharacterStyle style;
        style.setFontFamily(family);
        style.setFontWeight(QFont::Normal);
        style.setFontItalic(true);
        QTextCharFormat format;
        style.applyStyle(format);
        const QRawFont actual = QRawFont::fromFont(format.font());
        QVERIFY(actual.isValid());
        QCOMPARE(actual.weight(), QFont::Normal);
        QCOMPARE(QFontDatabase::weight(family, QFontInfo(format.font()).styleName()), QFont::Normal);
        QVERIFY(format.fontItalic());
    }
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

    void installedFontIsNotFallback()
    {
        if (!QFontDatabase::families().contains(QStringLiteral("Times New Roman")))
            QSKIP("Times New Roman is not installed");
        KoCharacterStyle style;
        style.setFontFamily(QStringLiteral("Times New Roman"));
        QVERIFY(!style.value(KoCharacterStyle::FontYStretchIsFallback).toBool());
    }

    void missingFontIsFallback()
    {
        const QString missing = QStringLiteral("ZZZUninstalledTestFont");
        if (QFontDatabase::families().contains(missing))
            QSKIP("The deliberately missing font is installed");
        KoCharacterStyle style;
        style.setFontFamily(missing);
        QVERIFY(style.value(KoCharacterStyle::FontYStretchIsFallback).toBool());
    }

    void styleHintRecomputesSubstitutedMetrics()
    {
        // Consolas is absent on Linux. Qt substitutes a monospaced face by
        // default but a serif face when the generic family is roman; those
        // faces have different line metrics.
        const QString missing = QStringLiteral("Consolas");
        if (QFontDatabase::families().contains(missing))
            QSKIP("Consolas is installed");
        KoCharacterStyle style;
        style.setFontFamily(missing);
        const qreal anyStretch = style.fontYStretch();
        style.setFontStyleHint(QFont::Serif);
        const qreal serifStretch = style.fontYStretch();
        // The hint changes which face Qt substitutes, so the measured line
        // height has to follow it rather than staying at the first result.
        QVERIFY(!qFuzzyCompare(anyStretch, serifStretch));
    }
};
QTEST_MAIN(TestFontMetrics)
#include "TestFontMetrics.moc"
