/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2001 Andrea Rizzi <rizzi@kde.org>
                      Ulrich Kuettler <ulrich.kuettler@mailbox.tu-dresden.de>
                 2006 Martin Pfeiffer <hubipete@gmx.net>
   SPDX-FileCopyrightText: 2006-2007 Alfredo Beaumont Sainz <alfredo.beaumont@gmail.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "FencedElement.h"
#include "AttributeManager.h"
#include "OperatorElement.h"
#include <QPainter>
#include <QFontMetricsF>
#include <QRegularExpression>

FencedElement::FencedElement(BasicElement *parent)
    : RowElement(parent)
{
}

void FencedElement::paint(QPainter &painter, AttributeManager *am)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(am->colorOf("mathcolor", this));
    painter.drawPath(m_fence);
}

void FencedElement::layout(const AttributeManager *am)
{
    RowElement::layout(am);
    m_fence = QPainterPath();
    const QFont font = am->font(this);
    const QFontMetricsF metrics(font);
    const qreal contentHeight = height();
    const qreal contentBaseline = baseLine();
    qreal x = 0;
    auto append = [&](const QString &text, bool stretch) {
        if (text.isEmpty()) {
            return;
        }
        QPainterPath glyph;
        glyph.addText(QPointF(0, 0), font, text);
        const QRectF bounds = glyph.boundingRect();
        if (stretch && bounds.height() > 0 && contentHeight > bounds.height()) {
            QTransform scale;
            scale.scale(1, contentHeight / bounds.height());
            glyph = scale.map(glyph);
            glyph.translate(x - qMin(qreal(0), bounds.left()), -glyph.boundingRect().top());
        } else {
            glyph.translate(x - qMin(qreal(0), bounds.left()), contentBaseline);
        }
        m_fence.addPath(glyph);
        x += qMax(metrics.horizontalAdvance(text), bounds.width());
    };
    append((hasAttribute("open") ? attribute("open") : am->stringOf("open", this)), true);
    const QString separators = (hasAttribute("separators") ? attribute("separators") : am->stringOf("separators", this)).remove(QRegularExpression(QStringLiteral("\\s")));
    const auto children = childElements();
    for (int i = 0; i < children.size(); ++i) {
        BasicElement *child = children.at(i);
        child->setOrigin(QPointF(x, contentBaseline - child->baseLine()));
        x += child->width();
        if (i + 1 < children.size() && !separators.isEmpty()) {
            append(QString(separators.at(qMin(i, int(separators.size()) - 1))), false);
        }
    }
    append((hasAttribute("close") ? attribute("close") : am->stringOf("close", this)), true);
    const qreal top = qMin(qreal(0), m_fence.boundingRect().top());
    if (top < 0) {
        m_fence.translate(0, -top);
        for (BasicElement *child : children) {
            child->setOrigin(child->origin() + QPointF(0, -top));
        }
    }
    setWidth(x);
    setHeight(qMax(contentHeight - top, m_fence.boundingRect().bottom()));
    setBaseLine(contentBaseline - top);
    setChildrenBoundingRect(QRectF(0, 0, width(), height()));
}

QString FencedElement::attributesDefaultValue(const QString &attribute) const
{
    if (attribute == "open")
        return "(";
    else if (attribute == "close")
        return ")";
    else if (attribute == "separators")
        return ",";
    else
        return QString();
}

ElementType FencedElement::elementType() const
{
    return Fenced;
}
