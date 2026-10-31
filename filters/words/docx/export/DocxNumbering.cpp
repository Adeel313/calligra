/* This file is part of the KDE project

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Own
#include "DocxNumbering.h"

// Qt
#include <QBuffer>
#include <QList>
#include <QPair>

#include <algorithm>

// Calligra
#include <KoXmlWriter.h>

namespace
{
// Default nine level bullet definition used when a document references a list
// style that is not defined anywhere in the package.
QVector<NumberingLevel> defaultLevels()
{
    QVector<NumberingLevel> levels;
    static const char *const bullets[] = {"\u2022", "\u25e6", "\u25aa", "\u2022", "\u25e6", "\u25aa", "\u2022", "\u25e6", "\u25aa"};
    for (int level = 1; level <= 9; ++level) {
        NumberingLevel entry;
        entry.level = level;
        entry.format = QStringLiteral("bullet");
        entry.text = QString::fromUtf8(bullets[level - 1]);
        entry.leftTwips = 720 * level;
        entry.hangingTwips = 360;
        levels.append(entry);
    }
    return levels;
}
}

void DocxNumbering::setLevels(const QString &styleName, const QVector<NumberingLevel> &levels)
{
    if (styleName.isEmpty() || levels.isEmpty()) {
        return;
    }
    m_levels.insert(styleName, levels);
}

int DocxNumbering::numIdForStyle(const QString &styleName)
{
    const QString key = styleName.isEmpty() ? QStringLiteral("__default__") : styleName;
    const auto existing = m_numIds.constFind(key);
    if (existing != m_numIds.constEnd()) {
        return existing.value();
    }

    const int numId = m_nextNumId++;
    m_numIds.insert(key, numId);
    if (!m_levels.contains(key)) {
        m_levels.insert(key, defaultLevels());
    }
    return numId;
}

QByteArray DocxNumbering::toXml() const
{
    QByteArray result;
    QBuffer buffer(&result);
    KoXmlWriter writer(&buffer);

    writer.startDocument(nullptr);
    writer.startElement("w:numbering");
    writer.addAttribute("xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    writer.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");

    // One abstractNum per allocated list style, in numId order so the output
    // stays stable across runs.
    QList<QPair<int, QString>> allocated;
    for (auto it = m_numIds.constBegin(); it != m_numIds.constEnd(); ++it) {
        allocated.append(qMakePair(it.value(), it.key()));
    }
    std::sort(allocated.begin(), allocated.end());

    for (const auto &entry : allocated) {
        const int numId = entry.first;
        const QVector<NumberingLevel> levels = m_levels.value(entry.second);

        writer.startElement("w:abstractNum");
        writer.addAttribute("w:abstractNumId", numId - 1);
        writer.startElement("w:multiLevelType");
        writer.addAttribute("w:val", QStringLiteral("hybridMultilevel"));
        writer.endElement(); // w:multiLevelType

        for (const NumberingLevel &level : levels) {
            const int ilvl = qBound(0, level.level - 1, 8);
            writer.startElement("w:lvl");
            writer.addAttribute("w:ilvl", ilvl);
            writer.startElement("w:start");
            writer.addAttribute("w:val", level.start);
            writer.endElement(); // w:start
            writer.startElement("w:numFmt");
            writer.addAttribute("w:val", level.format);
            writer.endElement(); // w:numFmt
            writer.startElement("w:suff");
            writer.addAttribute("w:val", "tab");
            writer.endElement(); // w:suff
            writer.startElement("w:lvlText");
            writer.addAttribute("w:val", level.text);
            writer.endElement(); // w:lvlText
            writer.startElement("w:lvlJc");
            writer.addAttribute("w:val", "left");
            writer.endElement(); // w:lvlJc
            if (level.leftTwips != 0 || level.hangingTwips != 0) {
                writer.startElement("w:pPr");
                writer.startElement("w:ind");
                writer.addAttribute("w:left", level.leftTwips);
                writer.addAttribute("w:hanging", qMax(0, level.hangingTwips));
                writer.endElement(); // w:ind
                writer.endElement(); // w:pPr
            }
            writer.endElement(); // w:lvl
        }
        writer.endElement(); // w:abstractNum
    }

    for (const auto &entry : allocated) {
        writer.startElement("w:num");
        writer.addAttribute("w:numId", entry.first);
        writer.startElement("w:abstractNumId");
        writer.addAttribute("w:val", entry.first - 1);
        writer.endElement(); // w:abstractNumId
        writer.endElement(); // w:num
    }

    writer.endElement(); // w:numbering
    writer.endDocument();
    return result;
}
