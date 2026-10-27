/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2013 Inge Wallin <inge@lysator.liu.se>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Own
#include "OdfTextReaderDocxBackend.h"

// Qt
#include <QtGlobal>
#include <QUrl>

// Calligra
#include <KoOdfStyle.h>
#include <KoOdfStyleManager.h>
#include <KoOdfStyleProperties.h>
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

// ================================================================
//                 class OdfTextReaderDocxBackend

OdfTextReaderDocxBackend::OdfTextReaderDocxBackend()
    : OdfTextReaderBackend()
    , m_insideSpanLevel(0)
    , m_currentOutlineLevel(-1)
    , m_commentIndex(0)
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
    KoXmlStreamAttributes attributes = reader.attributes();
    m_currentOutlineLevel = attributes.value("text:outline-level").toString().toInt();
    elementTextP(reader, context);
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

    KoXmlWriter *writer = docxContext->m_documentWriter;
    if (m_insideComment) {
        writer = docxContext->m_commentsWriter;
    }
    if (reader.isStartElement()) {
        writer->startElement("w:p");
        if (!m_cellParagraphStack.isEmpty()) {
            m_cellParagraphStack.top() = true;
        }
        // FIXME: Add paragraph attributes here
        writer->startElement("w:pPr");
        if (m_currentOutlineLevel >= 0) {
            writer->startElement("w:outlineLvl");
            writer->addAttribute("w:val", m_currentOutlineLevel);
            writer->endElement(); // w:outlineLvl
        }
        KoXmlStreamAttributes attributes = reader.attributes();
        QString textStyle = attributes.value("text:style-name").toString();
        if (!textStyle.isEmpty()) {
            KoOdfStyle *style = docxContext->styleManager()->style(textStyle, "paragraph");
            KoOdfStyleProperties *parProperties = style->properties("style:paragraph-properties");
            m_currentParagraphTextProperties = style->properties("style:text-properties");
            m_currentParagraphParent = style->parent();
            if (!m_currentParagraphParent.isEmpty()) {
                writer->startElement("w:pStyle");
                writer->addAttribute("w:val", m_currentParagraphParent);
                writer->endElement(); // w:pStyle
            }
            DocxStyleHelper::handleParagraphStyles(parProperties, writer);
            writer->startElement("w:rPr");
            DocxStyleHelper::handleTextStyles(m_currentParagraphTextProperties, writer);
            writer->endElement(); // w:rPr
        }
        // FIXME: Add paragraph properties (styling) here
        writer->endElement(); // w:pPr
    } else {
        writer->endElement(); // w:p
    }
}

void OdfTextReaderDocxBackend::elementTableTable(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = m_insideComment ? docxContext->m_commentsWriter : docxContext->m_documentWriter;
    if (reader.isStartElement()) {
        writer->startElement("w:tbl");
        writer->startElement("w:tblPr");
        writer->startElement("w:tblW");
        writer->addAttribute("w:w", 0);
        writer->addAttribute("w:type", "auto");
        writer->endElement(); // w:tblW
        writer->endElement(); // w:tblPr
    } else {
        writer->endElement(); // w:tbl
    }
}

