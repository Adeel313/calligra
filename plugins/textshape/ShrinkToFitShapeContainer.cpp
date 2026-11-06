/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2010 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2010 Sebastian Sauer <sebsauer@kdab.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ShrinkToFitShapeContainer.h"

#include <KoShapeSavingContext.h>
#include <QTextBlock>
#include <KoParagraphStyle.h>
#include <QTextLayout>
#include <QTextFrame>
#include <QtMath>
#include <KoTextLayoutRootArea.h>

ShrinkToFitShapeContainer::ShrinkToFitShapeContainer(KoShape *childShape, KoDocumentResourceManager *documentResources)
    : KoShapeContainer(*(new ShrinkToFitShapeContainerPrivate(this, childShape)))
{
    Q_UNUSED(documentResources);
    Q_D(ShrinkToFitShapeContainer);

    setPosition(childShape->position());
    setSize(childShape->size());
    setZIndex(childShape->zIndex());
    setRunThrough(childShape->runThrough());
    rotate(childShape->rotation());
    // setTransformation(childShape->transformation());

    if (childShape->parent()) {
        childShape->parent()->addShape(this);
        childShape->setParent(nullptr);
    }

    childShape->setPosition(QPointF(0.0, 0.0)); // since its relative to my position, this won't move it
    childShape->setSelectable(false); // our ShrinkToFitShapeContainer will handle that from now on

    d->model = new ShrinkToFitShapeContainerModel(this, d);
    addShape(childShape);

    QSet<KoShape *> delegates;
    delegates << childShape;
    setToolDelegates(delegates);

    KoTextShapeData *data = dynamic_cast<KoTextShapeData *>(childShape->userData());
    Q_ASSERT(data);
    KoTextDocumentLayout *lay = qobject_cast<KoTextDocumentLayout *>(data->document()->documentLayout());
    Q_ASSERT(lay);
    QObject::connect(lay,
                     &KoTextDocumentLayout::finishedLayout,
                     static_cast<ShrinkToFitShapeContainerModel *>(d->model),
                     &ShrinkToFitShapeContainerModel::finishedLayout);
}

ShrinkToFitShapeContainer::~ShrinkToFitShapeContainer() = default;

void ShrinkToFitShapeContainer::paintComponent(QPainter &painter, const KoViewConverter &converter, KoShapePaintingContext &)
{
    Q_UNUSED(painter);
    Q_UNUSED(converter);
    // painter.fillRect(converter.documentToView(QRectF(QPointF(0,0),size())), QBrush(QColor("#ffcccc"))); // for testing
}

bool ShrinkToFitShapeContainer::loadOdf(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    Q_UNUSED(element);
    Q_UNUSED(context);
    return true;
}

void ShrinkToFitShapeContainer::saveOdf(KoShapeSavingContext &context) const
{
    Q_D(const ShrinkToFitShapeContainer);
    d->childShape->saveOdf(context);
}

ShrinkToFitShapeContainer *ShrinkToFitShapeContainer::wrapShape(KoShape *shape, KoDocumentResourceManager *documentResourceManager)
{
    Q_ASSERT(dynamic_cast<KoTextShapeData *>(shape->userData()));
    Q_ASSERT(qobject_cast<KoTextDocumentLayout *>(dynamic_cast<KoTextShapeData *>(shape->userData())->document()->documentLayout()));

    return new ShrinkToFitShapeContainer(shape, documentResourceManager);
}

void ShrinkToFitShapeContainer::tryWrapShape(KoShape *shape, const KoXmlElement &element, KoShapeLoadingContext &context)
{
    KoTextShapeData *data = dynamic_cast<KoTextShapeData *>(shape->userData());
    if (!data || data->resizeMethod() != KoTextShapeData::ShrinkToFitResize)
        return;

    KoShapeContainer *oldParent = shape->parent();
    ShrinkToFitShapeContainer *tos = wrapShape(shape, context.documentResourceManager());
    if (!tos->loadOdf(element, context)) {
        shape->setParent(oldParent);
        delete tos;
    }
}

