/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2013 Inge Wallin <inge@lysator.liu.se>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Own
#include "OdfTextReaderDocxBackend.h"

// Qt
#include <QRegularExpression>
#include <QSet>
#include <QtGlobal>
#include <QUrl>

// Calligra
#include <KoOdfStyle.h>
#include <KoOdfStyleManager.h>
#include <KoOdfStyleProperties.h>
#include <KoUnit.h>
#include <KoXmlWriter.h>

// This filter
#include "DocxExportDebug.h"
#include "DocxFile.h"
#include "DocxStyleHelper.h"
#include "OdfReaderDocxContext.h"

#if 0
#define DEBUG_BACKEND() debugDocx << (reader.isStartElement() ? "start" : (reader.isEndElement() ? "end" : "other")) << reader.qualifiedName().toString()
#else
#define DEBUG_BACKEND() // NOTHING
#endif

static bool toTwipsValue(const QString &value, qint64 *twips);
static bool toTwipsOffset(const QString &value, qint64 *twips);
static QByteArray sectionProperties(const QByteArray &base, int columns, int gapTwips);

// ================================================================
//                 class OdfTextReaderDocxBackend

OdfTextReaderDocxBackend::OdfTextReaderDocxBackend()
    : OdfTextReaderBackend()
    , m_insideSpanLevel(0)
    , m_currentOutlineLevel(-1)
    , m_commentIndex(0)
    , m_insideListItem(0)
    , m_writeComment(false)
    , m_insideComment(false)
    , m_insideDcCreator(false)
    , m_insideDcDate(false)
    , m_currentParagraphTextProperties(nullptr)
{
}

OdfTextReaderDocxBackend::~OdfTextReaderDocxBackend() = default;

// ----------------------------------------------------------------
// Text level functions: paragraphs, headings, sections, frames, objects, etc

void OdfTextReaderDocxBackend::elementOfficeAnnotation(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = docxContext->m_commentsWriter;

    if (reader.isStartElement()) {
        m_writeComment = true;
        m_insideComment = true;
        writer->startElement("w:comment");
        writer->addAttribute("w:id", m_commentIndex);
    } else {
        writer->endElement(); // w:comment
        m_insideComment = false;
    }
    Q_UNUSED(reader);
}

void OdfTextReaderDocxBackend::elementDcCreator(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    Q_UNUSED(reader);
    Q_UNUSED(context);
    if (reader.isStartElement()) {
        m_insideDcCreator = true;
    } else {
        m_insideDcCreator = false;
    }
}

void OdfTextReaderDocxBackend::elementDcDate(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    Q_UNUSED(reader);
    Q_UNUSED(context);
    if (reader.isStartElement()) {
        m_insideDcDate = true;
    } else {
        m_insideDcDate = false;
    }
}

void OdfTextReaderDocxBackend::elementTextH(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    if (reader.isStartElement()) {
        // ODF outline levels are 1-based, w:outlineLvl is 0-based.
        const int level = reader.attributes().value("text:outline-level").toInt();
        m_currentOutlineLevel = level > 0 ? level - 1 : 0;
        elementTextP(reader, context);
    } else {
        elementTextP(reader, context);
        m_currentOutlineLevel = -1;
    }
}

