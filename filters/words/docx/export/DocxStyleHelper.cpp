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
#include <QRegularExpression>

const qreal DefaultFontSize = 12.0;

// ----------------------------------------------------------------
//                     class DocxStyleHelper

void DocxStyleHelper::inheritTextStyles(KoOdfStyleProperties *destinationProperties, const QString &parent, KoOdfStyleManager *manager)
{
    // Inherits text styles from paragraphs
    KoOdfStyle *style = manager ? manager->style(parent, "paragraph") : nullptr;
    if (!style) {
        return;
    }
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

// Maps an ODF underline style to the corresponding WordprocessingML value.
static QString wordUnderline(const QString &odfStyle)
{
    if (odfStyle == QLatin1String("solid")) {
        return QStringLiteral("single");
    } else if (odfStyle == QLatin1String("dotted")) {
        return QStringLiteral("dotted");
    } else if (odfStyle == QLatin1String("dash") || odfStyle == QLatin1String("long-dash")) {
        return QStringLiteral("dashed");
    } else if (odfStyle == QLatin1String("dot-dash")) {
        return QStringLiteral("dotDash");
    } else if (odfStyle == QLatin1String("dot-dot-dash")) {
        return QStringLiteral("dotDotDash");
    } else if (odfStyle == QLatin1String("wave")) {
        return QStringLiteral("wave");
    } else if (odfStyle == QLatin1String("none")) {
        return QStringLiteral("none");
    }
    return QString();
}

// Returns true when an ODF weight/style value selects the "on" state.
static bool isBoldValue(const QString &value)
{
    if (value == QLatin1String("bold") || value == QLatin1String("bolder")) {
        return true;
    }
    bool ok = false;
    const int numeric = value.toInt(&ok);
    return ok && numeric >= 600;
}

void DocxStyleHelper::handleTextStyles(KoOdfStyleProperties *properties, KoXmlWriter *writer)
{
    if (!properties) {
        return;
    }

    // The children of w:rPr have a fixed order in WordprocessingML; Word
    // ignores elements that appear out of sequence.  The order used here is:
    // rFonts, b, bCs, i, iCs, caps, smallCaps, strike/dstrike, vanish, color,
    // spacing, w, position, sz, szCs, u, shd, vertAlign, lang.

    const QString fontName = properties->attribute("style:font-name");
    const QString fontNameComplex = properties->attribute("style:font-name-complex");
    const QString fontNameAsian = properties->attribute("style:font-name-asian");
    if (!fontName.isEmpty() || !fontNameComplex.isEmpty() || !fontNameAsian.isEmpty()) {
        writer->startElement("w:rFonts");
        if (!fontName.isEmpty()) {
            writer->addAttribute("w:ascii", fontName);
            writer->addAttribute("w:hAnsi", fontName);

        }
        if (!fontNameAsian.isEmpty() || !fontName.isEmpty())
            writer->addAttribute("w:eastAsia", fontNameAsian.isEmpty() ? fontName : fontNameAsian);
        if (!fontNameComplex.isEmpty() || !fontName.isEmpty())
            writer->addAttribute("w:cs", fontNameComplex.isEmpty() ? fontName : fontNameComplex);
        writer->endElement(); // w:rFonts
    }

    // ODF styles may explicitly reset an inherited bold/italic setting, so an
    // explicit "normal" has to be written as an off switch in DOCX as well.
    const QString fontWeight = properties->attribute("fo:font-weight");
    if (!fontWeight.isEmpty()) {
        writer->startElement("w:b");
        if (!isBoldValue(fontWeight)) {
            writer->addAttribute("w:val", "0");
        }
        writer->endElement(); // w:b
    }
    const QString fontWeightC = properties->attribute("style:font-weight-complex");
    if (!fontWeightC.isEmpty()) {
        writer->startElement("w:bCs");
        if (!isBoldValue(fontWeightC)) {
            writer->addAttribute("w:val", "0");
        }
        writer->endElement(); // w:bCs
    }
    const QString fontStyle = properties->attribute("fo:font-style");
    if (!fontStyle.isEmpty()) {
        writer->startElement("w:i");
        if (fontStyle != QLatin1String("italic") && fontStyle != QLatin1String("oblique")) {
            writer->addAttribute("w:val", "0");
        }
        writer->endElement(); // w:i
    }
    const QString fontStyleC = properties->attribute("style:font-style-complex");
    if (!fontStyleC.isEmpty()) {
        writer->startElement("w:iCs");
        if (fontStyleC != QLatin1String("italic") && fontStyleC != QLatin1String("oblique")) {
            writer->addAttribute("w:val", "0");
        }
        writer->endElement(); // w:iCs
    }

    // Forced upper/lower case and small caps are separate attributes in ODF.
    const QString textTransform = properties->attribute("fo:text-transform");
    if (textTransform == QLatin1String("uppercase")) {
        writer->startElement("w:caps");
        writer->endElement(); // w:caps
    } else if (textTransform == QLatin1String("lowercase") || textTransform == QLatin1String("none")) {
        writer->startElement("w:caps");
        writer->addAttribute("w:val", "0");
        writer->endElement(); // w:caps
    }
    const QString fontVariant = properties->attribute("fo:font-variant");
    if (!fontVariant.isEmpty()) {
        writer->startElement("w:smallCaps");
        if (fontVariant != QLatin1String("small-caps")) {
            writer->addAttribute("w:val", "0");
        }
        writer->endElement(); // w:smallCaps
    }

    const QString strikeStyle = properties->attribute("style:text-line-through-style");
    if (!strikeStyle.isEmpty()) {
        if (strikeStyle == QLatin1String("none")) {
            writer->startElement("w:strike");
            writer->addAttribute("w:val", "0");
            writer->endElement(); // w:strike
        } else if (properties->attribute("style:text-line-through-type") == QLatin1String("double")) {
            writer->startElement("w:dstrike");
            writer->endElement(); // w:dstrike
        } else {
            writer->startElement("w:strike");
            writer->endElement(); // w:strike
        }
    }

    const QString display = properties->attribute("text:display");
    if (!display.isEmpty()) {
        writer->startElement("w:vanish");
        if (display != QLatin1String("none")) writer->addAttribute("w:val", "0");
        writer->endElement();
    }

    const QString textColor = properties->attribute("fo:color");
    if (textColor.startsWith(QLatin1Char('#')) && textColor.size() == 7) {
        writer->startElement("w:color");
        writer->addAttribute("w:val", textColor.mid(1));
        writer->endElement(); // w:color
    }

    // Character spacing (letter spacing) and horizontal scaling.
    const QString letterSpacing = properties->attribute("fo:letter-spacing");
    qint64 spacingTwips = 0;
    if (letterSpacing == QLatin1String("normal") || toTwips(letterSpacing, &spacingTwips)) {
        writer->startElement("w:spacing");
        writer->addAttribute("w:val", wordTwips(spacingTwips));
        writer->endElement(); // w:spacing
    }
    QString textScale = properties->attribute("style:text-scale");
    if (textScale.endsWith(QLatin1Char('%'))) {
        textScale.chop(1);
    }
    bool scaleOk = false;
    const int scale = textScale.toInt(&scaleOk);
    if (scaleOk && scale > 0 && scale <= 600) {
        writer->startElement("w:w");
        writer->addAttribute("w:val", scale);
        writer->endElement(); // w:w
    }

    const QString textPosition = properties->attribute("style:text-position");
    const QRegularExpressionMatch positionMatch = QRegularExpression(QStringLiteral("^(-?[0-9.]+)%")).match(textPosition);
    if (positionMatch.hasMatch() && qFuzzyIsNull(positionMatch.captured(1).toDouble())) {
        writer->startElement("w:position");
        writer->addAttribute("w:val", 0);
        writer->endElement();
    }

    const QString fontSize = properties->attribute("fo:font-size");
    if (!fontSize.isEmpty()) {
        const qreal sizeInHalfPoints = getHalfPoints(fontSize, DefaultFontSize * 2);
        if (sizeInHalfPoints > 0) {
            writer->startElement("w:sz");
            writer->addAttribute("w:val", static_cast<int>(qRound(sizeInHalfPoints)));
            writer->endElement(); // w:sz
        }
    }
    const QString fontSizeC = properties->attribute("style:font-size-complex");
    if (!fontSizeC.isEmpty()) {
        const qreal sizeInHalfPoints = getHalfPoints(fontSizeC, DefaultFontSize * 2);
        if (sizeInHalfPoints > 0) {
            writer->startElement("w:szCs");
            writer->addAttribute("w:val", static_cast<int>(qRound(sizeInHalfPoints)));
            writer->endElement(); // w:szCs
        }
    }

    const QString underlineStyle = properties->attribute("style:text-underline-style");
    if (!underlineStyle.isEmpty()) {
        QString wordStyle = wordUnderline(underlineStyle);
        if (underlineStyle == QLatin1String("solid") && properties->attribute("style:text-underline-type") == QLatin1String("double"))
            wordStyle = QStringLiteral("double");
        if (!wordStyle.isEmpty()) {
            writer->startElement("w:u");
            writer->addAttribute("w:val", wordStyle);
            const QString color = properties->attribute("style:text-underline-color");
            if (color.startsWith(QLatin1Char('#')) && color.size() == 7)
                writer->addAttribute("w:color", color.mid(1));
            writer->endElement(); // w:u
        }
    }

    // Character background is a shading pattern in DOCX.
    const QString backgroundColor = properties->attribute("fo:background-color");
    if (!backgroundColor.isEmpty()) {
        QString fill = backgroundColor;
        if (fill == QLatin1String("transparent")) {
            fill = QStringLiteral("auto");
        } else if (fill.startsWith(QLatin1Char('#'))) {
            fill = fill.mid(1);
        }
        writer->startElement("w:shd");
        writer->addAttribute("w:val", "clear");
        writer->addAttribute("w:color", "auto");
        writer->addAttribute("w:fill", fill);
        writer->endElement(); // w:shd
    }

    QString alignment;
    if (textPosition.startsWith(QLatin1String("super")) || (positionMatch.hasMatch() && positionMatch.captured(1).toDouble() > 0))
        alignment = QStringLiteral("superscript");
    else if (textPosition.startsWith(QLatin1String("sub")) || (positionMatch.hasMatch() && positionMatch.captured(1).toDouble() < 0))
        alignment = QStringLiteral("subscript");
    else if (positionMatch.hasMatch()) alignment = QStringLiteral("baseline");
    if (!alignment.isEmpty()) {
        writer->startElement("w:vertAlign");
        writer->addAttribute("w:val", alignment);
        writer->endElement();
    }

    const auto languageTag = [&](const QString &prefix) {
        const QString language = properties->attribute(prefix + QStringLiteral("language"));
        const QString country = properties->attribute(prefix + QStringLiteral("country"));
        return language.isEmpty() || language == QLatin1String("none") ? QString()
            : language + (country.isEmpty() || country == QLatin1String("none") ? QString() : QLatin1Char('-') + country);
    };
    const QString language = languageTag(QStringLiteral("fo:"));
    const auto scriptLanguage = [&](const QString &script) {
        const QString lang = properties->attribute(QStringLiteral("style:language-") + script);
        const QString country = properties->attribute(QStringLiteral("style:country-") + script);
        return lang.isEmpty() || lang == QLatin1String("none") ? QString()
            : lang + (country.isEmpty() || country == QLatin1String("none") ? QString() : QLatin1Char('-') + country);
    };
    const QString asianLanguage = scriptLanguage(QStringLiteral("asian"));
    const QString complexLanguage = scriptLanguage(QStringLiteral("complex"));
    if (!language.isEmpty() || !asianLanguage.isEmpty() || !complexLanguage.isEmpty()) {
        writer->startElement("w:lang");
        if (!language.isEmpty()) writer->addAttribute("w:val", language);
        if (!asianLanguage.isEmpty()) writer->addAttribute("w:eastAsia", asianLanguage);
        if (!complexLanguage.isEmpty()) writer->addAttribute("w:bidi", complexLanguage);
        writer->endElement();
    }

}

void DocxStyleHelper::handleParagraphStyles(KoOdfStyleProperties *properties, KoXmlWriter *writer, const ParagraphExtras &extras)
{
    if (!properties && extras.numId < 0 && extras.outlineLevel < 0 && extras.tabStops.isEmpty()) {
        return;
    }

    // The children of w:pPr have a fixed order in WordprocessingML.  Word
    // silently drops or misinterprets elements that appear out of sequence,
    // so this function writes them in schema order:
    // keepNext, keepLines, pageBreakBefore, numPr, pBdr, shd, tabs, bidi,
    // spacing, ind, jc, outlineLvl.

    QString breakBefore;
    QString keepTogether;
    QString keepWithNext;
    QString backgroundColor;
    QString border;
    QString writingMode;
    QString marginTop;
    QString marginBottom;
    QString marginLeft;
    QString marginRight;
    QString textIndent;
    QString lineHeight;
    QString textAlign;
    if (properties) {
        breakBefore = properties->attribute("fo:break-before");
        keepTogether = properties->attribute("fo:keep-together");
        keepWithNext = properties->attribute("fo:keep-with-next");
        backgroundColor = properties->attribute("fo:background-color");
        border = properties->attribute("fo:border");
        writingMode = properties->attribute("style:writing-mode");
        marginTop = properties->attribute("fo:margin-top");
        marginBottom = properties->attribute("fo:margin-bottom");
        marginLeft = properties->attribute("fo:margin-left");
        marginRight = properties->attribute("fo:margin-right");
        textIndent = properties->attribute("fo:text-indent");
        lineHeight = properties->attribute("fo:line-height");
        textAlign = properties->attribute("fo:text-align");
    }

    // Keep controls.
    if (!keepWithNext.isEmpty()) {

        writer->startElement("w:keepNext");
        if (keepWithNext != QLatin1String("always")) writer->addAttribute("w:val", "0");
        writer->endElement(); // w:keepNext
    }
    if (!keepTogether.isEmpty()) {
        writer->startElement("w:keepLines");
        if (keepTogether != QLatin1String("always")) writer->addAttribute("w:val", "0");
        writer->endElement(); // w:keepLines
    }
    if (breakBefore == QLatin1String("page") || breakBefore == QLatin1String("auto")) {
        writer->startElement("w:pageBreakBefore");
        if (breakBefore == QLatin1String("auto")) writer->addAttribute("w:val", "0");
        writer->endElement(); // w:pageBreakBefore
    }

    // List numbering.
    if (extras.numId >= 0) {
        writer->startElement("w:numPr");
        writer->startElement("w:ilvl");
        writer->addAttribute("w:val", qBound(0, extras.ilvl, 8));
        writer->endElement(); // w:ilvl
        writer->startElement("w:numId");
        writer->addAttribute("w:val", extras.numId);
        writer->endElement(); // w:numId
        writer->endElement(); // w:numPr
    }

    // ODF permits a border on an individual edge (the proposal's cover
    // rule is fo:border-bottom), as well as a shorthand for all four.
    const QStringList edges = {QStringLiteral("top"), QStringLiteral("left"), QStringLiteral("bottom"), QStringLiteral("right")};
    bool hasBorder = !border.isEmpty();
    if (properties) {
        for (const QString &edge : edges) hasBorder |= !properties->attribute(QStringLiteral("fo:border-") + edge).isEmpty();
    }
    if (hasBorder) {
        writer->startElement("w:pBdr");
        for (const QString &edge : edges) {
            QString value = properties ? properties->attribute(QStringLiteral("fo:border-") + edge) : QString();
            if (value.isEmpty()) value = border;
            if (value.isEmpty()) continue;
            QString width, color = QStringLiteral("auto"), style = QStringLiteral("single");
            for (const QString &part : value.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                if (part.startsWith(QLatin1Char('#'))) color = part.mid(1);
                else if (part == QLatin1String("none")) style = QStringLiteral("nil");
                else if (part == QLatin1String("dashed") || part == QLatin1String("dotted") || part == QLatin1String("double")) style = part;
                else if (part.endsWith(QLatin1String("pt")) || part.endsWith(QLatin1String("in"))
                         || part.endsWith(QLatin1String("cm")) || part.endsWith(QLatin1String("mm"))) width = part;
            }
            qint64 twips = 0;
            const int size = toTwips(width, &twips) && twips > 0 ? qBound(2, int(qRound(twips * .4)), 96) : 4;
            writer->startElement((QStringLiteral("w:") + edge).toLatin1().constData());
            writer->addAttribute("w:val", style);
            writer->addAttribute("w:sz", size);
            QString padding = properties ? properties->attribute(QStringLiteral("fo:padding-") + edge) : QString();
            if (padding.isEmpty() && properties) padding = properties->attribute(QStringLiteral("fo:padding"));
            qint64 paddingTwips = 0;
            writer->addAttribute("w:space", toTwips(padding, &paddingTwips) ? qBound(0, int(qRound(paddingTwips / 20.0)), 31) : 0);
            writer->addAttribute("w:color", color);
            writer->endElement();
        }
        writer->endElement();
    }

    // Paragraph background.
    if (!backgroundColor.isEmpty()) {
        writer->startElement("w:shd");
        writer->addAttribute("w:val", "clear");
        writer->addAttribute("w:color", "auto");
        writer->addAttribute("w:fill", backgroundColor == QLatin1String("transparent") ? "auto" : backgroundColor.mid(backgroundColor.startsWith(QLatin1Char('#')) ? 1 : 0));
        writer->endElement(); // w:shd
    }

    // Tab stops collected from the ODF paragraph style.
    if (!extras.tabStops.isEmpty()) {
        writer->addCompleteElement(extras.tabStops.constData());
    }

    // Writing direction. Word needs an explicit bidi flag for right-to-left
    // paragraphs, otherwise the alignment and indentation are mirrored.
    if (writingMode == QLatin1String("rl-tb")) {
        writer->startElement("w:bidi");
        writer->endElement(); // w:bidi
    } else if (writingMode == QLatin1String("lr-tb")) {
        writer->startElement("w:bidi");
        writer->addAttribute("w:val", "0");
        writer->endElement(); // w:bidi
    }

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
    if (!textAlign.isEmpty()) {
        QString value;
        if (textAlign == QLatin1String("center")) {
            value = QStringLiteral("center");
        } else if (textAlign == QLatin1String("start")) {
            value = writingMode == QLatin1String("rl-tb") ? QStringLiteral("right") : QStringLiteral("left");
        } else if (textAlign == QLatin1String("left")) {
            value = QStringLiteral("left");
        } else if (textAlign == QLatin1String("end")) {
            value = writingMode == QLatin1String("rl-tb") ? QStringLiteral("left") : QStringLiteral("right");
        } else if (textAlign == QLatin1String("right")) {
            value = QStringLiteral("right");
        } else if (textAlign == QLatin1String("justify")) {
            value = QStringLiteral("both");
        }
        if (!value.isEmpty()) {
            writer->startElement("w:jc");
            writer->addAttribute("w:val", value);
            writer->endElement(); // w:jc
        }
    }

    if (extras.outlineLevel >= 0) {
        writer->startElement("w:outlineLvl");
        writer->addAttribute("w:val", qBound(0, extras.outlineLevel, 8));
        writer->endElement(); // w:outlineLvl
    }
}
