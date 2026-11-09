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
    // Stacked constructs keep their linear spelling, scripts stay small and an
    // unmodelled structure never swallows the surrounding expression.
    const QByteArray structured = R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
        xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
        xmlns:math="http://schemas.microsoft.com/office/drawing/2010/main"
        xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math">
        <p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:rPr sz="1800"/><a:t>base</a:t></a:r><math:m><m:oMath>
        <m:sSup><m:e><m:r><m:t>x</m:t></m:r></m:e><m:sup><m:r><m:t>2</m:t></m:r></m:sup></m:sSup>
        <m:f><m:num><m:r><m:t>1</m:t></m:r></m:num><m:den><m:r><m:t>2</m:t></m:r></m:den></m:f>
        <m:rad><m:e><m:r><m:t>y</m:t></m:r></m:e></m:rad>
        <m:nary><m:naryPr><m:chr m:val="∑"/><m:supHide m:val="1"/></m:naryPr><m:sub><m:r><m:t>k</m:t></m:r></m:sub>
        <m:sup><m:r><m:t>9</m:t></m:r></m:sup><m:e><m:r><m:t>z</m:t></m:r></m:e></m:nary>
        <m:unmodelled><m:r><m:t>!</m:t></m:r></m:unmodelled>
        </m:oMath></math:m></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>)";
    QDomDocument formulas;
    if (!formulas.setContent(structured, QDomDocument::ParseOption::UseNamespaceProcessing) || !preparePptxMath(formulas)) return 20;
    const auto texts = formulas.elementsByTagNameNS(QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main"), QStringLiteral("t"));
    QString flattened;
    bool scriptSized = false;
    bool literalsInherit = true;
    for (int i = 0; i < texts.size(); ++i) {
        const auto text = texts.at(i).toElement();
        flattened += text.text();
        const auto properties = text.parentNode().firstChildElement();
        // The exponent "2" is raised and shrunk to 65% of the 18pt base run.
        if (text.text() == QLatin1String("2") && properties.attribute(QStringLiteral("baseline")) == QLatin1String("30000"))
            scriptSized = properties.attribute(QStringLiteral("sz")) == QLatin1String("1170");
        // Math without run properties takes the size of the paragraph text, so
        // the parentheses around the root are not larger than the formula.
        if ((text.text() == QLatin1String("(") || text.text() == QLatin1String(")"))
            && properties.attribute(QStringLiteral("sz")) != QLatin1String("1800"))
            literalsInherit = false;
    }
    if (flattened != QString::fromUtf8("basex21/2\u221a(y)\u2211k z!")) return 21;
    if (!scriptSized) return 22;
    if (!literalsInherit) return 24;
    if (flattened.contains(QLatin1String("9"))) return 23;
    // Punctuation the flattener inserts belongs to the expression: it carries
    // the same size, weight, slant and colour as the terms around it instead
    // of falling back to the paragraph default.
    const QString ans = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
    const QByteArray styled = R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
        xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
        xmlns:math="http://schemas.microsoft.com/office/drawing/2010/main"
        xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math">
        <p:cSld><p:spTree><p:sp><p:txBody><a:p><math:m><m:oMath>
        <m:r><a:rPr sz="1600" b="1" i="1"><a:solidFill><a:srgbClr val="1F497D"/></a:solidFill><a:latin typeface="Cambria Math"/></a:rPr><m:t>x</m:t></m:r>
        <m:f><m:num><m:r><a:rPr sz="1600" b="1" i="1"><a:solidFill><a:srgbClr val="1F497D"/></a:solidFill><a:latin typeface="Cambria Math"/></a:rPr><m:t>100</m:t></m:r></m:num>
        <m:den><m:r><a:rPr sz="1600" b="1" i="1"><a:solidFill><a:srgbClr val="1F497D"/></a:solidFill><a:latin typeface="Cambria Math"/></a:rPr><m:t>200</m:t></m:r></m:den></m:f>
        </m:oMath></math:m></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>)";
    QDomDocument styledDoc;
    if (!styledDoc.setContent(styled, QDomDocument::ParseOption::UseNamespaceProcessing) || !preparePptxMath(styledDoc)) return 25;
    const auto styledRuns = styledDoc.elementsByTagNameNS(ans, QStringLiteral("r"));
    int literalCount = 0;
    for (int i = 0; i < styledRuns.size(); ++i) {
        const auto run = styledRuns.at(i).toElement();
        const auto value = run.elementsByTagNameNS(ans, QStringLiteral("t")).at(0).toElement().text();
        if (value != QLatin1String("(") && value != QLatin1String(")") && value != QLatin1String("/")) continue;
        ++literalCount;
        const auto properties = run.elementsByTagNameNS(ans, QStringLiteral("rPr")).at(0).toElement();
        if (properties.attribute(QStringLiteral("sz")) != QLatin1String("1600")
            || properties.attribute(QStringLiteral("b")) != QLatin1String("1")
            || properties.attribute(QStringLiteral("i")) != QLatin1String("1")) return 26;
        const auto colors = properties.elementsByTagNameNS(ans, QStringLiteral("srgbClr"));
        if (colors.isEmpty() || colors.at(0).toElement().attribute(QStringLiteral("val")) != QLatin1String("1F497D")) return 27;
    }
    if (literalCount != 5) return 28;
    // Inline math follows the size of the sentence it sits in. A pasted
    // expression can carry a stale 32pt size inside 28pt text, and a stray
    // enlarged space must not be mistaken for the sentence's size.
    const QByteArray inlineMath = R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
        xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"
        xmlns:math="http://schemas.microsoft.com/office/drawing/2010/main"
        xmlns:m="http://schemas.openxmlformats.org/officeDocument/2006/math">
        <p:cSld><p:spTree><p:sp><p:txBody><a:p>
        <a:r><a:rPr sz="2800"/><a:t>The value </a:t></a:r>
        <math:m><m:oMath><m:sSub>
        <m:e><m:r><a:rPr sz="3200" b="1" i="1"/><m:t>N</m:t></m:r></m:e>
        <m:sub><m:r><a:rPr sz="3200" b="1" i="1"/><m:t>y</m:t></m:r></m:sub>
        </m:sSub></m:oMath></math:m>
        <a:r><a:rPr sz="4000"/><a:t> </a:t></a:r>
        <a:r><a:t>is shown</a:t></a:r>
        </a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>)";
    QDomDocument inlineDoc;
    if (!inlineDoc.setContent(inlineMath, QDomDocument::ParseOption::UseNamespaceProcessing)
        || !preparePptxMath(inlineDoc)) return 29;
    int baseRuns = 0;
    int scriptRuns = 0;
    const auto inlineRuns = inlineDoc.elementsByTagNameNS(ans, QStringLiteral("r"));
    for (int i = 0; i < inlineRuns.size(); ++i) {
        const auto run = inlineRuns.at(i).toElement();
        const auto value = run.elementsByTagNameNS(ans, QStringLiteral("t")).at(0).toElement().text();
        if (value != QLatin1String("N") && value != QLatin1String("y")) continue;
        const auto properties = run.elementsByTagNameNS(ans, QStringLiteral("rPr")).at(0).toElement();
        if (properties.attribute(QStringLiteral("baseline")).isEmpty()) {
            ++baseRuns;
            if (properties.attribute(QStringLiteral("sz")) != QLatin1String("2800")) return 30;
        } else {
            ++scriptRuns;
            if (properties.attribute(QStringLiteral("sz")) != QLatin1String("1820")) return 31;
        }
    }
    if (baseRuns != 1 || scriptRuns != 1) return 32;
    QDomDocument rotated;
    const QByteArray rectangle = R"(<p:sld xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"
        xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"><p:sp><p:spPr>
        <a:xfrm rot="16200000"><a:off x="100" y="100"/><a:ext cx="200" cy="400"/></a:xfrm>
        <a:prstGeom prst="rect"/><a:gradFill><a:lin ang="10800000"/></a:gradFill>
        </p:spPr></p:sp></p:sld>)";
    if (!rotated.setContent(rectangle, QDomDocument::ParseOption::UseNamespaceProcessing)
        || !preparePptxGradientRectangles(rotated)) return 8;
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
