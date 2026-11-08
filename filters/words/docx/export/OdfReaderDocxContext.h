/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2013-2014 Inge Wallin <inge@lysator.liu.se>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef ODFREADERDOCXCONTEXT_H
#define ODFREADERDOCXCONTEXT_H

// Qt
#include <QBuffer>
#include <QHash>
#include <QTextStream>

// Calligra
#include <KoFilter.h>

// libodfreader
#include "OdfReaderContext.h"

// This filter
#include "DocxNumbering.h"

class QByteArray;

class KoStore;
class KoXmlWriter;

class DocxFile;
class DocxDrawReaderBackend;

class OdfReaderDocxContext : public OdfReaderContext
{
public:
    OdfReaderDocxContext(KoStore *store, DocxFile *dxf);
    ~OdfReaderDocxContext() override;

    QByteArray documentContent() const
    {
        return m_documentContent;
    }
    QByteArray commentsContent() const
    {
        return m_commentsContent;
    }
    QByteArray footnotesContent() const
    {
        return m_footnotesContent;
    }
    QByteArray endnotesContent() const
    {
        return m_endnotesContent;
    }

    /** Temporarily redirect where the backends write their output XML.
     *  Used to generate additional parts (headers, footers) with the same
     *  backends that generate the document body.  Returns the previous
     *  writer so the caller can restore it afterwards.
     */
    KoXmlWriter *setDocumentWriter(KoXmlWriter *writer);

    /** Name of the package part currently being generated, e.g.
     *  "word/document.xml" or "word/header1.xml".  Relationships for
     *  embedded content (images, hyperlinks) must be attached to this
     *  part.
     */
    QString outputPart() const
    {
        return m_outputPart;
    }
    void setOutputPart(const QString &part)
    {
        m_outputPart = part;
    }

    /** The serialized <w:sectPr> element to append at the end of the
     *  document body (page geometry plus header/footer references).
     *  Empty when no such information was found in styles.xml.
     */
    QByteArray sectPrContent() const
    {
        return m_sectPrContent;
    }
    void setSectPrContent(const QByteArray &content)
    {
        m_sectPrContent = content;
    }

    QHash<QString, QByteArray> masterPageSections;
    QString activeMasterPage;
    //! True when at least one even-page header/footer was exported.
    bool evenAndOddHeaders = false;

    /** Tab stops of a paragraph style, already serialized as a <w:tabs>
     *  element.  Empty when the style (or its ancestors) define none. */
    QByteArray tabStopsForStyle(const QString &styleName) const
    {
        return m_tabStops.value(styleName);
    }
    void setTabStopsForStyle(const QString &styleName, const QByteArray &tabs)
    {
        m_tabStops.insert(styleName, tabs);
    }

    /** Numbering definitions collected for the document. */
    DocxNumbering &numbering()
    {
        return m_numbering;
    }
    const DocxNumbering &numbering() const
    {
        return m_numbering;
    }

private:
    // These members should be accessible to the backend but nobody else.
    // Exception: see getter above.
    friend class OdtReaderDocxBackend;
    friend class OdfTextReaderDocxBackend;
    friend class DocxDrawReaderBackend;

    DocxFile *m_docxFile; //!< Collect embedded files here
    QByteArray m_documentContent; //!< m_documentWriter writes here;
    QByteArray m_commentsContent;
    QByteArray m_footnotesContent;
    QByteArray m_endnotesContent;
    KoXmlWriter *m_documentWriter; //!< XML writer for the document contents
    KoXmlWriter *m_commentsWriter; //!< XML writer for the comments
    KoXmlWriter *m_footnotesWriter; //!< XML writer for footnotes
    KoXmlWriter *m_endnotesWriter; //!< XML writer for endnotes

    // These members should not be accessed from the outside at all.
    QBuffer m_documentIO; //!< IODevice for the XML writer
    QBuffer m_commentsIO; //!< IODevice for the Comments XML Writer
    QBuffer m_footnotesIO; //!< IODevice for the Footnotes XML Writer
    QBuffer m_endnotesIO; //!< IODevice for the Endnotes XML Writer

    QString m_outputPart = QStringLiteral("word/document.xml"); //!< Part the backends currently write relationships for
    QByteArray m_sectPrContent; //!< Serialized <w:sectPr> for the end of w:body
    QHash<QString, QByteArray> m_tabStops; //!< Paragraph style name -> serialized <w:tabs>
    DocxNumbering m_numbering; //!< List definitions used while exporting
    //! True while a w:p element is open in the current output part, so
    //! drawings that arrive outside any ODF text:p (body-level frames)
    //! can be wrapped in a paragraph as OOXML requires.
    bool m_insideParagraph = false;
    //! True while the text backend has an open w:r. Drawings inside a text
    //! span belong directly in that run and must not create a nested w:r.
    bool m_insideRun = false;
};

#endif // ODFREADERDOCXCONTEXT_H
