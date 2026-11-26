/* This file is part of the KDE project
 *
 * SPDX-FileCopyrightText: 2010 Thorsten Zachmann <zachmann@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KPrFadeCrossStrategy.h"
#include "KPrFadeEffectFactory.h"

#include <QPainter>
#include <QWidget>

KPrFadeCrossStrategy::KPrFadeCrossStrategy()
    : KPrPageEffectStrategy(KPrFadeEffectFactory::CrossFade, "fade", "crossfade", false)
{
}

KPrFadeCrossStrategy::~KPrFadeCrossStrategy() = default;

void KPrFadeCrossStrategy::setup(const KPrPageEffect::Data &, QTimeLine &timeLine)
{
    timeLine.setFrameRange(0, 1000);
}

void KPrFadeCrossStrategy::paintStep(QPainter &p, int, const KPrPageEffect::Data &data)
{
    p.drawPixmap(0, 0, data.m_oldPage);
    p.save();
    p.setOpacity(data.m_timeLine.valueForTime(data.m_currentTime));
    p.drawPixmap(0, 0, data.m_newPage);
    p.restore();
}

void KPrFadeCrossStrategy::next(const KPrPageEffect::Data &data)
{
    data.m_widget->update();
}
