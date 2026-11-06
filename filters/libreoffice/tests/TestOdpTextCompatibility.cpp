// SPDX-License-Identifier: LGPL-2.0-or-later
#include <QCoreApplication>
#include <QDomDocument>
#include <QMap>
#include <QXmlStreamReader>
#include "../OdpTextCompatibility.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString draw = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:drawing:1.0");
    const QString text = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0");
    QDomDocument content, styles;
    const QByteArray namespaces = " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\"";
    if (!styles.setContent("<root" + namespaces + "><style:style style:name=\"fit\"><style:graphic-properties style:shrink-to-fit=\"true\"/></style:style><style:style style:name=\"inherited\" style:parent-style-name=\"fit\"/><style:style style:name=\"plain\"/><style:style style:name=\"cycle\" style:parent-style-name=\"cycle\"/></root>", QDomDocument::ParseOption::UseNamespaceProcessing)) return 1;
    if (!content.setContent("<root" + namespaces + "><draw:custom-shape xml:id=\"shape1\" draw:id=\"shape1\" draw:name=\"fitted\" draw:style-name=\"inherited\" svg:x=\"4cm\" svg:width=\"8cm\"><text:p>Before <text:span>equation</text:span><text:s text:c=\"3\"/></text:p><draw:enhanced-geometry draw:type=\"ooxml-rect\"/></draw:custom-shape><draw:custom-shape draw:style-name=\"fit\"><text:p>Rounded</text:p><draw:enhanced-geometry draw:type=\"ooxml-roundRect\"/></draw:custom-shape><draw:custom-shape draw:style-name=\"plain\"><text:p>Plain</text:p><draw:enhanced-geometry draw:type=\"ooxml-rect\"/></draw:custom-shape><draw:custom-shape draw:style-name=\"cycle\"><text:p>Cycle</text:p><draw:enhanced-geometry draw:type=\"ooxml-rect\"/></draw:custom-shape></root>", QDomDocument::ParseOption::UseNamespaceProcessing)) return 1;
    const QString styleNS = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:style:1.0");
    auto growingStyle = styles.createElementNS(styleNS, QStringLiteral("style:style"));
    growingStyle.setAttributeNS(styleNS, QStringLiteral("style:name"), QStringLiteral("growing"));
    growingStyle.setAttributeNS(styleNS, QStringLiteral("style:family"), QStringLiteral("graphic"));
    auto growingProps = styles.createElementNS(styleNS, QStringLiteral("style:graphic-properties"));
    growingProps.setAttributeNS(draw, QStringLiteral("draw:auto-grow-height"), QStringLiteral("true"));
    growingProps.setAttributeNS(draw, QStringLiteral("draw:fill-color"), QStringLiteral("#196b24"));
    growingStyle.appendChild(growingProps); styles.documentElement().appendChild(growingStyle);
    auto growing = content.documentElement().firstChildElement().cloneNode(true).toElement();
    growing.setAttributeNS(draw, QStringLiteral("draw:name"), QStringLiteral("growing"));
    growing.setAttributeNS(draw, QStringLiteral("draw:style-name"), QStringLiteral("growing"));
    growing.removeAttributeNS(QStringLiteral("http://www.w3.org/XML/1998/namespace"), QStringLiteral("id"));
    growing.removeAttributeNS(draw, QStringLiteral("id"));
    content.documentElement().appendChild(growing);
    auto existingFrame = content.createElementNS(draw, QStringLiteral("draw:frame"));
    existingFrame.setAttributeNS(draw, QStringLiteral("draw:style-name"), QStringLiteral("growing"));
    auto existingBox = content.createElementNS(draw, QStringLiteral("draw:text-box"));
    auto existingText = content.createElementNS(text, QStringLiteral("text:p"));
    existingText.appendChild(content.createTextNode(QStringLiteral("Existing text frame")));
    existingBox.appendChild(existingText); existingFrame.appendChild(existingBox); content.documentElement().appendChild(existingFrame);
    const QString svgNS = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0");
    const QString foNS = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0");
    auto layout = styles.createElementNS(styleNS, QStringLiteral("style:page-layout"));
    layout.setAttributeNS(styleNS, QStringLiteral("style:name"), QStringLiteral("pageLayout"));
    auto pageProps = styles.createElementNS(styleNS, QStringLiteral("style:page-layout-properties"));
    pageProps.setAttributeNS(foNS, QStringLiteral("fo:page-height"), QStringLiteral("10in")); layout.appendChild(pageProps); styles.documentElement().appendChild(layout);
    auto master = styles.createElementNS(styleNS, QStringLiteral("style:master-page"));
    master.setAttributeNS(styleNS, QStringLiteral("style:name"), QStringLiteral("master"));
    master.setAttributeNS(styleNS, QStringLiteral("style:page-layout-name"), QStringLiteral("pageLayout")); styles.documentElement().appendChild(master);
    auto page = content.createElementNS(draw, QStringLiteral("draw:page"));
    page.setAttributeNS(draw, QStringLiteral("draw:master-page-name"), QStringLiteral("master"));
    existingFrame.setAttributeNS(draw, QStringLiteral("draw:style-name"), QStringLiteral("fit"));
    existingFrame.setAttributeNS(svgNS, QStringLiteral("svg:y"), QStringLiteral("8in"));
    existingFrame.setAttributeNS(svgNS, QStringLiteral("svg:height"), QStringLiteral("4in"));
    page.appendChild(existingFrame); content.documentElement().appendChild(page);
    if (!normalizeRectangularTextFrames(content, styles)) qFatal("Fitted text box not normalized");
    const auto frames = content.elementsByTagNameNS(draw, QStringLiteral("frame"));
    if (frames.size() != 3) qFatal("Fitted and auto-sized rectangles should become frames");
    const auto frame = frames.at(0).toElement();
    if (frame.attributeNS(draw, QStringLiteral("name")) != QLatin1String("fitted") || frame.attributeNS(QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"), QStringLiteral("x")) != QLatin1String("4cm")) qFatal("Frame attributes lost");
    if (frame.elementsByTagNameNS(draw, QStringLiteral("text-box")).size() != 1 || frame.text() != QLatin1String("Before equation")
        || frame.elementsByTagNameNS(text, QStringLiteral("s")).size() != 1) qFatal("Text markup lost");
    if (existingFrame.attributeNS(svgNS, QStringLiteral("height")) != QLatin1String("144pt")) qFatal("Text below the slide was not fitted to visible bounds");
    const auto newStyles = content.elementsByTagNameNS(styleNS, QStringLiteral("style"));
    if (newStyles.size() != 1) qFatal("Auto-sized frame needs an independent fitting style");
    const auto fixedProperties = newStyles.at(0).firstChildElement();
    if (fixedProperties.attributeNS(draw, QStringLiteral("fill-color")) != QLatin1String("#196b24")
        || fixedProperties.attributeNS(draw, QStringLiteral("fit-to-size")) != QLatin1String("true")) qFatal("Auto-sized frame appearance lost");
    if (content.elementsByTagNameNS(draw, QStringLiteral("custom-shape")).size() != 3) qFatal("Unrelated shapes changed");
    if (normalizeRectangularTextFrames(content, styles)) qFatal("Normalization must be idempotent");
    QXmlStreamReader serialized(content.toByteArray());
    while (!serialized.atEnd()) serialized.readNext();
    if (serialized.hasError()) qFatal("Serialized XML is invalid: %s", qPrintable(serialized.errorString()));
    qInfo("PASS: inherited text fit, geometry, markup, non-rectangles, cyclic styles and idempotence");
}