void OdfTextReaderDocxBackend::elementTextP(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }

    m_currentParagraphTextProperties = nullptr;
    m_currentParagraphParent.clear();

    KoXmlWriter *writer = currentWriter(docxContext);
    if (reader.isStartElement()) {
        const QString styleName = reader.attributes().value("text:style-name").toString();
        KoOdfStyle *paragraphStyle = docxContext->styleManager()->style(styleName, "paragraph");
        QList<KoOdfStyle *> ancestry;
        QSet<QString> seen;
        for (KoOdfStyle *ancestor = paragraphStyle; ancestor && !seen.contains(ancestor->name());
             ancestor = docxContext->styleManager()->style(ancestor->parent(), "paragraph")) {
            seen.insert(ancestor->name());
            ancestry.prepend(ancestor);
        }
        KoOdfStyleProperties merged;
        QString master;
        for (KoOdfStyle *ancestor : std::as_const(ancestry)) {
            if (!ancestor->masterPageName().isEmpty()) master = ancestor->masterPageName();
            if (auto *properties = ancestor->properties("style:paragraph-properties")) {
                // Merge the pagination and decoration properties inherited by
                // automatic styles, without discarding explicit overrides.
                for (const QString &key : {QStringLiteral("fo:break-before"), QStringLiteral("fo:break-after"),
                     QStringLiteral("style:page-number"), QStringLiteral("fo:keep-with-next"),
                     QStringLiteral("fo:keep-together"), QStringLiteral("fo:background-color"), QStringLiteral("fo:border"),
                     QStringLiteral("fo:border-top"), QStringLiteral("fo:border-bottom"),
                     QStringLiteral("fo:border-left"), QStringLiteral("fo:border-right"),
                     QStringLiteral("fo:padding"), QStringLiteral("fo:padding-top"), QStringLiteral("fo:padding-bottom"),
                     QStringLiteral("fo:padding-left"), QStringLiteral("fo:padding-right"),
                     QStringLiteral("fo:margin-top"), QStringLiteral("fo:margin-bottom"),
                     QStringLiteral("fo:margin-left"), QStringLiteral("fo:margin-right"),
                     QStringLiteral("fo:text-indent"), QStringLiteral("fo:line-height"),
                     QStringLiteral("fo:text-align"), QStringLiteral("style:writing-mode")}) {
                    const QString value = properties->attribute(key);
                    if (!value.isEmpty()) merged.setAttribute(key, value);
                }
            }
        }
        const bool bodyParagraph = docxContext->outputPart() == QLatin1StringView("word/document.xml")
            && m_tables.isEmpty() && !m_insideFootnote && !m_insideEndnote;
        const QString sectionBreakBefore = merged.attribute(QStringLiteral("fo:break-before"));
        if (bodyParagraph && !master.isEmpty() && docxContext->masterPageSections.contains(master)
            && master != docxContext->activeMasterPage) {
            if (m_bodyHasContent) {
                writer->startElement("w:p");
                writer->startElement("w:pPr");
                writer->addCompleteElement(docxContext->sectPrContent().constData());
                writer->endElement();
                writer->endElement();
                // The section boundary already supplies the page break.
                merged.setAttribute(QStringLiteral("fo:break-before"), QStringLiteral("auto"));
            }
            docxContext->activeMasterPage = master;
            QByteArray section = docxContext->masterPageSections.value(master);
            const QString restart = merged.attribute(QStringLiteral("style:page-number"));
            if (restart.toInt() > 0) {
                const QByteArray attribute = " w:start=\"" + restart.toUtf8() + "\"";
                const QByteArray prefix = QByteArray("<w:pgNumType") + attribute;
                const QByteArray element = prefix + "/></w:sectPr>";
                if (section.contains("<w:pgNumType")) section.replace("<w:pgNumType", prefix);
                else section.replace("</w:sectPr>", element);
            }
            if (sectionBreakBefore == QLatin1String("auto")) {
                section.replace("w:val=\"nextPage\"", "w:val=\"continuous\"");
            }
            docxContext->setSectPrContent(section);
        }
        m_paragraphBreakAfter.push(merged.attribute(QStringLiteral("fo:break-after")));
        writer->startElement("w:p");
        if (!m_insideFootnote) {
            docxContext->m_insideParagraph = true;
        }
        if (!m_cellParagraphStack.isEmpty()) {
            m_cellParagraphStack.top() = true;
        }

        DocxStyleHelper::ParagraphExtras extras;
        if (m_currentOutlineLevel >= 0) {
            extras.outlineLevel = m_currentOutlineLevel;
        }

        KoXmlStreamAttributes attributes = reader.attributes();
        const QString textStyle = attributes.value("text:style-name").toString();

        // <text:numbered-paragraph> carries its own numbering; otherwise a
        // paragraph inside a list item inherits the numbering of the innermost
        // open <text:list>.
        if (reader.qualifiedName() == QLatin1StringView("text:numbered-paragraph")) {
            extras.numId = docxContext->numbering().numIdForStyle(textStyle);
            const int level = attributes.value("text:level").toInt();
            extras.ilvl = level > 0 ? level - 1 : 0;
        } else if (m_insideListItem > 0 && !m_listStack.isEmpty()) {
            const ListState &list = m_listStack.top();
            extras.numId = list.numId;
            extras.ilvl = list.ilvl;
        }

        if (!textStyle.isEmpty()) {
            extras.tabStops = docxContext->tabStopsForStyle(textStyle);
        }

        writer->startElement("w:pPr");
        KoOdfStyle *style = textStyle.isEmpty() ? nullptr : docxContext->styleManager()->style(textStyle, "paragraph");
        if (style) {
            KoOdfStyleProperties *parProperties = &merged;
            m_currentParagraphTextProperties = style->properties("style:text-properties");
            m_currentParagraphParent = style->parent();

            // Only named styles from styles.xml are emitted as w:style
            // entries. Automatic styles live in content.xml, so use
            // their named parent as the base style and keep their direct
            // formatting inline below.
            const QString wordStyleId = style->isFromStylesXml() ? style->name() : m_currentParagraphParent;
            if (!wordStyleId.isEmpty()) {
                writer->startElement("w:pStyle");
                writer->addAttribute("w:val", wordStyleId);
                writer->endElement(); // w:pStyle
            }
            DocxStyleHelper::handleParagraphStyles(parProperties, writer, extras);
            writer->startElement("w:rPr");
            DocxStyleHelper::handleTextStyles(m_currentParagraphTextProperties, writer);
            writer->endElement(); // w:rPr
        } else {
            DocxStyleHelper::handleParagraphStyles(nullptr, writer, extras);
        }
        writer->endElement(); // w:pPr
        if (merged.attribute(QStringLiteral("fo:break-before")) == QLatin1String("column")) {
            writer->startElement("w:r"); writer->startElement("w:br");
            writer->addAttribute("w:type", "column"); writer->endElement(); writer->endElement();
        }
        if (m_insideFootnote && m_footnoteNeedsReference) {
            // Word renders the note number from this marker; it belongs at
            // the start of the first paragraph of the note body.
            writer->startElement("w:r");
            writer->startElement(m_insideEndnote ? "w:endnoteRef" : "w:footnoteRef");
            writer->endElement(); // w:footnoteRef/w:endnoteRef
            writer->endElement(); // w:r
            m_footnoteNeedsReference = false;
        }
    } else {
        const QString breakAfter = m_paragraphBreakAfter.isEmpty() ? QString() : m_paragraphBreakAfter.pop();
        if (breakAfter == QLatin1String("page") || breakAfter == QLatin1String("column")) {
            writer->startElement("w:r"); writer->startElement("w:br");
            writer->addAttribute("w:type", breakAfter); writer->endElement(); writer->endElement();
        }
        writer->endElement(); // w:p
        if (!m_insideFootnote) {
            docxContext->m_insideParagraph = false;
        }
        if (docxContext->outputPart() == QLatin1StringView("word/document.xml")
            && !m_insideFootnote && !m_insideEndnote && m_tables.isEmpty()) {
            m_bodyHasContent = true;
            m_lastWasSectionBreak = false;
        }
    }
}

void OdfTextReaderDocxBackend::elementTextSection(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }

    if (reader.isStartElement()) {
        ++m_insideSection;
        m_sectionStyles.push(reader.attributes().value("text:style-name").toString());

        // A Word section is delimited by the sectPr of its last paragraph,
        // so the text before an ODF section has to be terminated by a break
        // that keeps its own (single column) layout.  Without it that text
        // would inherit the column layout of the section that follows.
        if (m_insideSection == 1 && m_bodyHasContent && !m_lastWasSectionBreak
            && m_tables.isEmpty() && !m_insideFootnote && !m_insideEndnote
            && docxContext->outputPart() == QLatin1StringView("word/document.xml")) {
            writeSectionBreak(docxContext, 1, 720);
        }
        return;
    }

    if (m_insideSection > 0) {
        --m_insideSection;
    }
    const QString sectionStyle = m_sectionStyles.isEmpty() ? QString() : m_sectionStyles.pop();

    // Nested sections end with their own break; the outermost one also ends a
    // Word section.  Word sections cannot live inside tables, footnotes or
    // headers, so only the body is considered.
    if (m_insideSection > 0 || !m_tables.isEmpty() || m_insideFootnote || m_insideEndnote) {
        return;
    }
    if (docxContext->outputPart() != QLatin1StringView("word/document.xml")) {
        return;
    }

    int columns = 1;
    int gapTwips = 720;
    if (!sectionStyle.isEmpty()) {
        if (KoOdfStyle *style = docxContext->styleManager()->style(sectionStyle, "section")) {
            if (KoOdfStyleProperties *properties = style->properties("style:section-properties")) {
                const int count = properties->attribute("fo:column-count").toInt();
                if (count > 1) {
                    columns = qMin(count, 45);
                }
                qint64 gap = 0;
                if (toTwipsValue(properties->attribute("fo:column-gap"), &gap) && gap > 0) {
                    gapTwips = static_cast<int>(qMin<qint64>(gap, 31680));
                }
            }
        }
    }

    writeSectionBreak(docxContext, columns, gapTwips);
}

void OdfTextReaderDocxBackend::writeSectionBreak(OdfReaderDocxContext *context, int columns, int gapTwips)
{
    KoXmlWriter *writer = currentWriter(context);
    writer->startElement("w:p");
    writer->startElement("w:pPr");
    writer->addCompleteElement(sectionProperties(context->sectPrContent(), columns, gapTwips).constData());
    writer->endElement(); // w:pPr
    writer->endElement(); // w:p
    m_lastWasSectionBreak = true;
}

void OdfTextReaderDocxBackend::elementTextTableOfContent(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) return;
    if (reader.isStartElement()) {
        if (reader.qualifiedName() == QLatin1StringView("text:table-of-content-source")) {
            m_tocLevels = qBound(1, reader.attributes().value("text:outline-level").toInt(), 9);
        } else {
            m_tocLevels = 3;
        }
        return;
    }
    if (!m_tocFieldOpen) {
        return;
    }
    m_tocFieldOpen = false;
    if (docxContext->outputPart() != QLatin1StringView("word/document.xml")) {
        return;
    }

    KoXmlWriter *writer = currentWriter(docxContext);
    writer->startElement("w:p");
    writer->startElement("w:r");
    writer->startElement("w:fldChar");
    writer->addAttribute("w:fldCharType", "end");
    writer->endElement(); // w:fldChar
    writer->endElement(); // w:r
    writer->endElement(); // w:p
}

