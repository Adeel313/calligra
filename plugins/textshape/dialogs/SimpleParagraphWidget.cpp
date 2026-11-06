/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2007, 2008, 2010 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2009-2010 C. Boemann <cbo@boemann.dk>
 * SPDX-FileCopyrightText: 2011 Mojtaba Shahi Senobari <mojtaba.shahi3000@gmail.com>
 * SPDX-FileCopyrightText: 2011-2012 Pierre Stirnweiss <pstirnweiss@googlemail.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "SimpleParagraphWidget.h"
#include "FormattingButton.h"
#include "TextTool.h"
#include <KoStyleThumbnailer.h>
#include <KoRibbonLayout.h>
#include <ListItemsHelper.h>

#include "DockerStylesComboModel.h"
#include "ListLevelWidget.h"
#include "StylesCombo.h"
#include "StylesDelegate.h"
#include "StylesModel.h"
#include "commands/ChangeListLevelCommand.h"

#include <KoDialog.h>
#include <KoEmbeddedDocumentSaver.h>
#include <KoGenStyles.h>
#include <KoInlineTextObjectManager.h>
#include <KoListLevelProperties.h>
#include <KoOdfLoadingContext.h>
#include <KoOdfReadStore.h>
#include <KoParagraphStyle.h>
#include <KoShapeLoadingContext.h>
#include <KoShapePaintingContext.h>
#include <KoShapeSavingContext.h>
#include <KoStore.h>
#include <KoStyleManager.h>
#include <KoTextBlockData.h>
#include <KoTextDocumentLayout.h>
#include <KoTextEditor.h>
#include <KoTextRangeManager.h>
#include <KoXmlReader.h>
#include <KoXmlWriter.h>
#include <KoZoomHandler.h>

#include <KConfigGroup>
#include <KSharedConfig>

#include <QAction>
#include <QBuffer>
#include <QFlags>
#include <QMenu>
#include <QLocale>
#include <QScrollBar>
#include <QTextLayout>
#include <QWidgetAction>
#include <QListView>
#include <QPainter>
#include <QStyleOptionViewItem>

#include <QDebug>
#include <functional>

SimpleParagraphWidget::SimpleParagraphWidget(TextTool *tool, QWidget *parent)
    : QWidget(parent)
    , m_styleManager(nullptr)
    , m_blockSignals(false)
    , m_tool(tool)
    , m_directionButtonState(Auto)
    , m_thumbnailer(new KoStyleThumbnailer())
    , m_stylesModel(new StylesModel(nullptr, StylesModel::ParagraphStyle))
    , m_sortedStylesModel(new DockerStylesComboModel())
    , m_stylesDelegate(nullptr)
{
    widget.setupUi(this);
    widget.alignCenter->setDefaultAction(tool->action("format_aligncenter"));
    widget.alignBlock->setDefaultAction(tool->action("format_alignblock"));
    // RTL layout will reverse the button order, but the align left/right then get mixed up.
    // this makes sure that whatever happens the 'align left' is to the left of the 'align right'
    if (QApplication::isRightToLeft()) {
        widget.alignLeft->setDefaultAction(tool->action("format_alignright"));
        widget.alignRight->setDefaultAction(tool->action("format_alignleft"));
    } else {
        widget.alignLeft->setDefaultAction(tool->action("format_alignleft"));
        widget.alignRight->setDefaultAction(tool->action("format_alignright"));
    }

    widget.decreaseIndent->setDefaultAction(tool->action("format_decreaseindent"));
    widget.increaseIndent->setDefaultAction(tool->action("format_increaseindent"));
    widget.changeTextDirection->setDefaultAction(tool->action("change_text_direction"));

    widget.moreOptions->setText("...");
    widget.moreOptions->setToolTip(i18n("Change paragraph format"));
    connect(widget.moreOptions, &QAbstractButton::clicked, tool->action("format_paragraph"), &QAction::trigger);

    connect(widget.changeTextDirection, &QAbstractButton::clicked, this, &SimpleParagraphWidget::doneWithFocus);
    connect(widget.alignCenter, &QAbstractButton::clicked, this, &SimpleParagraphWidget::doneWithFocus);
    connect(widget.alignBlock, &QAbstractButton::clicked, this, &SimpleParagraphWidget::doneWithFocus);
    connect(widget.alignLeft, &QAbstractButton::clicked, this, &SimpleParagraphWidget::doneWithFocus);
    connect(widget.alignRight, &QAbstractButton::clicked, this, &SimpleParagraphWidget::doneWithFocus);
    connect(widget.decreaseIndent, &QAbstractButton::clicked, this, &SimpleParagraphWidget::doneWithFocus);
    connect(widget.increaseIndent, &QAbstractButton::clicked, this, &SimpleParagraphWidget::doneWithFocus);

    widget.bulletListButton->setDefaultAction(tool->action("format_list"));

    fillListButtons();
    widget.bulletListButton->addSeparator();

    connect(widget.bulletListButton, &FormattingButton::itemTriggered, this, &SimpleParagraphWidget::listStyleChanged);

    m_stylesModel->setStyleThumbnailer(m_thumbnailer);
    widget.paragraphStyleCombo->setStylesModel(m_sortedStylesModel);
    connect(widget.paragraphStyleCombo,
            QOverload<const QModelIndex &>::of(&StylesCombo::selected),
            this,
            QOverload<const QModelIndex &>::of(&SimpleParagraphWidget::styleSelected));
    connect(widget.paragraphStyleCombo, &StylesCombo::newStyleRequested, this, &SimpleParagraphWidget::newStyleRequested);
    connect(widget.paragraphStyleCombo, &StylesCombo::newStyleRequested, this, &SimpleParagraphWidget::doneWithFocus);
    connect(widget.paragraphStyleCombo, &StylesCombo::showStyleManager, this, &SimpleParagraphWidget::slotShowStyleManager);

    m_sortedStylesModel->setStylesModel(m_stylesModel);
}

