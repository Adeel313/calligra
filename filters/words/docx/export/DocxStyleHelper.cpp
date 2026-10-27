/* This file is part of the KDE project

   SPDX-FileCopyrightText: 2014 Lassi Nieminen <lassniem@gmail.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

// Own
#include "DocxStyleHelper.h"

#include "DocxExportDebug.h"

// Calligra
#include <KoOdfStyle.h>
#include <KoOdfStyleManager.h>
#include <KoOdfStyleProperties.h>
#include <KoUnit.h>
#include <KoXmlWriter.h>

#include <cmath>
#include <limits>
#include <QtMath>

const qreal DefaultFontSize = 12.0;

// ----------------------------------------------------------------
//                     class DocxStyleHelper

void DocxStyleHelper::inheritTextStyles(KoOdfStyleProperties *destinationProperties, const QString &parent, KoOdfStyleManager *manager)
{
    // Inherits text styles from paragraphs
    KoOdfStyle *style = manager->style(parent, "paragraph");
    QString ancestor = style->parent();
    if (!ancestor.isEmpty()) {
        inheritTextStyles(destinationProperties, ancestor, manager);
    }
    KoOdfStyleProperties *properties = style->properties("style:text-properties");
    if (properties) {
        destinationProperties->copyPropertiesFrom(*properties);
    }
}

static qreal getHalfPoints(const QString &fontSize, qreal defaultSize)
{
    const qreal points = KoUnit::parseValue(fontSize, std::numeric_limits<qreal>::quiet_NaN());
    if (!std::isfinite(points) || points <= 0) {
        warnDocx << "Unsupported font size:" << fontSize;
        return defaultSize;
    }
    return points * 2.0;
}

static bool toTwips(const QString &value, qint64 *twips)
{
    if (value.isEmpty()) {
        return false;
    }
    const qreal points = KoUnit::parseValue(value, std::numeric_limits<qreal>::quiet_NaN());
    if (!std::isfinite(points)) {
        return false;
    }
    *twips = qRound64(points * 20.0);
    return true;
}

static int wordTwips(qint64 value)
{
    const qint64 minimum = std::numeric_limits<int>::min();
    const qint64 maximum = std::numeric_limits<int>::max();
    return static_cast<int>(qBound(minimum, value, maximum));
}

void DocxStyleHelper::handleTextStyles(KoOdfStyleProperties *properties, KoXmlWriter *writer)
{
    if (!properties) {
        return;
    }

    const QString fontName = properties->attribute("style:font-name");
    if (!fontName.isEmpty()) {
        writer->startElement("w:rFonts");
        writer->addAttribute("w:ascii", fontName);
        writer->addAttribute("w:hAnsi", fontName);
        writer->addAttribute("w:eastAsia", fontName);
        writer->addAttribute("w:cs", fontName);
        writer->endElement(); // w:rFonts
    }

    QString fontWeight = properties->attribute("fo:font-weight");
    if (fontWeight == "bold") {
        writer->startElement("w:b");
        writer->addAttribute("w:val", "1");
        writer->endElement(); // w:b
    }
    QString fontWeightC = properties->attribute("style:font-weight-complex");
    if (fontWeightC == "bold") {
        writer->startElement("w:bCs");
        writer->addAttribute("w:val", "1");
        writer->endElement(); // w:bCs
    }
    QString fontStyle = properties->attribute("fo:font-style");
    if (fontStyle == "italic") {
        writer->startElement("w:i");
        writer->addAttribute("w:val", "1");
        writer->endElement(); // w:i
    }
    QString fontStyleC = properties->attribute("style:font-style-complex");
    if (fontStyleC == "italic") {
        writer->startElement("w:iCs");
        writer->addAttribute("w:val", "1");
        writer->endElement(); // w:iCs
    }
    QString textColor = properties->attribute("fo:color");
    if (textColor.startsWith(QLatin1Char('#')) && textColor.size() == 7) {
        writer->startElement("w:color");
        writer->addAttribute("w:val", textColor.mid(1));
        writer->endElement(); // w:color
    }

    const QString fontSize = properties->attribute("fo:font-size");
    if (!fontSize.isEmpty()) {
        const qreal sizeInHalfPoints = getHalfPoints(fontSize, DefaultFontSize * 2);
        if (sizeInHalfPoints > 0) {
            writer->startElement("w:sz");
            writer->addAttribute("w:val", sizeInHalfPoints);
            writer->endElement(); // w:sz
        }
    }
    const QString fontSizeC = properties->attribute("fo:font-size-complex");
    if (!fontSizeC.isEmpty()) {
        const qreal sizeInHalfPoints = getHalfPoints(fontSizeC, DefaultFontSize * 2);
        if (sizeInHalfPoints > 0) {
            writer->startElement("w:szCs");
            writer->addAttribute("w:val", sizeInHalfPoints);
            writer->endElement(); // w:szCs
        }
    }

    const QString textPosition = properties->attribute("style:text-position");
    if (textPosition == QLatin1String("super") || textPosition == QLatin1String("sub")) {
        writer->startElement("w:vertAlign");
        writer->addAttribute("w:val", textPosition == QLatin1String("super") ? "superscript" : "subscript");
        writer->endElement(); // w:vertAlign
    }

    QString underlineStyle = properties->attribute("style:text-underline-style");
    if (!underlineStyle.isEmpty()) {
        if (underlineStyle == "solid") {
            writer->startElement("w:u");
            writer->addAttribute("w:val", "single");
            writer->endElement(); //: u
        }
    }
}

void DocxStyleHelper::handleParagraphStyles(KoOdfStyleProperties *properties, KoXmlWriter *writer)
{
    if (!properties) {
        return;
    }

    const QString marginTop = properties->attribute("fo:margin-top");
    const QString marginBottom = properties->attribute("fo:margin-bottom");
    const QString marginLeft = properties->attribute("fo:margin-left");
    const QString marginRight = properties->attribute("fo:margin-right");
    const QString textIndent = properties->attribute("fo:text-indent");
    const QString lineHeight = properties->attribute("fo:line-height");
    qint64 before = 0;
    qint64 after = 0;
    qint64 left = 0;
    qint64 right = 0;
    qint64 indent = 0;
    const bool hasBefore = toTwips(marginTop, &before);
    const bool hasAfter = toTwips(marginBottom, &after);
    const bool hasLeft = toTwips(marginLeft, &left);
    const bool hasRight = toTwips(marginRight, &right);
    const bool hasIndent = toTwips(textIndent, &indent);
    bool hasLine = false;
    qint64 line = 0;
    QString lineRule;
    if (lineHeight.endsWith(QLatin1Char('%'))) {
        bool ok = false;
        const qreal percentage = lineHeight.left(lineHeight.length() - 1).toDouble(&ok);
        if (ok && std::isfinite(percentage) && percentage > 0) {
            line = qRound64(percentage * 2.4);
            lineRule = QStringLiteral("auto");
            hasLine = true;
        }
    } else if (lineHeight != QLatin1String("normal") && toTwips(lineHeight, &line)) {
        lineRule = QStringLiteral("exact");
        hasLine = true;
    }
    if (hasBefore || hasAfter || hasLine) {
        writer->startElement("w:spacing");
        if (hasBefore) {
            writer->addAttribute("w:before", wordTwips(before));
        }
        if (hasAfter) {
            writer->addAttribute("w:after", wordTwips(after));
        }
        if (hasLine) {
            writer->addAttribute("w:lineRule", lineRule);
            writer->addAttribute("w:line", wordTwips(line));
        }
        writer->endElement(); // w:spacing
    }
    if (hasLeft || hasRight || hasIndent) {
        writer->startElement("w:ind");
        if (hasLeft) {
            writer->addAttribute("w:left", wordTwips(left));
        }
        if (hasRight) {
            writer->addAttribute("w:right", wordTwips(right));
        }
        if (hasIndent) {
            if (indent < 0) {
                writer->addAttribute("w:hanging", wordTwips(-indent));
            } else {
                writer->addAttribute("w:firstLine", wordTwips(indent));
            }
        }
        writer->endElement(); // w:ind
    }
    QString textAlign = properties->attribute("fo:text-align");
    if (!textAlign.isEmpty()) {
        writer->startElement("w:jc");
        if (textAlign == "center") {
            writer->addAttribute("w:val", "center");
        } else if (textAlign == "start") {
            writer->addAttribute("w:val", "left");
        } else if (textAlign == "left") {
            writer->addAttribute("w:val", "left");
        } else if (textAlign == "end") {
            writer->addAttribute("w:val", "right");
        } else if (textAlign == "right") {
            writer->addAttribute("w:val", "right");
        } else if (textAlign == "justify") {
            writer->addAttribute("w:val", "both");
        }
        writer->endElement(); // w:jc
    }
}
