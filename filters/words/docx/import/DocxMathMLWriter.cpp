/* SPDX-License-Identifier: LGPL-2.1-only */

#include "DocxMathMLWriter.h"

#include <QBuffer>
#include <QDomDocument>
#include <QStringList>
#include <QXmlStreamReader>

#include <KoXmlWriter.h>

namespace
{
const QString MathNs = QStringLiteral("http://www.w3.org/1998/Math/MathML");
const QString OmmlNs = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/math");

QString localName(const QDomElement &element)
{
    return element.localName().isEmpty() ? element.tagName() : element.localName();
}

bool isOmmlElement(const QDomElement &element)
{
    return element.namespaceURI() == OmmlNs || element.tagName().startsWith(QLatin1String("m:"));
}

QString ommlAttribute(const QDomElement &element, const char *name)
{
    const QString value = element.attributeNS(OmmlNs, QLatin1String(name));
    if (!value.isNull()) {
        return value;
    }
    return element.attribute(QLatin1String("m:") + QLatin1String(name));
}

QDomElement firstChild(const QDomElement &parent, const char *name)
{
    for (QDomElement child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        if (isOmmlElement(child) && localName(child) == QLatin1String(name)) {
            return child;
        }
    }
    return QDomElement();
}

QList<QDomElement> childrenNamed(const QDomElement &parent, const char *name)
{
    QList<QDomElement> result;
    for (QDomElement child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        if (isOmmlElement(child) && localName(child) == QLatin1String(name)) {
            result.append(child);
        }
    }
    return result;
}

bool isPropertyElement(const QDomElement &element)
{
    const QString name = localName(element);
    return name.endsWith(QLatin1String("Pr")) || name == QLatin1String("ctrlPr") || name == QLatin1String("argPr");
}

QList<QDomElement> contentChildren(const QDomElement &parent)
{
    QList<QDomElement> result;
    for (QDomElement child = parent.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
        if (isOmmlElement(child) && !isPropertyElement(child)) {
            result.append(child);
        }
    }
    return result;
}

QString textOf(const QDomElement &element)
{
    QString text;
    for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (node.isText() || node.isCDATASection()) {
            text += node.nodeValue();
        } else if (node.isElement()) {
            text += textOf(node.toElement());
        }
    }
    return text;
}

enum class TokenKind {
    Identifier,
    Number,
    Operator,
    Text,
};

TokenKind classifyToken(const QString &text)
{
    bool allDigits = true;
    bool allLetters = true;
    bool hasSpace = false;
    for (const QChar character : text) {
        if (character.isSpace()) {
            hasSpace = true;
            allDigits = false;
            allLetters = false;
            continue;
        }
        if (!character.isDigit() && character != QLatin1Char('.') && character != QLatin1Char(',')) {
            allDigits = false;
        }
        if (!character.isLetter()) {
            allLetters = false;
        }
    }
    if (hasSpace) {
        return TokenKind::Text;
    }
    if (allDigits) {
        return TokenKind::Number;
    }
    if (allLetters) {
        return TokenKind::Identifier;
    }
    // Short runs of symbols are operators; longer mixed runs are literal text.
    return text.size() <= 3 ? TokenKind::Operator : TokenKind::Text;
}

QString mathVariant(const QDomElement &run)
{
    const QDomElement runProperties = firstChild(run, "rPr");
    if (runProperties.isNull()) {
        return QString();
    }
    const QDomElement script = firstChild(runProperties, "scr");
    if (!script.isNull()) {
        const QString value = ommlAttribute(script, "val");
        if (value == QLatin1String("script")) {
            return QStringLiteral("script");
        }
        if (value == QLatin1String("double-struck")) {
            return QStringLiteral("double-struck");
        }
        if (value == QLatin1String("fraktur")) {
            return QStringLiteral("fraktur");
        }
        if (value == QLatin1String("sans-serif")) {
            return QStringLiteral("sans-serif");
        }
        if (value == QLatin1String("monospace")) {
            return QStringLiteral("monospace");
        }
        if (value == QLatin1String("roman")) {
            return QStringLiteral("normal");
        }
    }
    const QDomElement style = firstChild(runProperties, "sty");
    if (!style.isNull()) {
        const QString value = ommlAttribute(style, "val");
        if (value == QLatin1String("p")) {
            return QStringLiteral("normal");
        }
        if (value == QLatin1String("b")) {
            return QStringLiteral("bold");
        }
        if (value == QLatin1String("i")) {
            return QStringLiteral("italic");
        }
        if (value == QLatin1String("bi")) {
            return QStringLiteral("bold-italic");
        }
    }
    return QString();
}

void writeNode(KoXmlWriter &writer, const QDomElement &element);

