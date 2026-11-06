/*
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KWMAILMERGEFIELDFACTORY_H
#define KWMAILMERGEFIELDFACTORY_H

#include <KoInlineObjectFactoryBase.h>

/// Creates KWMailMergeField instances for ODF text:database-display elements.
class KWMailMergeFieldFactory : public KoInlineObjectFactoryBase
{
public:
    KWMailMergeFieldFactory();

    /// reimplemented
    KoInlineObject *createInlineObject(const KoProperties *properties) const override;
};

#endif // KWMAILMERGEFIELDFACTORY_H