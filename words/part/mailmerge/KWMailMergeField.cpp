/*
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWMailMergeField.h"
#include "KWMailMergeManager.h"

#include <KLocalizedString>
#include <KoShapeLoadingContext.h>
#include <KoShapeSavingContext.h>
#include <KoXmlNS.h>
#include <KoXmlReader.h>
#include <KoXmlWriter.h>

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QTextDocument>
#include <QTextLayout>

KWMailMergeField::KWMailMergeField(const QString &columnName)
    : KoVariable(false)
    , m_columnName(columnName)
{
    KWMailMergeManager::instance()->registerField(this);
    refresh();
}

KWMailMergeField::~KWMailMergeField()
{
    KWMailMergeManager::instance()->unregisterField(this);
}

QString KWMailMergeField::columnName() const
{
    return m_columnName;
}

void KWMailMergeField::setColumnName(const QString &columnName)
{
    m_columnName = columnName;
    refresh();
}

void KWMailMergeField::refresh()
{
    setValue(KWMailMergeManager::instance()->displayValue(m_columnName));
}

void KWMailMergeField::forceRepaint()
{
    if (m_document) {
        const_cast<QTextDocument *>(m_document)->markContentsDirty(positionInDocument(), 0);
    }
}

void KWMailMergeField::variableMoved(const QTextDocument *document, int posInDocument)
{
    KoVariable::variableMoved(document, posInDocument);
    m_document = document;
}

void KWMailMergeField::saveOdf(KoShapeSavingContext &context)
{
    KWMailMergeManager *manager = KWMailMergeManager::instance();
    KoXmlWriter *writer = &context.xmlWriter();

    if (manager->isBaking()) {
        // Finish & Merge writes the field out as its plain value.
        writer->addTextNode(manager->value(m_columnName));
        return;
    }

    // ODF standard mail merge field (see ODF 1.2, 5.7.6 Database Fields).
    writer->startElement("text:database-display", false);
    writer->addAttribute("text:table-name", QStringLiteral("Recipients"));
    if (!m_columnName.isEmpty()) {
        writer->addAttribute("text:column-name", m_columnName);
    }
    writer->addTextNode(KWMailMergeManager::placeholderText(m_columnName));
    writer->endElement();
}

bool KWMailMergeField::loadOdf(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    Q_UNUSED(context);
    const QString columnName = element.attributeNS(KoXmlNS::text, "column-name");
    if (!columnName.isEmpty()) {
        m_columnName = columnName;
    }
    refresh();
    return true;
}

void KWMailMergeField::paint(QPainter &painter,
                             QPaintDevice *pd,
                             const QTextDocument *document,
                             const QRectF &rect,
                             const QTextInlineObject &object,
                             int posInDocument,
                             const QTextCharFormat &format)
{
    Q_UNUSED(document);
    Q_UNUSED(posInDocument);

    // Microsoft Office style field shading: placeholders are always shaded
    // and real values only while "Highlight Merge Fields" is enabled.
    KWMailMergeManager *manager = KWMailMergeManager::instance();
    const bool shaded = manager->highlightEnabled() || !manager->previewEnabled();
    if (shaded) {
        painter.save();
        painter.fillRect(rect.adjusted(-1, 0, 1, 0), QColor(0, 0, 0, 40));
        painter.restore();
    }

    // Same text rendering as KoVariable::paint().
    const QString text = value();
    QFont font(format.font(), pd);
    QTextLayout layout(text, font, pd);
    layout.setCacheEnabled(true);
    QVector<QTextLayout::FormatRange> layouts;
    QTextLayout::FormatRange range;
    range.start = 0;
    range.length = text.length();
    range.format = format;
    layouts.append(range);
    layout.setFormats(layouts);

    QTextOption option(Qt::AlignLeft | Qt::AlignAbsolute);
    if (object.isValid()) {
        option.setTextDirection(object.textDirection());
    }
    layout.setTextOption(option);
    layout.beginLayout();
    layout.createLine();
    layout.endLayout();
    layout.draw(&painter, rect.topLeft());
}