/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2001 David Faure <faure@kde.org>
 * SPDX-FileCopyrightText: 2005-2007, 2009 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2010 Boudewijn Rempt <boud@kogmbh.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWGui.h"
#include "KWCanvas.h"
#include "KWDocument.h"
#include "KWPage.h"
#include "KWPageManager.h"
#include "KWPageStyle.h"
#include "KWView.h"
#include "Words.h"
#include "frames/KWFrame.h"
#include "frames/KWFrameSet.h"
#include "frames/KWTextFrameSet.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KoCanvasControllerWidget.h>
#include <KoChartInterface.h>
#include <KoCreateShapesTool.h>
#include <KoCanvasResourceManager.h>
#include <KoColorBackground.h>
#include <KoDocumentResourceManager.h>
#include <KoPatternBackground.h>
#include <KoProperties.h>
#include <KoShapeController.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeRegistry.h>
#include <KoDockerManager.h>
#include <KoFlake.h>
#include <KoAppRibbon.h>
#include <KoModeBoxFactory.h>
#include <KoParameterShape.h>
#include <KoParameterToPathCommand.h>
#include <KoParagraphStyle.h>
#include <KoRibbonIcons.h>
#include <KoRuler.h>
#include <KoRibbonLayout.h>
#include <KoZoomController.h>
#include <KoZoomMode.h>
#include <KoRulerController.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeManager.h>
#include <KoShapeTransformCommand.h>
#include <KoStyleManager.h>
#include <KoToolManager.h>
#include <KoToolBase.h>
#include <KoText.h>
#include <KoTextEditor.h>
#include <KoBookmarkManager.h>
#include <KoTextDocument.h>
#include <KoTextRangeManager.h>
#include <KoInlineObjectRegistry.h>
#include <KoInlineObjectFactoryBase.h>
#include <KoUnit.h>

#include "QRCodeGenerator.h"
#include "dialogs/QRCodeDialog.h"

#include <KoMainWindow.h>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDialog>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QImage>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QTextTable>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QStringList>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QTextDocument>
#include <QTransform>
#include <QVector>

#include <functional>
#include <memory>
#include <algorithm>

#include <kundo2command.h>

static QMenu *findMenu(QMainWindow *mainWindow, const QString &menuName, const QString &menuTitle)
{
    if (!mainWindow) {
        return nullptr;
    }
    if (QMenu *menu = mainWindow->findChild<QMenu *>(menuName)) {
        return menu;
    }
    for (QAction *action : mainWindow->menuBar()->actions()) {
        if (!action->menu()) {
            continue;
        }
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        if (text.compare(menuTitle, Qt::CaseInsensitive) == 0) {
            return action->menu();
        }
    }
    return nullptr;
}

static void populateRibbonSettingsLater(QToolButton *button, QMainWindow *window, int attemptsLeft)
{
    if (QMenu *menu = findMenu(window, QStringLiteral("settings"), i18n("Settings"))) {
        button->setMenu(menu);
        button->setPopupMode(QToolButton::InstantPopup);
        button->setEnabled(true);
    } else if (attemptsLeft > 0) {
        QTimer::singleShot(100, button, [button, window, attemptsLeft] {
            populateRibbonSettingsLater(button, window, attemptsLeft - 1);
        });
    }
}

namespace
{
/// A ribbon group: a title plus the actions to show in it (action name, button label).
struct RibbonGroup {
    enum Layout { LargeButtons, LargeAndStacked, Stacked, CheckBoxes, PreviewNavigation, ZoomButtons };
    QString title;
    QList<QPair<QString, QString>> actions;
    Layout layout = LargeButtons;
    /// Optional preview strip placed beside the group's buttons.
    QWidget *gallery = nullptr;
};

using ActionFinder = std::function<QAction *(const QString &)>;

// Page-level appearance changes keep the page style's background and layout
// together. KWPageStylePropertiesCommand detaches its style, which clears the
// background pointer, so use a command that updates both properties directly.
class PageStyleAppearanceCommand final : public KUndo2Command
{
public:
    PageStyleAppearanceCommand(KWDocument *document,
                               const KWPageStyle &style,
                               const KoPageLayout &layoutBefore,
                               const KoPageLayout &layoutAfter,
                               const QSharedPointer<KoShapeBackground> &backgroundBefore,
                               const QSharedPointer<KoShapeBackground> &backgroundAfter)
        : KUndo2Command(kundo2_i18n("Change Page Design"))
        , m_document(document)
        , m_style(style)
        , m_layoutBefore(layoutBefore)
        , m_layoutAfter(layoutAfter)
        , m_backgroundBefore(backgroundBefore)
        , m_backgroundAfter(backgroundAfter)
    {
    }

    void redo() override
    {
        KUndo2Command::redo();
        apply(m_layoutAfter, m_backgroundAfter);
    }

    void undo() override
    {
        KUndo2Command::undo();
        apply(m_layoutBefore, m_backgroundBefore);
    }

private:
    void apply(const KoPageLayout &layout, const QSharedPointer<KoShapeBackground> &background)
    {
        m_style.setPageLayout(layout);
        m_style.setBackground(background);
        if (m_document) {
            m_document->updatePagesForStyle(m_style);
            m_document->firePageSetupChanged();
        }
    }

    KWDocument *m_document;
    KWPageStyle m_style;
    KoPageLayout m_layoutBefore;
    KoPageLayout m_layoutAfter;
    QSharedPointer<KoShapeBackground> m_backgroundBefore;
    QSharedPointer<KoShapeBackground> m_backgroundAfter;
};

static int paragraphStylePriority(const KoParagraphStyle *style)
{
    QString normalized = style->name().toCaseFolded();
    normalized.remove(QLatin1Char(' '));
    normalized.remove(QLatin1Char('_'));
    normalized.remove(QLatin1Char('-'));
    if (normalized == QLatin1String("standard") || normalized == QLatin1String("normal")
        || normalized == QLatin1String("defaultparagraphstyle") || normalized == QLatin1String("textbody")) {
        return 0;
    }
    int level = style->outlineLevel();
    if (level < 1 || level > 9) {
        if (normalized.startsWith(QLatin1String("heading")) && normalized.size() == 8 && normalized.at(7).isDigit()) {
            level = normalized.at(7).digitValue();
        } else if (normalized.startsWith(QLatin1String("head")) && normalized.size() == 5 && normalized.at(4).isDigit()) {
            level = normalized.at(4).digitValue();
        }
    }
    if (level >= 1 && level <= 9) {
        return level + 1;
    }
    if (normalized == QLatin1String("title")) {
        return 11;
    }
    if (normalized == QLatin1String("subtitle")) {
        return 12;
    }
    return 20;
}

// A compact strip of paragraph-style previews for the Design tab.  The
// thumbnails use each style's own font and colour, and clicking one applies
// that style to the current paragraph.
static QWidget *createStyleGallery(KWDocument *document, KWCanvas *canvas, QWidget *parent)
{
    auto *gallery = new QListWidget(parent);
    gallery->setObjectName(QStringLiteral("ribbonThemeGallery"));
    gallery->setViewMode(QListView::IconMode);
    gallery->setFlow(QListView::LeftToRight);
    gallery->setWrapping(false);
    gallery->setMovement(QListView::Static);
    gallery->setResizeMode(QListView::Adjust);
    gallery->setSelectionMode(QAbstractItemView::SingleSelection);
    gallery->setFrameShape(QFrame::NoFrame);
    gallery->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    gallery->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    gallery->setIconSize(QSize(72, 40));
    gallery->setGridSize(QSize(88, 62));
    gallery->setFixedHeight(66);
    // The strip of styles takes the width the group does not need for its
    // buttons, so that the tab is filled instead of ending in empty space.
    gallery->setMinimumWidth(320);
    gallery->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    gallery->setProperty("ribbonExpandingGallery", true);

    auto *styleManager = document
        ? document->resourceManager()->resource(KoText::StyleManager).value<KoStyleManager *>()
        : nullptr;
    if (!styleManager) {
        return gallery;
    }

    QList<KoParagraphStyle *> styles = styleManager->paragraphStyles();
    std::sort(styles.begin(), styles.end(), [](const KoParagraphStyle *a, const KoParagraphStyle *b) {
        const int priorityA = paragraphStylePriority(a);
        const int priorityB = paragraphStylePriority(b);
        if (priorityA != priorityB) {
            return priorityA < priorityB;
        }
        return QString::localeAwareCompare(a->name(), b->name()) < 0;
    });
    for (KoParagraphStyle *style : std::as_const(styles)) {
        if (!style
            || style->name().trimmed().startsWith(QLatin1String("Bibliography"), Qt::CaseInsensitive)
            || style->name().trimmed().startsWith(QLatin1String("Contents"), Qt::CaseInsensitive)
            || paragraphStylePriority(style) >= 20) {
            continue;
        }
        QPixmap preview(72, 40);
        preview.fill(Qt::transparent);
        QPainter painter(&preview);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(216, 216, 216));
        painter.setBrush(QColor(255, 255, 255));
        painter.drawRoundedRect(QRectF(0.5, 0.5, 71.0, 39.0), 3, 3);

        QFont labelFont = painter.font();
        labelFont.setPointSizeF(5.5);
        labelFont.setBold(false);
        painter.setFont(labelFont);
        painter.setPen(QColor(120, 120, 120));
        painter.drawText(QRect(4, 1, 64, 9), Qt::AlignLeft | Qt::AlignVCenter,
                         painter.fontMetrics().elidedText(style->name(), Qt::ElideRight, 64));

        QFont sampleFont = style->font();
        const qreal stylePointSize = sampleFont.pointSizeF() > 0 ? sampleFont.pointSizeF() : 10.0;
        sampleFont.setPointSizeF(qBound(7.0, stylePointSize, 13.0));
        painter.setFont(sampleFont);
        const QColor textColor = style->foreground().color().isValid() ? style->foreground().color() : QColor(35, 35, 35);
        painter.setPen(textColor);
        painter.drawText(QRect(4, 10, 64, 17), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Aa"));

        painter.setFont(labelFont);
        painter.setPen(QColor(155, 155, 155));
        painter.drawText(QRect(4, 29, 64, 7), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("The quick brown fox"));
        painter.end();

        auto *item = new QListWidgetItem(QIcon(preview), style->name(), gallery);
        item->setData(Qt::UserRole, style->styleId());
        item->setToolTip(i18n("Apply the %1 style", style->name()));
        item->setTextAlignment(Qt::AlignHCenter);
    }

    const QPointer<KWCanvas> canvasGuard = canvas;
    QObject::connect(gallery, &QListWidget::itemClicked, gallery, [canvasGuard, styleManager](QListWidgetItem *item) {
        KoTextEditor *editor = canvasGuard ? KoTextEditor::getTextEditorFromCanvas(canvasGuard) : nullptr;
        if (!editor) {
            return;
        }
        if (KoParagraphStyle *style = styleManager->paragraphStyle(item->data(Qt::UserRole).toInt())) {
            editor->setStyle(style);
        }
    });
    return gallery;
}

// Tool activation replaces identically named actions in the view's action
// collection. A ribbon button must follow that replacement, rather than keep
// a disabled action from the tool that happened to be active during startup.
class RibbonActionButton final : public QToolButton
{
public:
    RibbonActionButton(const QString &name, const QString &label, const ActionFinder &findAction, QWidget *parent)
        : QToolButton(parent), m_name(name), m_label(label), m_findAction(findAction)
    {
        bindAction();
        if (defaultAction() && qobject_cast<KoToolBase *>(defaultAction()->parent())) {
            connect(KoToolManager::instance(), &KoToolManager::changedTool, this, [this] { bindAction(); });
        }
    }

private:
    void bindAction()
    {
        QAction *action = m_findAction(m_name);
        if (!action) {
            setEnabled(false);
            return;
        }
        if (action != defaultAction()) {
            disconnect(m_presentationConnection);
            setDefaultAction(action);
            m_presentationConnection = connect(action, &QAction::changed, this, [this] { updatePresentation(); });
        }
        updatePresentation();
    }

    void updatePresentation()
    {
        setText(m_label);
        if (defaultAction() && defaultAction()->icon().isNull()) {
            const QIcon fallback = property("ribbonFallbackIcon").value<QIcon>();
            if (!fallback.isNull()) {
                setIcon(fallback);
            }
        }
        if (defaultAction() && defaultAction()->menu()) {
            setPopupMode(m_name == QLatin1String("add_shape") || defaultAction()->property("ribbonMenuOnly").toBool()
                             ? QToolButton::InstantPopup : QToolButton::MenuButtonPopup);
        }
    }

