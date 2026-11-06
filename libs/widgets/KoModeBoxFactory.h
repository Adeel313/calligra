/*
 * SPDX-FileCopyrightText: 2006 Peter Simonsson <peter.simonsson@gmail.com>
 * SPDX-FileCopyrightText: 2007 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2011 C. Boemann <cbo@boemann.dk>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KOMODEBOXFACTORY_H
#define KOMODEBOXFACTORY_H

#include "kowidgets_export.h"
#include <KoDockFactoryBase.h>

#include <QDockWidget>
#include <QIcon>
#include <QString>

class KoCanvasControllerWidget;

/**
 * Factory class to create a new KoModeBox that contains a QToolBox which acts
 * as a replacement for KoToolBox and KoDockerManagers options docker.
 */
class KOWIDGETS_EXPORT KoModeBoxFactory : public KoDockFactoryBase
{
public:
    explicit KoModeBoxFactory(KoCanvasControllerWidget *canvas, const QString &applicationName, const QString &appName,
                              KoDockFactoryBase::DockPosition defaultPosition = KoDockFactoryBase::DockRight);
    ~KoModeBoxFactory() override;

    QString id() const override;
    KoDockFactoryBase::DockPosition defaultDockPosition() const override;
    bool keepDefaultDockPosition() const override;
    QDockWidget *createDockWidget() override;

    /**
     * Add a page which is shown as its own tab in the mode box, after the
     * tool tabs. Ownership of @p page passes to the mode box.
     *
     * When neither @p beforeToolTabs nor @p afterToolTabs is set, the page is
     * placed after the tool tab identified by @p afterToolId (or after the
     * Home tab of Words when @p afterToolId is empty, for compatibility).
     */
    void addCustomPage(const QString &title,
                       const QIcon &icon,
                       const QString &toolTip,
                       QWidget *page,
                       bool beforeToolTabs = false,
                       bool afterToolTabs = false,
                       const QString &afterToolId = QString());

    /** Add a widget to an existing tool tab, such as a ribbon toolbar. */
    void addWidgetToToolTab(const QString &toolId, QWidget *widget);

    /** Place quick access controls before and after the tab bar. */
    void setLeadingWidget(QWidget *widget);
    void setTrailingWidget(QWidget *widget);

    void setTitleWidget(QWidget *widget);


    bool isCollapsable() const override
    {
        return false;
    }

private:
    class Private;
    Private *const d;
};

#endif
