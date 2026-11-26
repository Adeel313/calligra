/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Calligra developers
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "KPrDesignTab.h"

#include <QAction>
#include <KActionCollection>
#include <QHBoxLayout>
#include <QFrame>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStandardPaths>
#include <QStyle>
#include <QToolButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KLocalizedString>
#include <kundo2command.h>
#include <kundo2stack.h>

#include <KoColorBackground.h>
#include <KoShapePaintingContext.h>
#include <KoViewConverter.h>
#include <KoShapeContainer.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
#include <KoTextShapeData.h>
#include <KoDocumentResourceManager.h>
#include <KoOdfLoadingContext.h>
#include <KoOdfReadStore.h>
#include <KoOdfStylesReader.h>
#include <KoPatternBackground.h>
#include <KoXmlNS.h>
#include <KoXmlReader.h>
#include <KoPALoadingContext.h>
#include <KoStore.h>
#include <KoPACanvasBase.h>
#include <KoPADocument.h>
#include <KoPAMasterPage.h>
#include <KoPAPage.h>
#include <KoPAViewMode.h>
#include <KoShapeBackground.h>
#include <KoShapeBackgroundCommand.h>
#include <KoRibbonIcons.h>
#include <KoRibbonLayout.h>

#include "KPrPageLayoutWidget.h"
#include "KPrThemeArtwork.h"
#include "KPrView.h"

#include <algorithm>
#include <functional>
#include <memory>

namespace
{
constexpr int ThemeWidth = 118;
constexpr int ThemeHeight = 64;

QString themeTileStyle()
{
    return QStringLiteral(
        "QToolButton { border: 1px solid palette(mid); border-radius: 4px; padding: 2px; }"
        "QToolButton:hover, QToolButton:focus { border-color: palette(highlight); }"
        "QToolButton:checked { border: 2px solid palette(highlight); background: palette(alternate-base); }");
}

constexpr int OriginalThemeForeground = QTextFormat::UserProperty + 20003;
const QColor adaptedForeground(31, 41, 55);

class ThemeTextCommand : public KUndo2Command
{
public:
    struct Range {
        int start;
        int length;
        QTextCharFormat format;
    };

    ThemeTextCommand(QTextDocument *document, QList<Range> ranges, KUndo2Command *parent)
        : KUndo2Command(parent)
        , m_document(document)
        , m_ranges(std::move(ranges))
    {
    }

    void redo() override
    {
        if (!m_document || m_ranges.isEmpty()) {
            KUndo2Command::redo();
            return;
        }
        // KoTextEditor records the edit block as a child of this command.
        QTextCursor cursor(m_document);
        cursor.beginEditBlock();
        for (const auto &range : m_ranges) {
            cursor.setPosition(range.start);
            cursor.setPosition(range.start + range.length, QTextCursor::KeepAnchor);
            cursor.setCharFormat(range.format);
        }
        cursor.endEditBlock();
        m_ranges.clear();
    }

private:
    QPointer<QTextDocument> m_document;
    QList<Range> m_ranges;
};

void adjustThemeText(KoShape *shape, bool light, bool restore, KUndo2Command *parent, QSet<QTextDocument *> &visited)
{
    auto *data = dynamic_cast<KoTextShapeData *>(shape->userData());
    if (!data || !data->document() || visited.contains(data->document()))
        return;

    auto *document = data->document();
    visited.insert(document);
    auto *editor = KoTextDocument(document).textEditor();
    if (!editor || !document->isUndoRedoEnabled())
        return;

    QList<ThemeTextCommand::Range> ranges;
    for (auto block = document->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            auto format = fragment.charFormat();
            const auto brush = format.foreground();
            if (format.hasProperty(OriginalThemeForeground)) {
                if (!restore && light)
                    continue;
                if (brush.style() == Qt::SolidPattern && brush.color() == adaptedForeground)
                    format.setForeground(format.property(OriginalThemeForeground).value<QBrush>());
                format.clearProperty(OriginalThemeForeground);
            } else {
                const auto color = brush.color();
                if (restore || !light || brush.style() != Qt::SolidPattern || color.alpha() == 0
                    || qMin(color.red(), qMin(color.green(), color.blue())) < 225)
                    continue;
                format.setProperty(OriginalThemeForeground, QVariant::fromValue(brush));
                format.setForeground(adaptedForeground);
            }
            ranges.append({fragment.position(), fragment.length(), format});
        }
    }
    if (!ranges.isEmpty())
        editor->addCommand(new ThemeTextCommand(document, std::move(ranges), parent));
}