void OdfTextReaderDocxBackend::elementTextIndexBody(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext || !reader.isStartElement()) {
        return;
    }
    if (docxContext->outputPart() != QLatin1StringView("word/document.xml")) {
        return;
    }

    // A table of contents is a multi-paragraph field.  The cached entry
    // paragraphs that follow are the field result until the end marker is
    // written when the enclosing text:table-of-content closes.
    KoXmlWriter *writer = currentWriter(docxContext);
    writer->startElement("w:p");
    writer->startElement("w:r");
    writer->startElement("w:fldChar");
    writer->addAttribute("w:fldCharType", "begin");
    writer->endElement(); // w:fldChar
    writer->endElement(); // w:r
    writer->startElement("w:r");
    writer->startElement("w:instrText");
    writer->addAttribute("xml:space", "preserve");
    writer->addTextNode(QStringLiteral(" TOC \\o \"1-%1\" \\h \\z \\u ").arg(m_tocLevels));
    writer->endElement(); // w:instrText
    writer->endElement(); // w:r
    writer->startElement("w:r");
    writer->startElement("w:fldChar");
    writer->addAttribute("w:fldCharType", "separate");
    writer->endElement(); // w:fldChar
    writer->endElement(); // w:r
    writer->endElement(); // w:p
    m_tocFieldOpen = true;
}

// Converts an ODF length value to twips.
// Like toTwipsValue, but a table offset is a signed position: dragging a table
// into the margin moves it by a negative amount, which must survive the export.
static bool toTwipsOffset(const QString &value, qint64 *twips)
{
    if (value.isEmpty()) {
        return false;
    }
    const qreal points = KoUnit::parseValue(value, qQNaN());
    if (qIsNaN(points)) {
        return false;
    }
    *twips = qRound64(points * 20.0);
    return true;
}

static bool toTwipsValue(const QString &value, qint64 *twips)
{
    if (value.isEmpty()) {
        return false;
    }
    const qreal points = KoUnit::parseValue(value, -1.0);
    if (points < 0) {
        return false;
    }
    *twips = qRound64(points * 20.0);
    return true;
}

// Appends a single border edge; shared by table and cell borders.
static void writeBorderEdge(KoXmlWriter *writer, const char *element, const QString &style, int size, const QString &color)
{
    writer->startElement(element);
    writer->addAttribute("w:val", style);
    writer->addAttribute("w:sz", size);
    writer->addAttribute("w:space", 0);
    writer->addAttribute("w:color", color.isEmpty() ? QStringLiteral("auto") : color);
    writer->endElement(); // element
}

// Parses an ODF "1pt solid #000000" border value.
static void parseBorder(const QString &border, QString *style, int *size, QString *color)
{
    *style = QStringLiteral("single");
    *size = 4;
    color->clear();
    const QStringList parts = border.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString width;
    QString odfStyle;
    for (const QString &part : parts) {
        if (part.startsWith(QLatin1Char('#'))) {
            *color = part.mid(1);
        } else if (part.endsWith(QLatin1String("pt")) || part.endsWith(QLatin1String("in")) || part.endsWith(QLatin1String("cm"))
                   || part.endsWith(QLatin1String("mm"))) {
            width = part;
        } else {
            odfStyle = part;
        }
    }
    if (odfStyle == QLatin1String("dashed")) {
        *style = QStringLiteral("dashed");
    } else if (odfStyle == QLatin1String("dotted")) {
        *style = QStringLiteral("dotted");
    } else if (odfStyle == QLatin1String("double")) {
        *style = QStringLiteral("double");
    } else if (odfStyle == QLatin1String("dot-dash")) {
        *style = QStringLiteral("dotDash");
    } else if (odfStyle == QLatin1String("dot-dot-dash")) {
        *style = QStringLiteral("dotDotDash");
    } else if (odfStyle == QLatin1String("dash-largegap")) {
        *style = QStringLiteral("dashSmallGap");
    } else if (odfStyle == QLatin1String("wave")) {
        *style = QStringLiteral("wave");
    } else if (odfStyle == QLatin1String("double-wave")) {
        *style = QStringLiteral("doubleWave");
    } else if (odfStyle == QLatin1String("groove")) {
        *style = QStringLiteral("threeDEngrave");
    } else if (odfStyle == QLatin1String("ridge")) {
        *style = QStringLiteral("threeDEmboss");
    } else if (odfStyle == QLatin1String("inset") || odfStyle == QLatin1String("outset")) {
        *style = odfStyle;
    }
    const qreal points = KoUnit::parseValue(width, 0.0);
    if (points > 0) {
        *size = qBound(2, static_cast<int>(qRound(points * 8.0)), 96);
    }
}

// Width of the text area (page width minus left/right margins) from the
// serialized <w:sectPr> of the document.  Used to scale relative table and
// column widths.  Returns 0 when the geometry is not known yet.
static qint64 pageTextWidthTwips(const QByteArray &sectPr)
{
    qint64 pageWidth = 0;
    qint64 leftMargin = 0;
    qint64 rightMargin = 0;
    QXmlStreamReader reader(sectPr);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QString name = reader.qualifiedName().toString();
        const QXmlStreamAttributes attributes = reader.attributes();
        if (name == QLatin1String("w:pgSz")) {
            pageWidth = attributes.value(QLatin1String("w:w")).toLongLong();
        } else if (name == QLatin1String("w:pgMar")) {
            leftMargin = attributes.value(QLatin1String("w:left")).toLongLong();
            rightMargin = attributes.value(QLatin1String("w:right")).toLongLong();
        }
    }
    if (pageWidth <= 0) {
        return 0;
    }
    return pageWidth - leftMargin - rightMargin;
}

