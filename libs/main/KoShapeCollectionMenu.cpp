// SPDX-FileCopyrightText: 2008 Peter Simonsson <peter.simonsson@gmail.com>
// SPDX-FileCopyrightText: 2018 Dag Andersen <danders@get2net.dk>
// SPDX-FileCopyrightText: 2024 Carl Schwan <carl@carlschwan.eu>
// SPDX-License-Identifier: LGPL-2.0-or-later

#include "KoShapeCollectionMenu.h"

#include <KoCanvasController.h>
#include <KoCanvasBase.h>
#include <KoCreateShapesTool.h>
#include <KoProperties.h>
#include <KoShape.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeRegistry.h>
#include <KoToolManager.h>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QAbstractItemModel>
#include <QApplication>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <memory>
#include <QScrollArea>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QButtonGroup>
#include <QDebug>
#include <QGridLayout>
#include <QIcon>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMimeData>
#include <QString>
#include <QToolButton>

class KoProperties;

inline bool operator<(const struct KoShapeTemplate &a, const struct KoShapeTemplate &b)
{
    if (a.order != b.order) {
        return a.order < b.order;
    }
    if (a.id != b.id) {
        return a.id < b.id;
    }
    return a.templateId < b.templateId;
}

/**
 * Struct containing the information stored in CollectionItemModel item
 */
struct KoCollectionItem {
    KoCollectionItem()
    {
        properties = nullptr;
    };

    QString id;
    QString name;
    QString toolTip;
    QIcon icon;
    const KoProperties *properties;
};

class CollectionItemModel : public QAbstractListModel
{
    Q_OBJECT
public:
    explicit CollectionItemModel(QObject *parent = nullptr);

    Qt::DropActions supportedDragActions() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    QStringList mimeTypes() const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /**
     * Set the list of KoCollectionItem to be stored in the model
     */
    void setShapeTemplateList(const QList<KoCollectionItem> &newlist);
    QList<KoCollectionItem> shapeTemplateList() const
    {
        return m_shapeTemplateList;
    }

    const KoProperties *properties(const QModelIndex &index) const;

private:
    QList<KoCollectionItem> m_shapeTemplateList;
    QString m_family;
};

CollectionItemModel::CollectionItemModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

Qt::DropActions CollectionItemModel::supportedDragActions() const
{
    return Qt::CopyAction;
}

QVariant CollectionItemModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_shapeTemplateList.count())
        return QVariant();

    switch (role) {
    case Qt::ToolTipRole:
        return m_shapeTemplateList[index.row()].toolTip;

    case Qt::DecorationRole:
        return m_shapeTemplateList[index.row()].icon;

    case Qt::UserRole:
        return m_shapeTemplateList[index.row()].id;

    case Qt::DisplayRole:
        return m_shapeTemplateList[index.row()].name;

    default:
        return {};
    }
}

int CollectionItemModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_shapeTemplateList.count();
}

void CollectionItemModel::setShapeTemplateList(const QList<KoCollectionItem> &newlist)
{
    beginResetModel();
    m_shapeTemplateList = newlist;
    endResetModel();
}

QMimeData *CollectionItemModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty())
        return nullptr;

    QModelIndex index = indexes.first();

    if (!index.isValid())
        return nullptr;

    if (m_shapeTemplateList.isEmpty())
        return nullptr;

    QByteArray itemData;
    QDataStream dataStream(&itemData, QIODevice::WriteOnly);
    dataStream << m_shapeTemplateList[index.row()].id;
    const KoProperties *props = m_shapeTemplateList[index.row()].properties;

    if (props)
        dataStream << props->store("shapes");
    else
        dataStream << QString();

    QMimeData *mimeData = new QMimeData;
    mimeData->setData(SHAPETEMPLATE_MIMETYPE, itemData);

    return mimeData;
}

QStringList CollectionItemModel::mimeTypes() const
{
    QStringList mimetypes;
    mimetypes << SHAPETEMPLATE_MIMETYPE;

    return mimetypes;
}

Qt::ItemFlags CollectionItemModel::flags(const QModelIndex &index) const
{
    if (index.isValid())
        return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;

    return QAbstractListModel::flags(index);
}

const KoProperties *CollectionItemModel::properties(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_shapeTemplateList.count())
        return nullptr;

    return m_shapeTemplateList[index.row()].properties;
}

