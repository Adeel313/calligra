/*
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWMailMergeFieldFactory.h"
#include "KWMailMergeField.h"

#include <KLocalizedString>
#include <KoXmlNS.h>

KWMailMergeFieldFactory::KWMailMergeFieldFactory()
    : KoInlineObjectFactoryBase("mailmergefield", TextVariable)
{
    // No template on purpose: merge fields are inserted through the
    // Mailings ribbon tab where the recipient columns are known.
    setOdfElementNames(KoXmlNS::text, QStringList() << "database-display");
}

KoInlineObject *KWMailMergeFieldFactory::createInlineObject(const KoProperties *properties) const
{
    Q_UNUSED(properties);
    return new KWMailMergeField();
}