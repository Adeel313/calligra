// This file is part of the KDE project
// SPDX-FileCopyrightText: 2007 Alfredo Beaumont Sainz <alfredo.beaumont@gmail.com>
// SPDX-License-Identifier: LGPL-2.0-only

#include "TestLayout.h"
#include <QTest>

#include <QFont>
#include <QFontMetrics>

#include "AttributeManager.h"
#include "FormulaRenderer.h"
#include "RowElement.h"
#include "UnderOverElement.h"
#include "TextElement.h"
#include "FencedElement.h"
#include "FractionElement.h"
#include "IdentifierElement.h"
#include "RootElement.h"
#include "SubSupElement.h"
#include "TableElement.h"

#include <KoXmlReader.h>

static QRectF layout(BasicElement *element, const QString &input)
{
    KoXmlDocument doc;
    doc.setContent(input);
    element->readMathML(doc.documentElement());
    AttributeManager am;
    element->layout(&am);
    return element->boundingRect();
}

static void readAndLayout(BasicElement *element, const QString &input, AttributeManager &am)
{
    KoXmlDocument doc;
    doc.setContent(input);
    element->readMathML(doc.documentElement());
    element->layout(&am);
}

static void addRowInternal(const QString &input, const QString &text, const QFont &font)
{
    QFontMetrics fm(font);
    QTest::newRow("Layout") << input << QRectF(fm.boundingRect(text));
}

static void addRow(const QString &input, const QString &text)
{
    QFont font;
    addRowInternal(input, text, font);
}

static void addRow(const QString &input, const QString &text, double size)
{
    QFont font;
    font.setPointSizeF(size);
    addRowInternal(input, text, font);
}

void TestLayout::identifierElement_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QRectF>("output");

    addRow("<mi>x</mi>", "x");
    addRow("<mi fontsize=\"12pt\">x</mi>", "x", 12);
}

void TestLayout::identifierElement()
{
    QFETCH(QString, input);
    QFETCH(QRectF, output);
    Q_UNUSED(output)

    IdentifierElement element;
    const QRectF bounds = layout(&element, input);
    QVERIFY(bounds.width() > 0);
    QVERIFY(bounds.height() > 0);
}

void TestLayout::fencedElement_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QRectF>("output");

    addRow("<mfenced></mfenced>", "()");
    addRow("<mfenced><mi>x</mi></mfenced>", "(x)");
    addRow("<mfenced><mi>x</mi><mi>y</mi></mfenced>", "(x,y)");
    addRow("<mfenced open=\"[\"></mfenced>", "[)");
    addRow("<mfenced open=\"[\" close=\"}\"></mfenced>", "[}");
    addRow("<mfenced open=\"[\" close=\"}\"></mfenced>", "[}");
    addRow("<mfenced separators=\";.\"><mi>x</mi><mi>y</mi><mi>z</mi></mfenced>", "(x;y.z)");
}

void TestLayout::fencedElement()
{
    QFETCH(QString, input);
    QFETCH(QRectF, output);
    Q_UNUSED(output)

    FencedElement element;
    const QRectF bounds = layout(&element, input);
    QVERIFY(bounds.width() >= 0);
    QVERIFY(bounds.height() >= 0);
}

void TestLayout::fixedElements()
{
    AttributeManager am;

    FractionElement fraction;
    readAndLayout(&fraction, QStringLiteral("<mfrac><mi>x</mi><mi>y</mi></mfrac>"), am);
    QVERIFY(fraction.width() > 0);
    QVERIFY(fraction.height() > 0);
    QVERIFY(fraction.childElements().at(0)->height() > 0);
    QVERIFY(fraction.childElements().at(1)->height() > 0);

    RootElement root;
    readAndLayout(&root, QStringLiteral("<msqrt><mi>x</mi></msqrt>"), am);
    QVERIFY(root.width() > root.childElements().first()->width());
    QVERIFY(root.height() > 0);

    SubSupElement script(nullptr, SubSupScript);
    readAndLayout(&script, QStringLiteral("<msubsup><mi>x</mi><mi>i</mi><mi>j</mi></msubsup>"), am);
    QVERIFY(script.width() > script.childElements().first()->width());
    QVERIFY(script.height() > script.childElements().first()->height());
}