QDebug operator<<(QDebug dbg, const KoCollectionItem &i)
{
    return dbg << "CollectionItem[" << i.id << ',' << i.name << "]";
}

// Keep the gallery compact; shape names remain available to assistive
// technologies and in tooltips instead of being clipped below small icons.
class ShapeGalleryDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem cell(option);
        initStyleOption(&cell, index);
        const QIcon icon = cell.icon;
        cell.icon = QIcon();
        const QWidget *widget = cell.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &cell, painter, widget);
        const QPixmap pixmap = icon.pixmap(QSize(32, 32), (cell.state & QStyle::State_Enabled) ? QIcon::Normal : QIcon::Disabled);
        const QSize size = pixmap.size().scaled(QSize(32, 32), Qt::KeepAspectRatio);
        QRect target(QPoint(), size);
        target.moveCenter(cell.rect.center());
        painter->save();
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawPixmap(target, pixmap);
        painter->restore();
    }
protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text.clear();
        option->displayAlignment = Qt::AlignCenter;
        option->decorationAlignment = Qt::AlignCenter;
        option->decorationPosition = QStyleOptionViewItem::Top;
    }
};

class KoShapeCollectionMenu::Private
{
public:
    Private(KoShapeCollectionMenu *_q)
        : q(_q)
        , buttonGroup(new QButtonGroup(q))
    {
    }

    KoShapeCollectionMenu *q;
    bool officeGallery = false;
    bool chartsOnly = false;
    CollectionItemModel *recentModel = nullptr;
    QLabel *recentHeading = nullptr;
    QMap<QString, KoCollectionItem> officePresets;
    QScrollArea *galleryScroll = nullptr;
    QVBoxLayout *galleryLayout = nullptr;
    int galleryCell = 48;
    int galleryColumns = 8;
    QHBoxLayout *collectionChooser;
    QMap<QString, CollectionItemModel *> modelMap;
    QListView *collectionView;
    QButtonGroup *const buttonGroup;
};

KoShapeCollectionMenu::KoShapeCollectionMenu(QWidget *parent, bool chartsOnly)
    : QMenu(parent)
    , d(std::make_unique<KoShapeCollectionMenu::Private>(this))
{
    d->chartsOnly = chartsOnly;
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins({1, 1, 1, 1});

    const QString appName = qApp->applicationName();
    d->officeGallery = appName.contains(QLatin1String("words"), Qt::CaseInsensitive)
        || appName.contains(QLatin1String("sheets"), Qt::CaseInsensitive)
        || appName.contains(QLatin1String("stage"), Qt::CaseInsensitive);
    if (d->officeGallery) {
        setObjectName(QStringLiteral("wordsShapesGallery"));
        d->galleryCell = qMax(44, fontMetrics().height() + 28);
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            d->galleryColumns = qMax(3, qMin(8, (screen->availableGeometry().width() - 48) / d->galleryCell));
        }
        d->galleryScroll = new QScrollArea(this);
        d->galleryScroll->setFrameShape(QFrame::NoFrame);
        d->galleryScroll->setWidgetResizable(true);
        d->galleryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content = new QWidget(d->galleryScroll);
        d->galleryLayout = new QVBoxLayout(content);
        d->galleryLayout->setContentsMargins(4, 4, 4, 4);
        d->galleryLayout->setSpacing(4);
        d->galleryScroll->setWidget(content);
        mainLayout->addWidget(d->galleryScroll);
        loadDefaultShapes();
        d->galleryLayout->addStretch();
        connect(this, &QMenu::aboutToShow, this, [this] {
            const QStringList recent = KSharedConfig::openConfig()->group(QStringLiteral("WordsRibbonShapes")).readEntry("Recent", QStringList());
            QList<KoCollectionItem> recentItems;
            for (const QString &preset : recent) {
                if (d->officePresets.contains(preset)) { recentItems.append(d->officePresets.value(preset)); }
                if (recentItems.size() == 8) { break; }
            }
            if (d->recentModel && !recentItems.isEmpty()) {
                d->recentModel->setShapeTemplateList(recentItems);
                d->recentHeading->setText(i18n("Recently Used Shapes"));
            }
            QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
            if (!screen) { screen = QGuiApplication::primaryScreen(); }
            const int availableHeight = screen ? screen->availableGeometry().height() - 80 : 600;
            d->galleryScroll->setFixedSize(d->galleryColumns * d->galleryCell + 32,
                                          qMin(qMax(160, availableHeight), d->galleryLayout->sizeHint().height() + 8));
        });
        return;
    }

    d->collectionChooser = new QHBoxLayout;
    mainLayout->addLayout(d->collectionChooser);

    auto frame = new QFrame(this);
    frame->setFrameStyle(QFrame::HLine);
    frame->setFixedHeight(1);
    mainLayout->addWidget(frame);

    d->collectionView = new QListView(this);
    mainLayout->addWidget(d->collectionView);
    d->collectionView->setViewMode(QListView::IconMode);
    d->collectionView->setDragDropMode(QListView::DragOnly);
    d->collectionView->setSelectionMode(QListView::SingleSelection);
    d->collectionView->setResizeMode(QListView::Adjust);
    d->collectionView->setGridSize(QSize(48 + 20, 48));
    d->collectionView->setMaximumHeight(2 * d->collectionView->gridSize().height() + 4);
    d->collectionView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    d->collectionView->setWordWrap(true);

    connect(d->collectionView, &QAbstractItemView::clicked, this, &KoShapeCollectionMenu::activateShapeCreationTool);

    // Load the default shapes and add them to the combobox
    loadDefaultShapes();
}

