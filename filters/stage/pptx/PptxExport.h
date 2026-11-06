/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef CALLIGRA_PPTX_EXPORT_H
#define CALLIGRA_PPTX_EXPORT_H
#include <KoFilter.h>
#include <QVariantList>
class PptxExport : public KoFilter
{
    Q_OBJECT
public:
    PptxExport(QObject *parent, const QVariantList &) : KoFilter(parent) {}
    ConversionStatus convert(const QByteArray &from, const QByteArray &to) override;
};
#endif