// Build a small map of the fills in paint order. A transparent text
// box may sit on a separate shape instead of directly on the slide fill.
class ThemeBackdrop
{
public:
    explicit ThemeBackdrop(KoPAPageBase *page)
        : m_page(page)
    {
        m_image.fill(Qt::white);
        m_painter.begin(&m_image);
        const auto &layout = page->pageLayout();
        m_painter.scale(m_image.width() / qMax(qreal(1), layout.width), m_image.height() / qMax(qreal(1), layout.height));
        m_painter.setPen(Qt::NoPen);
        page->paintBackground(m_painter, m_converter, m_context);

        auto *slide = dynamic_cast<KoPAPage *>(page);
        if (slide && slide->displayMasterShapes() && slide->masterPage()) {
            QSet<QTextDocument *> unused;
            paintShapes(slide->masterPage()->shapes(), false, nullptr, unused);
        }
    }

    void paintShapes(QList<KoShape *> shapes, bool restore, KUndo2Command *command, QSet<QTextDocument *> &visited)
    {
        std::stable_sort(shapes.begin(), shapes.end(), KoShape::compareShapeZIndex);
        for (auto *shape : shapes) {
            if (!shape->isVisible() || !m_page->displayShape(shape))
                continue;
            const auto transform = shape->absoluteTransformation(&m_converter);
            if (const auto background = shape->background()) {
                m_painter.save();
                m_painter.setWorldTransform(transform, true);
                m_painter.setOpacity(1.0 - shape->transparency(true));
                background->paint(m_painter, m_converter, m_context, shape->outline());
                m_painter.restore();
            }
            if (command && shape->userData()) {
                const QPointF center = transform.map(shape->outline().boundingRect().center());
                const QPoint pixel = m_painter.worldTransform().map(center).toPoint();
                const bool light = m_image.rect().contains(pixel) && m_image.pixelColor(pixel).lightnessF() >= 0.72;
                adjustThemeText(shape, light, restore, command, visited);
            }
            if (auto *container = dynamic_cast<KoShapeContainer *>(shape))
                paintShapes(container->shapes(), restore, command, visited);
        }
    }

private:
    KoPAPageBase *m_page;
    QImage m_image{256, 256, QImage::Format_ARGB32_Premultiplied};
    KoViewConverter m_converter;
    KoShapePaintingContext m_context;
    QPainter m_painter;
};

KPrSlideDesign slideDesign(KoPAPage *page)
{
    return {page->masterPage(), page->background(), page->displayMasterBackground(),
            page->isMasterBackgroundVisible(), page->displayMasterShapes()};
}

class ApplyThemeCommand : public KUndo2Command
{
public:
    ApplyThemeCommand(KoPADocument *document, KoPAMasterPage *master, std::unique_ptr<KoPAMasterPage> importedMaster = {})
        : KUndo2Command(kundo2_i18n("Apply theme"))
        , m_document(document)
        , m_master(master)
        , m_detachedMaster(std::move(importedMaster))
        , m_imported(bool(m_detachedMaster))
    {
        for (auto *base : document->pages()) {
            if (auto *page = dynamic_cast<KoPAPage *>(base)) {
                const auto previous = slideDesign(page);
                const KPrSlideDesign next{master, previous.background, true, true, true};
                if (previous != next)
                    m_pages.append({page, previous, next});
            }
        }
    }