SimpleParagraphWidget::~SimpleParagraphWidget()
{
    QBuffer dev;
    KoXmlWriter writer(&dev);
    KoGenStyles genStyles;
    KoEmbeddedDocumentSaver mainStyles;
    KoShapeSavingContext savingContext(writer, genStyles, mainStyles);

    writer.startElement("templates:templates");
    foreach (const KoListLevelProperties &llp, m_levelLibrary) {
        llp.saveOdf(&writer, savingContext);
    }
    writer.endElement(); // list-level-properties

    KSharedConfig::openConfig()->reparseConfiguration();
    KConfigGroup appAuthorGroup(KSharedConfig::openConfig("calligrarc"), "");
    appAuthorGroup.writeEntry("listLevelFormats", QString(dev.data()));

    // the style model is set on the comboBox who takes over ownership
    delete m_thumbnailer;
}

namespace
{
class RibbonStyleDelegate : public QStyledItemDelegate
{
public:
    explicit RibbonStyleDelegate(StylesModel *model, std::function<KoStyleManager *()> manager, QObject *parent)
        : QStyledItemDelegate(parent), m_model(model), m_manager(manager) {}

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return QSize(qMax(112, option.fontMetrics.horizontalAdvance(QStringLiteral("AaBbCcDd")) + 24),
                     qMax(68, option.fontMetrics.lineSpacing() * 3 + 16));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        const QRect card = option.rect.adjusted(2, 2, -2, -2);
        painter->fillRect(card, Qt::white);
        const int labelHeight = option.fontMetrics.lineSpacing() + 8;
        const QSize previewSize(qMax(1, card.width() - 8), qMax(1, card.height() - labelHeight - 8));
        const QImage preview = m_model->stylePreview(index.row(), previewSize);
        painter->drawImage(card.topLeft() + QPoint(4, 4), preview);
        auto *style = m_manager() ? m_manager()->paragraphStyle(index.internalId()) : nullptr;
        if (style) {
            painter->setFont(option.font);
            painter->setPen(Qt::black);
            const QRect label(card.left() + 4, card.bottom() - labelHeight, card.width() - 8, labelHeight);
            painter->drawText(label, Qt::AlignCenter,
                              option.fontMetrics.elidedText(style->name(), Qt::ElideRight, label.width()));
        }
        const bool active = option.state & (QStyle::State_Selected | QStyle::State_HasFocus);
        painter->setPen(QPen(option.palette.color(active ? QPalette::Highlight : QPalette::Midlight), active ? 2 : 1));
        painter->drawRect(card.adjusted(1, 1, -1, -1));
        painter->restore();
    }
private:
    StylesModel *m_model;
    std::function<KoStyleManager *()> m_manager;
};
}

