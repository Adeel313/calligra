/*
 *  SPDX-FileCopyrightText: 2011 Jignesh Kakadiya <jigneshhk1992@gmail.com>
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 */

// pagetool includes
#include "KWPageTool.h"
#include <KoRibbonLayout.h>
#include "KWPageSetupGallery.h"
#include "KWView.h"
#include "frames/KWTextFrameSet.h"

// words includes
#include "KWCanvas.h"
#include "KWDocument.h"

// Qt includes
#include <QList>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDoubleSpinBox>
#include <QIcon>
#include <QLabel>
#include <QToolButton>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QMenu>
#include <QTimer>
#include <QSignalBlocker>
#include <QWidgetAction>

#include <algorithm>

// calligra includes
#include <KoCanvasBase.h>
#include <KoCharacterStyle.h>
#include <KoColumns.h>
#include <KoOdfLineNumberingConfiguration.h>
#include <KoPageLayout.h>
#include <KoPointerEvent.h>
#include <KoRibbonIcons.h>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include <KoText.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
#include <KoToolManager.h>
#include <KoUnit.h>
#include <KoViewConverter.h>
#include <kundo2command.h>

// words includes
#include "KWPage.h"
#include "KWPageStyle.h"
#include "commands/KWPageStylePropertiesCommand.h"

KWPageTool::KWPageTool(KoCanvasBase *canvas)
    : KoToolBase(canvas)
{
    m_canvas = dynamic_cast<KWCanvas *>(canvas);
    if (m_canvas) {
        m_document = m_canvas->document();
    }
}

KWPageTool::~KWPageTool() = default;

void KWPageTool::paint(QPainter &painter, const KoViewConverter &converter)
{
    Q_UNUSED(painter)
    Q_UNUSED(converter)
}

void KWPageTool::activate(ToolActivation toolActivation, const QSet<KoShape *> &shapes)
{
    useCursor(Qt::ArrowCursor);
    repaintDecorations();
    Q_UNUSED(toolActivation);
    Q_UNUSED(shapes);
}

void KWPageTool::mousePressEvent(KoPointerEvent *event)
{
    event->ignore();
}

void KWPageTool::mouseMoveEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KWPageTool::mouseReleaseEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KWPageTool::insertPageBreak()
{
    if (m_document->mainFrameSet()) {
        // lets just add one to the main text frameset
        KoTextEditor *editor = KoTextDocument(m_document->mainFrameSet()->document()).textEditor();
        if (editor == KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
            editor->insertFrameBreak();
        }
    }
}

PageSetupState KWPageTool::pageSetupState() const
{
    PageSetupState state;
    KWView *view = m_canvas ? m_canvas->view() : nullptr;
    if (!view) {
        return state;
    }
    const KWPage page = view->currentPage();
    if (!page.isValid()) {
        return state;
    }
    const KWPageStyle style = page.pageStyle();
    const KoPageLayout layout = style.pageLayout();
    state.size = QSizeF(layout.width, layout.height);
    state.margins = QMarginsF(qMax(0.0, layout.leftMargin),
                              qMax(0.0, layout.topMargin),
                              qMax(0.0, layout.rightMargin),
                              qMax(0.0, layout.bottomMargin));
    state.format = layout.format;
    state.orientation = layout.orientation;
    state.columns = qMax(1, style.columns().count);
    return state;
}

void KWPageTool::refreshPageSetupGallery()
{
    if (m_gallery) {
        m_gallery->setState(pageSetupState());
    }
}

void KWPageTool::applyPageSetup(const std::function<void(KoPageLayout &layout, KoColumns &columns)> &change,
                                const KUndo2MagicString &text)
{
    KWView *view = m_canvas ? m_canvas->view() : nullptr;
    if (!view || !m_document || !m_document->isReadWrite()) {
        return;
    }
    const KWPage page = view->currentPage();
    if (!page.isValid()) {
        return;
    }

    KWPageStyle style = page.pageStyle();
    KoPageLayout layout = style.pageLayout();
    KoColumns columns = style.columns();
    change(layout, columns);

    // Every page that uses this style follows the change, so it is a style
    // change and not only a property of the page under the cursor.
    style.detach(style.name());
    style.setPageLayout(layout);
    style.setColumns(columns);

    auto *command = new KUndo2Command(text);
    new KWPageStylePropertiesCommand(m_document, page.pageStyle(), style, command);
    m_document->addCommand(command);
    m_document->firePageSetupChanged();
    refreshPageSetupGallery();
}

