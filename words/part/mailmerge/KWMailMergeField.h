/*
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KWMAILMERGEFIELD_H
#define KWMAILMERGEFIELD_H

#include <KoVariable.h>

class KoShapeLoadingContext;
class KoShapeSavingContext;

/**
 * A mail merge field shown inline in the text.
 *
 * The field is saved as the ODF standard mail merge element
 * text:database-display (table "Recipients", column-name attribute) and
 * displays either the placeholder «ColumnName» or, in preview mode or while
 * merging, the current recipient's value.
 */
class KWMailMergeField : public KoVariable
{
    Q_OBJECT
public:
    explicit KWMailMergeField(const QString &columnName = QString());
    ~KWMailMergeField() override;

    QString columnName() const;
    void setColumnName(const QString &columnName);

    /// reimplemented
    void saveOdf(KoShapeSavingContext &context) override;
    /// reimplemented
    bool loadOdf(const KoXmlElement &element, KoShapeLoadingContext &context) override;

    /// Update the displayed text from the mail merge manager.
    void refresh();
    /// Force a repaint even when the displayed text did not change.
    void forceRepaint();

protected:
    /// reimplemented
    void variableMoved(const QTextDocument *document, int posInDocument) override;

private:
    /// reimplemented: draws field shading like Microsoft Office does
    void paint(QPainter &painter,
               QPaintDevice *pd,
               const QTextDocument *document,
               const QRectF &rect,
               const QTextInlineObject &object,
               int posInDocument,
               const QTextCharFormat &format) override;

    QString m_columnName;
    const QTextDocument *m_document = nullptr;
};

#endif // KWMAILMERGEFIELD_H