QWidget *SimpleParagraphWidget::configureRibbon()
{
    setObjectName(QStringLiteral("ribbonParagraphGroup"));
    while (QLayoutItem *item = widget.gridLayout->takeAt(0)) {
        delete item;
    }
    widget.gridLayout->setSpacing(2);
    auto *numbering = new FormattingButton(this);
    numbering->setObjectName(QStringLiteral("ribbonNumbering"));
    numbering->setIcon(QIcon::fromTheme(QStringLiteral("format-list-ordered")));
    numbering->setToolTip(i18n("Numbering"));
    numbering->setAccessibleName(i18n("Numbering"));
    numbering->setAutoRaise(true);
    ItemChooserAction *numberFormats = numbering->addItemChooser(1);
    for (int i = 0; i < m_levelLibrary.size(); ++i) {
        if (KoListStyle::isNumberingStyle(m_levelLibrary[i].labelType())) {
            numbering->addItem(numberFormats, generateListLevelPixmap(m_levelLibrary[i]), i + 1000);
        }
    }
    connect(numbering, &FormattingButton::itemTriggered, this, &SimpleParagraphWidget::listStyleChanged);
    const QList<QToolButton *> topRow = {widget.bulletListButton, numbering, widget.decreaseIndent, widget.increaseIndent, widget.changeTextDirection};
    const QList<QToolButton *> bottomRow = {widget.alignLeft, widget.alignCenter, widget.alignRight, widget.alignBlock};
    for (int i = 0; i < topRow.size(); ++i) {
        KoRibbonLayout::configureButton(topRow[i], Qt::ToolButtonIconOnly);
        widget.gridLayout->addWidget(topRow[i], 0, i);
    }
    for (int i = 0; i < bottomRow.size(); ++i) {
        KoRibbonLayout::configureButton(bottomRow[i], Qt::ToolButtonIconOnly);
        widget.gridLayout->addWidget(bottomRow[i], 1, i);
    }
    auto *lineSpacing = new QToolButton(this);
    lineSpacing->setObjectName(QStringLiteral("ribbonLineSpacing"));
    lineSpacing->setText(i18n("Line Spacing"));
    lineSpacing->setToolTip(i18n("Change spacing between lines in the selected paragraphs"));
    lineSpacing->setAccessibleName(i18n("Line Spacing"));
    lineSpacing->setIcon(QIcon::fromTheme(QStringLiteral("format-line-spacing-normal")));
    lineSpacing->setPopupMode(QToolButton::InstantPopup);
    auto *spacingMenu = new QMenu(lineSpacing);
    lineSpacing->setMenu(spacingMenu);
    for (int percent : {100, 115, 150, 200, 250, 300}) {
        auto *preset = spacingMenu->addAction(QLocale().toString(percent / 100.0, 'f', percent == 115 ? 2 : 1));
        connect(preset, &QAction::triggered, this, [this, percent] {
            KoTextEditor *editor = m_tool->textEditor();
            if (!editor) {
                return;
            }
            // Clear competing absolute/minimum heights just as the paragraph
            // dialog does, then merge only the spacing properties.
            KoParagraphStyle spacing;
            spacing.setLineHeightAbsolute(0);
            spacing.setMinimumLineHeight(QTextLength(QTextLength::FixedLength, 0));
            spacing.setLineSpacing(0);
            spacing.setLineHeightPercent(percent);
            spacing.setLineSpacingFromFont(true);
            QTextBlockFormat format;
            spacing.applyStyle(format);
            editor->mergeBlockFormat(format);
            Q_EMIT doneWithFocus();
        });
    }
    spacingMenu->addSeparator();
    auto *options = spacingMenu->addAction(i18n("Line Spacing Options…"));
    QAction *paragraphAction = m_tool->action("format_paragraph");
    connect(options, &QAction::triggered, paragraphAction, &QAction::trigger);
    const auto updateSpacingEnabled = [lineSpacing, paragraphAction] {
        lineSpacing->setEnabled(paragraphAction->isEnabled());
    };
    connect(paragraphAction, &QAction::changed, lineSpacing, updateSpacingEnabled);
    updateSpacingEnabled();
    KoRibbonLayout::configureButton(lineSpacing, Qt::ToolButtonIconOnly);
    widget.gridLayout->addWidget(lineSpacing, 1, 4);
    widget.moreOptions->setText(QStringLiteral("↘"));
    widget.moreOptions->setAccessibleName(i18n("Paragraph dialog"));
    widget.gridLayout->addWidget(widget.moreOptions, 1, 5);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto *styles = new QWidget;
    styles->setObjectName(QStringLiteral("ribbonStylesGroup"));
    styles->setWindowTitle(i18n("Styles"));
    styles->setProperty("ribbonGroupOrder", 40);
    auto *layout = new QHBoxLayout(styles);
    layout->setContentsMargins(2, 0, 2, 0);
    layout->setSpacing(2);
    widget.paragraphStyleCombo->hide();
    m_ribbonGallery = new QListView(styles);
    m_ribbonGallery->setObjectName(QStringLiteral("ribbonStyleGallery"));
    m_ribbonGallery->setAccessibleName(i18n("Quick Styles"));
    m_ribbonGallery->setModel(m_stylesModel);
    m_ribbonGallery->setItemDelegate(new RibbonStyleDelegate(m_stylesModel, [this] { return m_styleManager; }, m_ribbonGallery));
    m_ribbonGallery->setFlow(QListView::LeftToRight);
    m_ribbonGallery->setWrapping(false);
    m_ribbonGallery->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_ribbonGallery->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ribbonGallery->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_ribbonGallery->setEditTriggers(QAbstractItemView::NoEditTriggers);
    QStyleOptionViewItem galleryOption;
    galleryOption.initFrom(m_ribbonGallery);
    const QSize card = m_ribbonGallery->itemDelegate()->sizeHint(galleryOption, QModelIndex());
    m_ribbonGallery->setFixedSize(card.width() * 3 + 4, card.height() + 4);
    connect(m_ribbonGallery, &QListView::clicked, this, QOverload<const QModelIndex &>::of(&SimpleParagraphWidget::styleSelected));
    connect(m_ribbonGallery, &QListView::activated, this, QOverload<const QModelIndex &>::of(&SimpleParagraphWidget::styleSelected));
    layout->addWidget(m_ribbonGallery);

    auto *controls = new QVBoxLayout;
    controls->setSpacing(0);
    for (bool forward : {false, true}) {
        auto *scroll = new QToolButton(styles);
        scroll->setArrowType(forward ? Qt::DownArrow : Qt::UpArrow);
        scroll->setAccessibleName(forward ? i18n("Next styles") : i18n("Previous styles"));
        scroll->setToolTip(scroll->accessibleName());
        connect(scroll, &QToolButton::clicked, this, [this, forward] {
            QScrollBar *bar = m_ribbonGallery->horizontalScrollBar();
            bar->setValue(bar->value() + (forward ? 1 : -1) * m_ribbonGallery->viewport()->width());
        });
        controls->addWidget(scroll);
    }
    auto *more = new QToolButton(styles);
    more->setObjectName(QStringLiteral("ribbonMoreStyles"));
    more->setArrowType(Qt::DownArrow);
    more->setAccessibleName(i18n("More Styles"));
    more->setToolTip(i18n("More Styles"));
    more->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(more);
    auto *popup = new QListView(menu);
    popup->setAccessibleName(i18n("All paragraph styles"));
    popup->setModel(m_stylesModel);
    // Bibliography styles are generated for bibliography entries; omit them
    // from both Home style galleries without removing document styles.
    const auto hideBibliographyStyles = [this, popup] {
        for (int row = 0; row < m_stylesModel->rowCount(QModelIndex()); ++row) {
            const QModelIndex index = m_stylesModel->index(row);
            const KoParagraphStyle *style = m_styleManager ? m_styleManager->paragraphStyle(index.internalId()) : nullptr;
            const bool hidden = style && style->name().trimmed().startsWith(QLatin1String("Bibliography"), Qt::CaseInsensitive);
            m_ribbonGallery->setRowHidden(row, hidden);
            popup->setRowHidden(row, hidden);
        }
    };
    connect(m_stylesModel, &QAbstractItemModel::modelReset, popup, hideBibliographyStyles);
    connect(m_stylesModel, &QAbstractItemModel::rowsInserted, popup, hideBibliographyStyles);
    connect(m_stylesModel, &QAbstractItemModel::rowsRemoved, popup, hideBibliographyStyles);
    connect(m_stylesModel, &QAbstractItemModel::rowsMoved, popup, hideBibliographyStyles);
    connect(m_stylesModel, &QAbstractItemModel::dataChanged, popup, hideBibliographyStyles);
    connect(m_stylesModel, &QAbstractItemModel::layoutChanged, popup, hideBibliographyStyles);
    hideBibliographyStyles();
    popup->setItemDelegate(new RibbonStyleDelegate(m_stylesModel, [this] { return m_styleManager; }, popup));
    popup->setFlow(QListView::LeftToRight);
    popup->setWrapping(true);
    popup->setResizeMode(QListView::Adjust);
    popup->setEditTriggers(QAbstractItemView::NoEditTriggers);
    popup->setFixedSize(card.width() * 3 + 24, card.height() * 3 + 4);
    auto *galleryAction = new QWidgetAction(menu);
    galleryAction->setDefaultWidget(popup);
    menu->addAction(galleryAction);
    const auto chooseStyle = [this, menu](const QModelIndex &index) {
        menu->close();
        styleSelected(index);
    };
    connect(popup, &QListView::clicked, this, chooseStyle);
    connect(popup, &QListView::activated, this, chooseStyle);
    connect(menu, &QMenu::aboutToShow, popup, [this, popup] {
        popup->setCurrentIndex(m_ribbonGallery->currentIndex());
        popup->scrollTo(popup->currentIndex());
        popup->setFocus();
    });
    menu->addSeparator();
    menu->addAction(m_tool->action("format_stylist"));
    more->setMenu(menu);
    controls->addWidget(more);
    layout->addLayout(controls);
    return styles;
}

