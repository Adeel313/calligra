/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef PPTEXPORT_H
#define PPTEXPORT_H

#include <KoFilter.h>

#include <QByteArray>
#include <QVariantList>

class KoPADocument;
class KoPAPageBase;
class KoShape;

/**
 * Export filter for the legacy binary PowerPoint 97-2003 (.ppt) format.
 *
 * Writes an OLE2 compound file containing a PowerPoint Document stream,
 * Current User stream, Pictures stream and CompObj stream.  The structural
 * scaffolding is based on a minimal valid deck (see PptExportTemplate.h);
 * slide containers are cloned and populated from the ODP document.
 */
class PptExport : public KoFilter
{
    Q_OBJECT
public:
    PptExport(QObject *parent, const QVariantList &);
    ~PptExport() override;

    ConversionStatus convert(const QByteArray &from, const QByteArray &to) override;
};

#endif // PPTEXPORT_H
