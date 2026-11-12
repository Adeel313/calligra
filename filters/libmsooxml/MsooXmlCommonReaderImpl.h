/*
 * This file is part of Office 2007 Filters for Calligra
 *
 * SPDX-FileCopyrightText: 2009-2010 Nokia Corporation and /or its subsidiary(-ies).
 *
 * Contact: Suresh Chande suresh.chande@nokia.com
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#ifndef MSOOXMLCOMMONREADER_IMPL_H
#define MSOOXMLCOMMONREADER_IMPL_H

#include <KoXmlWriter.h>

void MSOOXML_CURRENT_CLASS::initInternal()
{
    m_insideHdr = false;
    m_insideFtr = false;
    m_hasPosOffsetH = false;
    m_hasPosOffsetV = false;
    m_posOffsetH = 0;
    m_posOffsetV = 0;
    m_currentTextStyleProperties = nullptr;
    m_read_t_args = false;
}

void MSOOXML_CURRENT_CLASS::doneInternal()
{
    delete m_currentTextStyleProperties;
    m_currentTextStyleProperties = nullptr;
}

#undef CURRENT_EL
#define CURRENT_EL t
//! t (Text)
//! ECMA-376, 17.3.3.31, p.379. (WordprocessingML)
//! ECMA-376, 21.1.2.3.11, p.3632 (DrawingML)
//! ECMA-376, 22.1.2.116, p.4226 (MathML)
/*! This element specifies the actual text for this text run. This is
  the text that is formatted using all specified body, paragraph and
  run properties. This element shall be present within a run of text.

 Parent elements:
 ----------------
 WordprocessingML:
 - r (§22.1.2.87)
 - [done] r (§17.3.2.25)

 DrawingML:
 - [done] fld (§21.1.2.2.4)
 - [done] r (§21.1.2.3.8)

 No child elements.
*/
//! @todo support all elements
KoFilter::ConversionStatus MSOOXML_CURRENT_CLASS::read_t()
{
    if (m_read_t_args) {
        READ_PROLOGUE_IF_NS(a)
    } else {
        READ_PROLOGUE
    }

    while (!atEnd()) {
        readNext();
        // debugMsooXml << *this;
        if (isCharacters()) {
            m_paragraphHasText = true;
#ifdef PPTXXMLSLIDEREADER_CPP
            QString value = text().toString();
            value.replace(QLatin1String("\r\n"), QLatin1String("\n"));
            value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
            const auto lines = value.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
            for (int i = 0; i < lines.size(); ++i) {
                if (i) {
                    body->startElement("text:line-break");
                    body->endElement();
                }
                body->addTextSpan(lines.at(i));
            }
#else
            body->addTextSpan(text().toString());
#endif
#ifdef PPTXXMLSLIDEREADER_CPP
            d->textBoxHasContent = true;
#endif
        }
        if (m_read_t_args) {
            BREAK_IF_END_OF_WITH_NS(a, CURRENT_EL)
        } else {
            BREAK_IF_END_OF(CURRENT_EL)
        }
    }
    // debugMsooXml << "{1}";

    if (m_read_t_args) {
        m_read_t_args = false;
        READ_EPILOGUE_IF_NS(a)
    } else {
        READ_EPILOGUE
    }
}

// ================================================================
//                     Namespace: mc
// ================================================================
// ARRRRRGH!

// The way that READ_PROLOGUE is defined via QUALIFIED_NAME makes it
// impossible to use it in files that handle tags both with and
// without namespaces.  This means that we cannot use READ_PROLOGUE in
// the functions below, and most likely also not the READ_IF variants.
// The above is only true when called from XmlWorksheetReader.  For Docx,
// there are always namespaces, so it doesn't apply.
// Same is true for READ_EPILOGUE.

#undef MSOOXML_CURRENT_NS
#define MSOOXML_CURRENT_NS "mc"

#undef CURRENT_EL
#define CURRENT_EL AlternateContent
//! Alternate content handler
KoFilter::ConversionStatus MSOOXML_CURRENT_CLASS::read_AlternateContent()
{
    m_choiceAccepted = false;

    while (!atEnd()) {
        readNext();
        if (isEndElement() && name() == QLatin1StringView("AlternateContent")) {
            break;
        }

        if (isStartElement()) {
            if (name() == QLatin1StringView("Choice")) {
                TRY_READ(Choice)
            } else if (!m_choiceAccepted && qualifiedName() == QLatin1StringView("mc:Fallback")) {
                TRY_READ(Fallback)
            } else {
                skipCurrentElement();
            }
        }
    }

    return KoFilter::OK;
}

