/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Calligra developers
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "KPrDesignTab.h"

#include <QAction>
#include <KActionCollection>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <KLocalizedString>

#include <KoColorBackground.h>
#include <KoPACanvasBase.h>
#include <KoPADocument.h>
#include <KoPAMasterPage.h>
#include <KoPAPage.h>
#include <KoShapeBackground.h>
#include <KoRibbonIcons.h>
#include <KoRibbonLayout.h>
#include <KoToolBase.h>
#include <KoToolManager.h>

#include <tools/backgroundTool/KoPABackgroundTool.h>

#include "KPrPageLayoutWidget.h"
#include "KPrView.h"

namespace
{
constexpr int ThemeWidth = 148;
constexpr int ThemeHeight = 84;

// The slide backgrounds that the variant gallery offers.
QList<QColor> variantColors()
{
    return {QColor(QStringLiteral("#ffffff")), QColor(QStringLiteral("#f3f4f6")),
            QColor(QStringLiteral("#e8eef7")), QColor(QStringLiteral("#f7efe4")),
            QColor(QStringLiteral("#1f2430")), QColor(QStringLiteral("#12332c"))};
}

QString variantName(const QColor &color)
{
    if (color.lightnessF() < 0.4) {
        return i18n("Dark background");
    }
    if (qAbs(color.hueF() - QColor(QStringLiteral("#f7efe4")).hueF()) < 0.05f) {
        return i18n("Warm background");
    }
    return i18n("Light background");
}

QPixmap variantPreview(const QColor &color)
{
    QPixmap pixmap(56, 34);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QRectF card(0.5, 0.5, pixmap.width() - 1, pixmap.height() - 1);
    painter.setPen(QPen(QColor(0, 0, 0, 60)));
    painter.setBrush(color);
    painter.drawRoundedRect(card, 2, 2);
    // A title bar and three swatches, so a variant reads like a slide.
    const QRectF title(card.left() + 4, card.top() + 5, card.width() * 0.55, 4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color.lightnessF() < 0.4 ? QColor(255, 255, 255, 190) : QColor(40, 40, 40, 170));
    painter.drawRect(title);
    qreal x = card.left() + 4;
    for (const QColor &swatch : {QColor(QStringLiteral("#4472c4")), QColor(QStringLiteral("#ed7d31")),
                                 QColor(QStringLiteral("#70ad47"))}) {
        painter.setBrush(swatch);
        painter.drawRect(QRectF(x, card.bottom() - 11, 9, 7));
        x += 12;
    }
    return pixmap;
}
}

KPrDesignTab::KPrDesignTab(KPrView *view, QWidget *page)
    : QObject(page)
    , m_view(view)
    , m_page(page)
    , m_themesLayout(nullptr)
    , m_variantsLayout(nullptr)
{
    if (!m_view || !m_page) {
        return;
    }
    auto *layout = qobject_cast<QHBoxLayout *>(m_page->layout());
    if (!layout) {
        layout = new QHBoxLayout(m_page);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(0);
        layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }
    layout->addWidget(createThemesGroup());
    layout->addWidget(createVariantsGroup());
    layout->addWidget(createCustomizeGroup());
    layout->addWidget(createDesignerGroup());

    rebuildGalleries();

    if (KoPADocument *document = m_view->kopaDocument()) {
        connect(document, &KoPADocument::pageAdded, this, [this] { rebuildGalleries(); });
        connect(document, QOverload<KoPAPageBase *, int>::of(&KoPADocument::pageRemoved), this,
                [this](KoPAPageBase *, int) { rebuildGalleries(); });
    }
}

QToolButton *KPrDesignTab::addGalleryItem(QHBoxLayout *stripLayout, const QPixmap &pixmap, const QString &toolTip)
{
    auto *button = new QToolButton(stripLayout->parentWidget());
    button->setAutoRaise(true);
    button->setIcon(QIcon(pixmap));
    button->setIconSize(pixmap.size());
    button->setToolTip(toolTip);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    stripLayout->addWidget(button);
    return button;
}

