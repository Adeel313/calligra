/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include "../NativePptxPreparation.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir workspace;
    if (!workspace.isValid()) return 1;
    const QString path = workspace.filePath(QStringLiteral("math.pptx"));
    const QByteArray slide = R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
        xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
        xmlns:math="http://schemas.microsoft.com/office/drawing/2010/main"
        xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math">
        <p:cSld><p:spTree><p:sp><p:txBody><a:p><math:m><m:oMath><m:r><m:t>FORMULA</m:t></m:r><m:groupChr><m:groupChrPr><m:chr m:val="⇔"/></m:groupChrPr><m:e/></m:groupChr><m:d><m:e><m:r><m:t>x</m:t></m:r></m:e></m:d></m:oMath></math:m></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>)";
    const QByteArray ordinary = R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"><p:cSld><p:spTree/></p:cSld></p:sld>)";
    const QByteArray rels = R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="layout" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout" Target="../slideLayouts/slideLayout1.xml"/></Relationships>)";
    KZip output(path);
    if (!output.open(QIODevice::WriteOnly)
        || !output.writeFile(QStringLiteral("ppt/slides/slide1.xml"), slide)
        || !output.writeFile(QStringLiteral("ppt/slides/slide2.xml"), slide)
        || !output.writeFile(QStringLiteral("ppt/slides/_rels/slide2.xml.rels"), rels)
        || !output.writeFile(QStringLiteral("ppt/slides/slide3.xml"), ordinary)
        || !output.close() || !preparePptxDiagrams(path)) return 2;
    KZip input(path);
    if (!input.open(QIODevice::ReadOnly)) return 3;
    if (input.directory()->entry(QStringLiteral("ppt/slides/_rels/slide1.xml.rels"))) {
        qCritical("Math normalization created an empty relationships part");
        return 4;
    }
    const auto read = [&](const QString &name) {
        const auto *entry = dynamic_cast<const KArchiveFile *>(input.directory()->entry(name));
        return entry ? entry->data() : QByteArray();
    };
    if (read(QStringLiteral("ppt/slides/_rels/slide2.xml.rels")) != rels
        || read(QStringLiteral("ppt/slides/slide3.xml")) != ordinary) return 5;
    for (const QString &name : {QStringLiteral("ppt/slides/slide1.xml"), QStringLiteral("ppt/slides/slide2.xml")}) {
        QDomDocument document;
        if (!document.setContent(read(name), QDomDocument::ParseOption::UseNamespaceProcessing)) return 6;
        const auto texts = document.elementsByTagNameNS(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("t"));
        QString actual;
        for (int i = 0; i < texts.size(); ++i) actual += texts.at(i).toElement().text();
        if (actual != QString::fromUtf8("FORMULA⇔(x)")) return 7;
    }
    QDomDocument rotated;
    const QByteArray rectangle = R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
        xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"><p:sp><p:spPr>
        <a:xfrm rot="16200000"><a:off x="100" y="100"/><a:ext cx="200" cy="400"/></a:xfrm>
        <a:prstGeom prst="rect"/><a:gradFill><a:lin ang="10800000"/></a:gradFill>
        </p:spPr></p:sp></p:sld>)";
    if (!rotated.setContent(rectangle, QDomDocument::ParseOption::UseNamespaceProcessing)
        || !preparePptxGradientRectangles(rotated)) return 8;
    const QString ans = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
    auto transform = rotated.elementsByTagNameNS(ans, QStringLiteral("xfrm")).at(0).toElement();
    const auto offset = rotated.elementsByTagNameNS(ans, QStringLiteral("off")).at(0).toElement();
    const auto extent = rotated.elementsByTagNameNS(ans, QStringLiteral("ext")).at(0).toElement();
    const auto linear = rotated.elementsByTagNameNS(ans, QStringLiteral("lin")).at(0).toElement();
    if (transform.hasAttribute(QStringLiteral("rot")) || offset.attribute(QStringLiteral("x")) != QLatin1String("0")
        || offset.attribute(QStringLiteral("y")) != QLatin1String("200") || extent.attribute(QStringLiteral("cx")) != QLatin1String("400")
        || extent.attribute(QStringLiteral("cy")) != QLatin1String("200") || linear.attribute(QStringLiteral("ang")) != QLatin1String("5400000")) return 9;
    auto text = rotated.createElementNS(ans, QStringLiteral("a:t"));
    text.appendChild(rotated.createTextNode(QStringLiteral("Keep text orientation")));
    rotated.documentElement().firstChildElement().appendChild(text);
    transform.setAttribute(QStringLiteral("rot"), QStringLiteral("5400000"));
    if (preparePptxGradientRectangles(rotated)) return 10;
    const QString gradientPath = workspace.filePath(QStringLiteral("gradient-only.pptx"));
    KZip gradientPackage(gradientPath);
    if (!gradientPackage.open(QIODevice::WriteOnly)
        || !gradientPackage.writeFile(QStringLiteral("ppt/slides/slide1.xml"), rectangle)
        || !gradientPackage.close() || !preparePptxDiagrams(gradientPath)) return 11;
    KZip preparedGradient(gradientPath);
    if (!preparedGradient.open(QIODevice::ReadOnly)) return 12;
    const auto *preparedPart = dynamic_cast<const KArchiveFile *>(preparedGradient.directory()->entry(QStringLiteral("ppt/slides/slide1.xml")));
    QDomDocument packageDocument;
    if (!preparedPart || !packageDocument.setContent(preparedPart->data(), QDomDocument::ParseOption::UseNamespaceProcessing)) return 13;
    if (packageDocument.elementsByTagNameNS(ans, QStringLiteral("xfrm")).at(0).toElement().hasAttribute(QStringLiteral("rot"))) return 14;
    qInfo("PASS: math, relationships, ordinary slides, cardinal gradient bounds/direction and text-bearing rotation preservation");
    return 0;
}
