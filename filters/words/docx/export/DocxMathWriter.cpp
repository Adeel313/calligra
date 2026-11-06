/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "DocxMathWriter.h"

#include <QBuffer>
#include <QDomDocument>
#include <QStringList>

#include <KoXmlWriter.h>

namespace
{
const QString MathNs = QStringLiteral("http://www.w3.org/1998/Math/MathML");

QString localName(const QDomElement &element)
{
    return element.localName().isEmpty() ? element.tagName() : element.localName();
}

bool isMathElement(const QDomElement &element)
{
    return element.namespaceURI() == MathNs || element.tagName().startsWith(QLatin1String("m:"));
}

QList<QDomElement> childElements(const QDomElement &parent)
{
    QList<QDomElement> children;
    for (QDomElement child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        if (isMathElement(child)) {
            children.append(child);
        }
    }
    return children;
}

QDomElement firstMathChild(const QDomElement &parent)
{
    const QList<QDomElement> children = childElements(parent);
    return children.isEmpty() ? QDomElement() : children.first();
}

void writeTextRun(KoXmlWriter &writer, const QString &text, bool upright)
{
    if (text.isEmpty()) {
        return;
    }
    writer.startElement("m:r");
    if (upright) {
        writer.startElement("m:rPr");
        writer.startElement("m:sty");
        writer.addAttribute("m:val", "p");
        writer.endElement(); // m:sty
        writer.endElement(); // m:rPr
    }
    writer.startElement("m:t");
    writer.addAttribute("xml:space", "preserve");
    writer.addTextNode(text);
    writer.endElement(); // m:t
    writer.endElement(); // m:r
}

void writeNode(KoXmlWriter &writer, const QDomElement &element);

void writeChildren(KoXmlWriter &writer, const QDomElement &parent)
{
    const QList<QDomElement> children = childElements(parent);
    for (const QDomElement &child : children) {
        writeNode(writer, child);
    }
}

// Writes the content of an argument element (m:e, m:num, ...). MathML nests
// the actual expression in an mrow, which has no OMML counterpart, so its
// children are emitted directly.
void writeArgument(KoXmlWriter &writer, const QString &name, const QDomElement &argument)
{
    const QByteArray elementName = name.toUtf8();
    writer.startElement(elementName.constData());
    if (!argument.isNull()) {
        const QList<QDomElement> children = childElements(argument);
        if (children.size() == 1 && localName(children.first()) == QLatin1String("mrow")) {
            writeChildren(writer, children.first());
        } else {
            for (const QDomElement &child : children) {
                writeNode(writer, child);
            }
        }
    }
    writer.endElement(); // name
}

QString elementText(const QDomElement &element)
{
    QString text;
    for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (node.isText() || node.isCDATASection()) {
            text += node.nodeValue();
        } else if (node.isElement()) {
            const QDomElement child = node.toElement();
            if (localName(child) == QLatin1String("mglyph") || localName(child) == QLatin1String("none")) {
                continue;
            }
            text += elementText(child);
        }
    }
    return text;
}