QWidget *KPrDesignTab::createThemesGroup()
{
    auto *group = new QWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(6, 0, 6, 0);
    vertical->setSpacing(2);
    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    auto *scroll = new QScrollArea(group);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(ThemeHeight + 12);
    auto *strip = new QWidget;
    m_themesLayout = new QHBoxLayout(strip);
    m_themesLayout->setContentsMargins(2, 2, 2, 2);
    m_themesLayout->setSpacing(6);
    scroll->setWidget(strip);
    row->addWidget(scroll);
    auto *arrows = new QVBoxLayout;
    arrows->setSpacing(0);
    const auto addArrow = [&](Qt::ArrowType arrow, int step) {
        auto *button = new QToolButton(group);
        button->setAutoRaise(true);
        button->setArrowType(arrow);
        button->setFixedSize(20, (ThemeHeight + 12) / 2);
        connect(button, &QToolButton::clicked, this, [this, step] {
            if (m_themesLayout && m_themesLayout->parentWidget()) {
                // The strip scrolls inside the scroll area; move by one cell.
                if (auto *area = qobject_cast<QScrollArea *>(m_themesLayout->parentWidget()->parentWidget())) {
                    area->horizontalScrollBar()->setValue(area->horizontalScrollBar()->value() + step * (ThemeWidth + 6));
                }
            }
        });
        arrows->addWidget(button);
    };
    addArrow(Qt::LeftArrow, -1);
    addArrow(Qt::RightArrow, 1);
    row->addLayout(arrows);
    vertical->addLayout(row);
    vertical->addStretch();
    auto *caption = new QLabel(i18n("Themes"), group);
    caption->setProperty("ribbonGroupTitle", true);
    caption->setAlignment(Qt::AlignCenter);
    caption->setMinimumHeight(18);
    vertical->addWidget(caption);
    return group;
}

QWidget *KPrDesignTab::createVariantsGroup()
{
    auto *group = new QWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(6, 0, 6, 0);
    vertical->setSpacing(2);
    auto *strip = new QWidget(group);
    m_variantsLayout = new QHBoxLayout(strip);
    m_variantsLayout->setContentsMargins(2, 2, 2, 2);
    m_variantsLayout->setSpacing(4);
    vertical->addWidget(strip, 0, Qt::AlignTop);
    vertical->addStretch();
    auto *caption = new QLabel(i18n("Variants"), group);
    caption->setProperty("ribbonGroupTitle", true);
    caption->setAlignment(Qt::AlignCenter);
    caption->setMinimumHeight(18);
    vertical->addWidget(caption);
    return group;
}

QWidget *KPrDesignTab::createCustomizeGroup()
{
    auto *group = new QWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(6, 0, 6, 0);
    vertical->setSpacing(4);
    auto *row = new QHBoxLayout;
    row->setSpacing(8);

    auto *sizeButton = new QToolButton(group);
    sizeButton->setText(i18n("Slide Size"));
    sizeButton->setIcon(KoRibbonIcons::forCommand(QStringLiteral("fit_screen")));
    sizeButton->setToolTip(i18n("Change the size of the slides"));
    KoRibbonLayout::configureButton(sizeButton, Qt::ToolButtonTextUnderIcon);
    if (QAction *action = m_view->actionCollection()->action(QStringLiteral("format_pagelayout"))) {
        sizeButton->setDefaultAction(action);
        action->setIcon(KoRibbonIcons::forCommand(QStringLiteral("fit_screen")));
        action->setIconText(i18n("Slide Size"));
    }
    row->addWidget(sizeButton);

    auto *backgroundButton = new QToolButton(group);
    backgroundButton->setText(i18n("Format Background"));
    backgroundButton->setIcon(KoRibbonIcons::forCommand(QStringLiteral("format_color_fill")));
    backgroundButton->setToolTip(i18n("Fill, image and master background of the current slide"));
    KoRibbonLayout::configureButton(backgroundButton, Qt::ToolButtonTextUnderIcon);
    connect(backgroundButton, &QToolButton::clicked, this, [this] {
        // The background controls are the option widget of the background tool,
        // which the ribbon shows next to these commands.
        KoToolManager::instance()->switchToolRequested(QStringLiteral("KoPABackgroundTool"));
        if (auto *canvas = m_view->kopaCanvas()) {
            canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
        }
    });
    row->addWidget(backgroundButton);

    vertical->addLayout(row);
    vertical->addStretch();
    auto *caption = new QLabel(i18n("Customize"), group);
    caption->setProperty("ribbonGroupTitle", true);
    caption->setAlignment(Qt::AlignCenter);
    caption->setMinimumHeight(18);
    vertical->addWidget(caption);
    return group;
}