// Maps an ODF text field element to its Word field instruction.  Returns an
// empty string for fields that have no direct Word equivalent; their cached
// text is then kept as ordinary text.
static QString fieldInstruction(const QString &tagName, const QString &text)
{
    if (tagName == QLatin1StringView("text:page-number")) {
        return QStringLiteral(" PAGE ");
    }
    if (tagName == QLatin1StringView("text:page-count")) {
        return QStringLiteral(" NUMPAGES ");
    }
    if (tagName == QLatin1StringView("text:date")) {
        if (text.contains(QLatin1Char(':'))) {
            return QStringLiteral(" DATE \\@ \"yyyy-MM-dd HH:mm:ss\" ");
        }
        if (text.contains(QLatin1Char('-'))) {
            return QStringLiteral(" DATE \\@ \"yyyy-MM-dd\" ");
        }
        if (text.contains(QLatin1Char('/'))) {
            return QStringLiteral(" DATE \\@ \"M/d/yy\" ");
        }
        return QStringLiteral(" DATE ");
    }
    if (tagName == QLatin1StringView("text:time")) {
        return QStringLiteral(" TIME \\@ \"HH:mm:ss\" ");
    }
    if (tagName == QLatin1StringView("text:title")) {
        return QStringLiteral(" TITLE ");
    }
    if (tagName == QLatin1StringView("text:subject")) {
        return QStringLiteral(" SUBJECT ");
    }
    if (tagName == QLatin1StringView("text:description")) {
        return QStringLiteral(" COMMENTS ");
    }
    if (tagName == QLatin1StringView("text:keywords")) {
        return QStringLiteral(" KEYWORDS ");
    }
    if (tagName == QLatin1StringView("text:author-name") || tagName == QLatin1StringView("text:creator")
        || tagName == QLatin1StringView("text:initial-creator")) {
        return QStringLiteral(" AUTHOR ");
    }
    if (tagName == QLatin1StringView("text:author-initials")) {
        return QStringLiteral(" USERINITIALS ");
    }
    if (tagName == QLatin1StringView("text:file-name")) {
        return QStringLiteral(" FILENAME ");
    }
    if (tagName == QLatin1StringView("text:template-name")) {
        return QStringLiteral(" TEMPLATE ");
    }
    if (tagName == QLatin1StringView("text:creation-date")) {
        return QStringLiteral(" CREATEDATE ");
    }
    if (tagName == QLatin1StringView("text:creation-time")) {
        return QStringLiteral(" CREATETIME ");
    }
    if (tagName == QLatin1StringView("text:modification-date")) {
        return QStringLiteral(" SAVEDATE ");
    }
    if (tagName == QLatin1StringView("text:modification-time")) {
        return QStringLiteral(" SAVEDATETIME ");
    }
    if (tagName == QLatin1StringView("text:print-date")) {
        return QStringLiteral(" PRINTDATE ");
    }
    if (tagName == QLatin1StringView("text:print-time")) {
        return QStringLiteral(" PRINTTIME ");
    }
    if (tagName == QLatin1StringView("text:editing-cycles")) {
        return QStringLiteral(" REVNUM ");
    }
    if (tagName == QLatin1StringView("text:editing-duration")) {
        return QStringLiteral(" EDITTIME ");
    }
    if (tagName == QLatin1StringView("text:word-count")) {
        return QStringLiteral(" NUMWORDS ");
    }
    if (tagName == QLatin1StringView("text:character-count")) {
        return QStringLiteral(" NUMCHARS ");
    }
    if (tagName == QLatin1StringView("text:paragraph-count")) {
        return QStringLiteral(" NUMPARAS ");
    }
    return QString();
}

// Builds the <w:sectPr> that ends an ODF section.  The document's final
// section properties supply page size, margins and header/footer references;
// the break type becomes continuous so the section does not start a new page,
// and the ODF column count is mapped to <w:cols>.
static QByteArray sectionProperties(const QByteArray &base, int columns, int gapTwips)
{
    QString xml = QString::fromUtf8(base);
    const QString continuous = QStringLiteral("<w:type w:val=\"continuous\"/>");
    if (xml.isEmpty()) {
        xml = QStringLiteral("<w:sectPr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
                             "<w:type w:val=\"continuous\"/>"
                             "</w:sectPr>");
    } else {
        const QRegularExpression typeExpression(QStringLiteral("<w:type\\b[^>]*/>"));
        if (xml.contains(typeExpression)) {
            xml.replace(typeExpression, continuous);
        } else {
            int position = xml.indexOf(QStringLiteral("<w:pgSz"));
            if (position < 0) {
                position = xml.indexOf(QStringLiteral("</w:sectPr>"));
            }
            if (position >= 0) {
                xml.insert(position, continuous);
            }
        }
    }

    if (columns > 1) {
        const QString cols = QStringLiteral("<w:cols w:num=\"%1\" w:space=\"%2\" w:equalWidth=\"1\"/>")
                                 .arg(columns)
                                 .arg(qMax(0, gapTwips));
        const int position = xml.lastIndexOf(QStringLiteral("</w:sectPr>"));
        if (position >= 0) {
            xml.insert(position, cols);
        }
    }
    return xml.toUtf8();
}

// ODF stores borders either as one fo:border or as per-edge
// fo:border-top / -bottom / -left / -right properties.  Return the value that
// applies to a given edge, falling back to the generic property.
static QString borderForEdge(KoOdfStyleProperties *properties, const char *odfSide)
{
    const QString edge = properties->attribute(QStringLiteral("fo:border-") + QLatin1String(odfSide));
    if (!edge.isEmpty()) {
        return edge;
    }
    return properties->attribute(QStringLiteral("fo:border"));
}

static bool hasAnyBorder(KoOdfStyleProperties *properties)
{
    if (!properties) {
        return false;
    }
    for (const char *side : {"top", "left", "bottom", "right"}) {
        const QString border = borderForEdge(properties, side);
        if (!border.isEmpty() && border != QLatin1String("none")) {
            return true;
        }
    }
    return false;
}

void OdfTextReaderDocxBackend::writeTableBorders(KoOdfStyleProperties *properties, KoXmlWriter *writer)
{
    writer->startElement("w:tblBorders");
    for (const char *side : {"w:top", "w:left", "w:bottom", "w:right", "w:insideH", "w:insideV"}) {
        const char *odfSide = side + 2; // skip the "w:" prefix
        QString border = borderForEdge(properties, odfSide);
        if (border == QLatin1String("none")) {
            border.clear();
        }
        QString style;
        int size = 4;
        QString color;
        if (border.isEmpty()) {
            // No border on this edge: Word needs an explicit "none".
            style = QStringLiteral("none");
            size = 0;
        } else {
            parseBorder(border, &style, &size, &color);
        }
        writeBorderEdge(writer, side, style, size, color);
    }
    writer->endElement(); // w:tblBorders
}

void OdfTextReaderDocxBackend::writeCellBorders(KoOdfStyleProperties *properties, KoXmlWriter *writer)
{
    writer->startElement("w:tcBorders");
    for (const char *side : {"w:top", "w:left", "w:bottom", "w:right"}) {
        const char *odfSide = side + 2; // skip the "w:" prefix
        QString border = borderForEdge(properties, odfSide);
        QString style;
        int size = 4;
        QString color;
        if (border.isEmpty()) {
            continue;
        }
        if (border == QLatin1String("none")) {
            style = QStringLiteral("nil");
            size = 0;
        } else {
            parseBorder(border, &style, &size, &color);
        }
        writeBorderEdge(writer, side, style, size, color);
    }
    writer->endElement(); // w:tcBorders
}