void writeRowContents(KoXmlWriter &writer, const QDomElement &parent)
{
    const QList<QDomElement> children = contentChildren(parent);
    for (const QDomElement &child : children) {
        writeNode(writer, child);
    }
}

void writeRow(KoXmlWriter &writer, const QDomElement &parent)
{
    writer.startElement("mrow");
    writeRowContents(writer, parent);
    writer.endElement(); // mrow
}

// Writes one named argument (m:e, m:num, ...) wrapped in an mrow, which is
// what MathML layout elements expect as a single child.
void writeArgument(KoXmlWriter &writer, const QDomElement &parent, const char *name)
{
    writer.startElement("mrow");
    const QList<QDomElement> arguments = childrenNamed(parent, name);
    for (const QDomElement &argument : arguments) {
        writeRowContents(writer, argument);
    }
    writer.endElement(); // mrow
}

void writeRun(KoXmlWriter &writer, const QDomElement &run)
{
    const QString text = textOf(run);
    if (text.isEmpty()) {
        return;
    }
    const QDomElement runProperties = firstChild(run, "rPr");
    const bool normalText = !runProperties.isNull() && !firstChild(runProperties, "nor").isNull();
    const TokenKind kind = normalText ? TokenKind::Text : classifyToken(text);
    const char *elementName = "mtext";
    if (kind == TokenKind::Identifier) {
        elementName = "mi";
    } else if (kind == TokenKind::Number) {
        elementName = "mn";
    } else if (kind == TokenKind::Operator) {
        elementName = "mo";
    }
    writer.startElement(elementName);
    const QString variant = mathVariant(run);
    if (!variant.isEmpty()) {
        writer.addAttribute("mathvariant", variant);
    }
    writer.addTextNode(text);
    writer.endElement(); // token element
}

void writeDelimiter(KoXmlWriter &writer, const QDomElement &element)
{
    const QDomElement properties = firstChild(element, "dPr");
    QString open = QStringLiteral("(");
    QString close = QStringLiteral(")");
    QString separators = QStringLiteral(",");
    if (!properties.isNull()) {
        const QDomElement begin = firstChild(properties, "begChr");
        const QDomElement end = firstChild(properties, "endChr");
        const QDomElement separator = firstChild(properties, "sepChr");
        if (!begin.isNull()) {
            open = ommlAttribute(begin, "val");
        }
        if (!end.isNull()) {
            close = ommlAttribute(end, "val");
        }
        if (!separator.isNull()) {
            separators = ommlAttribute(separator, "val");
        }
    }
    const QList<QDomElement> arguments = childrenNamed(element, "e");
    if (open.isEmpty() && close.isEmpty()) {
        writer.startElement("mrow");
        for (const QDomElement &argument : arguments) {
            writeRow(writer, argument);
        }
        writer.endElement(); // mrow
        return;
    }
    if (separators.isEmpty()) {
        separators = QStringLiteral(",");
    }
    writer.startElement("mfenced");
    writer.addAttribute("open", open);
    writer.addAttribute("close", close);
    writer.addAttribute("separators", separators);
    for (const QDomElement &argument : arguments) {
        writeRow(writer, argument);
    }
    writer.endElement(); // mfenced
}

void writeNary(KoXmlWriter &writer, const QDomElement &element)
{
    const QDomElement properties = firstChild(element, "naryPr");
    QString character = QStringLiteral("\u2211");
    bool subHidden = false;
    bool supHidden = false;
    bool limitsUnderOver = true;
    if (!properties.isNull()) {
        const QDomElement chr = firstChild(properties, "chr");
        if (!chr.isNull()) {
            const QString value = ommlAttribute(chr, "val");
            if (!value.isEmpty()) {
                character = value;
            }
        }
        const QDomElement location = firstChild(properties, "limLoc");
        if (!location.isNull() && ommlAttribute(location, "val") == QLatin1String("subSup")) {
            limitsUnderOver = false;
        }
        const QDomElement subHide = firstChild(properties, "subHide");
        const QDomElement supHide = firstChild(properties, "supHide");
        subHidden = !subHide.isNull() && ommlAttribute(subHide, "val") != QLatin1String("0")
            && ommlAttribute(subHide, "val") != QLatin1String("false");
        supHidden = !supHide.isNull() && ommlAttribute(supHide, "val") != QLatin1String("0")
            && ommlAttribute(supHide, "val") != QLatin1String("false");
    }

    writer.startElement("mrow");
    const bool hasSub = !subHidden && !firstChild(element, "sub").isNull();
    const bool hasSup = !supHidden && !firstChild(element, "sup").isNull();
    if (hasSub && hasSup) {
        writer.startElement(limitsUnderOver ? "munderover" : "msubsup");
    } else if (hasSub) {
        writer.startElement(limitsUnderOver ? "munder" : "msub");
    } else if (hasSup) {
        writer.startElement(limitsUnderOver ? "mover" : "msup");
    }
    writer.startElement("mo");
    writer.addTextNode(character);
    writer.endElement(); // mo
    if (hasSub) {
        writeArgument(writer, element, "sub");
    }
    if (hasSup) {
        writeArgument(writer, element, "sup");
    }
    if (hasSub || hasSup) {
        writer.endElement(); // script element
    }
    writeArgument(writer, element, "e");
    writer.endElement(); // mrow
}