    ApplyThemeCommand(KoPADocument *document, const QHash<KoPAPage *, KPrSlideDesign> &originals, const KPrSlideDesign &fallback)
        : KUndo2Command(kundo2_i18n("Restore original theme"))
        , m_document(document)
        , m_restore(true)
    {
        for (auto *base : document->pages()) {
            if (auto *page = dynamic_cast<KoPAPage *>(base)) {
                const auto next = originals.value(page, fallback);
                const auto previous = slideDesign(page);
                if (previous != next && document->pages(true).contains(next.master))
                    m_pages.append({page, previous, next});
            }
        }
    }

    bool isEmpty() const { return m_pages.isEmpty(); }

    void redo() override
    {
        if (m_detachedMaster)
            m_document->insertPage(m_detachedMaster.release(), m_document->pages(true).last());
        for (const auto &state : m_pages)
            applyDesign(state.page, state.next);

        if (m_firstRedo) {
            m_firstRedo = false;
            QSet<QTextDocument *> visited;
            for (const auto &state : m_pages) {
                ThemeBackdrop backdrop(state.page);
                backdrop.paintShapes(state.page->shapes(), m_restore, this, visited);
            }
        } else {
            KUndo2Command::redo();
        }
    }

    void undo() override
    {
        KUndo2Command::undo();
        for (const auto &state : m_pages)
            applyDesign(state.page, state.previous);
        if (m_imported) {
            m_document->takePage(m_master);
            m_detachedMaster.reset(m_master);
        }
    }

private:
    void applyDesign(KoPAPage *page, const KPrSlideDesign &design)
    {
        page->setMasterPage(design.master);
        if (page->background() != design.background)
            page->setBackground(design.background);
        page->setDisplayMasterBackground(design.inheritsBackground);
        page->setMasterBackgroundVisible(design.backgroundVisible);
        page->setDisplayMasterShapes(design.masterShapes);
        m_document->emitUpdate(page);
    }

    struct PageState {
        KoPAPage *page;
        KPrSlideDesign previous;
        KPrSlideDesign next;
    };
    KoPADocument *m_document;
    KoPAMasterPage *m_master = nullptr;
    QList<PageState> m_pages;
    std::unique_ptr<KoPAMasterPage> m_detachedMaster;
    bool m_imported = false;
    bool m_restore = false;
    bool m_firstRedo = true;
};

class ApplySlideColorCommand : public KoShapeBackgroundCommand
{
public:
    ApplySlideColorCommand(KoPAPageBase *page, const QSharedPointer<KoShapeBackground> &background, KUndo2Command *parent)
        : KoShapeBackgroundCommand(page, background, parent)
        , m_page(page)
        , m_usedMasterBackground(page->displayMasterBackground())
    {
    }

    void redo() override
    {
        m_page->setDisplayMasterBackground(false);
        KoShapeBackgroundCommand::redo();
        if (m_firstRedo) {
            m_firstRedo = false;
            QSet<QTextDocument *> visited;
            ThemeBackdrop backdrop(m_page);
            backdrop.paintShapes(m_page->shapes(), false, this, visited);
        }
        m_page->pageUpdated();
    }

    void undo() override
    {
        m_page->setDisplayMasterBackground(m_usedMasterBackground);
        KoShapeBackgroundCommand::undo();
        m_page->pageUpdated();
    }

private:
    KoPAPageBase *m_page;
    bool m_usedMasterBackground;
    bool m_firstRedo = true;
};

QPixmap colorPreview(const QColor &color)
{
    QPixmap pixmap(56, 34);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QColor(0, 0, 0, 60));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(0.5, 0.5, pixmap.width() - 1, pixmap.height() - 1), 3, 3);
    return pixmap;
}
}

