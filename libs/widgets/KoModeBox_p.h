/*
 * SPDX-FileCopyrightText: 2005 Boudewijn Rempt <boud@valdyas.org>
 * SPDX-FileCopyrightText: 2005-2008 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2009 Peter Simonsson <peter.simonsson@gmail.com>
 * SPDX-FileCopyrightText: 2010 Cyrille Berger <cberger@cberger.net>
 * SPDX-FileCopyrightText: 2011 C. Boemann <cbo@boemann.dk>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef KO_MODEBOX_H
#define KO_MODEBOX_H

#include <KoCanvasObserverBase.h>

#include <QDockWidget>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QScrollArea>

#include <KoToolManager.h>

class KoCanvasControllerWidget;
class KoCanvasController;
class KoCanvasBase;
class KoShapeLayer;

class ScrollArea : public QScrollArea
{
    Q_OBJECT
protected:
    void showEvent(QShowEvent *) override;
};

/**
 * KoModeBox is housed in a dock widget that presents tools as headings in a QToolBox
 * according to type and priority.
 *
 * The ModeBox is a container for tool buttons which are themselves
 * divided into sections.
 *
 * @see KoToolManager
 */
class KoModeBox : public QWidget
{
    Q_OBJECT
public:
    /// constructor
    explicit KoModeBox(KoCanvasControllerWidget *canvas, const QString &applicationName);
    ~KoModeBox() override;

    enum HorizontalTabsSide {
        LeftSide,
        RightSide
    };

public Q_SLOTS:
    /**
     * Using the buttongroup id passed in addButton() you can set the new active tool.
     * If the id does not resolve to a visible heading, this call is ignored.
     * @param canvas the currently active canvas.
     * @param id an id to identify the tool/heading to activate.
     */
    void setActiveTool(KoCanvasController *canvas, int id);

    /**
     * Show only the dynamic buttons that have a code from parameter codes.
     * The modebox allows buttons to be optionally registered with a visibilityCode. This code
     * can be passed here and all buttons that have that code are shown. All buttons that
     * have another visibility code registered are hidden.
     * @param codes a list of all the codes to show.
     */
    void updateShownTools(const QList<QString> &codes);

    void setOptionWidgets(const QList<QPointer<QWidget>> &optionWidgetList);

    /// set the canvas this docker should listen to for changes.
    void setCanvas(KoCanvasBase *canvas);
    void unsetCanvas();

    /**
     * Set the orientation of the mode box.
     * A vertical mode box is a sidebar with rotated tabs on the left or right.
     * A horizontal mode box is a ribbon-like bar with tabs on top and the
     * tool options laid out side by side.
     */
    void setOrientation(Qt::Orientation orientation);

    /**
     * Add a page that is not bound to a tool, shown as its own tab after the
     * tool tabs, for instance a ribbon style View tab.
     * The mode box takes ownership of @p page.
     */
    void addCustomPage(const QString &title, const QIcon &icon, const QString &toolTip, QWidget *page, bool beforeToolTabs = false);

    /// Add a widget to a tool's page, preserving it when tool pages are rebuilt.
    void addWidgetToToolTab(const QString &toolId, QWidget *widget);

    /// Place quick access controls before and after the tab bar.
    void setLeadingWidget(QWidget *widget);
    void setTrailingWidget(QWidget *widget);



private Q_SLOTS:
    void setCurrentLayer(const KoCanvasController *canvas, const KoShapeLayer *newLayer);

    /// add a tool post-initialization. The tool will also be activated.
    void toolAdded(KoToolAction *toolAction, KoCanvasController *canvas);

    /// slot for when a new item have been selected in the QToolBox
    void toolSelected(int index);

    /// slot for context menu of the tabbar
    void slotContextMenuRequested(const QPoint &pos);

    /// switch icon mode
    void switchIconMode(int);

    /**
     * Add a tool to the modebox.
     * The tools should all be added before the first showing since adding will not really add
     * them to the UI until setup() is called.
     *
     * @param toolAction the action of the tool
     * @see setup()
     */
    void addToolAction(KoToolAction *toolAction);

Q_SIGNALS:
    void switchTabsSide(HorizontalTabsSide side);

public:
    static QString applicationName;

private:
    enum IconMode {
        IconAndText,
        IconOnly
    };

    QIcon createTextIcon(KoToolAction *toolAction) const;
    QIcon createSimpleIcon(KoToolAction *toolAction) const;
    void addItem(KoToolAction *toolAction);
    void setIconSize() const;

    /// re-create the tabs and pages of all pages added with addCustomPage()
    void appendCustomPages(bool beforeToolTabs);

private:
    class Private;
    std::unique_ptr<Private> const d;
};

#endif // _KO_TOOLBOX_H_