void writeNode(KoXmlWriter &writer, const QDomElement &element)
{
    const QString name = localName(element);

    if (name == QLatin1String("r")) {
        writeRun(writer, element);
    } else if (name == QLatin1String("t")) {
        const QString text = textOf(element);
        if (!text.isEmpty()) {
            writer.startElement("mtext");
            writer.addTextNode(text);
            writer.endElement(); // mtext
        }
    } else if (name == QLatin1String("f")) {
        const QDomElement properties = firstChild(element, "fPr");
        bool noBar = false;
        if (!properties.isNull()) {
            const QDomElement type = firstChild(properties, "type");
            noBar = !type.isNull() && ommlAttribute(type, "val") == QLatin1String("noBar");
        }
        writer.startElement("mfrac");
        if (noBar) {
            writer.addAttribute("linethickness", "0");
        }
        writeArgument(writer, element, "num");
        writeArgument(writer, element, "den");
        writer.endElement(); // mfrac
    } else if (name == QLatin1String("sSub")) {
        writer.startElement("msub");
        writeArgument(writer, element, "e");
        writeArgument(writer, element, "sub");
        writer.endElement(); // msub
    } else if (name == QLatin1String("sSup")) {
        writer.startElement("msup");
        writeArgument(writer, element, "e");
        writeArgument(writer, element, "sup");
        writer.endElement(); // msup
    } else if (name == QLatin1String("sSubSup")) {
        writer.startElement("msubsup");
        writeArgument(writer, element, "e");
        writeArgument(writer, element, "sub");
        writeArgument(writer, element, "sup");
        writer.endElement(); // msubsup
    } else if (name == QLatin1String("sPre")) {
        writer.startElement("mmultiscripts");
        writeArgument(writer, element, "e");
        writer.startElement("mprescripts");
        writer.endElement(); // mprescripts
        writeArgument(writer, element, "sub");
        writeArgument(writer, element, "sup");
        writer.endElement(); // mmultiscripts
    } else if (name == QLatin1String("rad")) {
        const QDomElement properties = firstChild(element, "radPr");
        bool degreeHidden = false;
        if (!properties.isNull()) {
            const QDomElement degreeHide = firstChild(properties, "degHide");
            degreeHidden = !degreeHide.isNull() && ommlAttribute(degreeHide, "val") != QLatin1String("0")
                && ommlAttribute(degreeHide, "val") != QLatin1String("false");
        }
        const bool hasDegree = !degreeHidden && !firstChild(element, "deg").isNull();
        if (hasDegree) {
            writer.startElement("mroot");
            writeArgument(writer, element, "e");
            writeArgument(writer, element, "deg");
            writer.endElement(); // mroot
        } else {
            writer.startElement("msqrt");
            writeArgument(writer, element, "e");
            writer.endElement(); // msqrt
        }
    } else if (name == QLatin1String("nary")) {
        writeNary(writer, element);
    } else if (name == QLatin1String("acc")) {
        QString character = QStringLiteral("\u005e");
        const QDomElement properties = firstChild(element, "accPr");
        if (!properties.isNull()) {
            const QDomElement chr = firstChild(properties, "chr");
            if (!chr.isNull()) {
                const QString value = ommlAttribute(chr, "val");
                if (!value.isEmpty()) {
                    character = value;
                }
            }
        }
        writer.startElement("mover");
        writer.addAttribute("accent", "true");
        writeArgument(writer, element, "e");
        writer.startElement("mo");
        writer.addTextNode(character);
        writer.endElement(); // mo
        writer.endElement(); // mover
    } else if (name == QLatin1String("bar")) {
        bool bottom = false;
        const QDomElement properties = firstChild(element, "barPr");
        if (!properties.isNull()) {
            const QDomElement position = firstChild(properties, "pos");
            bottom = !position.isNull() && ommlAttribute(position, "val") == QLatin1String("bot");
        }
        writer.startElement(bottom ? "munder" : "mover");
        writer.addAttribute(bottom ? "accentunder" : "accent", "true");
        writeArgument(writer, element, "e");
        writer.startElement("mo");
        writer.addTextNode(QStringLiteral("\u00af"));
        writer.endElement(); // mo
        writer.endElement(); // munder/mover
    } else if (name == QLatin1String("d")) {
        writeDelimiter(writer, element);
    } else if (name == QLatin1String("func")) {
        writer.startElement("mrow");
        writeArgument(writer, element, "fName");
        writer.startElement("mo");
        writer.addTextNode(QStringLiteral("\u2061"));
        writer.endElement(); // mo
        writeArgument(writer, element, "e");
        writer.endElement(); // mrow
    } else if (name == QLatin1String("limLow")) {
        writer.startElement("munder");
        writeArgument(writer, element, "e");
        writeArgument(writer, element, "lim");
        writer.endElement(); // munder
    } else if (name == QLatin1String("limUpp")) {
        writer.startElement("mover");
        writeArgument(writer, element, "e");
        writeArgument(writer, element, "lim");
        writer.endElement(); // mover
    } else if (name == QLatin1String("m")) {
        writer.startElement("mtable");
        const QList<QDomElement> rows = childrenNamed(element, "mr");
        for (const QDomElement &row : rows) {
            writer.startElement("mtr");
            const QList<QDomElement> cells = childrenNamed(row, "e");
            for (const QDomElement &cell : cells) {
                writer.startElement("mtd");
                writeRow(writer, cell);
                writer.endElement(); // mtd
            }
            writer.endElement(); // mtr
        }
        writer.endElement(); // mtable
    } else if (name == QLatin1String("eqArr")) {
        writer.startElement("mtable");
        const QList<QDomElement> rows = childrenNamed(element, "e");
        for (const QDomElement &row : rows) {
            writer.startElement("mtr");
            writer.startElement("mtd");
            writeRow(writer, row);
            writer.endElement(); // mtd
            writer.endElement(); // mtr
        }
        writer.endElement(); // mtable
    } else if (name == QLatin1String("groupChr")) {
        QString character = QStringLiteral("\u23df");
        bool bottom = true;
        const QDomElement properties = firstChild(element, "groupChrPr");
        if (!properties.isNull()) {
            const QDomElement chr = firstChild(properties, "chr");
            const QDomElement position = firstChild(properties, "pos");
            if (!chr.isNull()) {
                const QString value = ommlAttribute(chr, "val");
                if (!value.isEmpty()) {
                    character = value;
                }
            }
            bottom = position.isNull() || ommlAttribute(position, "val") != QLatin1String("top");
        }
        writer.startElement(bottom ? "munder" : "mover");
        writeArgument(writer, element, "e");
        writer.startElement("mo");
        writer.addTextNode(character);
        writer.endElement(); // mo
        writer.endElement(); // munder/mover
    } else if (name == QLatin1String("oMath") || name == QLatin1String("oMathPara")) {
        writeRow(writer, element);
    } else {
        // Wrappers such as box, borderBox, phant, del, ins, moveFrom and
        // moveTo carry no math of their own: keep their contents.
        writeRow(writer, element);
    }
}

