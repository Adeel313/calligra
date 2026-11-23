/* This file is part of the KDE project
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KWRESEARCHDOCKER_H
#define KWRESEARCHDOCKER_H

#include <KoCanvasObserverBase.h>
#include <QDockWidget>

class KWResearchWidget;
class KoCanvasBase;

class KWResearchDocker : public QDockWidget, public KoCanvasObserverBase
{
    Q_OBJECT
public:
    explicit KWResearchDocker();
    ~KWResearchDocker() override;
    void setCanvas(KoCanvasBase *canvas) override;
    void unsetCanvas() override;

public Q_SLOTS:
    void lookup(const QString &query);

private:
    KWResearchWidget *m_widget;
};

#endif