KoShapeCollectionMenu::~KoShapeCollectionMenu() = default;

// Returns a sizehint based on the layout and not on the number (0) of menu items
QSize KoShapeCollectionMenu::sizeHint() const
{
    return layout()->sizeHint();
}

void KoShapeCollectionMenu::activateShapeCollection(const QString &id)
{
    if (d->modelMap.contains(id)) {
        d->collectionView->setModel(d->modelMap[id]);
    } else {
        qCritical() << "Didn't find a model with id ==" << id;
    }
}

void KoShapeCollectionMenu::loadDefaultShapes()
{
    QMap<KoShapeTemplate, KoCollectionItem> defaultList;
    QMap<KoShapeTemplate, KoCollectionItem> arrowList;
    QMap<KoShapeTemplate, KoCollectionItem> funnyList;
    QMap<KoShapeTemplate, KoCollectionItem> geometricList;
    QMap<KoShapeTemplate, KoCollectionItem> quicklist;
    QMap<QString, QMap<KoShapeTemplate, KoCollectionItem>> extras;
    QMap<QString, QString> extraNames;

    int quickCount = 0;

    QStringList quickShapes;
    quickShapes << "TextShapeID"
                << "PictureShape"
                << "ChartShape_Bar"
                << "ArtisticText";
    KConfigGroup cfg = KSharedConfig::openConfig()->group("KoShapeCollection");
    quickShapes = cfg.readEntry("QuickShapes", quickShapes);

    const auto keys = KoShapeRegistry::instance()->keys();
    for (const QString &id : keys) {
        KoShapeFactoryBase *factory = KoShapeRegistry::instance()->value(id);
        if (!factory) {
            continue;
        }
        if (d->chartsOnly && factory->family() != QLatin1String("chart")) {
            continue;
        }
        if (d->officeGallery && !d->chartsOnly && factory->family() == QLatin1String("chart")) {
            continue;
        }
        if (factory->id() == QLatin1String("OfficeShape") && !d->officeGallery) {
            continue;
        }
        // don't show hidden factories
        if (factory->hidden()) {
            continue;
        }
        // don't show stencil factories for now, exclusively available by StencilBox
        if (factory->family() == QLatin1String("stencil")) {
            continue;
        }
        bool oneAdded = false;

        const auto templates = factory->templates();
        for (const KoShapeTemplate &shapeTemplate : templates) {
            oneAdded = true;
            KoCollectionItem temp;
            temp.id = shapeTemplate.id;
            temp.name = shapeTemplate.name;
            temp.toolTip = shapeTemplate.toolTip;
            temp.icon = QIcon::fromTheme(shapeTemplate.iconName);
            temp.properties = shapeTemplate.properties;
            if (shapeTemplate.family.startsWith(QLatin1String("office-"))) {
                std::unique_ptr<KoShape> preview(factory->createShape(shapeTemplate.properties));
                if (preview) {
                    const QPainterPath outline = preview->outline();
                    const QRectF bounds = outline.boundingRect();
                    if (!bounds.isEmpty() && qIsFinite(bounds.width()) && qIsFinite(bounds.height())) {
                        QPixmap pixmap(64, 64);
                        pixmap.setDevicePixelRatio(2);
                        pixmap.fill(Qt::transparent);
                        QPainter painter(&pixmap);
                        painter.setRenderHint(QPainter::Antialiasing);
                        const qreal scale = 26.0 / qMax(bounds.width(), bounds.height());
                        painter.translate(16, 16);
                        painter.scale(scale, scale);
                        painter.translate(-bounds.center());
                        QPen pen(QColor(QStringLiteral("#34495e")));
                        pen.setWidthF(1.2 / scale);
                        painter.setPen(pen);
                        painter.setBrush(shapeTemplate.family == QLatin1String("office-lines") ? QBrush(Qt::NoBrush) : QBrush(QColor(QStringLiteral("#9fc5e8"))));
                        painter.drawPath(outline);
                        painter.end();
                        temp.icon = QIcon(pixmap);
                    }
                }
                d->officePresets.insert(shapeTemplate.templateId, temp);
                extras[shapeTemplate.family][shapeTemplate] = temp;
                continue;
            }
            if (shapeTemplate.family == "funny")
                funnyList[shapeTemplate] = temp;
            else if (shapeTemplate.family == "arrow")
                arrowList[shapeTemplate] = temp;
            else if (shapeTemplate.family == "geometric")
                geometricList[shapeTemplate] = temp;
            else if (factory->family() == "default" || factory->family().isEmpty()) {
                defaultList[shapeTemplate] = temp;
            } else {
                extras[factory->family()][shapeTemplate] = temp;
                extraNames[factory->family()] = factory->name();
            }

            QString id = temp.id;
            if (!shapeTemplate.templateId.isEmpty()) {
                id += '_' + shapeTemplate.templateId;
            }

            if (quickShapes.contains(id)) {
                quicklist[shapeTemplate] = temp;
                quickCount++;
            }
        }

        if (!oneAdded) {
            KoCollectionItem temp;
            KoShapeTemplate tempShape;
            temp.id = tempShape.id = factory->id();
            temp.name = tempShape.name = factory->name();
            temp.toolTip = tempShape.toolTip = factory->toolTip();
            temp.icon = QIcon::fromTheme(factory->iconName());
            tempShape.iconName = factory->iconName();
            temp.properties = tempShape.properties = nullptr;
            if (factory->family() == "funny")
                funnyList[tempShape] = temp;
            else if (factory->family() == "arrow")
                arrowList[tempShape] = temp;
            else if (factory->family() == "geometric")
                geometricList[tempShape] = temp;
            else
                defaultList[tempShape] = temp;

            if (quickShapes.contains(temp.id)) {
                quicklist[tempShape] = temp;
                quickCount++;
            }
        }
    }

    if (d->officeGallery) {
        if (d->chartsOnly) {
            const auto it = extras.constFind(QStringLiteral("chart"));
            if (it != extras.constEnd()) {
                auto *model = new CollectionItemModel(this);
                model->setShapeTemplateList(it.value().values());
                addCollection(QStringLiteral("charts"), i18n("Charts"), model);
            }
            return;
        }
        QMap<KoShapeTemplate, KoCollectionItem> stars;
        QMap<KoShapeTemplate, KoCollectionItem> callouts;
        QMap<KoShapeTemplate, KoCollectionItem> lines;
        for (auto it = geometricList.begin(); it != geometricList.end();) {
            if (it.key().templateId == QLatin1String("star") || it.key().templateId == QLatin1String("flower")) {
                stars.insert(it.key(), it.value());
                it = geometricList.erase(it);
            } else { ++it; }
        }
        for (auto it = funnyList.begin(); it != funnyList.end();) {
            if (it.key().templateId.contains(QLatin1String("rectangular"))) {
                callouts.insert(it.key(), it.value());
                it = funnyList.erase(it);
            } else if (it.key().templateId == QLatin1String("flower")) {
                stars.insert(it.key(), it.value());
                it = funnyList.erase(it);
            } else { ++it; }
        }
        for (auto it = defaultList.begin(); it != defaultList.end();) {
            if (it.value().id == QLatin1String("KoPathShape") || it.value().id == QLatin1String("KoConnectionShape")) {
                lines.insert(it.key(), it.value());
                it = defaultList.erase(it);
            } else { ++it; }
        }
        const auto section = [this](const QString &id, const QString &title, const QMap<KoShapeTemplate, KoCollectionItem> &items) {
            if (items.isEmpty()) { return; }
            auto *model = new CollectionItemModel(this);
            model->setShapeTemplateList(items.values());
            addCollection(id, title, model);
        };
        const QMap<QString, QString> officeCategories{
            {QStringLiteral("office-lines"), i18n("Lines")},
            {QStringLiteral("office-rectangles"), i18n("Rectangles")},
            {QStringLiteral("office-basic"), i18n("Basic Shapes")},
            {QStringLiteral("office-arrows"), i18n("Block Arrows")},
            {QStringLiteral("office-equations"), i18n("Equation Shapes")},
            {QStringLiteral("office-flowchart"), i18n("Flowchart")},
            {QStringLiteral("office-stars"), i18n("Stars and Banners")},
            {QStringLiteral("office-callouts"), i18n("Callouts")},
            {QStringLiteral("office-buttons"), i18n("Action Buttons")},
        };
        QMap<KoShapeTemplate, KoCollectionItem> common;
        for (auto it = extras.constBegin(); it != extras.constEnd(); ++it) {
            if (!it.key().startsWith(QLatin1String("office-"))) { continue; }
            for (auto shape = it->constBegin(); shape != it->constEnd(); ++shape) {
                if (QStringList{QStringLiteral("rect"), QStringLiteral("roundRect"), QStringLiteral("ellipse"),
                                QStringLiteral("line"), QStringLiteral("rightArrow"), QStringLiteral("triangle"),
                                QStringLiteral("star5"), QStringLiteral("wedgeRectCallout")}.contains(shape.key().templateId)) {
                    common.insert(shape.key(), shape.value());
                }
            }
        }
        section(QStringLiteral("quick"), i18n("Common Shapes"), common.isEmpty() ? quicklist : common);
        for (const QString &category : {QStringLiteral("office-lines"), QStringLiteral("office-rectangles"),
                                       QStringLiteral("office-basic"), QStringLiteral("office-arrows"),
                                       QStringLiteral("office-equations"), QStringLiteral("office-flowchart"),
                                       QStringLiteral("office-stars"), QStringLiteral("office-callouts"),
                                       QStringLiteral("office-buttons")}) {
            section(category, officeCategories.value(category), extras.take(category));
        }
        if (common.isEmpty()) {
            section(QStringLiteral("lines"), i18n("Lines and Connectors"), lines);
            section(QStringLiteral("geometric"), i18n("Basic Shapes"), geometricList);
            section(QStringLiteral("arrow"), i18n("Block Arrows"), arrowList);
            section(QStringLiteral("stars"), i18n("Stars and Decorations"), stars);
            section(QStringLiteral("callouts"), i18n("Callouts"), callouts);
            section(QStringLiteral("funny"), i18n("Symbols"), funnyList);
        }
        section(QStringLiteral("default"), i18n("Text and Other Shapes"), defaultList);
        for (auto it = extras.constBegin(); it != extras.constEnd(); ++it) {
            section(it.key(), extraNames.value(it.key()), it.value());
        }
        return;
    }

    CollectionItemModel *model = new CollectionItemModel(this);
    model->setShapeTemplateList(defaultList.values());
    auto defaultCollection = addCollection("default", i18n("Default"), model);

    model = new CollectionItemModel(this);
    model->setShapeTemplateList(geometricList.values());
    addCollection("geometric", i18n("Geometrics"), model);

    QMap<QString, QMap<KoShapeTemplate, KoCollectionItem>>::const_iterator it;
    for (it = extras.constBegin(); it != extras.constEnd(); ++it) {
        model = new CollectionItemModel(this);
        model->setShapeTemplateList(it.value().values());
        addCollection(it.key(), extraNames[it.key()], model);
    }

    model = new CollectionItemModel(this);
    model->setShapeTemplateList(arrowList.values());
    addCollection("arrow", i18n("Arrows"), model);

    model = new CollectionItemModel(this);
    model->setShapeTemplateList(funnyList.values());
    addCollection("funny", i18n("Funny"), model);

    defaultCollection->toggle();
}

