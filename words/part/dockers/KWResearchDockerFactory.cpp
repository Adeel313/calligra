/* This file is part of the KDE project
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWResearchDockerFactory.h"
#include "KWResearchDocker.h"

KWResearchDockerFactory::KWResearchDockerFactory() = default;

QString KWResearchDockerFactory::id() const
{
    return QStringLiteral("Research");
}

QDockWidget *KWResearchDockerFactory::createDockWidget()
{
    auto *widget = new KWResearchDocker();
    widget->setObjectName(id());
    return widget;
}