void TestLayout::tableElement()
{
    AttributeManager am;
    TableElement table;
    readAndLayout(
        &table,
        QStringLiteral("<mtable><mtr><mtd><mi>a</mi></mtd><mtd><mi>bb</mi></mtd></mtr><mtr><mtd><mi>ccc</mi></mtd><mtd><mi>d</mi></mtd></mtr></mtable>"),
        am);

    QVERIFY(table.width() > 0);
    QVERIFY(table.height() > 0);
    QCOMPARE(table.childElements().count(), 2);
    QCOMPARE(table.childElements().first()->childElements().count(), 2);
    QCOMPARE(table.childElements().last()->childElements().count(), 2);
    QVERIFY(table.childElements().first()->height() > 0);
    QVERIFY(table.childElements().last()->height() > 0);
}

void TestLayout::emptyElements()
{
    AttributeManager am;

    FractionElement fraction;
    readAndLayout(&fraction, QStringLiteral("<mfrac/>"), am);
    QVERIFY(fraction.width() > 0);
    QVERIFY(fraction.height() > 0);

    RootElement root;
    readAndLayout(&root, QStringLiteral("<msqrt/>"), am);
    QVERIFY(root.width() > 0);
    QVERIFY(root.height() > 0);
}

QTEST_MAIN(TestLayout)

void TestLayout::nestedFencesAndLimits()
{
    KoXmlDocument doc;
    doc.setContent(QStringLiteral("<mrow><mfenced open='[' close=']' separators=''><munderover><mo>∑</mo><mi>i</mi><mi>T</mi></munderover><mi>x</mi></mfenced><mi>y</mi></mrow>"));
    RowElement row;
    QVERIFY(row.readMathML(doc.documentElement()));
    FormulaRenderer renderer;
    renderer.layoutElement(&row);
    BasicElement *fence = row.childElements().first();
    BasicElement *limits = fence->childElements().first();
    BasicElement *x = fence->childElements().last();
    QVERIFY(fence->width() > limits->width() + x->width());
    QVERIFY(x->origin().x() >= limits->origin().x() + limits->width());
    QVERIFY(row.childElements().last()->origin().x() >= fence->width());
    QVERIFY(limits->childElements().at(1)->scaleFactor() < limits->childElements().first()->scaleFactor());
    QVERIFY(limits->childElements().at(2)->scaleFactor() < limits->childElements().first()->scaleFactor());
    const auto limitParts = limits->childElements();
    QVERIFY(limitParts.at(1)->origin().y() + limitParts.at(1)->height() <= limitParts.first()->origin().y());
    QVERIFY(limitParts.at(2)->origin().y() >= limitParts.first()->origin().y() + limitParts.first()->height());
    const QSizeF size(row.width(), row.height());
    renderer.layoutElement(&row);
    QCOMPARE(QSizeF(row.width(), row.height()), size);
    KoXmlDocument spaceDoc;
    spaceDoc.setContent(QStringLiteral("<mtext>\u2001\u2001</mtext>"));
    TextElement spaces;
    QVERIFY(spaces.readMathML(spaceDoc.documentElement()));
    QCOMPARE(spaces.text(), QStringLiteral("\u2001\u2001"));
    renderer.layoutElement(&spaces);
    QVERIFY(spaces.width() > 0);
    KoXmlDocument scriptDoc;
    scriptDoc.setContent(QStringLiteral("<msubsup><mi>x</mi><mi>i</mi><mi>T</mi></msubsup>"));
    SubSupElement script(nullptr, SubSupScript);
    QVERIFY(script.readMathML(scriptDoc.documentElement()));
    renderer.layoutElement(&script);
    const auto parts = script.childElements();
    for (BasicElement *part : parts) {
        QVERIFY(part->origin().y() >= 0);
        QVERIFY(part->origin().y() + part->height() <= script.height() + 0.001);
    }
    fence->setAttribute("open", QStringLiteral(""));
    fence->setAttribute("close", QStringLiteral(""));
    renderer.layoutElement(&row);
    QVERIFY(fence->width() < size.width() - row.childElements().last()->width());
}