QToolButton *KoShapeCollectionMenu::addCollection(const QString &id, const QString &title, CollectionItemModel *model)
{
    if (d->modelMap.contains(id)) {
        return nullptr;
    }

    d->modelMap.insert(id, model);
    if (d->officeGallery) {
        if (model->rowCount() == 0) { return nullptr; }
        auto *heading = new QLabel(title, d->galleryScroll->widget());
        heading->setMargin(5);
        heading->setAutoFillBackground(true);
        QPalette colors = heading->palette();
        colors.setColor(QPalette::Window, colors.color(QPalette::AlternateBase));
        heading->setPalette(colors);
        QFont headingFont = heading->font();
        headingFont.setBold(true);
        heading->setFont(headingFont);
        d->galleryLayout->addWidget(heading);
        if (id == QLatin1String("quick")) {
            d->recentModel = model;
            d->recentHeading = heading;
        }
        auto *view = new QListView(d->galleryScroll->widget());
        view->setObjectName(QStringLiteral("shapesGallery_") + id);
        view->setAccessibleName(title);
        view->setModel(model);
        view->setItemDelegate(new ShapeGalleryDelegate(view));
        view->setViewMode(QListView::IconMode);
        view->setFlow(QListView::LeftToRight);
        view->setWrapping(true);
        view->setResizeMode(QListView::Adjust);
        view->setMovement(QListView::Static);
        view->setEditTriggers(QAbstractItemView::NoEditTriggers);
        view->setDragDropMode(QAbstractItemView::DragOnly);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setIconSize(QSize(32, 32));
        view->setGridSize(QSize(d->galleryCell, d->galleryCell));
        view->setFrameShape(QFrame::NoFrame);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        const int rows = (model->rowCount() + d->galleryColumns - 1) / d->galleryColumns;
        view->setFixedHeight(rows * d->galleryCell + 4);
        d->galleryLayout->addWidget(view);
        connect(view, &QAbstractItemView::clicked, this, &KoShapeCollectionMenu::activateShapeCreationTool);
        connect(view, &QAbstractItemView::activated, this, &KoShapeCollectionMenu::activateShapeCreationTool);
        return nullptr;
    }
    auto toolButton = new QToolButton(this);
    toolButton->setText(title);
    toolButton->setCheckable(true);
    toolButton->setAutoRaise(true);
    d->buttonGroup->addButton(toolButton);
    connect(toolButton, &QToolButton::toggled, this, [this, id](bool state) {
        if (state) {
            activateShapeCollection(id);
        }
    });
    d->collectionChooser->addWidget(toolButton);
    return toolButton;
}