void appendElement(QXmlStreamReader &reader, QDomDocument &document, QDomElement parent)
{
    Q_ASSERT(reader.isStartElement());
    QDomElement element = document.createElementNS(reader.namespaceUri().toString(), reader.qualifiedName().toString());
    const QXmlStreamAttributes attributes = reader.attributes();
    for (const QXmlStreamAttribute &attribute : attributes) {
        element.setAttributeNS(attribute.namespaceUri().toString(), attribute.qualifiedName().toString(), attribute.value().toString());
    }
    parent.appendChild(element);

    const bool keepsWhitespace = localName(element) == QLatin1String("t");
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement()) {
            break;
        }
        if (reader.isStartElement()) {
            appendElement(reader, document, element);
        } else if (reader.isCharacters() && (keepsWhitespace || !reader.isWhitespace())) {
            element.appendChild(document.createTextNode(reader.text().toString()));
        }
    }
}
}

namespace DocxMathMLWriter
{
QByteArray readMathML(QXmlStreamReader &reader)
{
    if (!reader.isStartElement()) {
        return QByteArray();
    }

    QDomDocument document;
    QDomElement root = document.createElementNS(OmmlNs, reader.qualifiedName().toString());
    document.appendChild(root);
    appendElement(reader, document, root);

    QByteArray result;
    QBuffer buffer(&result);
    if (!buffer.open(QIODevice::WriteOnly)) {
        return QByteArray();
    }
    KoXmlWriter writer(&buffer);
    writer.startDocument("math");
    writer.startElement("math");
    writer.addAttribute("xmlns", MathNs);
    writeRowContents(writer, root);
    writer.endElement(); // math
    writer.endDocument();
    return result;
}
}
