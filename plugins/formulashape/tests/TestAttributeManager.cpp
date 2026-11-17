#include "TestAttributeManager.h"

#include <AttributeManager.h>
#include <BasicElement.h>
#include <StyleElement.h>
#include <KoXmlReader.h>

#include <QTest>
#include <QFontMetricsF>

void TestAttributeManager::initTestCase()
{
    m_attributeManager = new AttributeManager();
    m_basicElement = new BasicElement(nullptr);
}

void TestAttributeManager::cleanupTestCase()
{
    delete m_attributeManager;
    delete m_basicElement;
}

void TestAttributeManager::testColorConversion()
{
    m_basicElement->setAttribute("color", "blue");
    m_basicElement->setAttribute("color1", "green");
    m_basicElement->setAttribute("color2", "#001122");
    m_basicElement->setAttribute("color3", "transparent");
    m_basicElement->setAttribute("color4", "#123");
}

void TestAttributeManager::testDocumentPointFontSize()
{
    BasicElement scaled(nullptr);
    scaled.setScaleLevel(0);
    QCOMPARE(scaled.scaleFactor(), 1.0);
    scaled.setScaleLevel(1);
    QCOMPARE(scaled.scaleFactor(), 0.71);
    StyleElement style;
    style.setAttribute("fontsize", "11pt");
    BasicElement inherited(&style);
    const QFont inheritedFont = m_attributeManager->font(&inherited);
    QVERIFY(qAbs(inheritedFont.pointSizeF() * QFontMetricsF(inheritedFont).fontDpi() / 72.0 - 11.0) < 0.01);
    inherited.setAttribute("mathvariant", "bold-italic");
    QVERIFY(m_attributeManager->font(&inherited).bold());
    QVERIFY(m_attributeManager->font(&inherited).italic());
    inherited.setAttribute("mathvariant", "normal");
    QVERIFY(!m_attributeManager->font(&inherited).italic());
    // QFontMetrics measures in device pixels; the formula scene uses points.
    BasicElement element(nullptr);
    for (const QString &size : {QString(), QStringLiteral("11pt"), QStringLiteral("16px"), QStringLiteral("1in")}) {
        element.setAttribute("fontsize", size);
        const qreal expected = size.isEmpty() ? 12.0 : size == QLatin1String("11pt") ? 11.0 : size == QLatin1String("16px") ? 16.0 : 72.0;
        for (qreal scale : {1.0, 0.71}) {
            element.setScaleFactor(scale);
            const QFont font = m_attributeManager->font(&element);
            const qreal actual = font.pointSizeF() * QFontMetricsF(font).fontDpi() / 72.0;
            QVERIFY(qAbs(actual - expected * scale) < 0.01);
        }
    }
}

QTEST_MAIN(TestAttributeManager)
