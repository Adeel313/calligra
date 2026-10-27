/*
 * SPDX-FileCopyrightText: 2006 Peter Simonsson <peter.simonsson@gmail.com>
 * SPDX-FileCopyrightText: 2011 C. Boemann <cbo@boemann.dk>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KoModeBoxFactory.h"
#include "KoModeBoxDocker_p.h"
#include "KoModeBox_p.h"
#include <KLocalizedString>

#include <QList>

class Q_DECL_HIDDEN KoModeBoxFactory::Private
{
public:
    struct CustomPage {
        QString title;
        QIcon icon;
        QString toolTip;
        QWidget *page;
        bool beforeToolTabs;
    };

    struct ToolTabWidget {
        QString toolId;
        QWidget *widget;
    };

    KoCanvasControllerWidget *canvasController;
    QString applicationName;
    KoDockFactoryBase::DockPosition defaultPosition;
    QList<CustomPage> customPages;
    QList<ToolTabWidget> toolTabWidgets;
    QWidget *leadingWidget = nullptr;
    QWidget *trailingWidget = nullptr;
};

KoModeBoxFactory::KoModeBoxFactory(KoCanvasControllerWidget *canvasController, const QString &applicationName, const QString & /*title*/,
                                   KoDockFactoryBase::DockPosition defaultPosition)
    : d(new Private())
{
    d->canvasController = canvasController;
    d->applicationName = applicationName;
    d->defaultPosition = defaultPosition;
}

KoModeBoxFactory::~KoModeBoxFactory()
{
    delete d;
}

QString KoModeBoxFactory::id() const
{
    return QString("ModeBox");
}

KoDockFactoryBase::DockPosition KoModeBoxFactory::defaultDockPosition() const
{
    return d->defaultPosition;
}

bool KoModeBoxFactory::keepDefaultDockPosition() const
{
    return d->defaultPosition == KoDockFactoryBase::DockTop;
}

void KoModeBoxFactory::addCustomPage(const QString &title, const QIcon &icon, const QString &toolTip, QWidget *page, bool beforeToolTabs)
{
    if (!page) {
        return;
    }
    d->customPages.append({title, icon, toolTip, page, beforeToolTabs});
}

void KoModeBoxFactory::addWidgetToToolTab(const QString &toolId, QWidget *widget)
{
    if (widget) {
        d->toolTabWidgets.append({toolId, widget});
    }
}

void KoModeBoxFactory::setLeadingWidget(QWidget *widget)
{
    d->leadingWidget = widget;
}

void KoModeBoxFactory::setTrailingWidget(QWidget *widget)
{
    d->trailingWidget = widget;
}

QDockWidget *KoModeBoxFactory::createDockWidget()
{
    KoModeBox *box = new KoModeBox(d->canvasController, d->applicationName);
    box->setOrientation(d->defaultPosition == KoDockFactoryBase::DockTop
                            || d->defaultPosition == KoDockFactoryBase::DockBottom
                        ? Qt::Horizontal
                        : Qt::Vertical);
    for (const Private::CustomPage &customPage : d->customPages) {
        box->addCustomPage(customPage.title, customPage.icon, customPage.toolTip, customPage.page, customPage.beforeToolTabs);
    }
    for (const Private::ToolTabWidget &toolTabWidget : d->toolTabWidgets) {
        box->addWidgetToToolTab(toolTabWidget.toolId, toolTabWidget.widget);
    }
    box->setLeadingWidget(d->leadingWidget);
    box->setTrailingWidget(d->trailingWidget);
    QDockWidget *docker = new KoModeBoxDocker(box);

    return docker;
}