KPrDesignTab::KPrDesignTab(KPrView *view, QWidget *page)
    : QObject(page)
    , m_view(view)
    , m_page(page)
    , m_themesButton(nullptr)
    , m_themesMenu(nullptr)
    , m_themeStrip(nullptr)
{
    if (!m_view || !m_page) {
        return;
    }
    // Save As can replace the view while the ribbon and queued updates still exist.
    connect(view, &QObject::destroyed, this, &QObject::deleteLater);
    auto *layout = qobject_cast<QHBoxLayout *>(m_page->layout());
    if (!layout) {
        layout = new QHBoxLayout(m_page);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(0);
    }
    layout->setAlignment(Qt::Alignment{});
    layout->addWidget(createSlideControls());
    layout->addWidget(createThemesGroup(), 1);
    layout->addWidget(createCustomizeGroup());

    if (KoPADocument *document = m_view->kopaDocument()) {
        captureOriginal(document);
        connect(document, &KoPADocument::pageAdded, this, &KPrDesignTab::rebuildThemes, Qt::QueuedConnection);
        connect(document, QOverload<KoPAPageBase *, int>::of(&KoPADocument::pageRemoved), this,
                &KPrDesignTab::rebuildThemes, Qt::QueuedConnection);
        connect(document->undoStack(), &KUndo2Stack::indexChanged, this, &KPrDesignTab::rebuildThemes, Qt::QueuedConnection);
    }
    connect(m_view->proxyObject, &KoPAViewProxyObject::activePageChanged, this, &KPrDesignTab::rebuildThemes);
    rebuildThemes();
}

QWidget *KPrDesignTab::createSlideControls()
{
    auto *group = new KoRibbonGroupWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    auto *layout = new QHBoxLayout(group);
    layout->setContentsMargins(8, 4, 8, 4);
    auto *clipboard = new QGridLayout;
    clipboard->setSpacing(0);
    const auto actionButton = [this, group](const QString &name, Qt::ToolButtonStyle style) {
        auto *button = new QToolButton(group);
        if (auto *action = m_view->actionCollection()->action(name)) {
            button->setDefaultAction(action);
            button->setIcon(KoRibbonIcons::forCommand(name));
        }
        KoRibbonLayout::configureButton(button, style);
        return button;
    };
    clipboard->addWidget(actionButton(QStringLiteral("edit_copy"), Qt::ToolButtonIconOnly), 0, 0);
    clipboard->addWidget(actionButton(QStringLiteral("edit_cut"), Qt::ToolButtonIconOnly), 0, 1);
    clipboard->addWidget(actionButton(QStringLiteral("edit_paste"), Qt::ToolButtonIconOnly), 1, 0);
    layout->addLayout(clipboard);

    auto *addSlide = actionButton(QStringLiteral("page_insertpage"), Qt::ToolButtonTextUnderIcon);
    addSlide->setText(i18n("Add Slide"));
    auto *slideMenu = new QMenu(addSlide);
    for (const QString &name : {QStringLiteral("page_insertpage"), QStringLiteral("page_copypage")}) {
        if (auto *command = m_view->actionCollection()->action(name))
            slideMenu->addAction(command);
    }
    addSlide->setMenu(slideMenu);
    addSlide->setPopupMode(QToolButton::MenuButtonPopup);
    layout->addWidget(addSlide);

    auto *slideTools = new QVBoxLayout;
    slideTools->setSpacing(0);
    auto *layoutButton = new QToolButton(group);
    layoutButton->setText(i18n("Slide Layout"));
    layoutButton->setToolTip(layoutButton->text());
    layoutButton->setIcon(QIcon::fromTheme(QStringLiteral("view-grid")));
    KoRibbonLayout::configureButton(layoutButton, Qt::ToolButtonIconOnly);
    auto *menu = new QMenu(layoutButton);
    auto *action = new QWidgetAction(menu);
    auto *layouts = new KPrPageLayoutWidget(menu);
    layouts->setView(m_view);
    action->setDefaultWidget(layouts);
    menu->addAction(action);
    connect(menu, &QMenu::aboutToShow, layouts, &KPrPageLayoutWidget::slotActivePageChanged);
    layoutButton->setMenu(menu);
    layoutButton->setPopupMode(QToolButton::InstantPopup);
    slideTools->addWidget(layoutButton);
    slideTools->addWidget(actionButton(QStringLiteral("slideshow_start_from_current"), Qt::ToolButtonIconOnly));
    layout->addLayout(slideTools);
    return group;
}

