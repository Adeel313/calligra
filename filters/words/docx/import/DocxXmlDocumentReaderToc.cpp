// SPDX-License-Identifier: LGPL-2.1-only
#include "DocxXmlDocumentReader.h"

#include <KoGenStyles.h>
#include <KoUnit.h>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
KoGenStyle resolvedStyle(const KoGenStyle &source, KoGenStyles *styles, const QByteArray &family)
{
    QList<const KoGenStyle *> chain;
    const KoGenStyle *style = &source;
    while (style && chain.size() < 20) {
        chain.prepend(style);
        style = style->parentName().isEmpty() ? nullptr : styles->style(style->parentName(), family);
    }
    KoGenStyle result(KoGenStyle::ParagraphStyle, "paragraph");
    for (const auto *ancestor : chain) {
        KoGenStyle::copyPropertiesFromStyle(*ancestor, result, KoGenStyle::ParagraphType);
        KoGenStyle::copyPropertiesFromStyle(*ancestor, result, KoGenStyle::TextType);
        const QString tabs = ancestor->childProperty("style:tab-stops", KoGenStyle::ParagraphType);
        if (!tabs.isEmpty()) result.addChildElement("style:tab-stops", tabs, KoGenStyle::ParagraphType);
    }
    return result;
}
}

void DocxXmlDocumentReader::beginToc()
{
    m_startToc = false;
    const auto levels = QRegularExpression(QStringLiteral("\\\\o\\s+\"?([1-9])-([1-9])")).match(m_tocInstruction);
    if (levels.hasMatch()) {
        m_tocLevels = levels.captured(2).toInt();
    }
    m_tocHyperlinks = m_tocInstruction.contains(QRegularExpression(QStringLiteral("\\\\h(?:\\s|$)")));
    m_tocPageNumbers = !m_tocInstruction.contains(QRegularExpression(QStringLiteral("\\\\n(?:\\s|$)")));

    // Word uses \t "Style,Level,..." instead of \o when the ToC is built from
    // custom styles. Keep the raw mapping so the exported field instruction
    // still tells Word which styles to use when the ToC is refreshed.
    m_tocSourceStyles.clear();
    const auto sourceStyles = QRegularExpression(QStringLiteral("\\\\t\\s+\"([^\"]+)\"")).match(m_tocInstruction);
    if (sourceStyles.hasMatch()) {
        m_tocSourceStyles = sourceStyles.captured(1);
    }
    m_tocEntryFormatting.clear();
    m_tocParentWriter = body;
    m_tocBodyBuffer.setData(QByteArray());
    m_tocBodyBuffer.open(QIODevice::WriteOnly);
    m_tocBodyWriter = std::make_unique<KoXmlWriter>(&m_tocBodyBuffer);
    body = m_tocBodyWriter.get();
    body->startElement("text:index-body");
}

void DocxXmlDocumentReader::collectTocEntry(const QString &paragraphXml)
{
    // Capture the entry text's formatting, not the field marker or page-number
    // run. Direct formatting is common in Word-generated cached TOCs.
    QXmlStreamReader xml(QStringLiteral("<root>") + paragraphXml + QStringLiteral("</root>"));
    xml.setNamespaceProcessing(false);
    QStringList spanStyles;
    QString textStyleName;
    bool hasText = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.qualifiedName() == QLatin1StringView("text:span")) {
            spanStyles.append(xml.attributes().value(QStringLiteral("text:style-name")).toString());
        } else if (xml.isEndElement() && xml.qualifiedName() == QLatin1StringView("text:span") && !spanStyles.isEmpty()) {
            spanStyles.removeLast();
        } else if (xml.isCharacters() && !xml.isWhitespace()) {
            hasText = true;
            if (!spanStyles.isEmpty()) textStyleName = spanStyles.constLast();
            break;
        }
    }
    if (!hasText) return;

    TocEntryFormatting entry;
    entry.style = resolvedStyle(m_currentParagraphStyle, mainStyles, "paragraph");
    if (const auto *textStyle = mainStyles->style(textStyleName, "text")) {
        const auto resolved = resolvedStyle(*textStyle, mainStyles, "text");
        KoGenStyle::copyPropertiesFromStyle(resolved, entry.style, KoGenStyle::TextType);
    }
    entry.style.setParentName(m_currentParagraphStyle.parentName());
    // Pagination controls belong to the index as a whole, not every entry.
    for (const QString &property : {QStringLiteral("fo:break-before"), QStringLiteral("fo:break-after"),
                                    QStringLiteral("fo:keep-with-next"), QStringLiteral("fo:keep-together")}) {
        entry.style.removeProperty(property, KoGenStyle::ParagraphType);
    }
    entry.indent = KoUnit::parseValue(entry.style.property("fo:margin-left", KoGenStyle::ParagraphType));
    const auto level = QRegularExpression(QStringLiteral("^(?:TOC|Contents)[ _]?([1-9])$"), QRegularExpression::CaseInsensitiveOption)
                           .match(m_currentParagraphStyle.parentName());
    if (level.hasMatch()) entry.level = level.captured(1).toInt();
    m_tocEntryFormatting.append(entry);
}

