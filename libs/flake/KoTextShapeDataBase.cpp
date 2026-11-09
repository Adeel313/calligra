/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2006, 2009-2010 Thomas Zander <zander@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KoTextShapeDataBase.h"
#include "KoTextShapeDataBase_p.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

KoTextShapeDataBasePrivate::KoTextShapeDataBasePrivate()
    : document(nullptr)
    , textAlignment(Qt::AlignLeft | Qt::AlignTop)
    , resizeMethod(KoTextShapeDataBase::NoResize)
    , autofitFontScale(0)
    , autofitLineSpacingReduction(0)
{
}

KoTextShapeDataBasePrivate::~KoTextShapeDataBasePrivate() = default;

KoTextShapeDataBase::KoTextShapeDataBase(KoTextShapeDataBasePrivate &dd)
    : d_ptr(&dd)
{
}

KoTextShapeDataBase::~KoTextShapeDataBase()
{
    delete d_ptr;
}

QTextDocument *KoTextShapeDataBase::document() const
{
    Q_D(const KoTextShapeDataBase);
    return d->document;
}

void KoTextShapeDataBase::setShapeMargins(const KoInsets &margins)
{
    Q_D(KoTextShapeDataBase);
    d->margins = margins;
}

KoInsets KoTextShapeDataBase::shapeMargins() const
{
    Q_D(const KoTextShapeDataBase);
    return d->margins;
}

void KoTextShapeDataBase::setVerticalAlignment(Qt::Alignment alignment)
{
    Q_D(KoTextShapeDataBase);
    d->textAlignment = (d->textAlignment & Qt::AlignHorizontal_Mask) | (alignment & Qt::AlignVertical_Mask);
}

Qt::Alignment KoTextShapeDataBase::verticalAlignment() const
{
    Q_D(const KoTextShapeDataBase);
    return d->textAlignment & Qt::AlignVertical_Mask;
}

void KoTextShapeDataBase::setResizeMethod(KoTextShapeDataBase::ResizeMethod method)
{
    Q_D(KoTextShapeDataBase);
    if (d->resizeMethod == method)
        return;
    d->resizeMethod = method;
}

KoTextShapeDataBase::ResizeMethod KoTextShapeDataBase::resizeMethod() const
{
    Q_D(const KoTextShapeDataBase);
    return d->resizeMethod;
}

int KoTextShapeDataBase::autofitFontScale() const
{
    Q_D(const KoTextShapeDataBase);
    return d->autofitFontScale;
}

void KoTextShapeDataBase::setAutofitFontScale(int scale)
{
    Q_D(KoTextShapeDataBase);
    d->autofitFontScale = scale;
}

int KoTextShapeDataBase::autofitLineSpacingReduction() const
{
    Q_D(const KoTextShapeDataBase);
    return d->autofitLineSpacingReduction;
}

void KoTextShapeDataBase::setAutofitLineSpacingReduction(int reduction)
{
    Q_D(KoTextShapeDataBase);
    d->autofitLineSpacingReduction = reduction;
}

void KoTextShapeDataBase::scaleFontSizes(QTextDocument *document, qreal factor)
{
    if (!document || factor <= 0.0 || qFuzzyCompare(factor, qreal(1.0)))
        return;

    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        // Runs that inherit their size from the paragraph style still have to
        // scale, so an inherited size falls back to the block or document size.
        qreal inheritedSize = block.charFormat().fontPointSize();
        if (inheritedSize <= 0)
            inheritedSize = document->defaultFont().pointSizeF();
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            QTextCharFormat format = fragment.charFormat();
            const qreal pointSize = format.fontPointSize() > 0 ? format.fontPointSize() : inheritedSize;
            if (pointSize <= 0)
                continue;
            format.setFontPointSize(qMax(qreal(1.0), pointSize * factor));
            QTextCursor cursor(document);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            cursor.setCharFormat(format);
        }
    }
}
