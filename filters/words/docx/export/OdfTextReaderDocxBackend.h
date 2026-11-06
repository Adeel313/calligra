/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2013 Inge Wallin <inge@lysator.liu.se>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef ODFTEXTREADERDOCXBACKEND_H
#define ODFTEXTREADERDOCXBACKEND_H

// Calligra
#include <QHash>
#include <QStack>
#include <QVector>
#include <KoFilter.h>
#include <KoXmlStreamReader.h>
#include <OdfTextReaderBackend.h>

class KoOdfStyleProperties;
class KoXmlWriter;
class OdfReaderContext;
class OdfReaderDocxContext;

class OdfTextReaderDocxBackend : public OdfTextReaderBackend
{
public:
    OdfTextReaderDocxBackend();
    ~OdfTextReaderDocxBackend() override;
    // ----------------------------------------------------------------
    // Text level functions: paragraphs, headings, sections, frames, objects, etc

    void elementTextH(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextP(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextSection(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextTableOfContent(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextIndexBody(KoXmlStreamReader &reader, OdfReaderContext *context) override;

    // ----------------------------------------------------------------
    // Paragraph level functions: spans, annotations, notes, text content itself, etc.

    void elementOfficeAnnotation(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTableTable(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTableTableRow(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTableTableCell(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTableCoveredTableCell(KoXmlStreamReader &reader, OdfReaderContext *context) override;

    void elementDcCreator(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementDcDate(KoXmlStreamReader &reader, OdfReaderContext *context) override;

    void elementTextSpan(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextS(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextTab(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextA(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextNote(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextNoteCitation(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextNoteBody(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextLineBreak(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextSoftPageBreak(KoXmlStreamReader &reader, OdfReaderContext *context) override;

    void elementTextField(const QString &tagName, const KoXmlStreamAttributes &attributes,
                          const QString &text, OdfReaderContext *context) override;

    void elementTextList(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextListItem(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementTextListHeader(KoXmlStreamReader &reader, OdfReaderContext *context) override;

    void elementTableTableColumn(KoXmlStreamReader &reader, OdfReaderContext *context) override;

    void characterData(KoXmlStreamReader &reader, OdfReaderContext *context) override;

private:
    void startRun(const KoXmlStreamAttributes &attributes, OdfReaderDocxContext *context);
    void endRun(OdfReaderDocxContext *context);
    KoXmlWriter *currentWriter(OdfReaderDocxContext *context) const;
    void writeSectionBreak(OdfReaderDocxContext *context, int columns, int gapTwips);
    void writeTableBorders(KoOdfStyleProperties *properties, KoXmlWriter *writer);
    void writeCellBorders(KoOdfStyleProperties *properties, KoXmlWriter *writer);

private:
    /// State of one open table, so nested tables work as well.
    struct TableState {
        QVector<int> columnWidths; //!< twips per column, 0 when unknown
        QVector<int> rowSpanRemaining; //!< rows still covered below, per column
        QVector<bool> rowSpanActive; //!< columns covered by a row span in this row
        int currentColumn = 0;
        bool gridOpen = false;
        qreal widthScale = 1.0; //!< scale applied to absolute column widths
    };

private:
    int m_insideSpanLevel; // Number of nested <text:span> levels.
    QVector<QString> m_spanStyles; // Logical ODF spans; emitted runs stay flat.
    int m_currentOutlineLevel;
    int m_commentIndex;
    int m_insideSection = 0; //!< Number of open <text:section> elements.
    QStack<QString> m_paragraphBreakAfter;
    int m_tocLevels = 3;
    bool m_tocFieldOpen = false; //!< true between TOC field begin and end.
    bool m_bodyHasContent = false; //!< A body paragraph or table has been written.
    bool m_lastWasSectionBreak = false; //!< Last body output was a section break.
    int m_insideListItem; // Number of open <text:list-item>/<text:list-header> elements.
    bool m_writeComment;
    bool m_insideComment;
    bool m_insideDcCreator;
    bool m_insideDcDate;
    bool m_insideFootnote = false;
    bool m_footnoteNeedsReference = false;
    bool m_insideEndnote = false; //!< true while writing an endnote instead of a footnote
    int m_footnoteIndex = 0;
    int m_endnoteIndex = 0;
    //! <text:span> nesting level captured when a note started, so content
    //! inside the note body can tell whether it owns its run relative to
    //! the note instead of relative to the whole document.
    int m_noteSpanBase = 0;
    KoOdfStyleProperties *m_currentParagraphTextProperties;
    QString m_currentParagraphParent;
    /// State of one open <text:list>.  The ODF level of a list can either be
    /// implicit (nesting depth) or explicit (the style defines a single level,
    /// as produced by the DOCX importer), so both are tracked.
    struct ListState {
        int numId = 1; //!< w:numId used by paragraphs in this list
        int ilvl = 0; //!< 0-based w:ilvl used by paragraphs in this list
        bool absoluteLevel = false; //!< style already encodes the absolute level
    };
    QStack<ListState> m_listStack;
    /// Style names of the open <text:section> elements (innermost on top).
    QStack<QString> m_sectionStyles;
    /// xml:id -> numbering state, so text:continue-list can reuse the numId
    /// of an earlier <text:list> instead of restarting at 1.
    QHash<QString, ListState> m_listContinuationStates;
    QStack<bool> m_linkStack;
    QStack<bool> m_cellParagraphStack;
    QStack<TableState> m_tables;
};

#endif // ODFTEXTREADERDOCXBACKEND_H
