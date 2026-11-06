/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KOMEDIASHAPE_H
#define KOMEDIASHAPE_H

#include "flake_export.h"
#include <QByteArray>
#include <QImage>
#include <QString>

/** Embedded media exposed to filters independently of playback plugins. */
class FLAKE_EXPORT KoMediaShape
{
public:
    virtual ~KoMediaShape() = default;
    virtual QByteArray mediaData() const = 0;
    virtual QString mediaFileName() const = 0;
    virtual QString mediaMimeType() const = 0;
    virtual QImage mediaPreview() const = 0;
};

#endif