QWidget *KPrDesignTab::createThemesGroup()
{
    auto *group = new KoRibbonGroupWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    auto *row = new QHBoxLayout(group);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);

    auto *scroll = new QScrollArea(group);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->horizontalScrollBar()->setFixedHeight(7);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumSize(ThemeWidth + 8, ThemeHeight + 15);
    scroll->setMaximumHeight(ThemeHeight + 15);
    auto *strip = new QWidget(scroll);
    strip->setObjectName(QStringLiteral("stageThemeStrip"));
    strip->setStyleSheet(themeTileStyle());
    m_themeStrip = new QHBoxLayout(strip);
    m_themeStrip->setContentsMargins(0, 0, 0, 0);
    m_themeStrip->setSpacing(4);
    scroll->setWidget(strip);
    row->addWidget(scroll, 1);

    m_themesButton = new QToolButton(group);
    m_themesButton->setObjectName(QStringLiteral("stageThemesButton"));
    m_themesButton->setText(i18n("Professional Themes"));
    m_themesButton->setToolTip(i18n("Browse professional themes"));
    m_themesButton->setIcon(group->style()->standardIcon(QStyle::SP_ArrowDown));
    m_themesButton->setAutoRaise(true);
    m_themesButton->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));
    m_themesButton->setPopupMode(QToolButton::InstantPopup);
    m_themesMenu = new QMenu(m_themesButton);
    m_themesMenu->setObjectName(QStringLiteral("stageThemesMenu"));
    m_themesButton->setMenu(m_themesMenu);
    row->addWidget(m_themesButton);
    return group;
}

QWidget *KPrDesignTab::createCustomizeGroup()
{
    auto *group = new KoRibbonGroupWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    auto *row = new QHBoxLayout(group);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(4);

    auto *colorsButton = new QToolButton(group);
    colorsButton->setText(i18n("Colors"));
    colorsButton->setIcon(KoRibbonIcons::forCommand(QStringLiteral("format_color_fill")));
    colorsButton->setToolTip(i18n("Choose a slide background color"));
    KoRibbonLayout::configureButton(colorsButton, Qt::ToolButtonTextUnderIcon);
    auto *colors = new QMenu(colorsButton);
    const std::pair<const char *, QString> palette[] = {
        {"#ffffff", i18n("White")},
        {"#f3f4f6", i18n("Light Gray")},
        {"#e8eef7", i18n("Pale Blue")},
        {"#f7efe4", i18n("Warm Sand")},
        {"#1f2430", i18n("Charcoal")},
        {"#12332c", i18n("Forest Green")},
    };
    for (const auto &[value, name] : palette) {
        const QColor color(QString::fromLatin1(value));
        colors->addAction(QIcon(colorPreview(color)), name, this, [this, color] { applyColor(color); });
    }
    colors->addSeparator();
    auto *background = colors->addMenu(i18n("Format Background"));
    background->setObjectName(QStringLiteral("stageDesignBackgroundMenu"));
    colorsButton->setMenu(colors);
    colorsButton->setPopupMode(QToolButton::InstantPopup);
    row->addWidget(colorsButton);

    auto *sizeButton = new QToolButton(group);
    sizeButton->setText(i18n("Slide Size"));
    sizeButton->setIcon(KoRibbonIcons::forCommand(QStringLiteral("fit_screen")));
    sizeButton->setToolTip(i18n("Change the size of the slides"));
    KoRibbonLayout::configureButton(sizeButton, Qt::ToolButtonTextUnderIcon);
    auto *sizes = new QMenu(sizeButton);
    const auto addSize = [this, sizes](const QString &name, qreal aspectRatio) {
        sizes->addAction(name, this, [this, aspectRatio] {
            auto *document = m_view ? m_view->kopaDocument() : nullptr;
            if (!document || !document->isReadWrite() || !m_view->activePage())
                return;
            auto layout = m_view->activePage()->pageLayout();
            layout.width = layout.height * aspectRatio;
            layout.format = KoPageFormat::CustomSize;
            layout.orientation = KoPageFormat::Landscape;
            auto *command = new KUndo2Command(kundo2_i18n("Change slide size"));
            m_view->viewMode()->changePageLayout(layout, true, command);
            m_view->kopaCanvas()->addCommand(command);
        });
    };
    addSize(i18n("Standard (4:3)"), 4.0 / 3.0);
    addSize(i18n("Widescreen (16:9)"), 16.0 / 9.0);
    if (auto *customSize = m_view->actionCollection()->action(QStringLiteral("format_pagelayout"))) {
        sizes->addSeparator();
        sizes->addAction(i18n("Custom Slide Size…"), customSize, &QAction::trigger);
    }
    sizeButton->setMenu(sizes);
    sizeButton->setPopupMode(QToolButton::InstantPopup);
    row->addWidget(sizeButton);
    return group;
}