void OdfTextReaderDocxBackend::elementTableTable(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = currentWriter(docxContext);
    if (reader.isStartElement()) {
        m_tables.push(TableState());
        TableState &state = m_tables.top();

        const KoXmlStreamAttributes attributes = reader.attributes();
        const QString tableStyle = attributes.value("table:style-name").toString();
        KoOdfStyleProperties *tableProperties = nullptr;
        if (!tableStyle.isEmpty()) {
            if (KoOdfStyle *style = docxContext->styleManager()->style(tableStyle, "table")) {
                tableProperties = style->properties("style:table-properties");
            }
        }

        writer->startElement("w:tbl");
        writer->startElement("w:tblPr");

        // A table that was dragged away from its place in the text flow is a
        // floating table for Word: it needs an explicit position (w:tblpPr).
        if (tableProperties) {
            qint64 offsetX = 0;
            qint64 offsetY = 0;
            const bool hasOffsetX = toTwipsOffset(tableProperties->attribute("calligra:table-offset-x"), &offsetX);
            const bool hasOffsetY = toTwipsOffset(tableProperties->attribute("calligra:table-offset-y"), &offsetY);
            if (hasOffsetX || hasOffsetY) {
                writer->startElement("w:tblpPr");
                writer->addAttribute("w:horzAnchor", "margin");
                writer->addAttribute("w:vertAnchor", "text");
                writer->addAttribute("w:tblpX", static_cast<int>(offsetX));
                writer->addAttribute("w:tblpY", static_cast<int>(offsetY));
                writer->endElement(); // w:tblpPr
            }
        }

        // ODF tables can express their width either absolutely (style:width)
        // or relative to the text area (style:rel-width).  Word knows both.
        qint64 tableWidth = 0;
        bool hasWidth = false;
        int relativeWidth = 0;
        if (tableProperties) {
            const QString width = tableProperties->attribute("style:width");
            if (!width.isEmpty()) {
                hasWidth = toTwipsValue(width, &tableWidth) && tableWidth > 0;
            }
            const QString relWidth = tableProperties->attribute("style:rel-width");
            if (relWidth.endsWith(QLatin1Char('%'))) {
                relativeWidth = qRound(relWidth.chopped(1).toDouble() * 50.0);
            }
        }
        writer->startElement("w:tblW");
        if (relativeWidth > 0) {
            writer->addAttribute("w:w", relativeWidth);
            writer->addAttribute("w:type", "pct");
        } else if (hasWidth) {
            writer->addAttribute("w:w", static_cast<int>(tableWidth));
            writer->addAttribute("w:type", "dxa");
        } else {
            writer->addAttribute("w:w", 5000);
            writer->addAttribute("w:type", "pct");
        }
        writer->endElement(); // w:tblW

        // Horizontal table position.  ODF either centers/right aligns the
        // table or gives it a left margin; Word uses w:jc / w:tblInd.
        if (tableProperties) {
            const QString align = tableProperties->attribute("table:align");
            if (align == QLatin1String("center") || align == QLatin1String("right")) {
                writer->startElement("w:tblJc");
                writer->addAttribute("w:val", align);
                writer->endElement(); // w:tblJc
            }
            qint64 indent = 0;
            if (toTwipsValue(tableProperties->attribute("fo:margin-left"), &indent) && indent != 0) {
                writer->startElement("w:tblInd");
                writer->addAttribute("w:w", static_cast<int>(indent));
                writer->addAttribute("w:type", "dxa");
                writer->endElement(); // w:tblInd
            }
        }
        if (hasAnyBorder(tableProperties)) {
            writeTableBorders(tableProperties, writer);
        }
        // w:tblLayout must come after w:tblBorders / w:shd in the schema.
        if (hasWidth || relativeWidth > 0) {
            writer->startElement("w:tblLayout");
            writer->addAttribute("w:type", "fixed");
            writer->endElement(); // w:tblLayout
        }
        writer->endElement(); // w:tblPr

        // Column widths in ODF are often the widths for a 100% table even
        // when the table itself is only e.g. 80% of the text area.  Scale the
        // grid down so that Word reproduces the original proportions.
        if (relativeWidth > 0 && hasWidth && tableWidth > 0) {
            const qint64 textWidth = pageTextWidthTwips(docxContext->sectPrContent());
            if (textWidth > 0) {
                const qreal targetWidth = textWidth * relativeWidth / 5000.0;
                state.widthScale = qBound(0.05, targetWidth / tableWidth, 4.0);
            }
        }
    } else {
        if (!m_tables.isEmpty()) {
            TableState &state = m_tables.top();
            if (state.gridOpen) {
                writer->endElement(); // w:tblGrid
                state.gridOpen = false;
            }
            m_tables.pop();
        }
        writer->endElement(); // w:tbl
        if (m_tables.isEmpty() && !m_insideFootnote && !m_insideEndnote
            && docxContext->outputPart() == QLatin1StringView("word/document.xml")) {
            m_bodyHasContent = true;
            m_lastWasSectionBreak = false;
        }
    }
}

void OdfTextReaderDocxBackend::elementTableTableColumn(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    if (!reader.isStartElement()) {
        return;
    }
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext || m_tables.isEmpty()) {
        return;
    }
    KoXmlWriter *writer = currentWriter(docxContext);
    TableState &state = m_tables.top();

    qint64 width = 0;
    const QString columnStyle = reader.attributes().value("table:style-name").toString();
    if (!columnStyle.isEmpty()) {
        if (KoOdfStyle *style = docxContext->styleManager()->style(columnStyle, "table-column")) {
            if (KoOdfStyleProperties *properties = style->properties("style:table-column-properties")) {
                if (!toTwipsValue(properties->attribute("style:column-width"), &width) || width <= 0) {
                    // Fall back to the relative column width ("1234*"): split
                    // the remaining space proportionally to the weights.
                    const QString relWidth = properties->attribute("style:rel-column-width");
                    if (relWidth.endsWith(QLatin1Char('*'))) {
                        const int weight = relWidth.chopped(1).toInt();
                        if (weight > 0) {
                            width = weight;
                        }
                    }
                }
            }
        }
    }
    if (width > 0 && state.widthScale != 1.0) {
        width = qRound64(width * state.widthScale);
    }
    const int repeat = qMax(1, reader.attributes().value("table:number-columns-repeated").toInt());
    for (int i = 0; i < qMin(repeat, 64); ++i) {
        state.columnWidths.append(static_cast<int>(width));
        if (!state.gridOpen) {
            writer->startElement("w:tblGrid");
            state.gridOpen = true;
        }
        writer->startElement("w:gridCol");
        writer->addAttribute("w:w", static_cast<int>(width));
        writer->endElement(); // w:gridCol
    }
}

void OdfTextReaderDocxBackend::elementTableTableRow(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = currentWriter(docxContext);
    if (reader.isStartElement()) {
        if (m_tables.isEmpty()) {
            return;
        }
        TableState &state = m_tables.top();
        if (state.gridOpen) {
            writer->endElement(); // w:tblGrid
            state.gridOpen = false;
        } else if (state.columnWidths.isEmpty()) {
            // Word expects a w:tblGrid before the first row even when the ODF
            // table does not declare any columns.
            writer->startElement("w:tblGrid");
            writer->endElement(); // w:tblGrid
        }
        state.currentColumn = 0;

        // Columns still covered by a row span from an earlier row belong to
        // the continuation cells of this row.  Consume one of their remaining
        // rows here; spans started by cells of this row are unaffected.
        state.rowSpanActive.resize(state.rowSpanRemaining.size());
        for (int i = 0; i < state.rowSpanRemaining.size(); ++i) {
            state.rowSpanActive[i] = state.rowSpanRemaining.at(i) > 0;
            if (state.rowSpanActive.at(i)) {
                state.rowSpanRemaining[i] -= 1;
            }
        }

        writer->startElement("w:tr");

        KoXmlWriter *trPrWriter = writer;
        trPrWriter->startElement("w:trPr");
        const QString rowStyle = reader.attributes().value("table:style-name").toString();
        if (!rowStyle.isEmpty()) {
            if (KoOdfStyle *style = docxContext->styleManager()->style(rowStyle, "table-row")) {
                if (KoOdfStyleProperties *properties = style->properties("style:table-row-properties")) {
                    qint64 height = 0;
                    if (toTwipsValue(properties->attribute("style:row-height"), &height) && height > 0) {
                        writer->startElement("w:trHeight");
                        writer->addAttribute("w:val", static_cast<int>(height));
                        writer->addAttribute("w:hRule", "atLeast");
                        writer->endElement(); // w:trHeight
                    }
                }
            }
        }
        trPrWriter->endElement(); // w:trPr
    } else {
        if (m_tables.isEmpty()) {
            return;
        }
        writer->endElement(); // w:tr
    }
}