#undef CURRENT_EL
#define CURRENT_EL Choice
//! Choice handler
KoFilter::ConversionStatus MSOOXML_CURRENT_CLASS::read_Choice()
{
    const QXmlStreamAttributes attrs(attributes());

    TRY_READ_ATTR_WITHOUT_NS(Requires)

    // 'Requires="v"' means that the contents of the Choice part
    // is VML. For other namespaces, accept only standard PresentationML
    // drawing elements that this reader already understands. This keeps
    // ordinary shapes and their text when a producer wraps them in an
    // extension choice (and supplies an empty fallback), while unknown
    // extension-only alternatives still use the fallback below.
    if (m_choiceAccepted) {
        skipCurrentElement();
        return KoFilter::OK;
    }
    if (Requires != "v") {
        bool parsedPresentationShape = false;
        while (!atEnd()) {
            readNext();
            if (isEndElement() && name() == QLatin1StringView("Choice")) {
                break;
            }
            if (!isStartElement()) {
                continue;
            }
#ifdef PPTXXMLSLIDEREADER_CPP
            if (qualifiedName() == QLatin1StringView("p:sp")) {
                TRY_READ_IF_NS(p, sp)
                parsedPresentationShape = true;
            } else if (qualifiedName() == QLatin1StringView("p:grpSp")) {
                TRY_READ_IF_NS(p, grpSp)
                parsedPresentationShape = true;
            } else if (qualifiedName() == QLatin1StringView("p:pic")) {
                TRY_READ_IF_NS(p, pic)
                parsedPresentationShape = true;
            } else if (qualifiedName() == QLatin1StringView("p:cxnSp")) {
                TRY_READ_IF_NS(p, cxnSp)
                parsedPresentationShape = true;
            } else if (qualifiedName() == QLatin1StringView("p:graphicFrame")) {
                TRY_READ_IF_NS(p, graphicFrame)
                parsedPresentationShape = true;
            } else {
                skipCurrentElement();
            }
#elif defined DOCXXMLDOCREADER_CPP
            if (qualifiedName() == QLatin1StringView("w:drawing")) {
                TRY_READ(drawing)
                parsedPresentationShape = true;
            } else if (qualifiedName() == QLatin1StringView("w:pict")) {
                TRY_READ(pict)
                parsedPresentationShape = true;
            } else {
                skipCurrentElement();
            }
#else
            skipCurrentElement();
#endif
        }
        if (parsedPresentationShape) {
            m_choiceAccepted = true;
        }
        return KoFilter::OK;
    }

#ifdef PPTXXMLSLIDEREADER_CPP
    // VML is not supported by this reader. In particular, OLE choices
    // omit the picture supplied by their PresentationML fallback.
    skipCurrentElement();
    return KoFilter::OK;
#endif
    m_choiceAccepted = true;
    while (!atEnd()) {
        readNext();
        if (isEndElement() && name() == QLatin1StringView("Choice")) {
            break;
        }
        if (isStartElement()) {
#ifdef PPTXXMLSLIDEREADER_CPP
            TRY_READ_IF_NS(p, oleObj)
#endif
        }
    }
    return KoFilter::OK;
}

#undef CURRENT_EL
#define CURRENT_EL Fallback
//! Fallback handler
KoFilter::ConversionStatus MSOOXML_CURRENT_CLASS::read_Fallback()
{
    while (!atEnd()) {
        readNext();
        if (isEndElement() && name() == QLatin1StringView("Fallback")) {
            break;
        }

        if (isStartElement()) {
#ifdef PPTXXMLSLIDEREADER_CPP
            TRY_READ_IF_NS(p, sp)
            ELSE_TRY_READ_IF_NS(p, oleObj)
            ELSE_TRY_READ_IF_NS(p, pic)
            ELSE_TRY_READ_IF_NS(p, graphicFrame)
#endif
#ifdef DOCXXMLDOCUMENTREADER_H
            TRY_READ_IF_NS(w, pict)
#endif
#ifdef XLSXXMLWORKSHEETREADER_CPP
            // FIXME: This Choice/Content/Fallback structure needs a more general treatment.
            if (name() == QLatin1StringView("oleObject")) {
                TRY_READ(oleObject)
            }
#endif
        }
    }
    return KoFilter::OK;
}

#endif