void KoShapeCollectionMenu::activateShapeCreationTool(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    KoCanvasController *canvasController = KoToolManager::instance()->activeCanvasController();

    if (canvasController) {
        KoCreateShapesTool *tool = KoToolManager::instance()->shapeCreatorTool(canvasController->canvas());
        const auto *model = qobject_cast<const CollectionItemModel *>(index.model());
        if (!model) { return; }
        QString id = model->data(index, Qt::UserRole).toString();
        const KoProperties *properties = model->properties(index);

        if (!tool) { return; }
        if (d->officeGallery && properties) {
            const QString preset = properties->stringProperty(QStringLiteral("officePreset"));
            if (!preset.isEmpty()) {
                KConfigGroup config = KSharedConfig::openConfig()->group(QStringLiteral("WordsRibbonShapes"));
                QStringList recent = config.readEntry("Recent", QStringList());
                recent.removeAll(preset);
                recent.prepend(preset);
                while (recent.size() > 8) { recent.removeLast(); }
                config.writeEntry("Recent", recent);
                config.sync();
            }
        }
        tool->setShapeId(id);
        tool->setShapeProperties(properties);
        KoToolManager::instance()->switchToolRequested(KoCreateShapesTool_ID);
        if (!isWindow() && canvasController->canvas()->canvasWidget()) {
            canvasController->canvas()->canvasWidget()->setFocus(Qt::OtherFocusReason);
        }
    }
    if (isWindow()) {
        hide();
    }
}

#include "KoShapeCollectionMenu.moc"