    QString m_name;
    QString m_label;
    ActionFinder m_findAction;
    QMetaObject::Connection m_presentationConnection;
};

// Follow the selected text document as the cursor moves between body text,
// headers and footers. The page keeps its controls while its tab is hidden.
class TableRibbonContext final : public QObject
{
public:
    TableRibbonContext(KWCanvas *canvas, QWidget *page)
        : QObject(page), m_canvas(canvas), m_page(page)
    {
        connect(canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, [this] { update(); });
        connect(canvas->resourceManager(), &KoCanvasResourceManager::canvasResourceChanged, this, [this](int key, const QVariant &) {
            if (key == KoText::CurrentTextPosition || key == KoText::CurrentTextAnchor || key == KoText::CurrentTextDocument) {
                update();
            }
        });
        QTimer::singleShot(0, this, [this] { update(); });

        // A document that is opened with the cursor already inside a table has
        // nothing left to trigger the first update: the text shape was selected
        // and the cursor placed before this object existed. Keep looking for the
        // editor for a few seconds so the table tab is there from the start.
        auto *retry = new QTimer(this);
        retry->setInterval(100);
        connect(retry, &QTimer::timeout, this, [this, retry] {
            update();
            if (m_editor || ++m_retries > 60) {
                retry->stop();
            }
        });
        retry->start();
    }

private:
    void update()
    {
        KoTextEditor *editor = m_canvas ? KoTextEditor::getTextEditorFromCanvas(m_canvas) : nullptr;
        if (editor != m_editor) {
            for (const auto &connection : std::as_const(m_connections)) {
                disconnect(connection);
            }
            m_connections.clear();
            m_editor = editor;
            if (editor) {
                m_connections.append(connect(editor, &KoTextEditor::cursorPositionChanged, this, [this] { update(); }));
                m_connections.append(connect(editor, &KoTextEditor::textFormatChanged, this, [this] { update(); }));
                m_connections.append(connect(editor->document(), &QTextDocument::contentsChanged, this, [this] { update(); }));
                m_connections.append(connect(editor, &QObject::destroyed, this, [this] {
                    m_editor = nullptr;
                    m_page->setProperty("ribbonContextActive", false);
                }));
            }
        }
        const bool tableActive = editor && editor->tableAtCursor();
        const QVariant current = m_page->property("ribbonContextActive");
        if (!current.isValid() || current.toBool() != tableActive) {
            m_page->setProperty("ribbonContextActive", tableActive);
        }
    }

    QPointer<KWCanvas> m_canvas;
    QWidget *m_page;
    QPointer<KoTextEditor> m_editor;
    QList<QMetaObject::Connection> m_connections;
    int m_retries = 0;
};

// Chart Tools: selecting a chart on the page shows the chart tab, and the chart
// plugin fills it (the same tab Stage shows for a selected chart).
class ChartRibbonContext final : public QObject
{
public:
    ChartRibbonContext(KWCanvas *canvas, QWidget *page)
        : QObject(page), m_canvas(canvas), m_page(page)
    {
        connect(canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, [this] { update(); });
        QTimer::singleShot(0, this, [this] { update(); });
    }

private:
    void update()
    {
        if (!m_canvas) {
            return;
        }
        bool chartSelected = false;
        const QList<KoShape *> shapes = m_canvas->shapeManager()->selection()->selectedShapes(KoFlake::StrippedSelection);
        for (KoShape *shape : shapes) {
            for (KoShape *candidate = shape; candidate && !chartSelected; candidate = candidate->parent()) {
                chartSelected = candidate->shapeId() == QLatin1String(ChartShapeId);
            }
            if (chartSelected) {
                break;
            }
        }
        if (chartSelected) {
            build();
            // The chart panels live on this tab now, so the tab the chart tool
            // brings with it only duplicates them.
            hideToolTab();
        }
        const QVariant current = m_page->property("ribbonContextActive");
        if (current.isValid() && current.toBool() == chartSelected) {
            return;
        }
        m_page->setProperty("ribbonContextActive", chartSelected);
        if (chartSelected) {
            // Selecting a chart also switches to the chart tool, and Words shows
            // the page of the active tool. Ask for this tab again once that has
            // settled, so it is the one the user sees.
            QTimer::singleShot(0, this, [this] {
                if (!m_page->property("ribbonContextActive").toBool()) {
                    return;
                }
                // Activating the tool recreates its tab, so hide it again
                // before asking for this one.
                hideToolTab();
                m_page->setProperty("ribbonContextActive", false);
                m_page->setProperty("ribbonContextActive", true);
            });
        }
    }

    void hideToolTab()
    {
        QWidget *window = m_page->window();
        QWidget *box = window ? window->findChild<QWidget *>(QStringLiteral("wordsRibbon")) : nullptr;
        if (box) {
            QMetaObject::invokeMethod(box, "setToolTabVisible", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("ChartToolFactory_ID")), Q_ARG(bool, false));
        }
    }

    // The page lives as long as the window, so it is filled once, by the first
    // chart tool the canvas has.
    void build()
    {
        if (m_page->property("chartTabBuilt").toBool()) {
            return;
        }
        KoToolBase *chartTool = KoToolManager::instance()->toolById(m_canvas, QStringLiteral("ChartToolFactory_ID"));
        if (!chartTool) {
            return; // The chart plugin is not loaded yet.
        }
        m_page->setProperty("chartTabBuilt", true);
        QMetaObject::invokeMethod(chartTool, "configureRibbon", Qt::DirectConnection, Q_ARG(QWidget *, m_page));
    }

    QPointer<KWCanvas> m_canvas;
    QWidget *m_page;
};
}

