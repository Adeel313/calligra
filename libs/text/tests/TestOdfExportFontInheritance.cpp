/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra contributors <info@calligra.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "TestOdfExportFontInheritance.h"

#include <KoEmbeddedDocumentSaver.h>
#include <KoGenStyle.h>
#include <KoGenStyles.h>
#include <KoShapeSavingContext.h>
#include <KoTextWriter.h>
#include <KoXmlWriter.h>
#include <styles/KoCharacterStyle.h>
#include <styles/KoParagraphStyle.h>
#include <styles/KoStyleManager.h>

#include <QBuffer>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTest>

namespace {

QString savedFontFamily(const QString &parentFontFamily)
{
    QByteArray output;
    QBuffer buffer(&output);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter xmlWriter(&buffer);
    KoGenStyles mainStyles;
    KoEmbeddedDocumentSaver embeddedSaver;
    KoShapeSavingContext context(xmlWriter, mainStyles, embeddedSaver);

    KoStyleManager styleManager;
    styleManager.defaultCharacterStyle()->setFontFamily(QStringLiteral("Calibri"));
    styleManager.defaultCharacterStyle()->setFontPointSize(11.0);
    styleManager.defaultParagraphStyle()->setFontFamily(QStringLiteral("Calibri"));
    styleManager.defaultParagraphStyle()->setFontPointSize(11.0);

    KoParagraphStyle parentStyle;
    parentStyle.setName(QStringLiteral("Normal"));
    parentStyle.setDefaultStyle(styleManager.defaultParagraphStyle());
    if (!parentFontFamily.isEmpty()) {
        parentStyle.setFontFamily(parentFontFamily);
    }
    styleManager.add(&parentStyle);

    QTextBlockFormat blockFormat;
    blockFormat.setBottomMargin(16);
    blockFormat.setProperty(KoParagraphStyle::StyleId, parentStyle.styleId());

    QTextCharFormat charFormat;
    charFormat.setProperty(QTextFormat::FontFamily, QStringLiteral("Calibri"));
    charFormat.setProperty(QTextFormat::FontPointSize, 11.0);

    const QString styleName = KoTextWriter::saveParagraphStyle(blockFormat, charFormat, &styleManager, context);
    const KoGenStyle *autoStyle = mainStyles.style(styleName, "paragraph");
    return autoStyle ? autoStyle->property(QStringLiteral("fo:font-family"), KoGenStyle::TextType) : QString();
}

} // namespace

void TestOdfExportFontInheritance::testInheritedFontIsNotWrittenToAutoStyle()
{
    QCOMPARE(savedFontFamily(QString()), QString());
}

void TestOdfExportFontInheritance::testFontOverridingParentStyleIsWritten()
{
    QCOMPARE(savedFontFamily(QStringLiteral("Liberation Serif")), QStringLiteral("Calibri"));
}

QTEST_MAIN(TestOdfExportFontInheritance)
