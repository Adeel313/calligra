/* SPDX-License-Identifier: LGPL-2.0-or-later */
#pragma once

#include <KoShapeFactoryBase.h>

// Exposes the Office preset geometry already used by the OOXML importer.
class OfficeShapeFactory : public KoShapeFactoryBase
{
public:
    OfficeShapeFactory();
    KoShape *createDefaultShape(KoDocumentResourceManager *resources = nullptr) const override;
    KoShape *createShape(const KoProperties *properties, KoDocumentResourceManager *resources = nullptr) const override;
    KoShape *createShapeFromOdf(const KoXmlElement &element, KoShapeLoadingContext &context) override;
    bool supports(const KoXmlElement &element, KoShapeLoadingContext &context) const override;
};
