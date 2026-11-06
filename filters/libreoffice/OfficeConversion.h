/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef CALLIGRA_OFFICE_CONVERSION_H
#define CALLIGRA_OFFICE_CONVERSION_H
#include <KoFilter.h>
#include <QString>

namespace OfficeConversion
{
struct Result {
    KoFilter::ConversionStatus status = KoFilter::InternalError;
    bool backendUnavailable = false;
    QString error;
};
Result run(const QString &input, const QString &output, const QString &from, const QString &to);
}
#endif