void ShrinkToFitShapeContainer::unwrapShape(KoShape *shape)
{
    Q_ASSERT(shape->parent() == this);

    removeShape(shape);
    shape->setParent(parent());

    QSet<KoShape *> delegates = toolDelegates();
    delegates.remove(shape);
    setToolDelegates(delegates);

    shape->setPosition(position());
    shape->setSize(size());
    shape->rotate(rotation());
    shape->setSelectable(true);
}

ShrinkToFitShapeContainerModel::ShrinkToFitShapeContainerModel(ShrinkToFitShapeContainer *q, ShrinkToFitShapeContainerPrivate *d)
    : q(q)
    , d(d)
    , m_scale(1.0)
    , m_minScale(0.0)
    , m_maxScale(1.0)
    , m_dirty(14)
    , m_documentRevision(-1)
    , m_maybeUpdate(false)
{
}

void ShrinkToFitShapeContainerModel::finishedLayout()
{
    m_maybeUpdate = true;
    containerChanged(q, KoShape::SizeChanged);
    m_maybeUpdate = false;
}

void ShrinkToFitShapeContainerModel::containerChanged(KoShapeContainer *container, KoShape::ChangeType type)
{
    Q_ASSERT(container == q);
    Q_UNUSED(container);
    if (type == KoShape::SizeChanged) {
        KoTextShapeData *data = dynamic_cast<KoTextShapeData *>(d->childShape->userData());
        Q_ASSERT(data);
        KoTextLayoutRootArea *rootArea = data->rootArea();
        Q_ASSERT(rootArea);

        QSizeF shapeSize = q->size();
        QSizeF documentSize = rootArea->boundingRect().size();
        // Empty editing paragraphs at the end do not contribute visible ink.
        // Counting them can shrink a short slide caption to a fraction of its
        // intended size. Keep the paragraphs, but fit to the last visible line.
        if (data->document()->rootFrame()->childFrames().isEmpty()) {
            auto last = data->document()->lastBlock();
            const auto end = last;
            while (last.isValid() && last.text().trimmed().isEmpty() && !last.textList()) {
                last = last.previous();
            }
            if (last.isValid() && last != end && last.layout()->lineCount() > 0) {
                const qreal bottom = last.layout()->position().y() + last.layout()->boundingRect().bottom();
                documentSize.setHeight(qMin(documentSize.height(), qMax(qreal(0), bottom - rootArea->boundingRect().top())));
            }
        }

        if (shapeSize != m_shapeSize || !m_maybeUpdate || data->document()->revision() != m_documentRevision) {
            m_documentRevision = data->document()->revision();
            m_minScale = 0;
            m_maxScale = 1;
            m_dirty = 14;
            m_shapeSize = shapeSize;
            if (m_scale != 1) {
                m_scale = 1;
                d->childShape->setSize(shapeSize);
                d->childShape->setTransformation(QTransform());
                return; // Refit after layout at the new unscaled size.
            }
        }
        const qreal authoredScale = data->autofitFontScale() > 0
            ? qBound(0.001, data->autofitFontScale() / 100000.0, 1.0) : 1.0;
        m_maxScale = qMin(m_maxScale, authoredScale);
        if (m_scale > authoredScale) {
            m_scale = authoredScale;
            d->childShape->setSize(QSizeF(shapeSize.width() / m_scale, shapeSize.height() / m_scale));
            QTransform transform;
            transform.scale(m_scale, m_scale);
            d->childShape->setTransformation(transform);
            return;
        }
        if (!m_maybeUpdate || m_dirty == 0) return;
        auto lastVisible = data->document()->lastBlock();
        while (lastVisible.isValid() && lastVisible.text().trimmed().isEmpty() && !lastVisible.textList())
            lastVisible = lastVisible.previous();
        qreal maximumFont = 0;
        if (data->document()->rootFrame()->childFrames().isEmpty()) {
            for (auto block = data->document()->begin(); block.isValid() && block.position() <= lastVisible.position(); block = block.next()) {
                for (int i = 0; i < block.layout()->lineCount(); ++i) {
                    const auto line = block.layout()->lineAt(i);
                    documentSize.setHeight(qMax(documentSize.height(), block.layout()->position().y() + line.rect().bottom() - rootArea->top()));
                }
                for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment)
                    maximumFont = qMax(maximumFont, fragment.fragment().charFormat().fontPointSize());
            }
        }
        const QSizeF padding(data->leftPadding() + data->rightPadding(), data->topPadding() + data->bottomPadding());
        const QSizeF actualSize = documentSize + padding;
        const bool actualFits = actualSize.width() * m_scale <= shapeSize.width() + 0.1
            && actualSize.height() * m_scale <= shapeSize.height() + 0.1;
        if (m_dirty == -1) {
            m_dirty = 0;
            if (actualFits) return;
            m_scale = qMax(qreal(0.001), m_minScale);
            d->childShape->setSize(QSizeF(shapeSize.width() / m_scale, shapeSize.height() / m_scale));
            QTransform transform;
            transform.scale(m_scale, m_scale);
            d->childShape->setTransformation(transform);
            return;
        }
        if (data->autofitFontScale() > 0 && m_scale == authoredScale && actualFits) {
            m_dirty = 0;
            return; // Reuse a valid authored fit, including full last-line ink.
        }
        // Select font size using natural leading, then honor the authored
        // line compression when rendering. Otherwise applying compression
        // feeds back into the fitter and enlarges the font on the next pass.
        const qreal reduction = qBound(0, data->autofitLineSpacingReduction(), 80000) / 100000.0;
        if (reduction > 0) {
            auto last = data->document()->lastBlock();
            while (last.isValid() && last.text().trimmed().isEmpty() && !last.textList()) last = last.previous();
            for (auto block = data->document()->begin(); block.isValid() && block.position() <= last.position(); block = block.next()) {
                const auto format = block.blockFormat();
                const qreal fixed = format.property(KoParagraphStyle::FixedLineHeight).toDouble();
                const qreal percent = format.property(KoParagraphStyle::PercentLineHeight).toDouble();
                for (int i = 0; i < block.layout()->lineCount(); ++i) {
                    const auto line = block.layout()->lineAt(i);
                    const qreal natural = fixed > 0 ? fixed : (line.ascent() + line.descent()) * (percent > 0 ? percent / 100 : 1);
                    documentSize.rheight() += natural * reduction;
                }
            }
        }
        m_documentSize = documentSize;
        const QSizeF contentSize = documentSize
            + QSizeF(data->leftPadding() + data->rightPadding(), data->topPadding() + data->bottomPadding());
        const bool fits = contentSize.width() * m_scale <= shapeSize.width() + 0.1
            && contentSize.height() * m_scale <= shapeSize.height() + 0.1;
        if (fits) m_minScale = m_scale;
        else m_maxScale = m_scale;
        if (fits && m_scale == authoredScale) { m_dirty = 0; return; }
        // Wrapping changes the required height as the scale changes. Bracket
        // the largest fitting scale instead of averaging width/height ratios,
        // which can stop with the last lines still outside the frame.
        --m_dirty;
        qreal nextScale = m_dirty > 0 ? (m_minScale + m_maxScale) / 2 : qMax(qreal(0.001), m_minScale);
        if (m_dirty == 0 && reduction > 0 && maximumFont > 0) {
            // Condensed lines can leave room for a more legible whole-point
            // font. Try that size and accept it only after checking real ink.
            const qreal rounded = qMin(authoredScale, qCeil(maximumFont * nextScale - 0.0001) / maximumFont);
            if (rounded > nextScale + 0.00001) { nextScale = rounded; m_dirty = -1; }
        }
        if (qAbs(nextScale - m_scale) < 0.00001) { m_dirty = 0; return; }
        m_scale = nextScale;

        QSizeF newSize(shapeSize.width() / m_scale, shapeSize.height() / m_scale);
        d->childShape->setSize(newSize);

        QTransform m;
        m.scale(m_scale, m_scale);
        d->childShape->setTransformation(m);
    }
}

bool ShrinkToFitShapeContainerModel::inheritsTransform(const KoShape *child) const
{
    Q_ASSERT(child == d->childShape);
    Q_UNUSED(child);
    return true;
}

bool ShrinkToFitShapeContainerModel::isChildLocked(const KoShape *child) const
{
    Q_ASSERT(child == d->childShape);
    Q_UNUSED(child);
    return true;
}

bool ShrinkToFitShapeContainerModel::isClipped(const KoShape *child) const
{
    Q_ASSERT(child == d->childShape);
    Q_UNUSED(child);
    return false;
}