QWidget *KPrDesignTab::createDesignerGroup()
{
    auto *group = new QWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(6, 0, 6, 0);
    vertical->setSpacing(2);
    auto *button = new QToolButton(group);
    button->setText(i18n("Design Ideas"));
    button->setIcon(KoRibbonIcons::forCommand(QStringLiteral("dashboard_customize")));
    button->setToolTip(i18n("Apply one of the layouts of the master slide"));
    KoRibbonLayout::configureButton(button, Qt::ToolButtonTextUnderIcon);
    auto *menu = new QMenu(button);
    auto *action = new QWidgetAction(menu);
    auto *layouts = new KPrPageLayoutWidget(menu);
    layouts->setView(m_view);
    layouts->setWindowTitle(i18n("Design Ideas"));
    action->setDefaultWidget(layouts);
    menu->addAction(action);
    connect(menu, &QMenu::aboutToShow, layouts, &KPrPageLayoutWidget::slotActivePageChanged);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    vertical->addWidget(button, 0, Qt::AlignTop);
    vertical->addStretch();
    auto *caption = new QLabel(i18n("Designer"), group);
    caption->setProperty("ribbonGroupTitle", true);
    caption->setAlignment(Qt::AlignCenter);
    caption->setMinimumHeight(18);
    vertical->addWidget(caption);
    return group;
}

void KPrDesignTab::rebuildGalleries()
{
    if (!m_view) {
        return;
    }
    KoPADocument *document = m_view->kopaDocument();
    if (!document) {
        return;
    }

    if (m_themesLayout) {
        while (QLayoutItem *item = m_themesLayout->takeAt(0)) {
            if (QWidget *widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
        const QList<KoPAPageBase *> masters = document->pages(true);
        for (KoPAPageBase *page : masters) {
            auto *master = dynamic_cast<KoPAMasterPage *>(page);
            if (!master) {
                continue;
            }
            QPixmap preview = master->thumbnail(QSize(ThemeWidth, ThemeHeight));
            if (preview.isNull()) {
                continue;
            }
            preview = preview.scaled(ThemeWidth, ThemeHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPixmap card(ThemeWidth + 8, ThemeHeight + 8);
            card.fill(Qt::transparent);
            QPainter painter(&card);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.fillRect(QRectF(0, 0, card.width(), card.height()), QColor(255, 255, 255, 0));
            painter.setPen(QPen(QColor(0, 0, 0, 50)));
            painter.setBrush(QColor(QStringLiteral("#ffffff")));
            painter.drawRect(QRectF(0.5, 0.5, card.width() - 1, card.height() - 1));
            painter.drawPixmap((card.width() - preview.width()) / 2, (card.height() - preview.height()) / 2, preview);
            painter.end();
            const QString name = master->name().isEmpty() ? i18n("Master slide") : master->name();
            auto *button = addGalleryItem(m_themesLayout, card, i18n("Apply the theme of \"%1\" to all slides", name));
            connect(button, &QToolButton::clicked, this, [this, master] { applyTheme(master); });
        }
    }

    if (m_variantsLayout) {
        while (QLayoutItem *item = m_variantsLayout->takeAt(0)) {
            if (QWidget *widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
        const QList<QColor> colors = variantColors();
        for (int i = 0; i < colors.size(); ++i) {
            auto *button = addGalleryItem(m_variantsLayout, variantPreview(colors.at(i)),
                                          i18n("%1 for all slides", variantName(colors.at(i))));
            connect(button, &QToolButton::clicked, this, [this, i] { applyVariant(i); });
        }
    }
}

void KPrDesignTab::applyTheme(KoPAMasterPage *master)
{
    KoPADocument *document = m_view ? m_view->kopaDocument() : nullptr;
    if (!document || !master) {
        return;
    }
    const QList<KoPAPageBase *> pages = document->pages();
    bool changed = false;
    for (KoPAPageBase *page : pages) {
        auto *normalPage = dynamic_cast<KoPAPage *>(page);
        if (!normalPage) {
            continue;
        }
        normalPage->setMasterPage(master);
        normalPage->pageUpdated();
        changed = true;
    }
    if (changed) {
        document->setModified(true);
        m_view->kopaCanvas()->canvasWidget()->update();
    }
}

void KPrDesignTab::applyVariant(int index)
{
    KoPADocument *document = m_view ? m_view->kopaDocument() : nullptr;
    const QList<QColor> colors = variantColors();
    if (!document || index < 0 || index >= colors.size()) {
        return;
    }
    QSharedPointer<KoShapeBackground> background(new KoColorBackground(colors.at(index)));
    for (KoPAPageBase *page : document->pages()) {
        page->setBackground(background);
        page->pageUpdated();
    }
    document->setModified(true);
    m_view->kopaCanvas()->canvasWidget()->update();
}