void OdfTextReaderDocxBackend::elementTableTableCell(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = currentWriter(docxContext);
    if (reader.isStartElement()) {
        const KoXmlStreamAttributes attributes = reader.attributes();
        const int columnSpan = qMax(1, attributes.value("table:number-columns-spanned").toInt());
        const int rowSpan = qMax(1, attributes.value("table:number-rows-spanned").toInt());

        int columnWidth = 0;
        int columnIndex = 0;
        if (!m_tables.isEmpty()) {
            TableState &state = m_tables.top();
            columnIndex = state.currentColumn;
            for (int i = 0; i < columnSpan && columnIndex + i < state.columnWidths.size(); ++i) {
                columnWidth += state.columnWidths.at(columnIndex + i);
            }
            state.currentColumn += columnSpan;
            if (rowSpan > 1) {
                while (state.rowSpanRemaining.size() < columnIndex + columnSpan) {
                    state.rowSpanRemaining.append(0);
                }
                for (int i = 0; i < columnSpan; ++i) {
                    state.rowSpanRemaining[columnIndex + i] = rowSpan - 1;
                }
            }
        }

        writer->startElement("w:tc");
        writer->startElement("w:tcPr");
        writer->startElement("w:tcW");
        writer->addAttribute("w:w", columnWidth);
        writer->addAttribute("w:type", columnWidth > 0 ? "dxa" : "auto");
        writer->endElement(); // w:tcW
        if (columnSpan > 1) {
            writer->startElement("w:gridSpan");
            writer->addAttribute("w:val", columnSpan);
            writer->endElement(); // w:gridSpan
        }
        if (rowSpan > 1) {
            writer->startElement("w:vMerge");
            writer->addAttribute("w:val", "restart");
            writer->endElement(); // w:vMerge
        }

        // Cell formatting from the ODF cell style.
        const QString cellStyle = attributes.value("table:style-name").toString();
        if (!cellStyle.isEmpty()) {
            if (KoOdfStyle *style = docxContext->styleManager()->style(cellStyle, "table-cell")) {
                KoOdfStyleProperties *properties = style->properties("style:table-cell-properties");
                if (properties) {
                    if (hasAnyBorder(properties)) {
                        writeCellBorders(properties, writer);
                    }
                    const QString background = properties->attribute("fo:background-color");
                    if (!background.isEmpty() && background != QLatin1String("transparent")) {
                        writer->startElement("w:shd");
                        writer->addAttribute("w:val", "clear");
                        writer->addAttribute("w:color", "auto");
                        writer->addAttribute("w:fill", background.mid(background.startsWith(QLatin1Char('#')) ? 1 : 0));
                        writer->endElement(); // w:shd
                    }
                    const QString verticalAlign = properties->attribute("style:vertical-align");
                    if (!verticalAlign.isEmpty()) {
                        writer->startElement("w:vAlign");
                        writer->addAttribute("w:val", verticalAlign == QLatin1String("middle") ? QStringLiteral("center") : verticalAlign);
                        writer->endElement(); // w:vAlign
                    }
                }
            }
        }
        writer->endElement(); // w:tcPr
        m_cellParagraphStack.push(false);
    } else {
        if (!m_cellParagraphStack.isEmpty() && !m_cellParagraphStack.pop()) {
            writer->startElement("w:p");
            writer->endElement(); // w:p
        }
        writer->endElement(); // w:tc
    }
}

void OdfTextReaderDocxBackend::elementTableCoveredTableCell(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    if (!reader.isStartElement()) {
        return;
    }
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = currentWriter(docxContext);

    int columnIndex = 0;
    bool verticallyMerged = false;
    int columnWidth = 0;
    if (!m_tables.isEmpty()) {
        TableState &state = m_tables.top();
        columnIndex = state.currentColumn;
        verticallyMerged = columnIndex < state.rowSpanActive.size() && state.rowSpanActive.at(columnIndex);
        if (columnIndex < state.columnWidths.size()) {
            columnWidth = state.columnWidths.at(columnIndex);
        }
        // A horizontally covered cell is part of the gridSpan of the cell
        // before it, so it does not advance the column counter.  Only a
        // vertical continuation occupies a column of its own in this row.
        if (verticallyMerged) {
            state.currentColumn += 1;
        }
    } else {
        return;
    }

    // A cell covered horizontally is already represented by the gridSpan of
    // the preceding cell; only vertical continuations need a w:tc of their own.
    // Do not touch the reader here: the OdtReader still has to traverse the
    // element and relies on its position.
    if (!verticallyMerged) {
        return;
    }

    writer->startElement("w:tc");
    writer->startElement("w:tcPr");
    writer->startElement("w:tcW");
    writer->addAttribute("w:w", columnWidth);
    writer->addAttribute("w:type", columnWidth > 0 ? "dxa" : "auto");
    writer->endElement(); // w:tcW
    writer->startElement("w:vMerge");
    writer->endElement(); // w:vMerge
    writer->endElement(); // w:tcPr
    writer->startElement("w:p");
    writer->endElement(); // w:p
    writer->endElement(); // w:tc
}

// ----------------------------------------------------------------
// Paragraph level functions: spans, annotations, notes, text content itself, etc.

void OdfTextReaderDocxBackend::elementTextSpan(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }

    if (reader.isStartElement()) {
        m_spanStyles.append(reader.attributes().value("text:style-name").toString());
        ++m_insideSpanLevel;
    } else {
        if (!m_spanStyles.isEmpty()) m_spanStyles.removeLast();
        --m_insideSpanLevel;
    }
}

void OdfTextReaderDocxBackend::elementTextS(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    if (!reader.isStartElement())
        return;

    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }

    bool ok = false;
    int count = reader.attributes().value("text:c").toInt(&ok);
    if (!ok || count < 1) {
        count = 1;
    }
    count = qMin(count, 32767);

    KoXmlWriter *writer = currentWriter(docxContext);
    startRun(reader.attributes(), docxContext);
    writer->startElement("w:t");
    writer->addAttribute("xml:space", "preserve");
    writer->addTextNode(QString(count, QLatin1Char(' ')));
    writer->endElement(); // w:t
    endRun(docxContext);
}

