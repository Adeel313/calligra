/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Adeel <adeel@example.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef ODTTODOC_EXPORT_H
#define ODTTODOC_EXPORT_H

#include <KoFilter.h>

#include <QVariantList>

class DocExport : public KoFilter
{
    Q_OBJECT

public:
    DocExport(QObject *parent, const QVariantList &);
    ~DocExport() override;

    KoFilter::ConversionStatus convert(const QByteArray &from, const QByteArray &to) override;
};

#endif