QPixmap SimpleParagraphWidget::generateListLevelPixmap(const KoListLevelProperties &llp)
{
    KoZoomHandler zoomHandler;
    zoomHandler.setZoom(1.2);
    zoomHandler.setDpi(72, 72);

    KoInlineTextObjectManager itom;
    KoTextRangeManager tlm;
    TextShape textShape(&itom, &tlm);
    textShape.setSize(QSizeF(300, 100));
    QTextCursor cursor(textShape.textShapeData()->document());
    textShape.textShapeData()->document()->setUndoRedoEnabled(false); // let's not bother

    QPixmap pm(48, 48);

    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.translate(0, -1.5);
    p.setRenderHint(QPainter::Antialiasing);
    if (llp.labelType() == KoListStyle::None) {
    } else if (KoListStyle::isNumberingStyle(llp.labelType())) {
        KoListStyle listStyle;

        listStyle.setLevelProperties(llp);
        cursor.select(QTextCursor::Document);
        QTextCharFormat textCharFormat = cursor.blockCharFormat();
        textCharFormat.setFontPointSize(11);
        textCharFormat.setFontWeight(QFont::Normal);
        cursor.setCharFormat(textCharFormat);

        QTextBlock cursorBlock = cursor.block();
        KoTextBlockData data(cursorBlock);
        cursor.insertText("----");
        listStyle.applyStyle(cursor.block(), 1);
        cursorBlock = cursor.block();
        KoTextBlockData data1(cursorBlock);
        cursor.insertText("\n----");
        cursorBlock = cursor.block();
        KoTextBlockData data2(cursorBlock);
        cursor.insertText("\n----");
        cursorBlock = cursor.block();
        KoTextBlockData data3(cursorBlock);
    } else {
        KoListStyle listStyle;
        listStyle.setLevelProperties(llp);
        cursor.select(QTextCursor::Document);
        QTextCharFormat textCharFormat = cursor.blockCharFormat();
        textCharFormat.setFontPointSize(27);
        textCharFormat.setFontWeight(QFont::Normal);
        cursor.setBlockCharFormat(textCharFormat);

        QTextBlock cursorBlock = cursor.block();
        KoTextBlockData data(cursorBlock);
        listStyle.applyStyle(cursor.block(), 1);
    }

    KoTextDocumentLayout *lay = dynamic_cast<KoTextDocumentLayout *>(textShape.textShapeData()->document()->documentLayout());
    if (lay)
        lay->layout();

    KoShapePaintingContext paintContext; // FIXME
    textShape.paintComponent(p, zoomHandler, paintContext);

    return pm;
}