void DocxXmlDocumentReader::finishToc()
{
    m_endToc = false;
    if (!m_tocBodyWriter) return;
    body->endElement(); // text:index-body
    m_tocBodyWriter.reset();
    m_tocBodyBuffer.close();
    body = m_tocParentWriter;
    m_tocParentWriter = nullptr;

    // Unstyled caches encode the hierarchy through indentation. Named TOC
    // styles, when available, provide the level without this inference.
    QList<qreal> indents;
    for (const auto &entry : std::as_const(m_tocEntryFormatting)) {
        if (!indents.contains(entry.indent)) indents.append(entry.indent);
    }
    std::sort(indents.begin(), indents.end());
    QMap<int, KoGenStyle> entries;
    for (const auto &entry : std::as_const(m_tocEntryFormatting)) {
        const int level = entry.level ? entry.level : indents.indexOf(entry.indent) + 1;
        if (!entries.contains(level)) entries.insert(level, entry.style);
    }

    body->startElement("text:table-of-content");
    body->addAttribute("text:name", "Table of Contents");
    body->startElement("text:table-of-content-source");
    body->addAttribute("text:outline-level", m_tocLevels);
    body->addAttribute("text:use-outline-level", m_tocSourceStyles.isEmpty() ? "true" : "false");
    body->addAttribute("text:relative-tab-stop-position", "false");
    if (!m_tocSourceStyles.isEmpty()) {
        body->addAttribute("text:use-index-source-styles", "true");
        // Keep the exact Word mapping; rebuilding it from ODF style names
        // would lose the display names the \t switch relies on.
        body->addAttribute("calligra:toc-source-styles", m_tocSourceStyles);
    }
    if (!m_tocEntryFormatting.isEmpty()) {
        // DOCX field results are the document's last rendered TOC. Preserve
        // them until an explicit update so paragraphs accidentally carrying
        // Heading styles do not expand into pages of prose during import.
        body->addAttribute("calligra:preserve-cached-results", "true");
    }
    if (!m_tocSourceStyles.isEmpty()) {
        // Also declare the mapping in standard ODF so other consumers and
        // Calligra's own ToC generator can rebuild the index from styles.
        QMap<int, QStringList> stylesByLevel;
        const QStringList pairs = m_tocSourceStyles.split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (int i = 0; i + 1 < pairs.size(); i += 2) {
            const int level = pairs.at(i + 1).trimmed().toInt();
            if (level > 0) {
                stylesByLevel[level].append(pairs.at(i).trimmed());
            }
        }
        for (auto it = stylesByLevel.constBegin(); it != stylesByLevel.constEnd(); ++it) {
            body->startElement("text:index-source-styles");
            body->addAttribute("text:outline-level", it.key());
            for (const QString &style : it.value()) {
                body->startElement("text:index-source-style");
                body->addAttribute("text:style-name", style);
                body->endElement(); // text:index-source-style
            }
            body->endElement(); // text:index-source-styles
        }
    }
    for (int level = 1; level <= m_tocLevels; ++level) {
        KoGenStyle entry(KoGenStyle::ParagraphStyle, "paragraph");
        if (entries.contains(level)) {
            entry = entries.value(level);
        } else if (const auto *named = mainStyles->style(QStringLiteral("TOC%1").arg(level), "paragraph")) {
            entry = resolvedStyle(*named, mainStyles, "paragraph");
        } else {
            entry.setParentName(m_context->m_namedDefaultStyles.value("paragraph"));
            entry.addPropertyPt("fo:margin-left", (level - 1) * 21.6, KoGenStyle::ParagraphType);
            entry.addPropertyPt("fo:margin-top", 0, KoGenStyle::ParagraphType);
            entry.addPropertyPt("fo:margin-bottom", 0, KoGenStyle::ParagraphType);
            entry.addProperty("fo:line-height", "100%", KoGenStyle::ParagraphType);
            entry.addPropertyPt("fo:font-size", 10, KoGenStyle::TextType);
        }
        const QString name = mainStyles->insert(entry, QStringLiteral("Contents%1").arg(level));
        body->startElement("text:table-of-content-entry-template");
        body->addAttribute("text:outline-level", level);
        body->addAttribute("text:style-name", name);
        if (m_tocHyperlinks) { body->startElement("text:index-entry-link-start"); body->endElement(); }
        body->startElement("text:index-entry-text"); body->endElement();
        if (m_tocPageNumbers) {
            QString position;
            QString leader = QStringLiteral(".");
            QXmlStreamReader tabs(entry.childProperty("style:tab-stops", KoGenStyle::ParagraphType));
            tabs.setNamespaceProcessing(false);
            while (!tabs.atEnd()) {
                tabs.readNext();
                if (tabs.isStartElement() && tabs.qualifiedName() == QLatin1StringView("style:tab-stop")
                    && tabs.attributes().value(QStringLiteral("style:type")) == QLatin1StringView("right")) {
                    position = tabs.attributes().value(QStringLiteral("style:position")).toString();
                    leader = tabs.attributes().value(QStringLiteral("style:leader-text")).toString();
                }
            }
            body->startElement("text:index-entry-tab-stop");
            body->addAttribute("style:type", "right");
            body->addAttribute("style:leader-char", leader);
            if (!position.isEmpty()) body->addAttribute("style:position", position);
            body->endElement();
            body->startElement("text:index-entry-page-number"); body->endElement();
        }
        if (m_tocHyperlinks) { body->startElement("text:index-entry-link-end"); body->endElement(); }
        body->endElement(); // text:table-of-content-entry-template
    }
    body->endElement(); // text:table-of-content-source
    body->addCompleteElement(m_tocBodyBuffer.data());
    body->endElement(); // text:table-of-content
    m_tocEntryFormatting.clear();
}