// Lay out ribbon groups in the Microsoft Office style: large buttons with the
// text under the icon, the group label centred underneath and thin separators
// between the groups.
static void populateRibbonGroups(QWidget *page, const QList<RibbonGroup> &groups, const ActionFinder &findAction, bool showGroupTitles)
{
    auto *pageLayout = qobject_cast<QHBoxLayout *>(page->layout());
    if (!pageLayout) {
        pageLayout = new QHBoxLayout(page);
    }
    pageLayout->setContentsMargins(4, 4, 4, 2);
    pageLayout->setSpacing(0);
    bool firstGroup = true;
    bool hasExpandingGroup = false;
    int groupPosition = 0;
    for (const RibbonGroup &groupInfo : groups) {
        auto *group = new QWidget(page);
        auto *groupLayout = new QVBoxLayout(group);
        groupLayout->setContentsMargins(8, 0, 8, 0);
        groupLayout->setSpacing(0);
        auto *buttons = new QWidget(group);
        buttons->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        auto *buttonLayout = new QGridLayout(buttons);
        buttonLayout->setContentsMargins(1, 0, 1, 0);
        buttonLayout->setAlignment(Qt::AlignTop);
        buttonLayout->setHorizontalSpacing(4);
        buttonLayout->setVerticalSpacing(1);

        int buttonIndex = 0;
        for (const auto &[actionName, label] : groupInfo.actions) {
            QAction *action = findAction(actionName);
            if (!action) {
                continue;
            }
            if (groupInfo.layout == RibbonGroup::CheckBoxes) {
                auto *check = new QCheckBox(label, buttons);
                check->setObjectName(actionName + QStringLiteral("RibbonCheck"));
                check->setChecked(action->isChecked());
                check->setEnabled(action->isEnabled());
                check->setToolTip(action->toolTip());
                QObject::connect(check, &QCheckBox::clicked, action, &QAction::trigger);
                QObject::connect(action, &QAction::changed, check, [check, action] {
                    check->setChecked(action->isChecked());
                    check->setEnabled(action->isEnabled());
                });
                buttonLayout->addWidget(check, buttonIndex % 2, buttonIndex / 2);
                ++buttonIndex;
                continue;
            }
            const QString displayLabel = actionName == QLatin1String("edit_select_all") ? i18n("Select") : label;
            auto *button = new RibbonActionButton(actionName, displayLabel, findAction, buttons);
            button->setObjectName(actionName + QStringLiteral("RibbonButton"));
            if (action->icon().isNull()) {
                static const QMap<QString, QString> commandIcons = {
                    {QStringLiteral("format_stylist"), QStringLiteral("preferences-desktop-theme")},
                    {QStringLiteral("format_textcolor"), QStringLiteral("format-text-color")},
                    {QStringLiteral("format_font"), QStringLiteral("preferences-desktop-font")},
                    {QStringLiteral("format_paragraph"), QStringLiteral("format-justify-left")},
                    {QStringLiteral("page_color"), QStringLiteral("format-color-fill")},
                    {QStringLiteral("insert_annotation"), QStringLiteral("comment-new")},
                    {QStringLiteral("insert_header"), QStringLiteral("insert-text-frame")},
                    {QStringLiteral("insert_footer"), QStringLiteral("insert-text-frame")},
                    {QStringLiteral("insert_variable"), QStringLiteral("insert-text")},
                };
                button->setIcon(QIcon::fromTheme(commandIcons.value(actionName, QStringLiteral("document-edit"))));
                button->setProperty("ribbonFallbackIcon", button->icon());
            }
            // A single prominent command beside compact stacks of three
            // mirrors Office's groups without making every action oversized.
            const int largeCount = groupInfo.layout == RibbonGroup::ZoomButtons ? 3
                : ((groupInfo.layout == RibbonGroup::LargeAndStacked || groupInfo.layout == RibbonGroup::PreviewNavigation) ? 1 : 0);
            const bool large = groupInfo.layout == RibbonGroup::LargeButtons || buttonIndex < largeCount;
            if (action->menu()) {
                button->setPopupMode(actionName == QLatin1String("add_shape") || action->property("ribbonMenuOnly").toBool()
                                         ? QToolButton::InstantPopup : QToolButton::MenuButtonPopup);
            }
            KoRibbonLayout::configureButton(button, large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
            // The format painter is a brush, which reads better as one larger
            // picture than as a picture beside its name.
            const bool iconOnly = actionName == QLatin1String("format_painter");
            if (iconOnly) {
                KoRibbonLayout::configureButton(button, Qt::ToolButtonIconOnly);
                button->setIconSize(QSize(28, 28));
                button->setMinimumSize(36, 36);
                button->setToolTip(label);
                button->setAccessibleName(label);
            }
            const int compactIndex = buttonIndex - largeCount;
            if (large) {
                buttonLayout->addWidget(button, 0, buttonIndex, 2, 1, Qt::AlignTop);
            } else if (groupInfo.layout == RibbonGroup::PreviewNavigation) {
                KoRibbonLayout::configureButton(button, Qt::ToolButtonIconOnly);
                button->setAccessibleName(label);
                buttonLayout->addWidget(button, 1, compactIndex + 1);
            } else {
                buttonLayout->addWidget(button, compactIndex % 2, compactIndex / 2 + largeCount, Qt::AlignLeft);
            }
            ++buttonIndex;
        }

        if (buttonIndex == 0) {
            group->deleteLater();
            continue;
        }
        bool expandingGallery = false;
        if (groupInfo.gallery) {
            expandingGallery = groupInfo.gallery->property("ribbonExpandingGallery").toBool();
            auto *content = new QWidget(group);
            auto *contentLayout = new QHBoxLayout(content);
            contentLayout->setContentsMargins(0, 0, 0, 0);
            contentLayout->setSpacing(6);
            contentLayout->addWidget(buttons);
            contentLayout->addWidget(groupInfo.gallery, expandingGallery ? 1 : 0);
            groupLayout->addWidget(content, 1);
        } else {
            groupLayout->addWidget(buttons, 1);
        }
        if (expandingGallery) {
            group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        }
        if (showGroupTitles) {
            auto *title = new QLabel(groupInfo.title, group);
            title->setProperty("ribbonGroupTitle", true);
            title->setMinimumHeight(18);
            title->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
            groupLayout->addWidget(title);
        }

        if (!firstGroup) {
            auto *separator = new QFrame(page);
            separator->setFrameStyle(QFrame::VLine);
            separator->setFixedWidth(1);
            pageLayout->insertWidget(groupPosition++, separator);
        }
        firstGroup = false;
        if (expandingGallery) {
            hasExpandingGroup = true;
        }
        pageLayout->insertWidget(groupPosition++, group, expandingGallery ? 1 : 0);
    }
    if (!hasExpandingGroup
        && (pageLayout->count() == 0 || !pageLayout->itemAt(pageLayout->count() - 1)->spacerItem())) {
        pageLayout->addStretch(1);
    }
}

// Ribbon pages are filled once the XMLGUI factory has created the actions.
/// The pictures of the Design tab groups that have no icon of their own.
enum class DesignIcon { Theme, Colors, Fonts };

/// @return a picture of a theme, a colour palette or a font, drawn large
/// enough for a button of the ribbon.
static QIcon designIcon(DesignIcon kind)
{
    QPixmap pixmap(64, 64);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    if (kind == DesignIcon::Colors) {
        const QList<QColor> colors{QColor(0x2f, 0x54, 0x96), QColor(0xe3, 0x6c, 0x0a), QColor(0xa5, 0xa5, 0xa5), QColor(0x1e, 0x1e, 0x1e)};
        for (int i = 0; i < colors.count(); ++i) {
            const QRectF square(3 + (i % 2) * 13, 3 + (i / 2) * 13, 11, 11);
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.at(i));
            painter.drawRoundedRect(square, 1, 1);
        }
        return QIcon(pixmap);
    }

    if (kind == DesignIcon::Fonts) {
        QFont font = painter.font();
        font.setPixelSize(28);
        font.setBold(false);
        font.setFamily(QStringLiteral("Serif"));
        painter.setFont(font);
        painter.setPen(QPen(QColor(0x1e, 0x1e, 0x1e)));
        painter.drawText(QRectF(2, 1, 28, 31), Qt::AlignCenter, QStringLiteral("A"));
        return QIcon(pixmap);
    }

    // A theme: a page whose first line is set in a large face.
    painter.setPen(QPen(QColor(0x8f, 0x9a, 0xa8), 1.0));
    painter.setBrush(QColor(255, 255, 255));
    painter.drawRoundedRect(QRectF(4.5, 2.5, 23, 27), 1.5, 1.5);
    QFont themeFont = painter.font();
    themeFont.setPixelSize(11);
    themeFont.setBold(true);
    painter.setFont(themeFont);
    painter.setPen(QPen(QColor(0x2f, 0x54, 0x96)));
    painter.drawText(QRectF(6, 5, 20, 10), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Aa"));
    painter.setPen(QPen(QColor(0xc8, 0xcf, 0xd8), 1.0));
    for (int i = 0; i < 4; ++i) {
        painter.drawLine(QPointF(7, 18 + i * 2.6), QPointF(25, 18 + i * 2.6));
    }
    return QIcon(pixmap);
}

/// @return a group of the Design tab; @p content is where its controls go.
static QWidget *createDesignGroup(QWidget *page, const QString &title, QWidget **content)
{
    auto *group = new QWidget(page);
    auto *groupLayout = new QVBoxLayout(group);
    groupLayout->setContentsMargins(8, 0, 8, 0);
    groupLayout->setSpacing(0);

    auto *body = new QWidget(group);
    body->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    groupLayout->addWidget(body, 1);

    auto *label = new QLabel(title, group);
    label->setProperty("ribbonGroupTitle", true);
    label->setMinimumHeight(18);
    label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    groupLayout->addWidget(label);

    *content = body;
    return group;
}

/// Connect a ribbon button to the view action it stands for. The action may
/// only exist once the text tool has registered it, so it is looked up later.
static void bindDesignButton(QToolButton *button, const QString &actionName, const ActionFinder &findAction)
{
    QTimer::singleShot(0, button, [button, actionName, findAction] {
        QAction *action = findAction(actionName);
        if (!action) {
            button->setEnabled(false);
            return;
        }
        button->setEnabled(action->isEnabled());
        QObject::connect(button, &QToolButton::clicked, action, &QAction::trigger);
        QObject::connect(action, &QAction::changed, button, [button, action] { button->setEnabled(action->isEnabled()); });
    });
}

/**
 * Builds the Design tab: one large button for the themes, the strip of document
 * styles with the colour and font buttons beside it, and the page background.
 */
static void buildDesignRibbonPage(QWidget *page, const ActionFinder &findAction, QWidget *styleGallery)
{
    auto *layout = qobject_cast<QHBoxLayout *>(page->layout());
    if (!layout) {
        layout = new QHBoxLayout(page);
    }
    layout->setContentsMargins(4, 4, 4, 2);
    layout->setSpacing(0);

    const auto addGroup = [layout, page](QWidget *group, bool first, int stretch) {
        if (!first) {
            auto *separator = new QFrame(page);
            separator->setFrameStyle(QFrame::VLine);
            separator->setFixedWidth(1);
            layout->addWidget(separator);
        }
        layout->addWidget(group, stretch);
    };

    // --- Themes -------------------------------------------------------------
    QWidget *themesContent = nullptr;
    QWidget *themesGroup = createDesignGroup(page, i18n("Themes"), &themesContent);
    auto *themesLayout = new QVBoxLayout(themesContent);
    themesLayout->setContentsMargins(0, 0, 0, 0);
    themesLayout->setSpacing(0);
    themesLayout->addStretch();
    auto *themesButton = new QToolButton(themesContent);
    themesButton->setText(i18n("Themes"));
    themesButton->setIcon(designIcon(DesignIcon::Theme));
    themesButton->setIconSize(QSize(32, 32));
    themesButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    themesButton->setMinimumSize(64, 72);
    themesButton->setAutoRaise(true);
    themesButton->setToolTip(i18n("Change the styles, colours and fonts of the document"));
    themesLayout->addWidget(themesButton, 0, Qt::AlignHCenter);
    themesLayout->addStretch();
    bindDesignButton(themesButton, QStringLiteral("format_stylist"), findAction);
    addGroup(themesGroup, true, 0);

    // --- Document Formatting ------------------------------------------------
    QWidget *formattingContent = nullptr;
    QWidget *formattingGroup = createDesignGroup(page, i18n("Document Formatting"), &formattingContent);
    formattingGroup->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *formattingLayout = new QHBoxLayout(formattingContent);
    formattingLayout->setContentsMargins(0, 0, 0, 0);
    formattingLayout->setSpacing(6);
    formattingLayout->addWidget(styleGallery, 1);

    const auto addMediumButton = [&](const QString &actionName, const QString &text, const QIcon &icon) {
        auto *button = new QToolButton(formattingContent);
        button->setText(text);
        button->setIcon(icon);
        button->setIconSize(QSize(28, 28));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setMinimumSize(54, 62);
        button->setAutoRaise(true);
        formattingLayout->addWidget(button, 0, Qt::AlignVCenter);
        bindDesignButton(button, actionName, findAction);
        return button;
    };
    addMediumButton(QStringLiteral("format_textcolor"), i18n("Colors"), designIcon(DesignIcon::Colors));
    addMediumButton(QStringLiteral("format_font"), i18n("Fonts"), designIcon(DesignIcon::Fonts));

    // The remaining formatting commands sit in a column of small rows beside
    // the colours and fonts, as they do in an office suite.
    auto *rows = new QVBoxLayout;
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(2);
    rows->addStretch();
    auto *paragraphSpacing = new QToolButton(formattingContent);
    paragraphSpacing->setText(i18n("Paragraph Spacing"));
    paragraphSpacing->setIcon(QIcon::fromTheme(QStringLiteral("format-justify-left")));
    paragraphSpacing->setIconSize(QSize(16, 16));
    paragraphSpacing->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    paragraphSpacing->setAutoRaise(true);
    rows->addWidget(paragraphSpacing, 0, Qt::AlignLeft);
    rows->addStretch();
    formattingLayout->addLayout(rows);
    bindDesignButton(paragraphSpacing, QStringLiteral("format_paragraph"), findAction);
    addGroup(formattingGroup, false, 1);

    // --- Page Background ----------------------------------------------------
    QWidget *backgroundContent = nullptr;
    QWidget *backgroundGroup = createDesignGroup(page, i18n("Page Background"), &backgroundContent);
    auto *backgroundLayout = new QHBoxLayout(backgroundContent);
    backgroundLayout->setContentsMargins(0, 0, 0, 0);
    backgroundLayout->setSpacing(4);
    const QList<QPair<QString, QString>> backgroundCommands = {
        {QStringLiteral("watermark"), i18n("Watermark")},
        {QStringLiteral("page_color"), i18n("Page Color")},
        {QStringLiteral("page_borders"), i18n("Page Borders")},
    };
    for (const auto &[actionName, text] : backgroundCommands) {
        auto *button = new QToolButton(backgroundContent);
        button->setText(text);
        button->setIcon(KoRibbonIcons::forCommand(actionName));
        button->setIconSize(QSize(32, 32));
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setMinimumSize(64, 72);
        button->setAutoRaise(true);
        backgroundLayout->addWidget(button);
        bindDesignButton(button, actionName, findAction);
    }
    addGroup(backgroundGroup, false, 0);
}

static void populateRibbonGroupsLater(QWidget *page,
                                      const QList<RibbonGroup> &groups,
                                      const ActionFinder &findAction,
                                      bool showGroupTitles,
                                      int attemptsLeft)
{
    if (!page || attemptsLeft <= 0) {
        return;
    }
    bool allFound = true;
    for (const RibbonGroup &groupInfo : groups) {
        for (const auto &[actionName, label] : groupInfo.actions) {
            if (!findAction(actionName)) {
                allFound = false;
                break;
            }
        }
        if (!allFound) {
            break;
        }
    }
    if (allFound || attemptsLeft == 1) {
        populateRibbonGroups(page, groups, findAction, showGroupTitles);
        return;
    }
    QTimer::singleShot(100, page, [page, groups, findAction, showGroupTitles, attemptsLeft] {
        populateRibbonGroupsLater(page, groups, findAction, showGroupTitles, attemptsLeft - 1);
    });
}

static QAction *findFileAction(KoMainWindow *mainWindow, KActionCollection *viewActions, const QString &name)
{
    if (viewActions) {
        if (QAction *action = viewActions->action(name)) {
            return action;
        }
    }
    return mainWindow ? mainWindow->actionCollection()->action(name) : nullptr;
}

static void showRibbonCommandSearch(KWView *view)
{
    QDialog dialog(view);
    dialog.setWindowTitle(i18n("Search Commands"));
    dialog.resize(540, 420);
    auto *layout = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit(&dialog);
    search->setPlaceholderText(i18n("Search for a command…"));
    search->setClearButtonEnabled(true);
    search->setAccessibleName(i18n("Search Commands"));
    layout->addWidget(search);
    auto *results = new QListWidget(&dialog);
    results->setIconSize(QSize(20, 20));
    results->setAlternatingRowColors(true);
    layout->addWidget(results);

    QList<QAction *> actions = view->actionCollection()->actions();
    if (view->mainWindow()) {
        actions.append(view->mainWindow()->actionCollection()->actions());
    }
    std::stable_sort(actions.begin(), actions.end(), [](QAction *a, QAction *b) {
        return QString::localeAwareCompare(a->text(), b->text()) < 0;
    });
    QList<QPointer<QAction>> commands;
    QSet<QAction *> seen;
    for (QAction *action : actions) {
        if (seen.contains(action) || action->isSeparator() || action->text().isEmpty() || action->menu()) {
            continue;
        }
        seen.insert(action);
        QString text = action->text();
        text.remove(QLatin1Char('&'));
        if (!action->shortcut().isEmpty()) {
            text += QStringLiteral("    ") + action->shortcut().toString(QKeySequence::NativeText);
        }
        auto *item = new QListWidgetItem(action->icon(), text, results);
        item->setToolTip(action->toolTip());
        if (!action->isEnabled()) {
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
        commands.append(action);
    }
    const auto filter = [results](const QString &query) {
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < results->count(); ++i) {
            auto *item = results->item(i);
            const bool match = item->text().contains(query, Qt::CaseInsensitive);
            item->setHidden(!match);
            if (!first && match && (item->flags() & Qt::ItemIsEnabled)) {
                first = item;
            }
        }
        results->setCurrentItem(first);
    };
    QObject::connect(search, &QLineEdit::textChanged, &dialog, filter);
    QPointer<QAction> selected;
    const auto activate = [&] {
        const int row = results->currentRow();
        if (row >= 0 && !results->item(row)->isHidden() && commands.value(row) && commands[row]->isEnabled()) {
            selected = commands[row];
            dialog.accept();
        }
    };
    QObject::connect(search, &QLineEdit::returnPressed, &dialog, activate);
    QObject::connect(results, &QListWidget::itemActivated, &dialog, activate);
    filter(QString());
    search->setFocus();
    if (dialog.exec() == QDialog::Accepted && selected && selected->isEnabled()) {
        selected->trigger();
    }
}

static void populateFilePage(QWidget *page, KoMainWindow *mainWindow, KActionCollection *viewActions)
{
    // Microsoft Office style File groups.
    const QList<RibbonGroup> groups = {
        {i18n("New"), {{QStringLiteral("file_new"), i18n("Blank Document")}}},
        {i18n("Open"), {{QStringLiteral("file_open"), i18n("Open")}}},
        {i18n("Save"),
         {{QStringLiteral("file_save"), i18n("Save")},
          {QStringLiteral("file_save_as"), i18n("Save As")},
          {QStringLiteral("extra_template"), i18n("Save as Template")}}, RibbonGroup::LargeAndStacked},
        {i18n("Print"), {{QStringLiteral("file_print"), i18n("Print")}, {QStringLiteral("file_print_preview"), i18n("Print Preview")}}, RibbonGroup::LargeAndStacked},
        {i18n("Export"), {{QStringLiteral("file_export_pdf"), i18n("Export as PDF")}, {QStringLiteral("file_export_file"), i18n("Export")}}, RibbonGroup::LargeAndStacked},
        {i18n("Info"), {{QStringLiteral("file_documentinfo"), i18n("Properties")}, {QStringLiteral("file_versions_file"), i18n("Versions")}}, RibbonGroup::Stacked},
    };
    const ActionFinder findAction = [mainWindow, viewActions](const QString &name) {
        return findFileAction(mainWindow, viewActions, name);
    };
    populateRibbonGroups(page, groups, findAction, true);

    if (QMenu *menu = findMenu(mainWindow, QStringLiteral("file"), i18n("File"))) {
        menu->menuAction()->setVisible(false);
    }
}

static void populateFilePageLater(QWidget *page, KoMainWindow *mainWindow, KActionCollection *viewActions, int attemptsLeft)
{
    if (!page || !mainWindow || attemptsLeft <= 0) {
        return;
    }
    if (findFileAction(mainWindow, viewActions, QStringLiteral("file_new"))
        && findFileAction(mainWindow, viewActions, QStringLiteral("file_print"))) {
        populateFilePage(page, mainWindow, viewActions);
        return;
    }
    QTimer::singleShot(100, page, [page, mainWindow, viewActions, attemptsLeft] {
        populateFilePageLater(page, mainWindow, viewActions, attemptsLeft - 1);
    });
}

static void populateQuickAccess(QToolBar *toolbar, KoMainWindow *mainWindow, KActionCollection *viewActions)
{
    static const QStringList actionNames = {QStringLiteral("file_new"),
                                            QStringLiteral("file_open"),
                                            QStringLiteral("file_save"),
                                            QStringLiteral("edit_undo"),
                                            QStringLiteral("edit_redo"),
                                            QStringLiteral("file_print")};
    for (const QString &name : actionNames) {
        if (QAction *action = findFileAction(mainWindow, viewActions, name)) {
            if (!toolbar->actions().contains(action)) {
                toolbar->addAction(action);
            }
        }
    }
}

static void populateQuickAccessLater(QToolBar *toolbar, KoMainWindow *mainWindow, KActionCollection *viewActions, int attemptsLeft)
{
    if (!toolbar || !mainWindow || attemptsLeft <= 0) {
        return;
    }
    populateQuickAccess(toolbar, mainWindow, viewActions);
    if (toolbar->actions().size() >= 6) {
        return;
    }
    QTimer::singleShot(100, toolbar, [toolbar, mainWindow, viewActions, attemptsLeft] {
        populateQuickAccessLater(toolbar, mainWindow, viewActions, attemptsLeft - 1);
    });
}

KWGui::KWGui(const QString &viewMode, KWView *parent)
    : QWidget(parent)
    , m_view(parent)
{
    QGridLayout *gridLayout = new QGridLayout(this);
    gridLayout->setContentsMargins({});
    gridLayout->setSpacing(0);

    setMouseTracking(true);

    // Ruler
    m_horizontalRuler = new KoRuler(this, Qt::Horizontal, m_view->viewConverter());
    m_horizontalRuler->setShowMousePosition(true);
    m_horizontalRuler->setUnit(m_view->kwdocument()->unit());
    m_verticalRuler = new KoRuler(this, Qt::Vertical, m_view->viewConverter());
    m_verticalRuler->setUnit(m_view->kwdocument()->unit());
    m_verticalRuler->setShowMousePosition(true);

    m_canvas = new KWCanvas(viewMode, static_cast<KWDocument *>(m_view->koDocument()), m_view, this);
    KoCanvasControllerWidget *canvasController = new KoCanvasControllerWidget(m_view->actionCollection(), this);
    m_canvasController = canvasController;
    // We need to set this as QDeclarativeView sets them a bit different from QAbstractScrollArea
    canvasController->setFrameStyle(QFrame::NoFrame);
    QPalette workspacePalette = canvasController->viewport()->palette();
    workspacePalette.setColor(QPalette::Window, workspacePalette.color(QPalette::Midlight));
    canvasController->viewport()->setPalette(workspacePalette);
    canvasController->setFocusPolicy(Qt::NoFocus);
    // setScene(0);

    canvasController->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_canvasController->setMargin(10);
    m_canvasController->setCanvas(m_canvas);
    m_canvasController->setCanvasMode(KoCanvasController::AlignTop);
    KoToolManager::instance()->addController(m_canvasController);
    KoToolManager::instance()->registerTools(m_view->actionCollection(), m_canvasController);

    if (m_view->mainWindow()) {
        KoModeBoxFactory modeBoxFactory(canvasController, qApp->applicationName(), i18n("Tools"), KoDockFactoryBase::DockTop);

        // Layout is a passive ribbon tab, so create its controls up front and
        // keep them on the tab without activating PageTool when the tab opens.
        if (KoToolBase *pageTool = KoToolManager::instance()->toolById(m_canvas, QStringLiteral("PageToolFactory_ID"))) {
            for (const QPointer<QWidget> &widget : pageTool->optionWidgets()) {
                if (widget) {
                    modeBoxFactory.addWidgetToToolTab(QStringLiteral("PageToolFactory_ID"), widget);
                }
            }
        }

        auto *filePage = new QWidget;
        filePage->setObjectName(QStringLiteral("fileRibbonPage"));
        filePage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("File"), QIcon::fromTheme("document-open"), i18n("Create, open, save, and print documents"), filePage, true);

        // Microsoft Office style Home tab groups that the text tool itself
        // does not provide: Clipboard, Styles and Editing.
        auto *goTo = new QAction(QIcon::fromTheme(QStringLiteral("go-jump")), i18n("Go to"), this);
        m_view->actionCollection()->addAction(QStringLiteral("ribbon_go_to_page"), goTo);
        connect(goTo, &QAction::triggered, this, [this] {
            const KWPageManager *pages = m_view->kwdocument()->pageManager();
            if (pages->pageCount() == 0) {
                return;
            }
            bool accepted = false;
            const int number = QInputDialog::getInt(this, i18n("Go to Page"), i18n("Page:"),
                                                    m_view->currentPage().pageNumber(),
                                                    pages->begin().pageNumber(), pages->last().pageNumber(), 1, &accepted);
            if (accepted) {
                m_view->goToPage(pages->page(number));
            }
        });
        const QPointer<KWView> ribbonView = m_view;
        const ActionFinder findRibbonAction = [ribbonView](const QString &name) {
            return ribbonView ? findFileAction(ribbonView->mainWindow(), ribbonView->actionCollection(), name) : nullptr;
        };
        const auto addHomeGroup = [&modeBoxFactory, &findRibbonAction](const QString &title, const QList<QPair<QString, QString>> &actions, int order, RibbonGroup::Layout layout) {
            auto *groupWidget = new QWidget;
            groupWidget->setWindowTitle(title);
            groupWidget->setProperty("ribbonGroupOrder", order);
            groupWidget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
            modeBoxFactory.addWidgetToToolTab(QStringLiteral("TextToolFactory_ID"), groupWidget);
            populateRibbonGroupsLater(groupWidget, {{title, actions, layout}}, findRibbonAction, false, 20);
        };
        addHomeGroup(i18n("Clipboard"),
                     {{QStringLiteral("edit_paste"), i18n("Paste")},
                      {QStringLiteral("edit_cut"), i18n("Cut")},
                      {QStringLiteral("edit_copy"), i18n("Copy")},
                      {QStringLiteral("format_painter"), i18n("Format painter")}},
                     10, RibbonGroup::LargeAndStacked);
        addHomeGroup(i18n("Editing"),
                     {{QStringLiteral("edit_find"), i18n("Search")},
                      {QStringLiteral("edit_replace"), i18n("Replace")},
                      {QStringLiteral("edit_findnext"), i18n("Search again")},
                      {QStringLiteral("edit_findprevious"), i18n("Search previous")},
                      {QStringLiteral("ribbon_go_to_page"), i18n("Go To")},
                      {QStringLiteral("edit_select_all"), i18n("Select")},
                      {QStringLiteral("edit_selectcurrentframe"), i18n("Select Current Frame")}},
                     50, RibbonGroup::LargeAndStacked);


        // Microsoft Office 2026 style ribbon pages: Insert, Draw and Design
        // after Home, Mailings after References, and View and Help at the end.
        auto *insertPage = new QWidget;
        insertPage->setObjectName(QStringLiteral("insertRibbonPage"));
        insertPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Insert"),
                                    QIcon::fromTheme("insert-image"),
                                    i18n("Insert pages, tables, pictures, links and document elements"),
                                    insertPage,
                                    false,
                                    false,
                                    QStringLiteral("TextToolFactory_ID"));

        // Actions that switch to the drawing tools, used by the Draw page.
        const auto localRibbonActions = std::make_shared<QMap<QString, QAction *>>();
        const QList<KoToolAction *> toolActionList = KoToolManager::instance()->toolActionList();
        struct ToolSwitchEntry {
            QString toolId;
            QString label;
            QString iconName;
        };
        const QList<ToolSwitchEntry> drawTools = {
            {QStringLiteral("KoPencilTool"), i18n("Freehand"), QStringLiteral("draw-freehand")},
            {QStringLiteral("CreatePathTool"), i18n("Draw Path"), QStringLiteral("createpath")},
            {QStringLiteral("ConnectionTool"), i18n("Connect Shapes"), QStringLiteral("x-shape-connection")},
            {QStringLiteral("InteractionTool"), i18n("Select"), QStringLiteral("arrow")},
        };
        auto *drawToolGroup = new QActionGroup(this);
        drawToolGroup->setExclusive(true);
        for (const ToolSwitchEntry &entry : drawTools) {
            bool available = false;
            for (KoToolAction *toolAction : toolActionList) {
                if (toolAction->id() == entry.toolId) {
                    available = true;
                    break;
                }
            }
            if (!available) {
                continue;
            }
            auto *action = new QAction(QIcon::fromTheme(entry.iconName), entry.label, this);
            // The shared ribbon icon lookup matches on the action name.
            action->setObjectName(entry.toolId);
            action->setCheckable(true);
            drawToolGroup->addAction(action);
            const QString toolId = entry.toolId;
            connect(action, &QAction::triggered, this, [toolId] {
                KoToolManager::instance()->switchToolRequested(toolId);
            });
            localRibbonActions->insert(entry.toolId, action);
        }
        // Keep the Draw tab's tool buttons in sync with the active tool: the
        // "Select" option shows as selected exactly when shape selection is
        // active, and none of the buttons is checked for other tools.
        connect(KoToolManager::instance(), &KoToolManager::changedTool, this, [this, localRibbonActions, toolActionList](KoCanvasController *controller, int buttonGroupId) {
            if (controller != m_canvas->canvasController()) {
                return;
            }
            QString changedToolId;
            for (KoToolAction *toolAction : toolActionList) {
                if (toolAction->buttonGroupId() == buttonGroupId && localRibbonActions->contains(toolAction->id())) {
                    localRibbonActions->value(toolAction->id())->setChecked(true);
                    changedToolId = toolAction->id();
                    break;
                }
            }
            if (changedToolId.isEmpty()) {
                for (const QString &toolId : {QStringLiteral("KoPencilTool"), QStringLiteral("CreatePathTool"), QStringLiteral("ConnectionTool"), QStringLiteral("InteractionTool")}) {
                    if (localRibbonActions->contains(toolId)) {
                        localRibbonActions->value(toolId)->setChecked(false);
                    }
                }
            } else if (changedToolId == QLatin1String("InteractionTool")) {
                // The body/header/footer text frames are only in the selection
                // as the text tool's editing target.  When the shape-handling
                // tool takes over they must not show up as selected objects
                // around the whole page.
                KoSelection *selection = m_canvas->shapeManager()->selection();
                const QList<KoShape *> selected = selection->selectedShapes();
                for (KoShape *shape : selected) {
                    const KWFrame *frame = m_view->kwdocument()->frameOfShape(shape);
                    if (frame && Words::isAutoGenerated(KWFrameSet::from(frame->shape()))) {
                        selection->deselect(shape);
                    }
                }
            }
        });

        // "Convert to Path" normally lives in the path editing tool's option
        // panel.  The Words ribbon hides that tab completely, so expose the
        // action in the Draw tab and run it against the canvas selection
        // directly (same behavior as KoPathTool::convertToPath).
        const auto collectParametricShapes = [this] {
            QList<KoParameterShape *> shapes;
            for (KoShape *shape : m_canvas->shapeManager()->selection()->selectedShapes()) {
                if (auto *parameterShape = dynamic_cast<KoParameterShape *>(shape); parameterShape && parameterShape->isParametricShape()) {
                    shapes.append(parameterShape);
                }
            }
            return shapes;
        };
        auto *convertToPathAction = new QAction(QIcon::fromTheme(QStringLiteral("format-convert-to-path")), i18n("Convert to Path"), this);
        convertToPathAction->setObjectName(QStringLiteral("convert-to-path"));
        const auto updateConvertToPathAction = [collectParametricShapes, convertToPathAction] {
            convertToPathAction->setEnabled(!collectParametricShapes().isEmpty());
        };
        updateConvertToPathAction();
        connect(convertToPathAction, &QAction::triggered, this, [this, collectParametricShapes, updateConvertToPathAction] {
            const QList<KoParameterShape *> shapes = collectParametricShapes();
            if (!shapes.isEmpty()) {
                m_canvas->addCommand(new KoParameterToPathCommand(shapes));
                updateConvertToPathAction();
            }
        });
        connect(m_canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, updateConvertToPathAction);
        localRibbonActions->insert(QStringLiteral("convert-to-path"), convertToPathAction);

        // Rotate the selected images and shapes in 90 degree steps.  Only
        // objects that actually allow rotation and are not the document's
        // auto-generated page frames take part.
        const auto collectRotatableShapes = [this] {
            QList<KoShape *> shapes;
            for (KoShape *shape : m_canvas->shapeManager()->selection()->selectedShapes(KoFlake::StrippedSelection)) {
                if (!shape->allowedInteraction(KoShape::RotationAllowed, false)) {
                    continue;
                }
                const KWFrame *frame = m_view->kwdocument()->frameOfShape(shape);
                if (frame && Words::isAutoGenerated(KWFrameSet::from(frame->shape()))) {
                    continue;
                }
                shapes.append(shape);
            }
            return shapes;
        };
        const auto rotateSelection = [this, collectRotatableShapes](qreal angle) {
            const QList<KoShape *> shapes = collectRotatableShapes();
            if (shapes.isEmpty()) {
                return;
            }
            QVector<QTransform> oldTransforms;
            QVector<QTransform> newTransforms;
            oldTransforms.reserve(shapes.count());
            newTransforms.reserve(shapes.count());
            for (KoShape *shape : shapes) {
                shape->update();
                oldTransforms << shape->transformation();
                shape->rotate(angle);
                newTransforms << shape->transformation();
                shape->update();
            }
            auto *command = new KoShapeTransformCommand(shapes, oldTransforms, newTransforms);
            command->setText(angle < 0.0 ? kundo2_i18n("Rotate Left") : kundo2_i18n("Rotate Right"));
            m_canvas->addCommand(command);
        };
        auto *rotateLeftAction = new QAction(QIcon::fromTheme(QStringLiteral("object-rotate-left")), i18n("Rotate Left"), this);
        rotateLeftAction->setObjectName(QStringLiteral("rotate-left"));
        rotateLeftAction->setToolTip(i18n("Rotate the selected objects 90° counterclockwise"));
        rotateLeftAction->setStatusTip(rotateLeftAction->toolTip());
        auto *rotateRightAction = new QAction(QIcon::fromTheme(QStringLiteral("object-rotate-right")), i18n("Rotate Right"), this);
        rotateRightAction->setObjectName(QStringLiteral("rotate-right"));
        rotateRightAction->setToolTip(i18n("Rotate the selected objects 90° clockwise"));
        rotateRightAction->setStatusTip(rotateRightAction->toolTip());
        const auto updateRotateActions = [collectRotatableShapes, rotateLeftAction, rotateRightAction] {
            const bool enabled = !collectRotatableShapes().isEmpty();
            rotateLeftAction->setEnabled(enabled);
            rotateRightAction->setEnabled(enabled);
        };
        updateRotateActions();
        connect(rotateLeftAction, &QAction::triggered, this, [rotateSelection] {
            rotateSelection(-90.0);
        });
        connect(rotateRightAction, &QAction::triggered, this, [rotateSelection] {
            rotateSelection(90.0);
        });
        connect(m_canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, updateRotateActions);
        localRibbonActions->insert(QStringLiteral("rotate-left"), rotateLeftAction);
        localRibbonActions->insert(QStringLiteral("rotate-right"), rotateRightAction);

        const ActionFinder findDrawAction = [findRibbonAction, localRibbonActions](const QString &name) {
            if (localRibbonActions->contains(name)) {
                return localRibbonActions->value(name);
            }
            return findRibbonAction(name);
        };

        // The Draw tab is the combined shape tool tab: its drawing groups are
        // shown together with the shape formatting options instead of adding a
        // second, separate "Shape Format" tab.
        auto *drawPage = new QWidget;
        drawPage->setObjectName(QStringLiteral("drawRibbonPage"));
        drawPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        drawPage->setProperty("ribbonGroupOrder", 10);
        modeBoxFactory.addWidgetToToolTab(QStringLiteral("ConnectionTool"), drawPage);

        auto *designPage = new QWidget;
        designPage->setObjectName(QStringLiteral("designRibbonPage"));
        designPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Design"),
                                    QIcon::fromTheme("document-edit"),
                                    i18n("Styles and page background"),
                                    designPage,
                                    false,
                                    false,
                                    QStringLiteral("TextToolFactory_ID"));

        auto *mailingsPage = new QWidget;
        mailingsPage->setObjectName(QStringLiteral("mailingsRibbonPage"));
        mailingsPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Mailings"),
                                    QIcon::fromTheme("mail-message-new"),
                                    i18n("Insert fields, variables and bookmarks"),
                                    mailingsPage,
                                    false,
                                    false,
                                    QStringLiteral("ReferencesToolFactory_ID"));

        auto *viewPage = new QWidget;
        viewPage->setObjectName(QStringLiteral("viewRibbonPage"));
        viewPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("View"), QIcon::fromTheme("view-preview"), i18n("Zoom, rulers, grid and other view options"), viewPage, false, true);

        auto *helpPage = new QWidget;
        helpPage->setObjectName(QStringLiteral("helpRibbonPage"));
        helpPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Help"), QIcon::fromTheme("help-contents"), i18n("Documentation, feedback and about"), helpPage, false, true);

        auto *tableDesignPage = new QWidget;
        tableDesignPage->setObjectName(QStringLiteral("tableDesignRibbonPage"));
        tableDesignPage->setProperty("ribbonContextActive", false);
        tableDesignPage->setProperty("ribbonAutoSelect", false);
        modeBoxFactory.addCustomPage(i18n("Table Design"), QIcon::fromTheme("insert-table"),
                                     i18n("Table styles, shading and borders"), tableDesignPage, false, true);
        new TableRibbonContext(m_canvas, tableDesignPage);

        auto *tablePage = new QWidget;
        tablePage->setObjectName(QStringLiteral("tableRibbonPage"));
        tablePage->setProperty("ribbonContextActive", false);
        // The table commands spread over the whole ribbon width.
        tablePage->setProperty("ribbonFillWidth", false);
        modeBoxFactory.addCustomPage(i18n("Table Layout"), QIcon::fromTheme("insert-table"), i18n("Table rows, columns and borders"), tablePage, false, true);
        new TableRibbonContext(m_canvas, tablePage);

        auto *chartPage = new QWidget;
        chartPage->setObjectName(QStringLiteral("chartRibbonPage"));
        chartPage->setProperty("ribbonContextActive", false);
        chartPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        modeBoxFactory.addCustomPage(i18n("Chart Design"), QIcon::fromTheme("office-chart-bar"),
                                     i18n("Chart type, data, colors and chart elements"), chartPage, false, true);
        new ChartRibbonContext(m_canvas, chartPage);

        QMenuBar *menuBar = m_view->mainWindow()->menuBar();
        menuBar->setNativeMenuBar(false);
        menuBar->hide();
        // XMLGUI can show the menubar again while merging a document's
        // actions. Keep its commands in the ribbon and Settings menu.
        menuBar->setFixedHeight(0);

        auto *quickAccess = new QToolBar;
        quickAccess->setObjectName(QStringLiteral("ribbonQuickAccess"));
        quickAccess->setMovable(false);
        quickAccess->setFloatable(false);
        quickAccess->setIconSize(QSize(20, 20));
        quickAccess->setToolButtonStyle(Qt::ToolButtonIconOnly);
        quickAccess->setContentsMargins(2, 0, 2, 0);
        quickAccess->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        quickAccess->setStyleSheet(QStringLiteral(
            "#ribbonQuickAccess { color: palette(text); background: palette(base); border: 0; border-radius: 4px; spacing: 2px; }"));
        populateQuickAccessLater(quickAccess, m_view->mainWindow(), m_view->actionCollection(), 30);

        auto *settingsButton = new QToolButton;
        settingsButton->setObjectName(QStringLiteral("ribbonSettings"));
        settingsButton->setEnabled(false);
        settingsButton->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
        settingsButton->setToolTip(i18n("Settings"));
        settingsButton->setAutoRaise(true);
        settingsButton->setIconSize(QSize(20, 20));
        settingsButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
        populateRibbonSettingsLater(settingsButton, m_view->mainWindow(), 20);

        for (QToolBar *toolbar : m_view->mainWindow()->findChildren<QToolBar *>()) {
            toolbar->hide();
            // KoPart::showStartUpWidget() looks up "mainToolBar" when the
            // File > New flow opens the template chooser. Keep the XMLGUI
            // container alive even though the ribbon replaces its display.
            if (toolbar->objectName() != QLatin1String("mainToolBar")) {
                m_view->mainWindow()->removeToolBar(toolbar);
                toolbar->deleteLater();
            } else {
                connect(toolbar, &QToolBar::visibilityChanged, toolbar, [toolbar](bool visible) {
                    if (visible) {
                        QTimer::singleShot(0, toolbar, &QWidget::hide);
                    }
                });
            }
        }
        // The document header bar (brand, document title and the search box)
        // has been removed.  The quick access toolbar now sits directly in the
        // tab bar row, in front of the ribbon tabs, and the commands help
        // (search) button moved next to the settings button behind them.
        auto *searchAction = new QAction(QIcon::fromTheme(QStringLiteral("edit-find")), i18n("Search Commands"), this);
        searchAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Q));
        m_view->addAction(searchAction);
        connect(searchAction, &QAction::triggered, this, [this] { showRibbonCommandSearch(m_view); });
        auto *searchButton = new QToolButton;
        searchButton->setObjectName(QStringLiteral("ribbonSearch"));
        searchButton->setDefaultAction(searchAction);
        searchButton->setText(i18n("Search Commands  (Alt+Q)"));
        searchButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        searchButton->setStyleSheet(QStringLiteral(
            "#ribbonSearch { color: palette(text); background: palette(base); border: 0; border-radius: 4px; padding: 3px 10px; }"
            "#ribbonSearch:hover { background: palette(alternate-base); }"));

        // Trailing group: commands help followed by the settings button.
        auto *trailingControls = new QWidget;
        trailingControls->setObjectName(QStringLiteral("ribbonTrailingControls"));
        auto *trailingLayout = new QHBoxLayout(trailingControls);
        trailingLayout->setContentsMargins(6, 0, 6, 0);
        trailingLayout->setSpacing(6);
        trailingLayout->addWidget(searchButton, 0, Qt::AlignVCenter);
        trailingLayout->addWidget(settingsButton, 0, Qt::AlignVCenter);

        modeBoxFactory.setLeadingWidget(quickAccess);
        modeBoxFactory.setTrailingWidget(trailingControls);

        QDockWidget *modeBox = m_view->mainWindow()->createDockWidget(&modeBoxFactory);
        modeBox->setAllowedAreas(Qt::TopDockWidgetArea);
        m_view->mainWindow()->addDockWidget(Qt::TopDockWidgetArea, modeBox);
        modeBox->show();
        // Draw is a contextual tab: it appears only while a document shape is
        // selected, instead of occupying the ribbon for the whole session.
        const auto updateDrawTabVisibility = [this, modeBox] {
            const QList<KoShape *> shapes = m_canvas->shapeManager()->selection()->selectedShapes(KoFlake::StrippedSelection);
            bool shapeSelected = false;
            for (KoShape *shape : shapes) {
                const KWFrame *frame = m_view->kwdocument()->frameOfShape(shape);
                // An object anchored inside text resolves to the text frame;
                // it is still a real shape, unlike the auto-generated page
                // and header/footer text frames.
                if (!frame || frame->shape() != shape
                    || !Words::isAutoGenerated(KWFrameSet::from(frame->shape()))) {
                    shapeSelected = true;
                    break;
                }
            }
            if (QWidget *ribbonBox = modeBox->findChild<QWidget *>(QStringLiteral("wordsRibbon"))) {
                QMetaObject::invokeMethod(ribbonBox, "setToolTabVisible", Qt::DirectConnection,
                                          Q_ARG(QString, QStringLiteral("ConnectionTool")),
                                          Q_ARG(bool, shapeSelected));
                if (shapeSelected) {
                    QMetaObject::invokeMethod(ribbonBox, "showToolTab", Qt::DirectConnection,
                                              Q_ARG(QString, QStringLiteral("ConnectionTool")));
                }
            }
        };
        connect(m_canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, updateDrawTabVisibility);
        QTimer::singleShot(0, this, updateDrawTabVisibility);
        QTimer::singleShot(0, modeBox, [this, modeBox] {
            m_view->mainWindow()->resizeDocks({modeBox}, {modeBox->sizeHint().height()}, Qt::Vertical);
        });
        m_view->mainWindow()->dockerManager()->removeToolOptionsDocker();
        dynamic_cast<KoCanvasObserverBase *>(modeBox)->setObservedCanvas(m_canvas);

        populateFilePageLater(filePage, m_view->mainWindow(), m_view->actionCollection(), 20);

        // Give the ribbon commands the shared, colour-tinted icons. The text
        // tool registers its formatting actions lazily, so refresh them after
        // startup and whenever the active tool changes.
        const auto applyRibbonIcons = [view = QPointer<KWView>(m_view)] {
            if (view) {
                KoAppRibbon::applyCommandIcons(view);
            }
        };
        applyRibbonIcons();
        QTimer::singleShot(0, this, applyRibbonIcons);
        QTimer::singleShot(250, this, applyRibbonIcons);
        connect(KoToolManager::instance(), &KoToolManager::changedTool, this,
                [applyRibbonIcons](KoCanvasController *, int) { applyRibbonIcons(); });

        auto *insertQrCode = new QAction(QIcon::fromTheme(QStringLiteral("insert-qrcode"), QIcon::fromTheme(QStringLiteral("insert-image"))), i18n("Insert QR Code"), this);
        insertQrCode->setToolTip(i18n("Encode text or a web address as a QR code and insert it at the cursor"));
        m_view->actionCollection()->addAction(QStringLiteral("insert_qrcode"), insertQrCode);
        connect(insertQrCode, &QAction::triggered, this, [this] {
            KWTextFrameSet *targetFrameSet = m_view->currentTextFrameSet();
            if (!targetFrameSet) {
                return;
            }
            QRCodeDialog dialog(this);
            if (dialog.exec() != QDialog::Accepted) {
                return;
            }
            const QImage image = dialog.image();
            if (image.isNull()) {
                return;
            }
            m_view->insertImagesIntoTextFrame(targetFrameSet, QVector<QImage>{image});
            m_canvas->setFocus(Qt::OtherFocusReason);
        });

        auto *insertImage = new QAction(QIcon::fromTheme(QStringLiteral("insert-image")), i18n("Insert Image"), this);
        insertImage->setToolTip(i18n("Choose an image to insert into the document"));
        m_view->actionCollection()->addAction(QStringLiteral("insert_picture"), insertImage);
        connect(insertImage, &QAction::triggered, this, [this] {
            // Remember the text frame before the file dialog takes focus: the
            // image belongs at the cursor, not at the page's top-left corner.
            KWTextFrameSet *targetFrameSet = m_view->currentTextFrameSet();
            QStringList mimeTypes;
            for (const QByteArray &mimeType : QImageReader::supportedMimeTypes()) {
                mimeTypes.append(QString::fromLatin1(mimeType));
            }
            QFileDialog dialog(this, i18n("Insert Image"));
            dialog.setFileMode(QFileDialog::ExistingFile);
            dialog.setAcceptMode(QFileDialog::AcceptOpen);
            dialog.setMimeTypeFilters(mimeTypes);
            if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
                return;
            }
            QImageReader reader(dialog.selectedFiles().first());
            reader.setAutoTransform(true);
            const QImage image = reader.read();
            if (image.isNull()) {
                QMessageBox::warning(this, i18n("Insert Image"), i18n("Could not load the selected image: %1", reader.errorString()));
                return;
            }
            if (targetFrameSet) {
                m_view->insertImagesIntoTextFrame(targetFrameSet, QVector<QImage>{image});
                m_canvas->setFocus(Qt::OtherFocusReason);
                return;
            }

            KoShapeFactoryBase *factory = KoShapeRegistry::instance()->value(QStringLiteral("PictureShape"));
            if (!factory) {
                return;
            }
            KoProperties properties;
            properties.setProperty(QStringLiteral("qimage"), image);
            KoShape *shape = factory->createShape(&properties, m_canvas->shapeController()->resourceManager());
            if (!shape) {
                return;
            }
            const QRectF page = m_view->currentPage().rect().adjusted(36, 36, -36, -36);
            if (page.width() > 0 && page.height() > 0) {
                shape->setSize(shape->size().scaled(page.size(), Qt::KeepAspectRatio).boundedTo(shape->size()));
                shape->setPosition(page.topLeft());
            }
            if (KUndo2Command *command = m_canvas->shapeController()->addShapeDirect(shape)) {
                m_canvas->addCommand(command);
                KoSelection *selection = m_canvas->shapeManager()->selection();
                selection->deselectAll();
                selection->select(shape);
            } else {
                delete shape;
            }
            m_canvas->setFocus(Qt::OtherFocusReason);
        });

        // Object types that used to hide at the bottom of the Shapes gallery:
        // each one is a button of its own on the Insert tab.
        struct InsertShape {
            const char *action;
            QString text;
            const char *shapeId;
            const char *icon;
            QString toolTip;
        };
        const InsertShape insertShapes[] = {
            {"insert_textbox", i18n("Text Box"), "TextShapeID", "insert-text",
             i18n("Draw a text box anywhere on the page")},
            {"insert_formula", i18n("Equation"), "FormulaShapeID", "functions",
             i18n("Insert a formula and edit it with the formula tools")},
            {"insert_wordart", i18n("Word Art"), "ArtisticText", "style",
             i18n("Insert an artistic text shape")},
            {"insert_stateshape", i18n("State"), "StateShape", "fact_check",
             i18n("Insert a state shape")},
            {"insert_3dscene", i18n("3D Scene"), "ThreedShape", "category",
             i18n("Insert a 3D scene")},
            {"insert_vectorimage", i18n("Vector Image"), "VectorShapeID", "polyline",
             i18n("Insert a vector image")},
            {"insert_webshape", i18n("Web Shape"), "WebShape", "link",
             i18n("Insert a web page shape")},
        };
        for (const InsertShape &shape : insertShapes) {
            auto *action = new QAction(KoRibbonIcons::forCommand(QLatin1String(shape.icon)), shape.text, this);
            action->setToolTip(shape.toolTip);
            m_view->actionCollection()->addAction(QLatin1String(shape.action), action);
            const QString shapeId = QLatin1String(shape.shapeId);
            connect(action, &QAction::triggered, this, [this, shapeId] {
                if (!m_view->koDocument()->isReadWrite()) {
                    return;
                }
                // An equation goes straight into the text at the cursor, with
                // the formula tools ready to type into it; other shapes are
                // still drawn with the shape tool.
                if (shapeId == QLatin1String("FormulaShapeID")) {
                    if (KWTextFrameSet *frameSet = m_view->currentTextFrameSet()) {
                        m_view->insertFormulaIntoTextFrame(frameSet);
                        return;
                    }
                }
                if (KoCreateShapesTool *tool = KoToolManager::instance()->shapeCreatorTool(m_canvas)) {
                    tool->setShapeId(shapeId);
                    tool->setShapeProperties(nullptr);
                    KoToolManager::instance()->switchToolRequested(KoCreateShapesTool_ID);
                    m_canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
                }
            });
        }

        // Keep the less common Calligra object types in a menu so the main
        // ribbon follows Word's group order without an oversized illustration band.
        auto *objects = new QAction(KoRibbonIcons::forCommand(QStringLiteral("insert_vectorimage")), i18n("Object"), this);
        auto *objectMenu = new QMenu(this);
        for (const char *name : {"insert_vectorimage", "insert_stateshape", "insert_3dscene", "insert_webshape", "insert_qrcode"}) {
            objectMenu->addAction(m_view->actionCollection()->action(QLatin1String(name)));
        }
        objects->setMenu(objectMenu);
        objects->setProperty("ribbonMenuOnly", true);
        m_view->actionCollection()->addAction(QStringLiteral("insert_object"), objects);

        auto *pageNumber = new QAction(QIcon::fromTheme(QStringLiteral("insert-text")), i18n("Page Number"), this);
        pageNumber->setToolTip(i18n("Insert the current page number at the cursor"));
        m_view->actionCollection()->addAction(QStringLiteral("insert_page_number"), pageNumber);
        connect(pageNumber, &QAction::triggered, this, [this] {
            if (!m_view->koDocument()->isReadWrite()) {
                return;
            }
            KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas);
            auto *factory = KoInlineObjectRegistry::instance()->value(QStringLiteral("page"));
            if (editor && factory) {
                KoProperties properties;
                properties.setProperty(QStringLiteral("vartype"), 2);
                if (auto *variable = factory->createInlineObject(&properties)) {
                    editor->insertInlineObject(variable);
                }
            }
        });

        auto *link = new QAction(QIcon::fromTheme(QStringLiteral("insert-link")), i18n("Link"), this);
        link->setToolTip(i18n("Insert a link to a web page, file or bookmark"));
        m_view->actionCollection()->addAction(QStringLiteral("ribbon_insert_link"), link);
        connect(link, &QAction::triggered, this, [this] {
            QPointer<KoTextEditor> editor = KoTextEditor::getTextEditorFromCanvas(m_canvas);
            if (!editor || !m_view->koDocument()->isReadWrite()) {
                return;
            }
            QDialog dialog(this);
            dialog.setWindowTitle(i18n("Insert Link"));
            auto *layout = new QVBoxLayout(&dialog);
            auto *textLabel = new QLabel(i18n("Text to display:"), &dialog);
            layout->addWidget(textLabel);
            auto *text = new QLineEdit(editor->selectedText(), &dialog);
            textLabel->setBuddy(text);
            layout->addWidget(text);
            auto *addressLabel = new QLabel(i18n("Address:"), &dialog);
            layout->addWidget(addressLabel);
            auto *address = new QLineEdit(&dialog);
            addressLabel->setBuddy(address);
            address->setToolTip(i18n("Enter a full URL such as https://example.com, a file:// URL, or a bookmark name"));
            address->setPlaceholderText(QStringLiteral("https://"));
            layout->addWidget(address);
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttons);
            buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
            connect(address, &QLineEdit::textChanged, &dialog, [buttons](const QString &value) {
                buttons->button(QDialogButtonBox::Ok)->setEnabled(!value.trimmed().isEmpty());
            });
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            address->setFocus();
            if (dialog.exec() == QDialog::Accepted && editor) {
                QString href = address->text().trimmed();
                // The text editor adds the fragment marker for bookmark links.
                if (href.startsWith(QLatin1Char('#'))) {
                    href.remove(0, 1);
                }
                if (!href.isEmpty()) {
                    editor->insertText(text->text().isEmpty() ? href : text->text(), href);
                }
                m_canvas->setFocus(Qt::OtherFocusReason);
            }
        });

        auto *bookmark = new QAction(QIcon::fromTheme(QStringLiteral("bookmark-new")), i18n("Bookmark"), this);
        m_view->actionCollection()->addAction(QStringLiteral("ribbon_insert_bookmark"), bookmark);
        connect(bookmark, &QAction::triggered, this, [this] {
            QPointer<KoTextEditor> editor = KoTextEditor::getTextEditorFromCanvas(m_canvas);
            if (!editor || !m_view->koDocument()->isReadWrite()) {
                return;
            }
            bool accepted = false;
            const QString name = QInputDialog::getText(this, i18n("Insert Bookmark"), i18n("Bookmark name:"),
                                                       QLineEdit::Normal, QString(), &accepted).trimmed();
            if (!accepted || name.isEmpty() || !editor) {
                return;
            }
            const auto *ranges = KoTextDocument(editor->document()).textRangeManager();
            if (ranges && ranges->bookmarkManager()->bookmark(name)) {
                QMessageBox::information(this, i18n("Insert Bookmark"), i18n("A bookmark with this name already exists."));
                return;
            }
            editor->addBookmark(name);
            m_canvas->setFocus(Qt::OtherFocusReason);
        });

        const QList<RibbonGroup> insertGroups = {
            {i18n("Pages"), {{QStringLiteral("insert_framebreak"), i18n("Page Break")}}},
            {i18n("Tables"), {{QStringLiteral("insert_table"), i18n("Table")}}},
            {i18n("Illustrations"),
             {{QStringLiteral("insert_picture"), i18n("Pictures")},
              {QStringLiteral("add_shape"), i18n("Shapes")},
              {QStringLiteral("insert_chart"), i18n("Chart")}}, RibbonGroup::LargeAndStacked},
            {i18n("Links"),
             {{QStringLiteral("ribbon_insert_link"), i18n("Link")},
              {QStringLiteral("ribbon_insert_bookmark"), i18n("Bookmark")}}, RibbonGroup::Stacked},
            {i18n("Comments"), {{QStringLiteral("insert_annotation"), i18n("Comment")}}},
            {i18n("Header & Footer"),
             {{QStringLiteral("insert_header"), i18n("Header")},
              {QStringLiteral("insert_footer"), i18n("Footer")},
              {QStringLiteral("insert_page_number"), i18n("Page Number")}}, RibbonGroup::Stacked},
            {i18n("Text"),
             {{QStringLiteral("insert_textbox"), i18n("Text Box")},
              {QStringLiteral("insert_variable"), i18n("Quick Parts")},
              {QStringLiteral("insert_wordart"), i18n("WordArt")},
              {QStringLiteral("insert_object"), i18n("Object")}}, RibbonGroup::LargeAndStacked},
            {i18n("Symbols"),
             {{QStringLiteral("insert_formula"), i18n("Equation")},
              {QStringLiteral("insert_specialchar"), i18n("Symbol")}}, RibbonGroup::Stacked},
        };
        populateRibbonGroupsLater(insertPage, insertGroups, findRibbonAction, true, 20);

        const QList<RibbonGroup> drawGroups = {
            {i18n("Select"), {{QStringLiteral("InteractionTool"), i18n("Select")}}},
            {i18n("Draw"),
             {{QStringLiteral("KoPencilTool"), i18n("Freehand")},
              {QStringLiteral("CreatePathTool"), i18n("Draw Path")},
              {QStringLiteral("ConnectionTool"), i18n("Connect Shapes")},
              {QStringLiteral("convert-to-path"), i18n("Convert to Path")}}, RibbonGroup::LargeAndStacked},
            {i18n("Rotate"),
             {{QStringLiteral("rotate-left"), i18n("Rotate Left")},
              {QStringLiteral("rotate-right"), i18n("Rotate Right")}}, RibbonGroup::Stacked},
        };
        populateRibbonGroupsLater(drawPage, drawGroups, findDrawAction, true, 20);

        auto *pageColorAction = new QAction(KoRibbonIcons::forCommand(QStringLiteral("page_color")), i18n("Page Color"), this);
        pageColorAction->setObjectName(QStringLiteral("page_color"));
        pageColorAction->setToolTip(i18n("Change the background colour of the current page style"));
        connect(pageColorAction, &QAction::triggered, this, [this] {
            KWPage page = m_view->currentPage();
            if (!page.isValid()) {
                return;
            }
            KWPageStyle styleBefore = page.pageStyle();
            QColor currentColor = Qt::white;
            if (auto *background = dynamic_cast<KoColorBackground *>(styleBefore.background().data())) {
                currentColor = background->color();
            }
            const QColor color = QColorDialog::getColor(currentColor, this, i18n("Page Color"));
            if (!color.isValid()) {
                return;
            }
            const QSharedPointer<KoShapeBackground> backgroundBefore = styleBefore.background();
            const QSharedPointer<KoShapeBackground> backgroundAfter(new KoColorBackground(color));
            m_view->kwdocument()->addCommand(new PageStyleAppearanceCommand(m_view->kwdocument(), styleBefore,
                                                                            styleBefore.pageLayout(), styleBefore.pageLayout(),
                                                                            backgroundBefore, backgroundAfter));
        });
        auto *watermarkAction = new QAction(KoRibbonIcons::forCommand(QStringLiteral("watermark")), i18n("Watermark"), this);
        watermarkAction->setObjectName(QStringLiteral("watermark"));
        watermarkAction->setToolTip(i18n("Add a text watermark behind the document content"));
        connect(watermarkAction, &QAction::triggered, this, [this] {
            KWPage page = m_view->currentPage();
            if (!page.isValid()) {
                return;
            }
            bool accepted = false;
            const QString text = QInputDialog::getText(this, i18n("Watermark"),
                                                       i18n("Watermark text (leave empty to remove):"),
                                                       QLineEdit::Normal, QStringLiteral("DRAFT"), &accepted);
            if (!accepted) {
                return;
            }
            KWPageStyle styleBefore = page.pageStyle();
            const KoPageLayout pageLayout = styleBefore.pageLayout();
            const QSharedPointer<KoShapeBackground> backgroundBefore = styleBefore.background();
            QSharedPointer<KoShapeBackground> backgroundAfter;
            QColor baseColor = Qt::white;
            if (auto *background = dynamic_cast<KoColorBackground *>(styleBefore.background().data())) {
                baseColor = background->color();
            }
            if (text.trimmed().isEmpty()) {
                backgroundAfter = QSharedPointer<KoShapeBackground>(new KoColorBackground(baseColor));
            } else {
                const int imageWidth = 1000;
                const int imageHeight = qBound(400, qRound(imageWidth * pageLayout.height / qMax(1.0, pageLayout.width)), 2400);
                QImage watermark(imageWidth, imageHeight, QImage::Format_ARGB32_Premultiplied);
                watermark.fill(baseColor);
                QPainter painter(&watermark);
                painter.setRenderHint(QPainter::Antialiasing);
                QFont font = painter.font();
                font.setBold(true);
                font.setPointSizeF(imageWidth / 7.0);
                painter.setFont(font);
                painter.setPen(baseColor.lightness() < 128 ? QColor(255, 255, 255, 80) : QColor(128, 128, 128, 80));
                painter.translate(imageWidth / 2.0, imageHeight / 2.0);
                painter.rotate(-45);
                painter.drawText(QRectF(-imageWidth, -imageHeight, imageWidth * 2.0, imageHeight * 2.0),
                                 Qt::AlignCenter, text.trimmed());
                painter.end();

                auto *pattern = new KoPatternBackground(m_view->kwdocument()->resourceManager()->imageCollection());
                pattern->setPattern(watermark);
                pattern->setRepeat(KoPatternBackground::Stretched);
                pattern->setReferencePoint(KoPatternBackground::Center);
                pattern->setPatternDisplaySize(QSizeF(pageLayout.width, pageLayout.height));
                backgroundAfter = QSharedPointer<KoShapeBackground>(pattern);
            }
            m_view->kwdocument()->addCommand(new PageStyleAppearanceCommand(m_view->kwdocument(), styleBefore,
                                                                            pageLayout, pageLayout,
                                                                            backgroundBefore, backgroundAfter));
        });

        auto *pageBordersAction = new QAction(KoRibbonIcons::forCommand(QStringLiteral("page_borders")), i18n("Page Borders"), this);
        pageBordersAction->setObjectName(QStringLiteral("page_borders"));
        pageBordersAction->setToolTip(i18n("Set a border around the pages"));
        connect(pageBordersAction, &QAction::triggered, this, [this] {
            KWPage page = m_view->currentPage();
            if (!page.isValid()) {
                return;
            }
            KWPageStyle styleBefore = page.pageStyle();
            const KoPageLayout pageLayout = styleBefore.pageLayout();

            QDialog dialog(this);
            dialog.setWindowTitle(i18n("Page Borders"));
            auto *dialogLayout = new QVBoxLayout(&dialog);
            auto *enabled = new QCheckBox(i18n("Show page border"), &dialog);
            enabled->setChecked(pageLayout.border.hasBorder());
            dialogLayout->addWidget(enabled);

            auto *options = new QWidget(&dialog);
            auto *optionsLayout = new QGridLayout(options);
            optionsLayout->addWidget(new QLabel(i18n("Style:"), options), 0, 0);
            auto *styleCombo = new QComboBox(options);
            styleCombo->addItem(i18n("Solid"), KoBorder::BorderSolid);
            styleCombo->addItem(i18n("Dashed"), KoBorder::BorderDashed);
            styleCombo->addItem(i18n("Dotted"), KoBorder::BorderDotted);
            styleCombo->addItem(i18n("Double"), KoBorder::BorderDouble);
            const int currentStyle = styleCombo->findData(pageLayout.border.hasBorder()
                                                               ? pageLayout.border.borderStyle(KoBorder::TopBorder)
                                                               : KoBorder::BorderSolid);
            styleCombo->setCurrentIndex(currentStyle >= 0 ? currentStyle : 0);
            optionsLayout->addWidget(styleCombo, 0, 1);

            optionsLayout->addWidget(new QLabel(i18n("Width:"), options), 1, 0);
            auto *widthSpin = new QDoubleSpinBox(options);
            widthSpin->setRange(0.25, 12.0);
            widthSpin->setSingleStep(0.25);
            widthSpin->setSuffix(i18n(" pt"));
            widthSpin->setValue(pageLayout.border.hasBorder()
                                    ? pageLayout.border.borderWidth(KoBorder::TopBorder) : 1.0);
            optionsLayout->addWidget(widthSpin, 1, 1);

            optionsLayout->addWidget(new QLabel(i18n("Colour:"), options), 2, 0);
            auto *colorButton = new QPushButton(options);
            QColor borderColor = pageLayout.border.hasBorder()
                ? pageLayout.border.borderColor(KoBorder::TopBorder) : QColor(Qt::black);
            if (!borderColor.isValid()) {
                borderColor = Qt::black;
            }
            const auto updateColorButton = [colorButton](const QColor &color) {
                colorButton->setText(color.name());
                colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name()));
            };
            updateColorButton(borderColor);
            connect(colorButton, &QPushButton::clicked, &dialog, [&borderColor, &dialog, updateColorButton] {
                const QColor color = QColorDialog::getColor(borderColor, &dialog, i18n("Border Colour"));
                if (color.isValid()) {
                    borderColor = color;
                    updateColorButton(color);
                }
            });
            optionsLayout->addWidget(colorButton, 2, 1);
            dialogLayout->addWidget(options);
            connect(enabled, &QCheckBox::toggled, options, &QWidget::setEnabled);
            options->setEnabled(enabled->isChecked());

            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            dialogLayout->addWidget(buttons);
            if (dialog.exec() != QDialog::Accepted) {
                return;
            }

            KoPageLayout newLayout = pageLayout;
            const bool showBorder = enabled->isChecked();
            const auto borderStyle = static_cast<KoBorder::BorderStyle>(styleCombo->currentData().toInt());
            const qreal borderWidth = widthSpin->value();
            for (KoBorder::BorderSide side : {KoBorder::TopBorder, KoBorder::LeftBorder, KoBorder::BottomBorder, KoBorder::RightBorder}) {
                newLayout.border.setBorderStyle(side, showBorder ? borderStyle : KoBorder::BorderNone);
                newLayout.border.setBorderWidth(side, showBorder ? borderWidth : 0.0);
                if (showBorder) {
                    newLayout.border.setBorderColor(side, borderColor);
                }
            }
            if (newLayout == pageLayout) {
                return;
            }
            const QSharedPointer<KoShapeBackground> backgroundBefore = styleBefore.background();
            m_view->kwdocument()->addCommand(new PageStyleAppearanceCommand(m_view->kwdocument(), styleBefore,
                                                                            pageLayout, newLayout,
                                                                            backgroundBefore, backgroundBefore));
        });

        const ActionFinder findDesignAction = [findRibbonAction, pageColorAction, watermarkAction, pageBordersAction](const QString &name) {
            if (name == QLatin1String("page_color")) {
                return pageColorAction;
            }
            if (name == QLatin1String("watermark")) {
                return watermarkAction;
            }
            if (name == QLatin1String("page_borders")) {
                return pageBordersAction;
            }
            return findRibbonAction(name);
        };
        QWidget *styleGallery = createStyleGallery(m_view->kwdocument(), m_canvas, designPage);
        buildDesignRibbonPage(designPage, findDesignAction, styleGallery);

        // Microsoft Office 2026 style groups of the Mailings tab.
        const QList<RibbonGroup> mailingsGroups = {
            {i18n("Start Mail Merge"),
             {{QStringLiteral("mailmerge_select_recipients"), i18n("Select Recipients")},
              {QStringLiteral("mailmerge_edit_recipients"), i18n("Edit Recipient List")}}, RibbonGroup::LargeAndStacked},
            {i18n("Write & Insert Fields"),
             {{QStringLiteral("mailmerge_insert_field"), i18n("Insert Merge Field")},
              {QStringLiteral("mailmerge_greeting_line"), i18n("Greeting Line")},
              {QStringLiteral("mailmerge_highlight"), i18n("Highlight Fields")}}, RibbonGroup::LargeAndStacked},
            {i18n("Preview Results"),
             {{QStringLiteral("mailmerge_preview"), i18n("Preview Results")},
              {QStringLiteral("mailmerge_first"), i18n("First")},
              {QStringLiteral("mailmerge_prev"), i18n("Previous")},
              {QStringLiteral("mailmerge_next"), i18n("Next")},
              {QStringLiteral("mailmerge_last"), i18n("Last")}}, RibbonGroup::PreviewNavigation},
            {i18n("Finish"), {{QStringLiteral("mailmerge_finish"), i18n("Edit Individual Documents")}}},
        };
        populateRibbonGroupsLater(mailingsPage, mailingsGroups, findRibbonAction, true, 20);

        // Microsoft Office 2026 style groups of the View tab.
        const auto viewActions = std::make_shared<QMap<QString, QAction *>>();
        const auto addViewAction = [this, viewActions](const QString &name, const QString &text, const QString &icon) {
            auto *command = new QAction(QIcon::fromTheme(icon), text, this);
            // The shared ribbon icon lookup matches on the action name.
            command->setObjectName(name);
            viewActions->insert(name, command);
            return command;
        };
        auto *horizontalRuler = addViewAction(QStringLiteral("ribbon_horizontal_ruler"), i18n("Horiz. ruler"), QStringLiteral("show-ruler"));
        auto *verticalRuler = addViewAction(QStringLiteral("ribbon_vertical_ruler"), i18n("Vert. ruler"), QStringLiteral("show-ruler"));
        for (QAction *ruler : {horizontalRuler, verticalRuler}) {
            ruler->setCheckable(true);
            ruler->setChecked(m_view->kwdocument()->config().viewRulers());
        }
        m_horizontalRuler->setProperty("ribbonRulerVisible", horizontalRuler->isChecked());
        m_verticalRuler->setProperty("ribbonRulerVisible", verticalRuler->isChecked());
        connect(horizontalRuler, &QAction::toggled, this, [this](bool visible) {
            m_horizontalRuler->setProperty("ribbonRulerVisible", visible);
            m_horizontalRuler->setVisible(visible);
        });
        connect(verticalRuler, &QAction::toggled, this, [this](bool visible) {
            m_verticalRuler->setProperty("ribbonRulerVisible", visible);
            m_verticalRuler->setVisible(visible);
        });
        QTimer::singleShot(0, this, [this, findRibbonAction, horizontalRuler, verticalRuler] {
            if (QAction *rulers = findRibbonAction(QStringLiteral("show_ruler"))) {
                connect(rulers, &QAction::toggled, horizontalRuler, &QAction::setChecked);
                connect(rulers, &QAction::toggled, verticalRuler, &QAction::setChecked);
            }
        });
        const auto addZoom = [this, addViewAction](const QString &name, const QString &label, const QString &icon, KoZoomMode::Mode mode, qreal factor) {
            QAction *command = addViewAction(name, label, icon);
            command->setCheckable(true);
            connect(command, &QAction::triggered, this, [this, command, mode, factor] {
                m_view->zoomController()->setZoom(mode, factor);
                command->setChecked(true);
            });
            QTimer::singleShot(0, command, [this, command, mode, factor] {
                const auto sync = [command, mode, factor](KoZoomMode::Mode currentMode, qreal currentFactor) {
                    command->setChecked(currentMode == mode
                                        && (mode != KoZoomMode::ZOOM_CONSTANT || qAbs(currentFactor - factor) < 0.001));
                };
                connect(m_view->zoomController(), &KoZoomController::zoomChanged, command, sync);
                auto *handler = static_cast<KoZoomHandler *>(m_view->viewConverter());
                sync(handler->zoomMode(), handler->zoom());
            });
        };
        auto *setZoom = addViewAction(QStringLiteral("ribbon_set_zoom"), i18n("Set\nzoom"), QStringLiteral("zoom-select"));
        connect(setZoom, &QAction::triggered, this, [this] {
            bool accepted = false;
            const int percent = QInputDialog::getInt(this, i18n("Set Zoom"), i18n("Zoom (%):"),
                                                    qRound(m_view->viewConverter()->zoom() * 100), 10, 500, 10, &accepted);
            if (accepted) {
                m_view->zoomController()->setZoom(KoZoomMode::ZOOM_CONSTANT, percent / 100.0);
            }
        });
        addZoom(QStringLiteral("ribbon_actual_size"), i18n("Actual\nsize"), QStringLiteral("zoom-original"), KoZoomMode::ZOOM_CONSTANT, 1.0);
        addZoom(QStringLiteral("ribbon_fit_width"), i18n("Fit width"), QStringLiteral("zoom-fit-width"), KoZoomMode::ZOOM_WIDTH, 1.0);
        addZoom(QStringLiteral("ribbon_full_page"), i18n("Full page"), QStringLiteral("zoom-fit-best"), KoZoomMode::ZOOM_PAGE, 1.0);
        addZoom(QStringLiteral("ribbon_fit_text"), i18n("Fit margins"), QStringLiteral("zoom-fit-width"), KoZoomMode::ZOOM_TEXT, 1.0);
        auto *zoomMenuAction = addViewAction(QStringLiteral("ribbon_zoom_menu"), i18n("Zoom"), QStringLiteral("zoom-in"));
        auto *zoomMenu = new QMenu(viewPage);
        for (int percent : {50, 75, 100, 125, 150, 200}) {
            auto *preset = zoomMenu->addAction(i18n("%1%", percent));
            connect(preset, &QAction::triggered, this, [this, percent] {
                m_view->zoomController()->setZoom(KoZoomMode::ZOOM_CONSTANT, percent / 100.0);
            });
        }
        zoomMenuAction->setMenu(zoomMenu);
        zoomMenuAction->setProperty("ribbonMenuOnly", true);
        auto *sidebars = addViewAction(QStringLiteral("ribbon_sidebars"), i18n("Sidebars"), QStringLiteral("view-sidetree"));
        auto *sidebarMenu = new QMenu(viewPage);
        connect(sidebarMenu, &QMenu::aboutToShow, viewPage, [this, sidebarMenu, modeBox] {
            sidebarMenu->clear();
            for (QDockWidget *dock : m_view->mainWindow()->findChildren<QDockWidget *>()) {
                if (dock != modeBox) {
                    sidebarMenu->addAction(dock->toggleViewAction());
                }
            }
        });
        sidebars->setMenu(sidebarMenu);
        sidebars->setProperty("ribbonMenuOnly", true);
        auto *windows = addViewAction(QStringLiteral("ribbon_windows"), i18n("Windows"), QStringLiteral("window-duplicate"));
        auto *windowMenu = new QMenu(viewPage);
        connect(windowMenu, &QMenu::aboutToShow, viewPage, [windowMenu, findRibbonAction] {
            windowMenu->clear();
            if (QAction *newWindow = findRibbonAction(QStringLiteral("view_newview"))) {
                windowMenu->addAction(newWindow);
            }
            windowMenu->addSeparator();
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *window = qobject_cast<KoMainWindow *>(widget)) {
                    if (window->isVisible()) {
                        QAction *activate = windowMenu->addAction(window->windowTitle());
                        const QPointer<KoMainWindow> target(window);
                        connect(activate, &QAction::triggered, windowMenu, [target] {
                            if (target) { target->showNormal(); target->raise(); target->activateWindow(); }
                        });
                    }
                }
            }
        });
        windows->setMenu(windowMenu);
        windows->setProperty("ribbonMenuOnly", true);
        const ActionFinder findViewAction = [viewActions, findDrawAction](const QString &name) {
            return viewActions->contains(name) ? viewActions->value(name) : findDrawAction(name);
        };
        const QList<RibbonGroup> viewGroups = {
            {i18n("Views"),
             {{QStringLiteral("view_fullscreen"), i18n("Full Screen")},
              {QStringLiteral("InteractionTool"), i18n("Select Objects")}}, RibbonGroup::LargeAndStacked},
            {i18n("Fields"),
             {{QStringLiteral("view_fieldshadings"), i18n("Shade fields")},
              {QStringLiteral("view_formattingchars"), i18n("Formatting marks")},
              {QStringLiteral("view_frameborders"), i18n("Text Boundaries")}}, RibbonGroup::CheckBoxes},
            {i18n("Show"),
             {{QStringLiteral("view_grid"), i18n("Gridlines")},
              {QStringLiteral("view_snaptogrid"), i18n("Snap to grid")},
              {QStringLiteral("view_show_guides"), i18n("Guides")},
              {QStringLiteral("ribbon_horizontal_ruler"), i18n("Horiz. ruler")},
              {QStringLiteral("ribbon_vertical_ruler"), i18n("Vertical Ruler")}}, RibbonGroup::CheckBoxes},
            {i18n("Zoom"),
             {{QStringLiteral("ribbon_set_zoom"), i18n("Set\nzoom")},
              {QStringLiteral("ribbon_zoom_menu"), i18n("Zoom")},
              {QStringLiteral("ribbon_actual_size"), i18n("Actual\nsize")},
              {QStringLiteral("ribbon_fit_text"), i18n("Fit margins")},
              {QStringLiteral("ribbon_full_page"), i18n("Full page")},
              {QStringLiteral("ribbon_fit_width"), i18n("Fit width")}}, RibbonGroup::ZoomButtons},
            {i18n("Windows"),
             {{QStringLiteral("ribbon_sidebars"), i18n("Sidebars")},
              {QStringLiteral("ribbon_windows"), i18n("Windows")}}},
        };
        populateRibbonGroupsLater(viewPage, viewGroups, findViewAction, true, 20);

        // Microsoft Office 2026 style groups of the Help tab.
        const QList<RibbonGroup> helpGroups = {
            {i18n("Help"),
             {{QStringLiteral("help_contents"), i18n("Help")},
              {QStringLiteral("help_report_bug"), i18n("Report Bug")},
              {QStringLiteral("help_switch_application_language"), i18n("Switch Language")}}, RibbonGroup::LargeAndStacked},
            {i18n("About"),
             {{QStringLiteral("help_about_app"), i18n("About Calligra Words")},
              {QStringLiteral("help_about_kde"), i18n("About KDE")}}, RibbonGroup::Stacked},
        };
        populateRibbonGroupsLater(helpPage, helpGroups, findRibbonAction, true, 20);
    }

    gridLayout->addWidget(m_horizontalRuler->tabChooser(), 0, 0);
    gridLayout->addWidget(m_horizontalRuler, 0, 1);
    gridLayout->addWidget(m_verticalRuler, 1, 0);
    gridLayout->addWidget(canvasController, 1, 1);

    new KoRulerController(m_horizontalRuler, m_canvas->resourceManager());

    connect(m_view->kwdocument(), &KoDocument::unitChanged, m_horizontalRuler, &KoRuler::setUnit);
    connect(m_view->kwdocument(), &KoDocument::unitChanged, m_verticalRuler, &KoRuler::setUnit);
    connect(m_view->kwdocument(), &KWDocument::pageSetupChanged, this, &KWGui::pageSetupChanged);

    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::canvasOffsetXChanged, m_horizontalRuler, &KoRuler::setOffset);
    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::canvasOffsetYChanged, m_verticalRuler, &KoRuler::setOffset);
    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::canvasOffsetYChanged, parent, &KWView::offsetInDocumentMoved);
    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::canvasMousePositionChanged, this, &KWGui::updateMousePos);
    connect(m_canvasController->proxyObject, &KoCanvasControllerProxyObject::moveDocumentOffset, m_canvas, &KWCanvas::setDocumentOffset);

    connect(m_canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, &KWGui::shapeSelectionChanged);

    m_verticalRuler->createGuideToolConnection(m_canvas);
    m_horizontalRuler->createGuideToolConnection(m_canvas);

    pageSetupChanged();

    QTimer::singleShot(0, this, &KWGui::setupUnitActions);
}