void SimpleParagraphWidget::fillListButtons()
{
    KSharedConfig::openConfig()->reparseConfiguration();
    KConfigGroup appAuthorGroup(KSharedConfig::openConfig("calligrarc"), "");
    QString formats = appAuthorGroup.readEntry("listLevelFormats", QString());
    formats.replace("\n", "");

    if (false /*!formats.isEmpty()*/) {
        KoXmlDocument document;
        document.setContent(formats);
        KoXmlElement styleElem;
        forEachElement(styleElem, document.documentElement())
        {
            KoListLevelProperties llp;
            //        properties.loadOdf(scontext, styleElem);
            m_levelLibrary.append(llp);
        }
        //    KoOdfLoadingContext odfContext;
        //    KoShapeLoadingContext loadingContext(odfContext);
    } else {
        KoListStyle listStyle;
        KoListLevelProperties llp = listStyle.levelProperties(1);
        llp.setMargin(36.0);
        llp.setMarginIncrease(18.0);
        llp.setTextIndent(-18.0);
        llp.setTabStopPosition(36.0);
        llp.setLabelFollowedBy(KoListStyle::ListTab);
        llp.setDisplayLevel(4);

        llp.setLabelType(KoListStyle::BulletCharLabelType);
        llp.setBulletCharacter(QChar(0x2022)); // Bullet
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::BulletCharLabelType);
        llp.setBulletCharacter(QChar(0x25A0)); // Square
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::BulletCharLabelType);
        llp.setBulletCharacter(QChar(0x25C6)); // Rhombus
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::BulletCharLabelType);
        llp.setBulletCharacter(QChar(0x25CB)); // Circle
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::BulletCharLabelType);
        llp.setBulletCharacter(QChar(0x2714)); // HeavyCheckMark
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::BulletCharLabelType);
        llp.setBulletCharacter(QChar(0x2794)); // Right Arrow
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::NumberLabelType);
        llp.setNumberFormat(KoOdfNumberDefinition::Numeric);
        llp.setListItemSuffix(".");
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::NumberLabelType);
        llp.setNumberFormat(KoOdfNumberDefinition::Numeric);
        llp.setListItemSuffix(")");
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::NumberLabelType);
        llp.setNumberFormat(KoOdfNumberDefinition::AlphabeticLowerCase);
        llp.setListItemSuffix(".");
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::NumberLabelType);
        llp.setNumberFormat(KoOdfNumberDefinition::AlphabeticLowerCase);
        llp.setListItemSuffix(")");
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::NumberLabelType);
        llp.setNumberFormat(KoOdfNumberDefinition::AlphabeticUpperCase);
        llp.setListItemSuffix("");
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::NumberLabelType);
        llp.setNumberFormat(KoOdfNumberDefinition::RomanLowerCase);
        llp.setListItemSuffix("");
        m_levelLibrary.append(llp);

        llp.setLabelType(KoListStyle::NumberLabelType);
        llp.setNumberFormat(KoOdfNumberDefinition::RomanUpperCase);
        llp.setListItemSuffix("");
        m_levelLibrary.append(llp);
    }

    m_recentChooserAction = widget.bulletListButton->addItemChooser(5, i18n("Recently Used Level Formats"));
    int id = 1;
    m_recentListFormats.append(m_levelLibrary.at(0));
    widget.bulletListButton->addItem(m_recentChooserAction, generateListLevelPixmap(m_recentListFormats.at(0)), id);

    m_libraryChooserAction = widget.bulletListButton->addItemChooser(5, i18n("Library of Level Formats"));
    id = 1000;
    foreach (const KoListLevelProperties &llp, m_levelLibrary) {
        widget.bulletListButton->addItem(m_libraryChooserAction, generateListLevelPixmap(llp), id);
        QAction *a = widget.bulletListButton->addItemMenuItem(m_libraryChooserAction, id, i18n("Delete"));
        a->setData(id);
        connect(a, &QAction::triggered, this, &SimpleParagraphWidget::deleteLevelFormat);
        a = widget.bulletListButton->addItemMenuItem(m_libraryChooserAction, id, i18n("Edit..."));
        a->setData(id);
        connect(a, &QAction::triggered, this, &SimpleParagraphWidget::editLevelFormat);
        id++;
    }

    widget.bulletListButton->addSeparator();

    QAction *action = new QAction(i18n("Define New Level Format..."), this);
    action->setToolTip(i18n("Define new bullet or numbering format"));
    widget.bulletListButton->addAction(action);
    connect(action, &QAction::triggered, this, &SimpleParagraphWidget::defineLevelFormat);
    /*    action = new QAction(i18n("Continue Previous List"),this);
        action->setToolTip(i18n("Continue the list from a previous list"));
        widget.bulletListButton->addAction(action);
        action = new QAction(i18n("Set Numbering Value..."),this);
        action->setToolTip(i18n("Set the numbering value"));
        widget.bulletListButton->addAction(action);
        */
}

