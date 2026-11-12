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
#include <QDoubleSpinBox>
#include <QIcon>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QMenu>
#include <QTimer>
#include <QPainter>

// calligra includes
#include <KoCanvasBase.h>
#include <KoCharacterStyle.h>
#include <KoColumns.h>
#include <KoOdfLineNumberingConfiguration.h>
#include <KoPageLayout.h>
#include <KoPointerEvent.h>
#include <KoRibbonIcons.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
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

    // Microsoft Office style "Page Setup" group: margins, orientation, size,
    // columns, breaks, line numbering and hyphenation.
    auto *pageSetupGroup = new QWidget;
    pageSetupGroup->setWindowTitle(i18n("Page Setup"));
    pageSetupGroup->setProperty("ribbonGroupOrder", 10);
    auto *pageSetupLayout = new QGridLayout(pageSetupGroup);
    pageSetupLayout->setContentsMargins(2, 1, 2, 1);
    pageSetupLayout->setHorizontalSpacing(4);
    pageSetupLayout->setVerticalSpacing(1);
    pageSetupLayout->setSizeConstraint(QLayout::SetFixedSize);

    // Margins, orientation, paper size and columns are a gallery of page
    // pictures; each one opens its presets and applies them right away.
    auto *gallery = new KWPageSetupGallery(pageSetupGroup);
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
    connect(gallery, &KWPageSetupGallery::pageSetupRequested, this, [this] {
        if (KWView *view = m_canvas ? m_canvas->view() : nullptr) {
            if (QAction *action = view->action(QStringLiteral("format_page"))) {
                action->trigger();
            }
        }
    });
    connect(gallery, &KWPageSetupGallery::refreshRequested, this, &KWPageTool::refreshPageSetupGallery);
    connect(m_document, &KWDocument::pageSetupChanged, this, &KWPageTool::refreshPageSetupGallery);
    QTimer::singleShot(0, gallery, [this] { refreshPageSetupGallery(); });
    pageSetupLayout->addWidget(gallery, 0, 0, 3, 1, Qt::AlignTop);

    // Breaks, line numbering and hyphenation are the small buttons next to the
    // gallery, like the ones under it in an office suite.
    int smallIndex = 0;
    const auto placeSmall = [pageSetupLayout, &smallIndex](QToolButton *button) {
        KoRibbonLayout::configureButton(button, Qt::ToolButtonTextBesideIcon);
        pageSetupLayout->addWidget(button, smallIndex % 3, 1 + smallIndex / 3, Qt::AlignLeft);
        ++smallIndex;
    };

    auto *lineNumbers = new QToolButton(pageSetupGroup);
    lineNumbers->setText(i18n("Line Numbering"));
    lineNumbers->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("format-list-ordered")));
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
    placeSmall(lineNumbers);

    auto *hyphenation = new QToolButton(pageSetupGroup);
    hyphenation->setText(i18n("Hyphenation"));
    hyphenation->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("format-text-hyphenation")));
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
    placeSmall(hyphenation);

    widgets.append(pageSetupGroup);

    auto *paragraphGroup = new QWidget;
    paragraphGroup->setWindowTitle(i18n("Paragraph"));
    paragraphGroup->setProperty("ribbonGroupOrder", 20);
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
        if (KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
            const QTextBlockFormat currentFormat = editor->blockFormat();
            qreal currentPoints = 0.0;
            switch (i) {
            case 0:
                currentPoints = currentFormat.topMargin();
                break;
            case 1:
                currentPoints = currentFormat.bottomMargin();
                break;
            case 2:
                currentPoints = currentFormat.leftMargin();
                break;
            case 3:
                currentPoints = currentFormat.rightMargin();
                break;
            }
            field->setValue(unit.toUserValue(currentPoints));
        }
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
    widgets.append(paragraphGroup);

    return widgets;
}
