/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2014 Lassi Nieminen <lassniem@gmail.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef DOCXSTYLEHELPER_H
#define DOCXSTYLEHELPER_H

class KoXmlWriter;
class KoOdfStyleProperties;
class KoOdfStyleManager;

#include <QByteArray>
#include <QString>

namespace DocxStyleHelper
{
/** Additional paragraph properties that do not come from a single ODF style
 *  property set but from the surrounding document structure.  They are passed
 *  in here so that all w:pPr children can be written in schema order. */
struct ParagraphExtras {
    int numId = -1; //!< List numbering id, or -1 when the paragraph is not in a list
    int ilvl = 0; //!< List level
    int outlineLevel = -1; //!< w:outlineLvl value, or -1
    QByteArray tabStops; //!< Serialized <w:tabs> element, empty if unused
};

void handleTextStyles(KoOdfStyleProperties *properties, KoXmlWriter *writer);
void handleParagraphStyles(KoOdfStyleProperties *properties, KoXmlWriter *writer, const ParagraphExtras &extras = ParagraphExtras());
void inheritTextStyles(KoOdfStyleProperties *destinationProperties, const QString &parent, KoOdfStyleManager *manager);
};

#endif // DOCXSTYLEHELPER_H