void OdfTextReaderDocxBackend::elementTableTableRow(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }
    KoXmlWriter *writer = m_insideComment ? docxContext->m_commentsWriter : docxContext->m_documentWriter;
    if (reader.isStartElement()) {
        writer->startElement("w:tr");
    } else {
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
    KoXmlWriter *writer = m_insideComment ? docxContext->m_commentsWriter : docxContext->m_documentWriter;
    if (reader.isStartElement()) {
        writer->startElement("w:tc");
        writer->startElement("w:tcPr");
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
    KoXmlWriter *writer = m_insideComment ? docxContext->m_commentsWriter : docxContext->m_documentWriter;
    writer->startElement("w:tc");
    writer->startElement("w:tcPr");
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
        startRun(reader, docxContext);
        ++m_insideSpanLevel;
    } else {
        endRun(docxContext);
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

    KoXmlWriter *writer = m_insideComment ? docxContext->m_commentsWriter : docxContext->m_documentWriter;
    const bool ownRun = m_insideSpanLevel == 0;
    if (ownRun) {
        startRun(reader, docxContext);
    }
    writer->startElement("w:t");
    writer->addAttribute("xml:space", "preserve");
    writer->addTextNode(QString(count, QLatin1Char(' ')));
    writer->endElement(); // w:t
    if (ownRun) {
        endRun(docxContext);
    }
}

void OdfTextReaderDocxBackend::elementTextA(KoXmlStreamReader &reader, OdfReaderContext *context)
{
    DEBUG_BACKEND();
    OdfReaderDocxContext *docxContext = dynamic_cast<OdfReaderDocxContext *>(context);
    if (!docxContext) {
        return;
    }

    if (reader.isStartElement()) {
        // WordprocessingML hyperlinks contain runs, so they cannot be nested
        // inside the run opened for an enclosing ODF text:span. In that case
        // preserve the link text and omit only the hyperlink wrapper.
        if (m_insideSpanLevel > 0) {
            m_linkStack.push(false);
            return;
        }
        const QString href = reader.attributes().value("xlink:href").toString();
        if (href.isEmpty()) {
            m_linkStack.push(false);
            return;
        }

        KoXmlWriter *writer = m_insideComment ? docxContext->m_commentsWriter : docxContext->m_documentWriter;
        writer->startElement("w:hyperlink");
        if (href.startsWith(QLatin1Char('#'))) {
            writer->addAttribute("w:anchor", href.mid(1));
        } else {
            const QString sourcePart = m_insideComment ? QStringLiteral("word/comments.xml") : QStringLiteral("word/document.xml");
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
            KoXmlWriter *writer = m_insideComment ? docxContext->m_commentsWriter : docxContext->m_documentWriter;
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
    KoXmlWriter *writer = m_insideComment ? docxContext->m_commentsWriter : docxContext->m_documentWriter;
    const bool ownRun = m_insideSpanLevel == 0;
    if (ownRun) {
        startRun(reader, docxContext);
    }
    writer->startElement("w:br");
    writer->endElement(); // w:br
    if (ownRun) {
        endRun(docxContext);
    }
}

void OdfTextReaderDocxBackend::characterData(KoXmlStreamReader &reader, OdfReaderContext *context)
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
        KoXmlWriter *writer = docxContext->m_documentWriter;
        if (m_insideComment) {
            writer = docxContext->m_commentsWriter;
        }

        if (m_insideSpanLevel == 0) {
            startRun(reader, docxContext);
        }

        const QString text = reader.text().toString();
        writer->startElement("w:t");
        if (text.startsWith(QLatin1Char(' ')) || text.endsWith(QLatin1Char(' ')) || text.contains(QStringLiteral("  "))) {
            writer->addAttribute("xml:space", "preserve");
        }
        writer->addTextNode(text);
        writer->endElement(); // w:t

        if (m_insideSpanLevel == 0) {
            endRun(docxContext);
        }
    }
}

// ----------------------------------------------------------------
//                         Private functions

void OdfTextReaderDocxBackend::startRun(const KoXmlStreamReader &reader, OdfReaderDocxContext *docxContext)
{
    KoXmlWriter *writer = docxContext->m_documentWriter;
    if (m_insideComment) {
        writer = docxContext->m_commentsWriter;
    }

    if (m_writeComment && !m_insideComment) {
        writer->startElement("w:commentRangeStart");
        writer->addAttribute("w:id", m_commentIndex);
        writer->endElement(); // w:commentRangeStart
    }
    writer->startElement("w:r");
    writer->startElement("w:rPr");
    KoXmlStreamAttributes attributes = reader.attributes();
    KoOdfStyleProperties properties;
    if (!m_currentParagraphParent.isEmpty()) {
        DocxStyleHelper::inheritTextStyles(&properties, m_currentParagraphParent, docxContext->styleManager());
    }
    if (m_currentParagraphTextProperties != nullptr) {
        properties.copyPropertiesFrom(*m_currentParagraphTextProperties);
    }

    QString textStyle = attributes.value("text:style-name").toString();
    if (!textStyle.isEmpty()) {
        KoOdfStyle *style = docxContext->styleManager()->style(textStyle, "text");
        KoOdfStyleProperties *textProperties = style->properties("style:text-properties");
        if (textProperties != nullptr) {
            properties.copyPropertiesFrom(*textProperties);
        }
        QString parent = style->parent();
        if (!parent.isEmpty()) {
            writer->startElement("w:rStyle");
            writer->addAttribute("w:val", parent);
            writer->endElement(); // w:rStyle
        }
    }
    DocxStyleHelper::handleTextStyles(&properties, writer);

    writer->endElement(); // w:rPr
}

void OdfTextReaderDocxBackend::endRun(OdfReaderDocxContext *docxContext)
{
    // FIXME: More here?
    KoXmlWriter *writer = docxContext->m_documentWriter;
    if (m_insideComment) {
        writer = docxContext->m_commentsWriter;
    }
    writer->endElement(); // w:r

    if (m_writeComment && !m_insideComment) {
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
