/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef CALLIGRA_XLSX_EXPORT_H
#define CALLIGRA_XLSX_EXPORT_H
#include <KoFilter.h>
#include <QVariantList>
class XlsxExport : public KoFilter
{
    Q_OBJECT
public:
    XlsxExport(QObject *parent, const QVariantList &) : KoFilter(parent) {}
    ConversionStatus convert(const QByteArray &from, const QByteArray &to) override;
};
#endif
