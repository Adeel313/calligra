/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2006-2007 Alfredo Beaumont Sainz <alfredo.beaumont@gmail.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "IdentifierElement.h"

#include "AttributeManager.h"
#include "Dictionary.h"
#include "FormulaDebug.h"

#include <QFontMetricsF>

IdentifierElement::IdentifierElement(BasicElement *parent)
    : TokenElement(parent)
{
}

QRectF IdentifierElement::renderToPath(const QString &raw, QPainterPath &path) const
{
    AttributeManager manager;
    QFont font = manager.font(this);
    if (manager.stringOf("mathvariant", this).isEmpty() && raw.size() == 1) {
        font.setItalic(true);
    }
    QString text = raw;
    if (manager.stringOf("mathvariant", this) == QLatin1String("double-struck")) {
        text.clear();
        const QString capitals = QString::fromUtf8("𝔸𝔹ℂ𝔻𝔼𝔽𝔾ℍ𝕀𝕁𝕂𝕃𝕄ℕ𝕆ℙℚℝ𝕊𝕋𝕌𝕍𝕎𝕏𝕐ℤ");
        const auto codepoints = capitals.toUcs4();
        for (QChar ch : raw) {
            char32_t codepoint = ch.unicode();
            if (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) {
                codepoint = codepoints.at(ch.unicode() - 'A');
            } else if (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) {
                codepoint = 0x1D552 + ch.unicode() - 'a';
            }
            text += QString::fromUcs4(&codepoint, 1);
        }
    }
    path.addText(path.currentPosition(), font, text);
    QFontMetricsF fm(font);
    return fm.boundingRect(text);
}

ElementType IdentifierElement::elementType() const
{
    return Identifier;
}
