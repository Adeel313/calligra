/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2006 Martin Pfeiffer <hubipete@gmx.net>
                 2009 Jeremias Epperlein <jeeree@web.de>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "SubSupElement.h"

#include "AttributeManager.h"
#include <QFontMetricsF>
#include "FormulaCursor.h"
#include "FormulaDebug.h"

#include <KoXmlReader.h>
#include <KoXmlWriter.h>

#include <QPainter>

SubSupElement::SubSupElement(BasicElement *parent, ElementType elementType)
    : FixedElement(parent)
    , m_baseElement(std::make_unique<RowElement>(this))
{
    if (elementType != SupScript) {
        m_subScript = std::make_unique<RowElement>(this);
    }
    if (elementType != SubScript) {
        m_superScript = std::make_unique<RowElement>(this);
    }
    m_elementType = elementType;
}

SubSupElement::~SubSupElement()
{
}

void SubSupElement::paint(QPainter &painter, AttributeManager *am)
{
    Q_UNUSED(painter)
    Q_UNUSED(am)
    /*do nothing as this element has no visual representation*/
}

void SubSupElement::layout(const AttributeManager *am)
{
    // Position scripts by their baselines, not half the base's ink box:
    // a short glyph such as x must still lower its subscript visibly.
    const QFontMetricsF metrics(am->font(this));
    const qreal subShift = qMax(am->doubleOf("subscriptshift", this), metrics.xHeight() * 0.45);
    const qreal supShift = qMax(am->doubleOf("superscriptshift", this), metrics.xHeight() * 0.9);
    qreal subY = 0;
    qreal supY = 0;
    if (m_subScript) {
        subY = qMax(m_baseElement->baseLine() + subShift - m_subScript->baseLine(),
                    m_baseElement->baseLine() - metrics.xHeight() * 0.65);
    }
    if (m_superScript) {
        supY = qMin(m_baseElement->baseLine() - supShift - m_superScript->baseLine(),
                    m_baseElement->height() * 0.35 - m_superScript->height());
    }
    if (m_subScript && m_superScript) {
        subY = qMax(subY, supY + m_superScript->height() + am->layoutSpacing(this));
    }
    const qreal offset = qMax(qreal(0), -supY);
    m_baseElement->setOrigin(QPointF(0, offset));
    qreal width = 0;
    qreal bottom = offset + m_baseElement->height();
    if (m_subScript) {
        m_subScript->setOrigin(QPointF(m_baseElement->width(), subY + offset));
        width = m_subScript->width();
        bottom = qMax(bottom, subY + offset + m_subScript->height());
    }
    if (m_superScript) {
        m_superScript->setOrigin(QPointF(m_baseElement->width(), supY + offset));
        width = qMax(width, m_superScript->width());
        bottom = qMax(bottom, supY + offset + m_superScript->height());
    }
    setWidth(m_baseElement->width() + width);
    setHeight(bottom);
    setBaseLine(offset + m_baseElement->baseLine());
}

const QList<BasicElement *> SubSupElement::childElements() const
{
    QList<BasicElement *> tmp;
    tmp << m_baseElement.get();
    if (m_subScript) {
        tmp << m_subScript.get();
    }
    if (m_superScript) {
        tmp << m_superScript.get();
    }
    return tmp;
}

bool SubSupElement::replaceChild(BasicElement *oldelement, BasicElement *newelement)
{
    // TODO: investigate, if we really need this check
    if (newelement->elementType() == Row) {
        RowElement *newrow = static_cast<RowElement *>(newelement);
        if (oldelement == m_baseElement.get()) {
            m_baseElement.reset(newrow);
            return true;
        } else if (oldelement == m_subScript.get()) {
            m_subScript.reset(newrow);
            return true;
        } else if (oldelement == m_superScript.get()) {
            m_superScript.reset(newrow);
            return true;
        }
    }
    return false;
}

QString SubSupElement::attributesDefaultValue(const QString &attribute) const
{
    Q_UNUSED(attribute)
    return QString();
}

ElementType SubSupElement::elementType() const
{
    return m_elementType;
}

bool SubSupElement::readMathMLContent(const KoXmlElement &parent)
{
    KoXmlElement tmp;
    int counter = 0;
    forEachElement(tmp, parent)
    {
        switch (counter) {
        case 0:
            loadElement(tmp, m_baseElement);
            break;
        case 1:
            if (m_elementType == SupScript) {
                loadElement(tmp, m_superScript);
            } else {
                // Valid for both Subscript and Subsupscript
                loadElement(tmp, m_subScript);
            }
            break;
        case 2:
            if (m_elementType == SubSupScript) {
                loadElement(tmp, m_superScript);
            } else {
                debugFormula << "Too many arguments to " << ElementFactory::elementName(m_elementType);
            }
            break;
        default:
            debugFormula << "Too many arguments to " << ElementFactory::elementName(m_elementType);
        }
        counter++;
    }

    // Check for too few arguments.
    if ((counter < 3 && m_elementType == SubSupScript) || (counter < 2)) {
        debugFormula << "Not enough arguments to " << ElementFactory::elementName(m_elementType);
        return false;
    }

    return true;
}

void SubSupElement::writeMathMLContent(KoXmlWriter *writer, const QString &ns) const
{
    // just save the children in the right order
    m_baseElement->writeMathML(writer, ns);

    if (m_elementType != SupScript)
        m_subScript->writeMathML(writer, ns);

    if (m_elementType != SubScript)
        m_superScript->writeMathML(writer, ns);
}

int SubSupElement::endPosition() const
{
    return (m_elementType == SubSupScript ? 5 : 3);
}

bool SubSupElement::setCursorTo(FormulaCursor &cursor, QPointF point)
{
    if (cursor.isSelecting()) {
        return false;
    }
    if (m_subScript && m_subScript->boundingRect().contains(point)) {
        return m_subScript->setCursorTo(cursor, point - m_subScript->origin());
    } else if (m_superScript && m_superScript->boundingRect().contains(point)) {
        return m_superScript->setCursorTo(cursor, point - m_superScript->origin());
    } else {
        return m_baseElement->setCursorTo(cursor, point - m_baseElement->origin());
    }
    return false;
}

bool SubSupElement::moveCursor(FormulaCursor &newcursor, FormulaCursor &oldcursor)
{
    // 0^1_2
    int childpos = newcursor.position() / 2;

    switch (newcursor.direction()) {
    case MoveUp:
    case MoveDown:
        if (m_elementType == SubScript) {
            return moveHorSituation(newcursor, oldcursor, 1, 0);
        } else if (m_elementType == SupScript) {
            return moveHorSituation(newcursor, oldcursor, 0, 1);
        } else {
            switch (childpos) {
            case 1:
            case 2:
                return moveVertSituation(newcursor, oldcursor, 1, 2);
            case 0:
                if (newcursor.direction() == MoveDown) {
                    return moveHorSituation(newcursor, oldcursor, 1, 0);
                } else {
                    return moveHorSituation(newcursor, oldcursor, 0, 2);
                }
            }
        }
        break;
    case MoveLeft:
    case MoveRight:
        switch (childpos) {
        case 0:
            return moveHorSituation(newcursor, oldcursor, 0, 1);
        case 1:
            return moveHorSituation(newcursor, oldcursor, 0, 1);
        case 2:
            return moveHorSituation(newcursor, oldcursor, 0, 2);
        }
        break;
    default:
        break;
    }
    return false;
}
