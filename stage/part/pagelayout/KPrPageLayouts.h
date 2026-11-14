/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2008 Thorsten Zachmann <zachmann@kde.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (  at your option ) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef KPRPAGELAYOUTS_H
#define KPRPAGELAYOUTS_H

#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>

#include "stage_export.h"

class QRectF;
class KPrPageLayout;
class KPrPageLayoutWrapper;
class KoPALoadingContext;
class KoPASavingContext;

class STAGE_EXPORT KPrPageLayouts : public QObject
{
    Q_OBJECT
public:
    explicit KPrPageLayouts(QObject *parent = nullptr);
    ~KPrPageLayouts() override;

    bool saveOdf(KoPASavingContext &context) const;

    /**
     * load all not yet loaded styles and add application styles
     */
    bool loadOdf(KoPALoadingContext &context);

    /**
     *
     */
    KPrPageLayout *pageLayout(const QString &name, KoPALoadingContext &loadingContext, const QRectF &pageRect);

    const QList<KPrPageLayout *> layouts() const;

    /// The layouts defined by the document itself, without Stage's built-in defaults.
    QList<KPrPageLayout *> documentLayouts() const;

    /// The document's Title and Content layout, with a built-in fallback.
    KPrPageLayout *defaultContentLayout();

private:
    // this is a simulation of a std::set<KPrPageLayout, compareByKPrPageLayout>()
    QMap<KPrPageLayoutWrapper, KPrPageLayout *> m_pageLayouts;
    QList<KPrPageLayout *> m_builtinLayouts; //!< entries of m_pageLayouts added from Stage's defaults
};

Q_DECLARE_METATYPE(KPrPageLayouts *)
#endif /* KPRPAGELAYOUTS_H */