void writeNode(KoXmlWriter &writer, const QDomElement &element)
{
    const QString name = localName(element);

    if (name == QLatin1String("semantics")) {
        // The first child is the presentation MathML; the rest is annotation.
        const QDomElement presentation = firstMathChild(element);
        if (!presentation.isNull()) {
            writeNode(writer, presentation);
        }
    } else if (name == QLatin1String("mrow") || name == QLatin1String("mstyle") || name == QLatin1String("mpadded")
               || name == QLatin1String("menclose") || name == QLatin1String("merror")) {
        writeChildren(writer, element);
    } else if (name == QLatin1String("mi") || name == QLatin1String("mn") || name == QLatin1String("mo")
               || name == QLatin1String("mtext") || name == QLatin1String("ms")) {
        // Identifiers stay italic, numbers and operators are upright.
        writeTextRun(writer, elementText(element), name != QLatin1String("mi"));
    } else if (name == QLatin1String("mspace")) {
        writeTextRun(writer, QStringLiteral(" "), true);
    } else if (name == QLatin1String("mfrac")) {
        const QList<QDomElement> children = childElements(element);
        writer.startElement("m:f");
        writeArgument(writer, "m:num", children.value(0));
        writeArgument(writer, "m:den", children.value(1));
        writer.endElement(); // m:f
    } else if (name == QLatin1String("msup") || name == QLatin1String("mover")) {
        const QList<QDomElement> children = childElements(element);
        writer.startElement(name == QLatin1String("mover") ? "m:limUpp" : "m:sSup");
        writeArgument(writer, "m:e", children.value(0));
        writeArgument(writer, name == QLatin1String("mover") ? "m:lim" : "m:sup", children.value(1));
        writer.endElement();
    } else if (name == QLatin1String("msub") || name == QLatin1String("munder")) {
        const QList<QDomElement> children = childElements(element);
        writer.startElement(name == QLatin1String("munder") ? "m:limLow" : "m:sSub");
        writeArgument(writer, "m:e", children.value(0));
        writeArgument(writer, name == QLatin1String("munder") ? "m:lim" : "m:sub", children.value(1));
        writer.endElement();
    } else if (name == QLatin1String("msubsup") || name == QLatin1String("munderover")) {
        const QList<QDomElement> children = childElements(element);
        writer.startElement("m:sSubSup");
        writeArgument(writer, "m:e", children.value(0));
        writeArgument(writer, "m:sub", children.value(1));
        writeArgument(writer, "m:sup", children.value(2));
        writer.endElement();
    } else if (name == QLatin1String("msqrt")) {
        writer.startElement("m:rad");
        writer.startElement("m:radPr");
        writer.startElement("m:degHide");
        writer.addAttribute("m:val", "1");
        writer.endElement(); // m:degHide
        writer.endElement(); // m:radPr
        writer.startElement("m:deg");
        writer.endElement(); // m:deg
        writeArgument(writer, "m:e", element);
        writer.endElement(); // m:rad
    } else if (name == QLatin1String("mroot")) {
        const QList<QDomElement> children = childElements(element);
        writer.startElement("m:rad");
        writeArgument(writer, "m:deg", children.value(1));
        writeArgument(writer, "m:e", children.value(0));
        writer.endElement(); // m:rad
    } else if (name == QLatin1String("mfenced")) {
        const QString open = element.attribute(QStringLiteral("open"), QStringLiteral("("));
        const QString close = element.attribute(QStringLiteral("close"), QStringLiteral(")"));
        const QString separators = element.attribute(QStringLiteral("separators"), QStringLiteral(","));
        writer.startElement("m:d");
        writer.startElement("m:dPr");
        if (!open.isEmpty()) {
            writer.startElement("m:begChr");
            writer.addAttribute("m:val", open);
            writer.endElement();
        }
        if (!close.isEmpty()) {
            writer.startElement("m:endChr");
            writer.addAttribute("m:val", close);
            writer.endElement();
        }
        if (!separators.isEmpty()) {
            writer.startElement("m:sepChr");
            writer.addAttribute("m:val", separators.left(1));
            writer.endElement();
        }
        writer.endElement(); // m:dPr
        writeArgument(writer, "m:e", element);
        writer.endElement(); // m:d
    } else if (name == QLatin1String("mtable")) {
        writer.startElement("m:m");
        for (const QDomElement &row : childElements(element)) {
            if (localName(row) != QLatin1String("mtr")) {
                continue;
            }
            writer.startElement("m:mr");
            for (const QDomElement &cell : childElements(row)) {
                if (localName(cell) != QLatin1String("mtd")) {
                    continue;
                }
                writeArgument(writer, "m:e", cell);
            }
            writer.endElement(); // m:mr
        }
        writer.endElement(); // m:m
    } else if (name == QLatin1String("mphantom")) {
        // Keep the content; OMML has no direct phantom equivalent.
        writeChildren(writer, element);
    } else if (name == QLatin1String("annotation") || name == QLatin1String("annotation-xml")) {
        return;
    } else {
        // Unknown construct: keep its text/children instead of dropping it.
        const QList<QDomElement> children = childElements(element);
        if (children.isEmpty()) {
            writeTextRun(writer, elementText(element), true);
        } else {
            for (const QDomElement &child : children) {
                writeNode(writer, child);
            }
        }
    }
}
}

QByteArray mathmlToOmml(const QByteArray &mathml)
{
    if (mathml.isEmpty()) {
        return {};
    }
    QDomDocument document;
    if (!document.setContent(mathml, QDomDocument::ParseOption::UseNamespaceProcessing)) {
        return {};
    }
    const QDomElement root = document.documentElement();
    if (localName(root) != QLatin1String("math") || root.namespaceURI() != MathNs) {
        return {};
    }

    QByteArray result;
    QBuffer buffer(&result);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return {};
    }
    KoXmlWriter writer(&buffer);
    writer.startElement("m:oMath");
    writer.addAttribute("xmlns:m", "http://schemas.openxmlformats.org/officeDocument/2006/math");
    writeChildren(writer, root);
    writer.endElement(); // m:oMath
    return result;
}
