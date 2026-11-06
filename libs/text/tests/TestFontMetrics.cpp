// SPDX-License-Identifier: LGPL-2.0-or-later
#include <KoCharacterStyle.h>
#include <QFontDatabase>
#include <QTest>

class TestFontMetrics : public QObject
{
    Q_OBJECT
private Q_SLOTS:
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
        if (!QFontDatabase::families().contains(QStringLiteral("Calibri")))
            QSKIP("Calibri is not installed");
        KoCharacterStyle style;
        style.setFontFamily(QStringLiteral("Calibri"));
        // Calibri does not request USE_TYPO_METRICS; preserve existing spacing.
        QVERIFY(qAbs(style.fontYStretch() - 1.15) < 0.01);
    }
};
QTEST_MAIN(TestFontMetrics)
#include "TestFontMetrics.moc"