void SimpleParagraphWidget::defineLevelFormat()
{
    ListLevelWidget *llw = new ListLevelWidget();
    KoDialog dia(this);

    dia.setModal(true);
    dia.setButtons(KoDialog::Ok | KoDialog::Cancel);
    dia.setMainWidget(llw);
    dia.setWindowTitle(i18n("Define New List Level Format"));

    KoListLevelProperties llp;
    llp.setMargin(36.0);
    llp.setMarginIncrease(18.0);
    llp.setTextIndent(-18.0);
    llp.setTabStopPosition(36.0);
    llp.setLabelFollowedBy(KoListStyle::ListTab);
    llw->setDisplay(llp);

    if (dia.exec()) {
        for (int i = 0; i < m_levelLibrary.size(); ++i) {
            KoListLevelProperties llp = m_levelLibrary.at(i);
            llp.setLevel(1);
            widget.bulletListButton->addItem(m_libraryChooserAction, generateListLevelPixmap(m_levelLibrary.at(i)), i + 1000);
        }

        int id = m_levelLibrary.size() + 1000;
        llw->save(llp);

        m_levelLibrary.append(llp);
        llp.setLevel(1);
        widget.bulletListButton->addItem(m_libraryChooserAction, generateListLevelPixmap(m_levelLibrary.at(id - 1000)), id);
        QAction *a = widget.bulletListButton->addItemMenuItem(m_libraryChooserAction, id, i18n("Delete"));
        a->setData(id);
        connect(a, &QAction::triggered, this, &SimpleParagraphWidget::deleteLevelFormat);
        a = widget.bulletListButton->addItemMenuItem(m_libraryChooserAction, id, i18n("Edit..."));
        a->setData(id);
        connect(a, &QAction::triggered, this, &SimpleParagraphWidget::editLevelFormat);
    }
}