void OdfTextReaderDocxBackend::elementTextA(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }

    if (reader.isStartElement()) {
        // ODF spans are tracked logically; their text emits flat runs inside
        // this hyperlink, preserving both the link and nested formatting.
        const QString href = reader.attributes().value("xlink:href").toString();
        if (href.isEmpty()) {
            m_linkStack.push(false);
            return;
        }

        KoXmlWriter *writer = currentWriter(docxContext);
        writer->startElement("w:hyperlink");
        if (href.startsWith(QLatin1Char('#'))) {
            writer->addAttribute("w:anchor", href.mid(1));
        } else {
            QString sourcePart = docxContext->outputPart();
        if (m_insideComment) {
            sourcePart = QStringLiteral("word/comments.xml");
        } else if (m_insideFootnote) {
            sourcePart = m_insideEndnote ? QStringLiteral("word/endnotes.xml") : QStringLiteral("word/footnotes.xml");
        }
            const QString relationshipId = docxContext->m_docxFile->addRelationship(
                sourcePart,
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink",
                href,
                true);
            if (relationshipId.isEmpty()) {
                writer->endElement(); // w:hyperlink
                m_linkStack.push(false);
                return;
            }
            writer->addAttribute("r:id", relationshipId);
        }
        m_linkStack.push(true);
    } else if (!m_linkStack.isEmpty()) {
        const bool opened = m_linkStack.pop();
        if (opened) {
            KoXmlWriter *writer = currentWriter(docxContext);
            writer->endElement(); // w:hyperlink
        }
    }
}

void OdfTextReaderDocxBackend::elementTextLineBreak(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    if (!reader.isStartElement()) {
        return;
    }
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = currentWriter(docxContext);
    startRun(reader.attributes(), docxContext);
    writer->startElement("w:br");
    writer->endElement(); // w:br
    endRun(docxContext);
}

void OdfTextReaderDocxBackend::elementTextTab(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    if (!reader.isStartElement()) {
        return;
    }
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = currentWriter(docxContext);
    startRun(reader.attributes(), docxContext);
    writer->startElement("w:tab");
    writer->endElement(); // w:tab
    endRun(docxContext);
}

void OdfTextReaderDocxBackend::elementTextField(const QString &tagName, const KoXmlStreamAttributes &attributes,
                                                const QString &text, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext || !docxContext->m_insideParagraph) {
        return;
    }

    const QString instruction = fieldInstruction(tagName, text);
    QString cachedText = text;
    if (cachedText.isEmpty() && !instruction.isEmpty()) {
        if (tagName == QLatin1StringView("text:page-number") || tagName == QLatin1StringView("text:page-count")) {
            cachedText = QStringLiteral("1");
        } else if (tagName == QLatin1StringView("text:date")) {
            cachedText = attributes.value(QStringLiteral("text:date-value")).toString();
        } else if (tagName == QLatin1StringView("text:time")) {
            cachedText = attributes.value(QStringLiteral("text:time-value")).toString();
        }
    }
    if (instruction.isEmpty() && cachedText.isEmpty()) {
        return;
    }

    KoXmlWriter *writer = currentWriter(docxContext);
    startRun(attributes, docxContext);

    if (!instruction.isEmpty()) {
        // Complex field: begin / instruction / separate / cached result / end.
        // ODF spans are logical formatting scopes, so the field characters
        // can share this formatted run without creating nested runs.
        writer->startElement("w:fldChar");
        writer->addAttribute("w:fldCharType", "begin");
        writer->endElement(); // w:fldChar
        writer->startElement("w:instrText");
        writer->addAttribute("xml:space", "preserve");
        writer->addTextNode(instruction);
        writer->endElement(); // w:instrText
        writer->startElement("w:fldChar");
        writer->addAttribute("w:fldCharType", "separate");
        writer->endElement(); // w:fldChar
    }

    if (!cachedText.isEmpty()) {
        writer->startElement("w:t");
        writer->addAttribute("xml:space", "preserve");
        writer->addTextNode(cachedText);
        writer->endElement(); // w:t
    }

    if (!instruction.isEmpty()) {
        writer->startElement("w:fldChar");
        writer->addAttribute("w:fldCharType", "end");
        writer->endElement(); // w:fldChar
    }

    endRun(docxContext);
}

void OdfTextReaderDocxBackend::elementTextSoftPageBreak(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    // This is a producer's automatic pagination hint, not an authored break.
    // Turning it into w:br changes pagination and can split a text paragraph.
    Q_UNUSED(reader);
    Q_UNUSED(context);
}

// ----------------------------------------------------------------
//                             Notes

void OdfTextReaderDocxBackend::elementTextNote(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }

    if (reader.isStartElement()) {
        const QString noteClass = reader.attributes().value("text:note-class").toString();
        m_insideEndnote = (noteClass == QLatin1StringView("endnote"));

        // The reference marker goes at the position of the note in the
        // running text.  Word numbers notes automatically, so the ODF
        // citation text is never needed.
        KoXmlWriter *docWriter = currentWriter(docxContext); // still the text writer here
        const int id = m_insideEndnote ? ++m_endnoteIndex : ++m_footnoteIndex;
        const char *referenceElement = m_insideEndnote ? "w:endnoteReference" : "w:footnoteReference";
        if (docxContext->m_insideRun) {
            // Already inside a run (the note sits in a text:span); the
            // reference has to be a child of that open run.
            docWriter->startElement(referenceElement);
            docWriter->addAttribute("w:id", id);
            docWriter->endElement(); // reference
        } else {
            docWriter->startElement("w:r");
            docWriter->startElement(referenceElement);
            docWriter->addAttribute("w:id", id);
            docWriter->endElement(); // reference
            docWriter->endElement(); // w:r
        }

        // Everything from here on is written into the note part.
        m_noteSpanBase = m_insideSpanLevel;
        m_insideFootnote = true;
        m_footnoteNeedsReference = true;
        KoXmlWriter *notesWriter = m_insideEndnote ? docxContext->m_endnotesWriter : docxContext->m_footnotesWriter;
        notesWriter->startElement(m_insideEndnote ? "w:endnote" : "w:footnote");
        notesWriter->addAttribute("w:id", id);
    } else {
        KoXmlWriter *notesWriter = m_insideEndnote ? docxContext->m_endnotesWriter : docxContext->m_footnotesWriter;
        notesWriter->endElement(); // w:footnote/w:endnote
        m_insideFootnote = false;
        m_footnoteNeedsReference = false;
        m_insideEndnote = false;
    }
}

void OdfTextReaderDocxBackend::elementTextNoteCitation(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    Q_UNUSED(reader);
    Q_UNUSED(context);
    // Both sides number automatically: w:footnoteReference in the body and
    // w:footnoteRef in the note text replace the ODF citation.
}

void OdfTextReaderDocxBackend::elementTextNoteBody(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    Q_UNUSED(reader);
    Q_UNUSED(context);
    // The paragraphs of the body are written by elementTextP into the note
    // part; the first one picks up the w:footnoteRef marker through
    // m_footnoteNeedsReference.
}

// ----------------------------------------------------------------
//                             Lists

