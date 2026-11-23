/* This file is part of the KDE project
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KWRESEARCHDOCKERFACTORY_H
#define KWRESEARCHDOCKERFACTORY_H

#include <KoDockFactoryBase.h>

class KWResearchDockerFactory : public KoDockFactoryBase
{
public:
    KWResearchDockerFactory();
    QString id() const override;
    QDockWidget *createDockWidget() override;
    DockPosition defaultDockPosition() const override
    {
        return DockMinimized;
    }

    bool defaultVisible() const override
    {
        return false;
    }
};

#endif