void KWPageTool::applyMargins(const QMarginsF &margins)
{
    applyPageSetup(
        [margins](KoPageLayout &layout, KoColumns &) {
            layout.leftMargin = margins.left();
            layout.topMargin = margins.top();
            layout.rightMargin = margins.right();
            layout.bottomMargin = margins.bottom();
            // A single page layout, not the facing pages one.
            layout.pageEdge = -1;
            layout.bindingSide = -1;
        },
        kundo2_i18n("Change Margins"));
}

void KWPageTool::applyOrientation(KoPageFormat::Orientation orientation)
{
    applyPageSetup(
        [orientation](KoPageLayout &layout, KoColumns &) {
            if (layout.orientation == orientation) {
                return;
            }
            layout.orientation = orientation;
            // The paper keeps its format, so only the two sides exchange.
            const bool tooTall = layout.width < layout.height;
            if ((orientation == KoPageFormat::Landscape) == tooTall) {
                const qreal width = layout.width;
                layout.width = layout.height;
                layout.height = width;
            }
        },
        kundo2_i18n("Change Orientation"));
}

void KWPageTool::applyPageFormat(KoPageFormat::Format format)
{
    applyPageSetup(
        [format](KoPageLayout &layout, KoColumns &) {
            const auto orientation = layout.orientation == KoPageFormat::Landscape ? KoPageFormat::Landscape
                                                                                  : KoPageFormat::Portrait;
            layout.format = format;
            layout.width = MM_TO_POINT(KoPageFormat::width(format, orientation));
            layout.height = MM_TO_POINT(KoPageFormat::height(format, orientation));
        },
        kundo2_i18n("Change Paper Size"));
}

void KWPageTool::applyColumns(int count)
{
    applyPageSetup(
        [count](KoPageLayout &, KoColumns &columns) {
            columns.count = qBound(1, count, 8);
            // The columns share the text area evenly.
            columns.columnData.clear();
            if (columns.gapWidth <= 0.0) {
                columns.gapWidth = 14.0;
            }
        },
        kundo2_i18n("Change Columns"));
}