void OdfTextReaderDocxBackend::elementTextList(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    if (reader.isStartElement()) {
        DocxNumbering &numbering = docxContext->numbering();
        const KoXmlStreamAttributes attributes = reader.attributes();
        const QString styleName = attributes.value("text:style-name").toString();
        const QString listId = attributes.value("xml:id").toString();
        const QString continueId = attributes.value("text:continue-list").toString();

        ListState state;
        bool continued = false;
        if (!continueId.isEmpty()) {
            const auto previous = m_listContinuationStates.constFind(continueId);
            if (previous != m_listContinuationStates.constEnd()) {
                state = previous.value();
                continued = true;
            }
        }

        if (continued) {
            // Reuse the referenced list's numId so the counter continues.
        } else if (!styleName.isEmpty()) {
            const int levelCount = numbering.levelCountForStyle(styleName);
            const int firstLevel = numbering.firstLevelForStyle(styleName);
            if (levelCount == 1 && firstLevel > 1) {
                // The DOCX importer writes one single-level style per source
                // numbering level (level 2, level 5, ...) and reaches it by
                // wrapping the item in anonymous lists.  The style therefore
                // already carries the absolute level; extra nesting must not
                // increase it again.
                state.numId = numbering.numIdForStyle(styleName);
                state.ilvl = qBound(0, firstLevel - 1, 8);
                state.absoluteLevel = true;
            } else if (m_listStack.isEmpty()) {
                // A new top-level list starts at 1, even when the same style
                // was used for an earlier list.
                state.numId = numbering.newListInstance(styleName);
                state.ilvl = 0;
            } else {
                state.numId = numbering.numIdForStyle(styleName);
                state.ilvl = qMin(8, m_listStack.top().ilvl + 1);
            }
        } else if (!m_listStack.isEmpty()) {
            // <text:list> without a style inherits the enclosing list.  This
            // is how ODF represents a nested level, so keep the parent's
            // numId and only descend when the level is implicit.
            state.numId = m_listStack.top().numId;
            state.ilvl = m_listStack.top().absoluteLevel
                ? m_listStack.top().ilvl
                : qMin(8, m_listStack.top().ilvl + 1);
            state.absoluteLevel = m_listStack.top().absoluteLevel;
        } else {
            state.numId = numbering.newListInstance(QString());
            state.ilvl = 0;
        }
        if (!listId.isEmpty()) {
            m_listContinuationStates.insert(listId, state);
        }
        m_listStack.push(state);
    } else if (!m_listStack.isEmpty()) {
        m_listStack.pop();
    }
}

void OdfTextReaderDocxBackend::elementTextListItem(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    Q_UNUSED(context);
    if (reader.isStartElement()) {
        ++m_insideListItem;
    } else if (m_insideListItem > 0) {
        --m_insideListItem;
    }
}

void OdfTextReaderDocxBackend::elementTextListHeader(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    Q_UNUSED(context);
    if (reader.isStartElement()) {
        ++m_insideListItem;
    } else if (m_insideListItem > 0) {
        --m_insideListItem;
    }
}void OdfTextReaderDocxBackend::characterData(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    // debugDocx << reader.text().toString();

    if (m_insideDcCreator) {
        KoXmlWriter *commentsWriter = docxContext->m_commentsWriter;
        commentsWriter->addAttribute("w:author", reader.text().toString());
    } else if (m_insideDcDate) {
        // KoXmlWriter *commentsWriter = docxContext->m_commentsWriter;
        //  todo, convert the date and add as attribute
    } else {
        // In docx, a text always has to be inside a run (w:r). This is
        // created when a text:span is encountered in odf but text nodes
        // can exist also without a text:span surrounding it.
        KoXmlWriter *writer = currentWriter(docxContext);

        startRun(reader.attributes(), docxContext);

        const QString text = reader.text().toString();
        writer->startElement("w:t");
        if (text.startsWith(QLatin1Char(' ')) || text.endsWith(QLatin1Char(' ')) || text.contains(QStringLiteral("  "))) {
            writer->addAttribute("xml:space", "preserve");
        }
        writer->addTextNode(text);
        writer->endElement(); // w:t

        endRun(docxContext);
    }
}

// ----------------------------------------------------------------
//                         Private functions

KoXmlWriter *OdfTextReaderDocxBackend::currentWriter(OdfReaderDocxContext *context) const
{
    if (m_insideComment) {
        return context->m_commentsWriter;
    }
    if (m_insideFootnote) {
        return m_insideEndnote ? context->m_endnotesWriter : context->m_footnotesWriter;
    }
    return context->m_documentWriter;
}

void OdfTextReaderDocxBackend::startRun(const KoXmlStreamAttributes &attributes, OdfReaderDocxContext *docxContext)
{
    KoXmlWriter *writer = currentWriter(docxContext);

    if (m_writeComment && !m_insideComment && !m_insideFootnote) {
        writer->startElement("w:commentRangeStart");
        writer->addAttribute("w:id", m_commentIndex);
        writer->endElement(); // w:commentRangeStart
    }
    writer->startElement("w:r");
    writer->startElement("w:rPr");
    KoOdfStyleProperties properties;
    if (!m_currentParagraphParent.isEmpty()) {
        DocxStyleHelper::inheritTextStyles(&properties, m_currentParagraphParent, docxContext->styleManager());
    }
    if (m_currentParagraphTextProperties != nullptr) {
        properties.copyPropertiesFrom(*m_currentParagraphTextProperties);
    }

    QString wordStyleId;
    const auto applyCharacterStyle = [&](const QString &name) {
        QVector<KoOdfStyle *> chain;
        QSet<QString> visited;
        QString current = name;
        while (!current.isEmpty() && !visited.contains(current)) {
            visited.insert(current);
            KoOdfStyle *style = docxContext->styleManager()->style(current, "text");
            if (!style) break;
            chain.prepend(style);
            current = style->parent();
        }
        for (KoOdfStyle *style : chain) {
            if (auto *textProperties = style->properties("style:text-properties"))
                properties.copyPropertiesFrom(*textProperties);
            if (style->isFromStylesXml()) wordStyleId = style->name();
        }
    };
    // Text within a note must not inherit the span enclosing its body marker.
    const int firstSpan = m_insideFootnote ? m_noteSpanBase : 0;
    for (int i = firstSpan; i < m_spanStyles.size(); ++i)
        applyCharacterStyle(m_spanStyles.at(i));
    applyCharacterStyle(attributes.value("text:style-name").toString());
    if (!wordStyleId.isEmpty()) {
        writer->startElement("w:rStyle");
        writer->addAttribute("w:val", wordStyleId);
        writer->endElement();
    }
    DocxStyleHelper::handleTextStyles(&properties, writer);

    writer->endElement(); // w:rPr
    if (!m_insideFootnote) {
        docxContext->m_insideRun = true;
    }
}

void OdfTextReaderDocxBackend::endRun(OdfReaderDocxContext *docxContext)
{
    // FIXME: More here?
    KoXmlWriter *writer = currentWriter(docxContext);
    writer->endElement(); // w:r
    if (!m_insideFootnote) {
        docxContext->m_insideRun = false;
    }

    if (m_writeComment && !m_insideComment && !m_insideFootnote) {
        writer->startElement("w:commentRangeEnd");
        writer->addAttribute("w:id", m_commentIndex);
        writer->endElement(); // w:commentRangeEnd
        writer->startElement("w:r");
        writer->startElement("w:commentReference");
        writer->addAttribute("w:id", m_commentIndex);
        writer->endElement(); // w:commentReference
        writer->endElement(); // w:r
        ++m_commentIndex;
        m_writeComment = false;
    }
}
