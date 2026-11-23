/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2018 Dag Andersen <danders@get2net.dk>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef CALLOUTCONTAINERMODEL_H
#define CALLOUTCONTAINERMODEL_H

#include "KoShapeContainerDefaultModel.h"

#include <QHash>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

class KoShapeContainer;
class CalloutShape;
class PathShape;
class KoShape;

class CalloutContainerModel : public KoShapeContainerDefaultModel
{
public:
    CalloutContainerModel();

    void containerChanged(KoShapeContainer *container, KoShape::ChangeType type) override;

    void childChanged(KoShape *shape, KoShape::ChangeType type) override;

    bool isChildLocked(const KoShape *child) const override;

    void setIgnore(KoShape *shape, bool state);

    bool ignore(KoShape *shape) const;

    void resizePath(PathShape *path, const QPointF &newPos, const QSizeF &newSize);

    /**
     * Forgets the previous size and position before the shape is filled from
     * a document. The size change that arrives next comes from the document,
     * not from a user resize, so the pointer must be taken from the document
     * as-is instead of being derived from the previous size.
     */
    void beginLoad();

    /**
     * Fits the path to the container's current size and position while
     * keeping the callout pointer (the modifiers) exactly where the document
     * put it.
     *
     * resizePath() normally derives the pointer from the previous size, which
     * would be wrong while loading: the size change it sees is the one from
     * the document, not one the user made. This therefore pretends there is
     * no previous size, so only the plain layout part of resizePath() runs.
     */
    void layoutPath(KoShapeContainer *container);

private:
    QTransform m_prevTrans;
    QSizeF m_prevSize;
    QPointF m_prevPosition;
    bool m_resizing;
    QHash<KoShape *, bool> m_ignore;
};

#endif /* CALLOUTCONTAINERMODEL_H */