void SimpleParagraphWidget::setCurrentBlock(const QTextBlock &block)
{
    if (block == m_currentBlock) {
        return;
    }

    m_currentBlock = block;
    m_blockSignals = true;
    struct Finally {
        Finally(SimpleParagraphWidget *p)
        {
            parent = p;
        }
        ~Finally()
        {
            parent->m_blockSignals = false;
        }
        SimpleParagraphWidget *parent;
    };
    Finally finally(this);

    setCurrentFormat(m_currentBlock.blockFormat());
}

void SimpleParagraphWidget::setCurrentFormat(const QTextBlockFormat &format)
{
    if (!m_styleManager || format == m_currentBlockFormat)
        return;
    m_currentBlockFormat = format;

    int id = m_currentBlockFormat.intProperty(KoParagraphStyle::StyleId);
    KoParagraphStyle *style(m_styleManager->paragraphStyle(id));
    if (m_ribbonGallery && style) {
        m_ribbonGallery->setCurrentIndex(m_stylesModel->indexOf(style));
        m_ribbonGallery->scrollTo(m_ribbonGallery->currentIndex());
    }
    if (style) {
        bool unchanged = true;

        foreach (int property, m_currentBlockFormat.properties().keys()) {
            switch (property) {
            case QTextFormat::ObjectIndex:
            case KoParagraphStyle::ListStyleId:
            case KoParagraphStyle::OutlineLevel:
            case KoParagraphStyle::ListStartValue:
            case KoParagraphStyle::IsListHeader:
            case KoParagraphStyle::UnnumberedListItem:
                continue;
            // These can be both content and style properties so let's ignore
            case KoParagraphStyle::BreakBefore:
            case KoParagraphStyle::MasterPageName:
                continue;

            default:
                break;
            }
            if (property
                == QTextBlockFormat::BlockAlignment) { // the default alignment can be retrieved in the defaultTextOption. However, calligra sets the
                                                       // Qt::AlignAbsolute flag, so we need to or this flag with the default alignment before comparing.
                if ((m_currentBlockFormat.property(property) != style->value(property))
                    && !(style->value(property).isNull()
                         && ((m_currentBlockFormat.intProperty(property))
                             == int(m_currentBlock.document()->defaultTextOption().alignment() | Qt::AlignAbsolute)))) {
                    unchanged = false;
                    break;
                } else {
                    continue;
                }
            }
            if (property == KoParagraphStyle::TextProgressionDirection) {
                if (style->value(property).isNull() && m_currentBlockFormat.intProperty(property) == KoText::LeftRightTopBottom) {
                    // LTR seems to be Qt default when unset
                    continue;
                }
            }
            if ((m_currentBlockFormat.property(property) != style->value(property))
                && !(style->value(property).isNull() && !m_currentBlockFormat.property(property).toBool())) {
                // the last check seems to work. might be cause of a bug. The problem is when comparing an unset property in the style with a set to {0, false,
                // ...) property in the format (eg. set then unset bold)
                unchanged = false;
                break;
            }
        }
        // we are updating the combo's selected item to what is the current format. we do not want this to apply the style as it would mess up the undo stack,
        // the change tracking,...
        disconnect(widget.paragraphStyleCombo,
                   QOverload<const QModelIndex &>::of(&StylesCombo::selected),
                   this,
                   QOverload<const QModelIndex &>::of(&SimpleParagraphWidget::styleSelected));
        m_sortedStylesModel->styleApplied(style);
        widget.paragraphStyleCombo->setCurrentIndex(m_sortedStylesModel->indexOf(style).row());
        widget.paragraphStyleCombo->setStyleIsOriginal(unchanged);
        m_stylesModel->setCurrentParagraphStyle(id);
        widget.paragraphStyleCombo->slotUpdatePreview();
        connect(widget.paragraphStyleCombo,
                QOverload<const QModelIndex &>::of(&StylesCombo::selected),
                this,
                QOverload<const QModelIndex &>::of(&SimpleParagraphWidget::styleSelected));
    }
}

void SimpleParagraphWidget::setStyleManager(KoStyleManager *sm)
{
    Q_ASSERT(sm);
    if (!sm || m_styleManager == sm) {
        return;
    }
    if (m_styleManager) {
        disconnect(m_styleManager, &KoStyleManager::paragraphStyleApplied, this, &SimpleParagraphWidget::slotParagraphStyleApplied);
    }
    m_styleManager = sm;
    // we want to disconnect this before setting the stylemanager. Populating the model apparently selects the first inserted item. We don't want this to
    // actually set a new style.
    disconnect(widget.paragraphStyleCombo,
               QOverload<const QModelIndex &>::of(&StylesCombo::selected),
               this,
               QOverload<const QModelIndex &>::of(&SimpleParagraphWidget::styleSelected));
    m_stylesModel->setStyleManager(sm);
    m_sortedStylesModel->setStyleManager(sm);
    connect(widget.paragraphStyleCombo,
            QOverload<const QModelIndex &>::of(&StylesCombo::selected),
            this,
            QOverload<const QModelIndex &>::of(&SimpleParagraphWidget::styleSelected));
    connect(m_styleManager, &KoStyleManager::paragraphStyleApplied, this, &SimpleParagraphWidget::slotParagraphStyleApplied);
}