KWGui::~KWGui() = default;

int KWGui::visibleWidth() const
{
    return m_canvasController->visibleWidth();
}

int KWGui::visibleHeight() const
{
    return m_canvasController->visibleHeight();
}

QSize KWGui::viewportSize() const
{
    return m_canvasController->viewportSize();
}

bool KWGui::horizontalScrollBarVisible()
{
    return static_cast<KoCanvasControllerWidget *>(m_canvasController)->horizontalScrollBar()
        && static_cast<KoCanvasControllerWidget *>(m_canvasController)->horizontalScrollBar()->isVisible();
}

void KWGui::pageSetupChanged()
{
    const KWPageManager *pm = m_view->kwdocument()->pageManager();
    const KWPage firstPage = pm->begin();
    const KWPage lastPage = pm->last();
    int height = 0;
    if (lastPage.isValid())
        height = lastPage.offsetInDocument() + lastPage.height();
    m_verticalRuler->setRulerLength(height);
    updateRulers();
    int width = 0;
    if (firstPage.isValid())
        width = firstPage.width();
    m_horizontalRuler->setRulerLength(width);
    m_horizontalRuler->setActiveRange(0, width);
    m_verticalRuler->setActiveRange(0, height);
    updateRulers();
}

void KWGui::updateMousePos(const QPoint &point)
{
    QPoint canvasOffset(m_canvasController->canvasOffsetX(), m_canvasController->canvasOffsetY());
    // the offset is positive it the canvas is shown fully visible
    canvasOffset.setX(canvasOffset.x() < 0 ? canvasOffset.x() : 0);
    canvasOffset.setY(canvasOffset.y() < 0 ? canvasOffset.y() : 0);
    QPoint viewPos = point - canvasOffset;
    m_horizontalRuler->updateMouseCoordinate(viewPos.x());
    m_verticalRuler->updateMouseCoordinate(viewPos.y());
}

void KWGui::updateRulers() const
{
    for (KoRuler *ruler : {m_verticalRuler, m_horizontalRuler}) {
        const QVariant individual = ruler->property("ribbonRulerVisible");
        ruler->setVisible(individual.isValid() ? individual.toBool() : m_view->kwdocument()->config().viewRulers());
    }
}

void KWGui::shapeSelectionChanged()
{
}

void KWGui::setupUnitActions()
{
    QList<QAction *> unitActions = m_view->createChangeUnitActions();
    QAction *separator = new QAction(this);
    separator->setSeparator(true);
    unitActions.append(separator);
    unitActions.append(m_view->actionCollection()->action("format_page"));
    m_horizontalRuler->setPopupActionList(unitActions);
}

void KWGui::mouseMoveEvent(QMouseEvent *e)
{
    m_view->viewMouseMoveEvent(e);
}
