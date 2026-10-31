/* This file is part of the KDE project

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef DOCXNUMBERING_H
#define DOCXNUMBERING_H

// Qt
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVector>

/** One list level, already translated from ODF to WordprocessingML terms. */
struct NumberingLevel {
    int level = 1; //!< 1-based ODF level
    QString format = QStringLiteral("bullet"); //!< w:numFmt value, e.g. "decimal", "bullet"
    QString text = QStringLiteral("\u2022"); //!< w:lvlText, with %1..%9 for the level numbers
    int start = 1; //!< w:start value
    int leftTwips = 720; //!< w:ind/@w:left
    int hangingTwips = 360; //!< w:ind/@w:hanging
};

/** Collects the list definitions used during an export and serializes them as
 *  the word/numbering.xml part.
 *
 *  ODF references a list style by name on <text:list>; OOXML instead needs a
 *  numeric w:numId pointing into word/numbering.xml.  This class maps style
 *  names to freshly allocated ids and keeps the definitions that have to be
 *  written out.
 */
class DocxNumbering
{
public:
    /** Register the levels of an ODF list style.  Called up front for every
     *  <text:list-style> found in the package. */
    void setLevels(const QString &styleName, const QVector<NumberingLevel> &levels);

    /** Return the w:numId for a list style, allocating one on first use.
     *  An unknown or empty name gets a generic bullet definition so the
     *  paragraph still shows a marker instead of silently losing the list. */
    int numIdForStyle(const QString &styleName);

    bool isEmpty() const
    {
        return m_numIds.isEmpty();
    }

    /** Serialize the collected definitions as word/numbering.xml content. */
    QByteArray toXml() const;

private:
    QHash<QString, QVector<NumberingLevel>> m_levels;
    QHash<QString, int> m_numIds;
    int m_nextNumId = 1;
};

#endif // DOCXNUMBERING_H