void SimpleParagraphWidget::setInitialUsedStyles(QVector<int> list)
{
    m_sortedStylesModel->setInitialUsedStyles(list);
}

void SimpleParagraphWidget::listStyleChanged(int id)
{
    Q_EMIT doneWithFocus();
    if (m_blockSignals)
        return;
    KoListLevelProperties llp;

    if (id >= 1000) {
        llp = m_levelLibrary.at(id - 1000);
    } else {
        llp = m_recentListFormats.at(id - 1);
        m_recentListFormats.removeAt(id - 1);
    }

    llp.setLevel(1);
    m_recentListFormats.prepend(llp);
    if (m_recentListFormats.size() > 5) {
        m_recentListFormats.removeLast();
    }
    for (int i = 0; i < m_recentListFormats.size(); ++i) {
        widget.bulletListButton->addItem(m_recentChooserAction, generateListLevelPixmap(m_recentListFormats.at(i)), i + 1); // +1 as items are 1 based
    }
    KoTextEditor::ChangeListFlags flags(KoTextEditor::AutoListStyle | KoTextEditor::DontUnsetIfSame);
    m_tool->textEditor()->setListProperties(llp, flags);
}

void SimpleParagraphWidget::deleteLevelFormat()
{
    int id = qobject_cast<QAction *>(sender())->data().toInt();
    m_levelLibrary.takeAt(id - 1000);
    widget.bulletListButton->removeLastItem(m_libraryChooserAction);

    for (int i = 0; i < m_levelLibrary.size(); ++i) {
        KoListLevelProperties llp = m_levelLibrary.at(i);
        llp.setLevel(1);
        if (llp.labelType() != KoListStyle::None) {
            widget.bulletListButton->addItem(m_libraryChooserAction, generateListLevelPixmap(m_levelLibrary.at(i)), i + 1000);
/*            QAction *a = widget.bulletListButton->addItemMenuItem(m_libraryChooserAction, id, i18n("Delete"));
            a->setData(id);
            connect(a, SIGNAL(triggered(bool)), this, SLOT(deleteLevelFormat()));
            a = widget.bulletListButton->addItemMenuItem(m_libraryChooserAction, id, i18n("Edit..."));
            a->setData(id);
            connect(a, SIGNAL(triggered(bool)), this, SLOT(editLevelFormat()));
*/        }
    }
}

void SimpleParagraphWidget::editLevelFormat()
{
    int id = qobject_cast<QAction *>(sender())->data().toInt();

    ListLevelWidget *llw = new ListLevelWidget();
    KoDialog dia(this);

    dia.setModal(true);
    dia.setButtons(KoDialog::Ok | KoDialog::Cancel);
    dia.setMainWidget(llw);
    dia.setWindowTitle(i18n("Edit List Level Format"));

    llw->setDisplay(m_levelLibrary.at(id - 1000));

    if (dia.exec()) {
        llw->save(m_levelLibrary[id - 1000]);
        widget.bulletListButton->addItem(m_libraryChooserAction, generateListLevelPixmap(m_levelLibrary.at(id - 1000)), id);
    }
}

void SimpleParagraphWidget::styleSelected(int index)
{
    KoParagraphStyle *paragStyle = m_styleManager->paragraphStyle(m_sortedStylesModel->index(index, 0, QModelIndex()).internalId());
    if (paragStyle) {
        Q_EMIT paragraphStyleSelected(paragStyle);
    }
    Q_EMIT doneWithFocus();
}

void SimpleParagraphWidget::styleSelected(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }
    KoParagraphStyle *paragStyle = m_styleManager->paragraphStyle(index.internalId());
    if (paragStyle) {
        Q_EMIT paragraphStyleSelected(paragStyle);
    }
    Q_EMIT doneWithFocus();
}

void SimpleParagraphWidget::slotShowStyleManager(int index)
{
    int styleId = m_sortedStylesModel->index(index, 0, QModelIndex()).internalId();
    Q_EMIT showStyleManager(styleId);
    Q_EMIT doneWithFocus();
}

void SimpleParagraphWidget::slotParagraphStyleApplied(const KoParagraphStyle *style)
{
    m_sortedStylesModel->styleApplied(style);
}