QList<QPointer<QWidget>> KWPageTool::createOptionWidgets()
{
    QList<QPointer<QWidget>> widgets;

    const auto addGroup = [&widgets](const QString &title, int order) {
        auto *group = new QWidget;
        group->setWindowTitle(title);
        group->setProperty("ribbonGroupOrder", order);
        auto *layout = new QHBoxLayout(group);
        layout->setContentsMargins(3, 1, 3, 1);
        layout->setSpacing(4);
        layout->setAlignment(Qt::AlignTop);
        widgets.append(group);
        return layout;
    };
    const auto addButton = [](QHBoxLayout *layout, const QString &text, const QIcon &icon, bool iconOnly = false) {
        auto *button = new QToolButton(layout->parentWidget());
        button->setText(text);
        button->setIcon(icon);
        const int iconSize = iconOnly ? 22 : 28;
        button->setProperty("ribbonPreferredIconSize", QSize(iconSize, iconSize));
        KoRibbonLayout::configureButton(button, iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
        if (iconOnly) {
            button->setFixedSize(40, 34);
        } else {
            button->setMinimumWidth(qMax(60, button->sizeHint().width()));
        }
        layout->addWidget(button, 0, Qt::AlignTop);
        return button;
    };
    auto *pageSetupLayout = addGroup(i18n("Page Setup"), 10);
    auto *sectionLayout = addGroup(i18n("Section"), 20);
    auto *paragraphButtons = addGroup(i18n("Paragraph"), 30);

    auto *gallery = new KWPageSetupGallery(pageSetupLayout->parentWidget());
    m_gallery = gallery;
    connect(gallery, &KWPageSetupGallery::marginsSelected, this, &KWPageTool::applyMargins);
    connect(gallery, &KWPageSetupGallery::orientationSelected, this, &KWPageTool::applyOrientation);
    connect(gallery, &KWPageSetupGallery::pageFormatSelected, this, &KWPageTool::applyPageFormat);
    connect(gallery, &KWPageSetupGallery::columnsSelected, this, &KWPageTool::applyColumns);
    connect(gallery, &KWPageSetupGallery::actionRequested, this, [this](const QString &name) {
        // The break actions belong to the active text tool, so they are looked
        // up when one of them is picked.
        if (KWView *view = m_canvas ? m_canvas->view() : nullptr) {
            if (QAction *action = view->action(name)) {
                action->trigger();
            }
        }
    });
    const auto showPageSetup = [this] {
        if (KWView *view = m_canvas ? m_canvas->view() : nullptr) {
            if (QAction *action = view->action(QStringLiteral("format_page"))) {
                action->trigger();
            }
        }
    };
    connect(gallery, &KWPageSetupGallery::pageSetupRequested, this, showPageSetup);
    connect(gallery, &KWPageSetupGallery::refreshRequested, this, &KWPageTool::refreshPageSetupGallery);
    connect(m_document, &KWDocument::pageSetupChanged, this, &KWPageTool::refreshPageSetupGallery);
    QTimer::singleShot(0, gallery, [this] { refreshPageSetupGallery(); });
    pageSetupLayout->addWidget(gallery, 0, Qt::AlignTop);
    sectionLayout->addWidget(gallery->columnsTile(), 0, Qt::AlignTop);

    auto *lineNumbers = addButton(sectionLayout, i18n("Line Numbers"), KoRibbonIcons::forThemeIcon(QStringLiteral("format-list-ordered")));
    lineNumbers->setCheckable(true);
    if (m_document->mainFrameSet()) {
        KoTextDocument textDocument(m_document->mainFrameSet()->document());
        if (KoOdfLineNumberingConfiguration *configuration = textDocument.lineNumberingConfiguration()) {
            lineNumbers->setChecked(configuration->enabled());
        }
    }
    connect(lineNumbers, &QToolButton::toggled, this, [this](bool enabled) {
        if (!m_document->mainFrameSet()) {
            return;
        }
        QTextDocument *text = m_document->mainFrameSet()->document();
        KoTextDocument textDocument(text);
        auto *configuration = textDocument.lineNumberingConfiguration();
        if (!configuration) {
            configuration = new KoOdfLineNumberingConfiguration;
            textDocument.setLineNumberingConfiguration(configuration);
        }
        configuration->setEnabled(enabled);
        text->markContentsDirty(0, text->characterCount());
        m_document->setModified(true);
        m_document->relayout();
        m_canvas->update();
    });
    auto *hyphenation = addButton(paragraphButtons, i18n("Hyphenation"),
                                  KoRibbonIcons::forThemeIcon(QStringLiteral("format-text-hyphenation")));
    hyphenation->setCheckable(true);
    hyphenation->setToolTip(i18n("Allow hyphenation for the current text selection"));
    if (KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
        hyphenation->setChecked(editor->charFormat().property(KoCharacterStyle::HasHyphenation).toBool());
    }
    connect(hyphenation, &QToolButton::toggled, this, [this](bool enabled) {
        if (KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
            QTextCharFormat format = editor->charFormat();
            format.setProperty(KoCharacterStyle::HasHyphenation, enabled);
            editor->setCharFormat(format);
        }
    });
    auto *pageSetup = addButton(pageSetupLayout, i18n("Settings"), KoRibbonIcons::forThemeIcon(QStringLiteral("configure")));
    pageSetup->setObjectName(QStringLiteral("layoutPageSetup"));
    pageSetup->setToolTip(i18n("Open Page Setup"));
    connect(pageSetup, &QToolButton::clicked, this, showPageSetup);

    auto *indentSpacing = addButton(paragraphButtons, i18n("Spacing"),
                                    KoRibbonIcons::forCommand(QStringLiteral("format_line_spacing")));
    auto *indentMenu = new QMenu(indentSpacing);
    indentSpacing->setMenu(indentMenu);
    indentSpacing->setPopupMode(QToolButton::InstantPopup);
    auto *paragraphGroup = new QWidget(indentMenu);
    auto *paragraphLayout = new QGridLayout(paragraphGroup);
    paragraphLayout->setContentsMargins(2, 1, 2, 1);
    paragraphLayout->setHorizontalSpacing(4);
    paragraphLayout->setVerticalSpacing(1);
    paragraphLayout->setSizeConstraint(QLayout::SetFixedSize);
    const KoUnit unit = m_document->unit();
    const QStringList paragraphLabels = {i18n("Before"), i18n("After"), i18n("Left"), i18n("Right")};
    auto *indentTitle = new QLabel(i18n("Indent"), paragraphGroup);
    auto *spacingTitle = new QLabel(i18n("Spacing"), paragraphGroup);
    paragraphLayout->addWidget(indentTitle, 0, 0, 1, 2);
    paragraphLayout->addWidget(spacingTitle, 0, 2, 1, 2);
    for (int i = 0; i < paragraphLabels.size(); ++i) {
        auto *field = new QDoubleSpinBox(paragraphGroup);
        field->setObjectName(QStringLiteral("layoutParagraph%1").arg(i));
        field->setDecimals(2);
        field->setRange(-1000.0, 1000.0);
        field->setSingleStep(0.05);
        field->setSuffix(QStringLiteral(" %1").arg(unit.symbol()));
        field->setToolTip(paragraphLabels.at(i));
        field->setAccessibleName(paragraphLabels.at(i));
        connect(indentMenu, &QMenu::aboutToShow, field, [this, field, unit, i] {
            KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas);
            field->setEnabled(editor != nullptr);
            if (editor) {
                const QTextBlockFormat format = editor->blockFormat();
                const qreal values[] = {format.topMargin(), format.bottomMargin(), format.leftMargin(), format.rightMargin()};
                const QSignalBlocker blocker(field);
                field->setValue(unit.toUserValue(values[i]));
            }
        });
        auto *label = new QLabel(paragraphLabels.at(i), paragraphGroup);
        label->setBuddy(field);
        // Indentation on the left, spacing on the right; each is a
        // labelled pair of vertically stacked fields, as in Word.
        const int column = i < 2 ? 2 : 0;
        const int row = 1 + i % 2;
        paragraphLayout->addWidget(label, row, column);
        paragraphLayout->addWidget(field, row, column + 1);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, unit, i](double value) {
            KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas);
            if (!editor) {
                return;
            }
            QTextBlockFormat format;
            const qreal points = unit.fromUserValue(value);
            switch (i) {
            case 0:
                format.setTopMargin(points);
                break;
            case 1:
                format.setBottomMargin(points);
                break;
            case 2:
                format.setLeftMargin(points);
                break;
            case 3:
                format.setRightMargin(points);
                break;
            default:
                return;
            }
            editor->mergeBlockFormat(format);
        });
    }
    auto *indentAction = new QWidgetAction(indentMenu);
    indentAction->setDefaultWidget(paragraphGroup);
    indentMenu->addAction(indentAction);

    auto *paragraphSettings = addButton(paragraphButtons, i18n("Paragraph…"),
                                         KoRibbonIcons::forThemeIcon(QStringLiteral("configure")));
    paragraphSettings->setObjectName(QStringLiteral("layoutParagraphSettings"));
    const auto updateParagraphSettings = [this, paragraphSettings] {
        paragraphSettings->setEnabled(KoTextEditor::getTextEditorFromCanvas(m_canvas) != nullptr);
    };
    connect(m_canvas->resourceManager(), &KoCanvasResourceManager::canvasResourceChanged, paragraphSettings,
            [updateParagraphSettings](int key, const QVariant &) {
                if (key == KoText::CurrentTextDocument) {
                    updateParagraphSettings();
                }
            });
    updateParagraphSettings();
    connect(paragraphSettings, &QToolButton::clicked, this, [this] {
        if (KoToolBase *tool = KoToolManager::instance()->toolById(m_canvas, QStringLiteral("TextToolFactory_ID"))) {
            if (QAction *action = tool->action(QStringLiteral("format_paragraph"))) {
                action->trigger();
            }
        }
    });
    KWView *view = m_canvas->view();
    KoToolBase *interactionTool = KoToolManager::instance()->toolById(m_canvas, QStringLiteral("InteractionTool"));
    if (view && interactionTool) {
        auto *position = addGroup(i18n("Position"), 40);
        auto *positionRows = new QVBoxLayout;
        positionRows->setSpacing(3);
        position->addLayout(positionRows);
        auto *topRow = new QHBoxLayout;
        auto *bottomRow = new QHBoxLayout;
        for (auto *row : {topRow, bottomRow}) {
            row->setSpacing(3);
            positionRows->addLayout(row);
        }
        auto *objects = addGroup(i18n("Objects"), 50);
        const auto addMenu = [addButton](QHBoxLayout *layout, const QString &text, const QIcon &icon,
                                         QList<QAction *> actions, bool iconOnly = false) {
            actions.removeAll(nullptr);
            auto *button = addButton(layout, text, icon, iconOnly);
            auto *menu = new QMenu(button);
            menu->addActions(actions);
            button->setMenu(menu);
            button->setPopupMode(QToolButton::InstantPopup);
            const auto update = [button, menu] {
                const auto actions = menu->actions();
                button->setEnabled(std::any_of(actions.cbegin(), actions.cend(), [](QAction *action) {
                    return action->isEnabled() && action->isVisible();
                }));
            };
            for (QAction *action : actions) {
                QObject::connect(action, &QAction::changed, button, update);
            }
            update();
            return button;
        };
        const auto objectAction = [interactionTool](const char *name) {
            return interactionTool->action(QString::fromLatin1(name));
        };
        auto *shapeLayout = addButton(topRow, i18n("Shape Layout"), KoRibbonIcons::forCommand(QStringLiteral("anchor")), true);
        shapeLayout->setToolTip(i18n("Position the selected shape and change how text wraps around it"));
        shapeLayout->setEnabled(false);
        // KWView registers its document actions after constructing the ribbon.
        QTimer::singleShot(0, shapeLayout, [this, view, shapeLayout] {
            if (QAction *action = view->action(QStringLiteral("format_frameset"))) {
                const auto update = [this, shapeLayout, action] {
                    shapeLayout->setEnabled(action->isEnabled() && action->isVisible()
                                            && m_canvas->shapeManager()->selection()->count() > 0);
                };
                connect(action, &QAction::changed, shapeLayout, update);
                connect(m_canvas->shapeManager(), &KoShapeManager::selectionChanged, shapeLayout, update);
                connect(shapeLayout, &QToolButton::clicked, action, &QAction::trigger);
                update();
            }
        });
        addMenu(topRow, i18n("Forward"),
                QIcon::fromTheme(QStringLiteral("object-order-front"), KoRibbonIcons::forCommand(QStringLiteral("content_copy"))),
                {objectAction("object_order_front"), objectAction("object_order_raise")}, true);
        addMenu(topRow, i18n("Backward"),
                QIcon::fromTheme(QStringLiteral("object-order-back"), KoRibbonIcons::forCommand(QStringLiteral("content_copy"))),
                {objectAction("object_order_back"), objectAction("object_order_lower")}, true);
        addMenu(bottomRow, i18n("Rotate"),
                QIcon::fromTheme(QStringLiteral("object-rotate-right"), KoRibbonIcons::forCommand(QStringLiteral("text_rotate_vertical"))),
                {view->action(QStringLiteral("rotate-left")), view->action(QStringLiteral("rotate-right"))}, true);
        addMenu(bottomRow, i18n("Align"),
                QIcon::fromTheme(QStringLiteral("align-horizontal-left"), KoRibbonIcons::forCommand(QStringLiteral("format_align_left"))),
                {objectAction("object_align_horizontal_left"), objectAction("object_align_horizontal_center"),
                 objectAction("object_align_horizontal_right"), objectAction("object_align_vertical_top"),
                 objectAction("object_align_vertical_center"), objectAction("object_align_vertical_bottom")}, true);
        QTimer::singleShot(0, position, [addMenu, bottomRow, view] {
            addMenu(bottomRow, i18n("Grid and Guides"),
                    KoRibbonIcons::forCommand(QStringLiteral("border_all")),
                    {view->action(QStringLiteral("view_grid")), view->action(QStringLiteral("view_snaptogrid")),
                     view->action(QStringLiteral("view_show_guides"))}, true);
        });
        addMenu(objects, i18n("Group"),
                QIcon::fromTheme(QStringLiteral("object-group"), KoRibbonIcons::forCommand(QStringLiteral("select_all"))),
                {objectAction("object_group"), objectAction("object_ungroup")});
    }

    return widgets;
}
