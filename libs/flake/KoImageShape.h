/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KOIMAGESHAPE_H
#define KOIMAGESHAPE_H

#include "flake_export.h"
#include <QRectF>

/** Image geometry exposed to exporters without depending on a shape plugin. */
class FLAKE_EXPORT KoImageShape
{
public:
    virtual ~KoImageShape() = default;
    /// Visible source rectangle, expressed as fractions of the original image.
    virtual QRectF cropRect() const = 0;
};

#endif
