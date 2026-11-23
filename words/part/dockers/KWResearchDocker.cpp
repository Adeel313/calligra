/* This file is part of the KDE project
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWResearchDocker.h"

#include "KWResearchWidget.h"

#include <KLocalizedString>

KWResearchDocker::KWResearchDocker()
    : m_widget(new KWResearchWidget(this))
{
    setWindowTitle(i18n("Research"));
    setObjectName(QStringLiteral("Research"));
    setWidget(m_widget);
}

KWResearchDocker::~KWResearchDocker() = default;

void KWResearchDocker::setCanvas(KoCanvasBase *)
{
}

void KWResearchDocker::unsetCanvas()
{
}

void KWResearchDocker::lookup(const QString &query)
{
    m_widget->lookup(query);
    show();
    raise();
}