void KPrDesignTab::captureOriginal(KoPADocument *document)
{
    m_originalDesigns.clear();
    m_defaultOriginal = {};
    for (auto *base : document->pages()) {
        if (auto *slide = dynamic_cast<KoPAPage *>(base)) {
            const auto design = slideDesign(slide);
            if (!m_defaultOriginal.master)
                m_defaultOriginal = design;
            m_originalDesigns.insert(slide, design);
        }
    }
}

void KPrDesignTab::rebuildThemes()
{
    KoPADocument *document = m_view ? m_view->kopaDocument() : nullptr;
    if (!document || !m_themesMenu)
        return;
    if (!document->pages(true).contains(m_defaultOriginal.master))
        captureOriginal(document);
    m_themesMenu->clear();
    while (auto *item = m_themeStrip->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    auto *currentPage = dynamic_cast<KoPAPage *>(m_view->activePage());
    KoPAMasterPage *currentMaster = currentPage ? currentPage->masterPage() : nullptr;
    const auto createTile = [this, document](QWidget *parent, const QString &name, const QPixmap &preview, bool active, const std::function<void()> &apply) {
        auto *tile = new QToolButton(parent);
        tile->setAccessibleName(name);
        tile->setToolTip(name);
        tile->setIcon(QIcon(preview));
        tile->setIconSize(QSize(ThemeWidth, ThemeHeight));
        tile->setFixedSize(ThemeWidth + 6, ThemeHeight + 6);
        tile->setCheckable(true);
        tile->setChecked(active);
        tile->setEnabled(document->isReadWrite());
        connect(tile, &QToolButton::clicked, this, [tile, active, apply] {
            tile->setChecked(active);
            apply();
        });
        return tile;
    };
    const auto addTheme = [this, &createTile](const QString &name, const QPixmap &preview, bool active, const std::function<void()> &apply) {
        m_themeStrip->addWidget(createTile(m_themeStrip->parentWidget(), name, preview, active, apply));
    };
    const auto findMaster = [document, currentMaster](const QString &name, bool needsArtwork) {
        KoPAMasterPage *match = nullptr;
        for (auto *page : document->pages(true)) {
            auto *master = static_cast<KoPAMasterPage *>(page);
            if (master->name() != name || (needsArtwork && !qSharedPointerDynamicCast<KoPatternBackground>(master->background())))
                continue;
            if (!match || master == currentMaster)
                match = master;
        }
        return match;
    };

    const auto original = m_originalDesigns.value(currentPage, m_defaultOriginal);
    const bool isOriginal = currentPage && slideDesign(currentPage) == original;
    if (document->pages(true).contains(original.master)) {
        QPixmap preview(ThemeWidth * 2, ThemeHeight * 2);
        preview.fill(m_page->palette().color(QPalette::Base));
        QPainter painter(&preview);
        QFont font = painter.font();
        font.setPixelSize(29);
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(m_page->palette().color(QPalette::Text));
        painter.drawText(preview.rect(), Qt::AlignCenter, i18n("Original"));
        painter.end();
        addTheme(i18n("Original"), preview, isOriginal, [this] { applyOriginal(); });
    }

    auto *galleryAction = new QWidgetAction(m_themesMenu);
    auto *gallery = new QWidget(m_themesMenu);
    gallery->setObjectName(QStringLiteral("stageProfessionalThemeGrid"));
    gallery->setStyleSheet(themeTileStyle());
    auto *grid = new QGridLayout(gallery);
    grid->setContentsMargins(6, 6, 6, 6);
    grid->setSpacing(4);
    galleryAction->setDefaultWidget(gallery);
    m_themesMenu->addAction(galleryAction);

    constexpr int columns = 6;
    int position = 0;
    QSet<QString> libraryNames;
    for (const auto &theme : KPrThemeArtwork::themes()) {
        libraryNames.insert(theme.name);
        auto *master = findMaster(theme.name, true);
        const QString cacheKey = QStringLiteral("stage-theme-preview/") + theme.name;
        QPixmap preview;
        if (!QPixmapCache::find(cacheKey, &preview)) {
            preview = QPixmap::fromImage(KPrThemeArtwork::artwork(theme, QSize(ThemeWidth * 3, ThemeHeight * 3), true));
            QPixmapCache::insert(cacheKey, preview);
        }
        const auto apply = [this, theme, master] {
            if (master)
                applyTheme(master);
            else
                applyArtworkTheme(theme);
        };
        const bool active = !isOriginal && master && master == currentMaster;
        addTheme(theme.name, preview, active, apply);
        auto *tile = createTile(gallery, theme.name, preview, active, [this, apply] {
            m_themesMenu->hide();
            apply();
        });
        grid->addWidget(tile, position / columns, position % columns);
        ++position;
    }

    const std::pair<const char *, QString> templates[] = {
        {"cohere-blue", i18n("Cohere Blue")},
        {"skyline_night_wide", i18n("Night Skyline")},
        {"cohere-gold", i18n("Cohere Gold")},
        {"skyline_morning_wide", i18n("Morning Skyline")},
        {"cohere-magenta", i18n("Cohere Magenta")},
        {"cohere-red", i18n("Cohere Red")},
    };
    const QString directory = QStringLiteral("calligrastage/templates/odf/");
    for (const auto &[id, name] : templates) {
        const QString stem = QString::fromLatin1(id);
        const QString file = QStandardPaths::locate(QStandardPaths::GenericDataLocation, directory + QStringLiteral(".source/") + stem + QStringLiteral(".otp"));
        if (file.isEmpty())
            continue;
        const QPixmap preview(QStandardPaths::locate(QStandardPaths::GenericDataLocation, directory + QStringLiteral(".thumbnail/") + stem + QStringLiteral(".png")));
        if (preview.isNull())
            continue;
        libraryNames.insert(name);
        // Older Stage builds imported Cohere with a plain white master.
        // Offer the packaged theme again so that those decks can be repaired.
        const bool needsArtwork = stem.startsWith(QLatin1String("cohere-"));
        auto *master = findMaster(name, needsArtwork);
        addTheme(name, preview, !isOriginal && master && master == currentMaster, [this, file, name, master] {
            if (master)
                applyTheme(master);
            else
                applyTemplateTheme(file, name);
        });
    }
    int number = 0;
    for (auto *page : document->pages(true)) {
        auto *master = static_cast<KoPAMasterPage *>(page);
        ++number;
        if (libraryNames.contains(master->name()))
            continue;
        QPixmap preview = master->thumbnail(QSize(ThemeWidth, ThemeHeight));
        if (preview.isNull())
            preview = colorPreview(Qt::white);
        const QString name = master->name().isEmpty() ? i18n("Theme %1", number) : master->name();
        addTheme(name, preview, !isOriginal && master == currentMaster, [this, master] { applyTheme(master); });
    }
    m_themeStrip->addStretch();
    m_themesButton->setEnabled(document->isReadWrite());
}

void KPrDesignTab::applyOriginal()
{
    auto *document = m_view ? m_view->kopaDocument() : nullptr;
    if (!document || !document->isReadWrite())
        return;
    auto command = std::make_unique<ApplyThemeCommand>(document, m_originalDesigns, m_defaultOriginal);
    if (!command->isEmpty())
        m_view->kopaCanvas()->addCommand(command.release());
}

void KPrDesignTab::applyTheme(KoPAMasterPage *master)
{
    KoPADocument *document = m_view ? m_view->kopaDocument() : nullptr;
    if (!document || !document->isReadWrite() || !document->pages(true).contains(master))
        return;
    auto *command = new ApplyThemeCommand(document, master);
    if (command->isEmpty()) {
        delete command;
        return;
    }
    m_view->kopaCanvas()->addCommand(command);
}

void KPrDesignTab::applyTemplateTheme(const QString &file, const QString &name)
{
    KoPADocument *document = m_view ? m_view->kopaDocument() : nullptr;
    if (!document || !document->isReadWrite() || document->pages().isEmpty())
        return;

    std::unique_ptr<KoStore> store(KoStore::createStore(file, KoStore::Read));
    if (!store || store->bad())
        return;
    KoOdfReadStore odf(store.get());
    QString error;
    if (!odf.loadAndParse(error))
        return;

    const QString standard = QStringLiteral("Default1");
    auto *masterStyle = odf.styles().masterPages().value(standard);
    if (!masterStyle)
        return;
    KoOdfLoadingContext loading(odf.styles(), store.get(), document->defaultStylesResourcePath());
    KoPALoadingContext context(loading, document->resourceManager());
    const auto masters = document->loadOdfMasterPages({{standard, masterStyle}}, context);
    if (masters.isEmpty())
        return;
    auto master = std::unique_ptr<KoPAMasterPage>(static_cast<KoPAMasterPage *>(masters.first()));
    const auto body = KoXml::namedItemNS(odf.contentDoc().documentElement(), KoXmlNS::office, "body");
    const auto presentation = KoXml::namedItemNS(body, KoXmlNS::office, "presentation");
    const auto sample = KoXml::namedItemNS(presentation, KoXmlNS::draw, "page").toElement();
    if (!sample.isNull() && sample.attributeNS(KoXmlNS::draw, "master-page-name") == standard) {
        std::unique_ptr<KoPAPage> page(document->newPage(master.get()));
        if (page->loadOdf(sample, context) && !page->displayMasterBackground() && page->background()) {
            const auto background = page->background();
            if (auto pattern = qSharedPointerDynamicCast<KoPatternBackground>(background))
                pattern->setRepeat(KoPatternBackground::Stretched);
            master->setBackground(background);
        }
    }
    master->setName(name);
    applyNewMaster(std::move(master));
}

void KPrDesignTab::applyArtworkTheme(const KPrThemeArtwork::Theme &theme)
{
    auto *document = m_view ? m_view->kopaDocument() : nullptr;
    if (!document || !document->isReadWrite() || document->pages().isEmpty())
        return;

    auto master = std::unique_ptr<KoPAMasterPage>(document->newMasterPage());
    master->setName(theme.name);
    auto background = QSharedPointer<KoPatternBackground>::create(document->resourceManager()->imageCollection());
    background->setPattern(KPrThemeArtwork::artwork(theme, QSize(1920, 1080)));
    background->setRepeat(KoPatternBackground::Stretched);
    master->setBackground(background);
    applyNewMaster(std::move(master));
}

void KPrDesignTab::applyNewMaster(std::unique_ptr<KoPAMasterPage> master)
{
    auto *document = m_view ? m_view->kopaDocument() : nullptr;
    if (!document || !m_view->activePage())
        return;
    master->setPageLayout(m_view->activePage()->pageLayout());
    auto *page = master.get();
    m_view->kopaCanvas()->addCommand(new ApplyThemeCommand(document, page, std::move(master)));
}

void KPrDesignTab::applyColor(const QColor &color)
{
    KoPADocument *document = m_view ? m_view->kopaDocument() : nullptr;
    if (!document || !document->isReadWrite() || document->pages().isEmpty())
        return;

    auto *command = new KUndo2Command(kundo2_i18n("Change slide background colors"));
    const auto background = QSharedPointer<KoColorBackground>::create(color);
    for (KoPAPageBase *page : document->pages())
        new ApplySlideColorCommand(page, background, command);
    m_view->kopaCanvas()->addCommand(command);
}
