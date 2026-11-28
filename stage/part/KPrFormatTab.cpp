/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Calligra developers
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "KPrFormatTab.h"

#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QScopedValueRollback>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <KActionCollection>
#include <KLocalizedString>
#include <kundo2command.h>

#include <KoCanvasBase.h>
#include <KoColorBackground.h>
#include <KoFlake.h>
#include <KoPACanvasBase.h>
#include <KoPAPageBase.h>
#include <KoProperties.h>
#include <KoRibbonIcons.h>
#include <KoRibbonLayout.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeBackground.h>
#include <KoCreateShapesTool.h>
#include <KoShapeController.h>
#include <KoShapeManager.h>
#include <KoToolManager.h>
#include <KoShapeRegistry.h>
#include <KoShapeStroke.h>
#include <KoUnit.h>
#include <KoUnitDoubleSpinBox.h>
#include <commands/KoShapeBackgroundCommand.h>
#include <commands/KoShapeStrokeCommand.h>
#include <commands/KoShapeTransformCommand.h>

#include "KPrView.h"

namespace
{
struct PresetShape {
    const char *preset;
    const char *name;
};

// The shapes of the Insert Shapes gallery, in PowerPoint's order.
const PresetShape presetShapes[] = {
    {"rect", QT_TRANSLATE_NOOP("KPrFormatTab", "Rectangle")},
    {"roundRect", QT_TRANSLATE_NOOP("KPrFormatTab", "Rounded Rectangle")},
    {"ellipse", QT_TRANSLATE_NOOP("KPrFormatTab", "Oval")},
    {"triangle", QT_TRANSLATE_NOOP("KPrFormatTab", "Triangle")},
    {"rightArrow", QT_TRANSLATE_NOOP("KPrFormatTab", "Right Arrow")},
    {"line", QT_TRANSLATE_NOOP("KPrFormatTab", "Line")},
    {"star5", QT_TRANSLATE_NOOP("KPrFormatTab", "Star")},
    {"wedgeRectCallout", QT_TRANSLATE_NOOP("KPrFormatTab", "Callout")},
    {"diamond", QT_TRANSLATE_NOOP("KPrFormatTab", "Diamond")},
    {"pentagon", QT_TRANSLATE_NOOP("KPrFormatTab", "Pentagon")},
    {"plus", QT_TRANSLATE_NOOP("KPrFormatTab", "Plus")},
    {"flowChartProcess", QT_TRANSLATE_NOOP("KPrFormatTab", "Flowchart")},
};

struct ShapeStyle {
    const char *name;
    QColor fill;
    QColor stroke;
    bool filled;
};

// The presets of the Shape Styles gallery.
const ShapeStyle shapeStyles[] = {
    {"Default", QColor(QStringLiteral("#ffffff")), QColor(QStringLiteral("#1f2430")), true},
    {"Blue", QColor(QStringLiteral("#4472c4")), QColor(QStringLiteral("#2e5aa0")), true},
    {"Orange", QColor(QStringLiteral("#ed7d31")), QColor(QStringLiteral("#c05a15")), true},
    {"Green", QColor(QStringLiteral("#70ad47")), QColor(QStringLiteral("#4f8230")), true},
    {"Dark", QColor(QStringLiteral("#1f2430")), QColor(QStringLiteral("#1f2430")), true},
    {"Outline", QColor(), QColor(QStringLiteral("#1f2430")), false},
};

QPixmap presetPixmap(const QString &preset, const QSize &size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    KoShapeFactoryBase *factory = KoShapeRegistry::instance()->value(QStringLiteral("OfficeShape"));
    if (!factory) {
        return pixmap;
    }
    KoProperties properties;
    properties.setProperty(QStringLiteral("officePreset"), preset);
    std::unique_ptr<KoShape> shape(factory->createShape(&properties, nullptr));
    if (!shape) {
        return pixmap;
    }
    const QPainterPath outline = shape->outline();
    const QRectF bounds = outline.boundingRect();
    if (bounds.isEmpty()) {
        return pixmap;
    }
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal scale = (size.width() - 4) / qMax(bounds.width(), bounds.height());
    painter.translate(size.width() / 2.0, size.height() / 2.0);
    painter.scale(scale, scale);
    painter.translate(-bounds.center());
    QPen pen(QColor(QStringLiteral("#34495e")));
    pen.setWidthF(1.0 / scale);
    painter.setPen(pen);
    painter.setBrush(preset == QLatin1String("line") ? QBrush(Qt::NoBrush) : QBrush(QColor(QStringLiteral("#cfe2f3"))));
    painter.drawPath(outline);
    return pixmap;
}

// A dimension arrow next to the outline of the shape, like the icons of the
// PowerPoint Size group.
QPixmap sizeIconPixmap(bool vertical)
{
    const QSize size(16, 16);
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QPen arrow(QColor(QStringLiteral("#3d6d99")));
    arrow.setWidthF(1.2);
    painter.setPen(arrow);
    if (vertical) {
        const qreal x = 4;
        painter.drawLine(QPointF(x, 2.5), QPointF(x, 13.5));
        painter.drawLine(QPointF(x, 2.5), QPointF(x - 2, 5));
        painter.drawLine(QPointF(x, 2.5), QPointF(x + 2, 5));
        painter.drawLine(QPointF(x, 13.5), QPointF(x - 2, 11));
        painter.drawLine(QPointF(x, 13.5), QPointF(x + 2, 11));
    } else {
        const qreal y = 11;
        painter.drawLine(QPointF(2.5, y), QPointF(13.5, y));
        painter.drawLine(QPointF(2.5, y), QPointF(5, y - 2));
        painter.drawLine(QPointF(2.5, y), QPointF(5, y + 2));
        painter.drawLine(QPointF(13.5, y), QPointF(11, y - 2));
        painter.drawLine(QPointF(13.5, y), QPointF(11, y + 2));
    }
    // The shape the dimension belongs to.
    painter.setPen(QPen(QColor(QStringLiteral("#7f7f7f")), 1.0));
    painter.drawRect(vertical ? QRectF(9, 4, 4, 8) : QRectF(3, 2, 10, 3.5));
    return pixmap;
}

QPixmap stylePixmap(const ShapeStyle &style, const QSize &size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box(1, 1, size.width() - 2, size.height() - 2);
    QPen pen(style.stroke);
    pen.setWidthF(1.2);
    painter.setPen(pen);
    painter.setBrush(style.filled ? QBrush(style.fill) : QBrush(Qt::NoBrush));
    painter.drawRoundedRect(box, 2, 2);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(qMax(9, size.height() / 2));
    painter.setFont(font);
    painter.setPen(style.filled && style.fill.lightnessF() < 0.5 ? QColor(Qt::white) : style.stroke);
    painter.drawText(box, Qt::AlignCenter, QStringLiteral("Abc"));
    return pixmap;
}
}

KPrFormatTab::KPrFormatTab(KPrView *view, QWidget *page)
    : QObject(page)
    , m_view(view)
    , m_page(page)
{
    if (!m_view || !m_page) {
        return;
    }
    auto *layout = qobject_cast<QHBoxLayout *>(m_page->layout());
    if (!layout) {
        layout = new QHBoxLayout(m_page);
        layout->setContentsMargins(4, 2, 4, 2);
        layout->setSpacing(0);
        layout->setAlignment(Qt::AlignLeft);
    }
    layout->insertWidget(0, createShapeStylesGroup());
    layout->insertWidget(0, createInsertShapesGroup());
    layout->addWidget(createSizeGroup());
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    if (KoPACanvasBase *canvas = m_view->kopaCanvas()) {
        KoSelection *selection = canvas->shapeManager()->selection();
        connect(selection, &KoSelection::selectionChanged, this, &KPrFormatTab::updateSizeFields);
        connect(canvas->shapeManager(), &KoShapeManager::selectionContentChanged,
                this, &KPrFormatTab::updateSizeFields);
    }
    updateSizeFields();
}

QWidget *KPrFormatTab::createInsertShapesGroup()
{
    auto *group = new QWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    group->setProperty("ribbonGroupOrder", 5);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(3, 0, 3, 0);
    vertical->setSpacing(0);
    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    auto *grid = new QWidget(group);
    auto *gridLayout = new QGridLayout(grid);
    gridLayout->setContentsMargins(0, 0, 0, 0);
    gridLayout->setSpacing(1);
    int index = 0;
    for (const PresetShape &shape : presetShapes) {
        auto *button = new QToolButton(grid);
        button->setAutoRaise(true);
        button->setIcon(QIcon(presetPixmap(QString::fromLatin1(shape.preset), QSize(30, 30))));
        button->setIconSize(QSize(30, 30));
        button->setFixedSize(36, 36);
        button->setToolTip(QCoreApplication::translate("KPrFormatTab", shape.name));
        const QString preset = QString::fromLatin1(shape.preset);
        connect(button, &QToolButton::clicked, this, [this, preset] { insertShape(preset); });
        gridLayout->addWidget(button, index / 6, index % 6);
        ++index;
    }
    row->addWidget(grid, 0, Qt::AlignTop);
    // The complete gallery stays one click away.
    if (QAction *shapes = m_view->actionCollection()->action(QStringLiteral("add_shape"))) {
        auto *more = new QToolButton(group);
        more->setAutoRaise(true);
        more->setDefaultAction(shapes);
        more->setMenu(shapes->menu());
        more->setPopupMode(QToolButton::InstantPopup);
        if (more->icon().isNull()) {
            more->setIcon(KoRibbonIcons::forCommand(QStringLiteral("category")));
        }
        more->setToolTip(i18n("More shapes"));
        more->setIconSize(QSize(26, 26));
        more->setFixedSize(36, 73);
        row->addWidget(more, 0, Qt::AlignTop);
    }
    vertical->addLayout(row);
    vertical->addStretch();
    auto *caption = new QLabel(i18n("Insert Shapes"), group);
    caption->setProperty("ribbonGroupTitle", true);
    caption->setAlignment(Qt::AlignCenter);
    caption->setMinimumHeight(15);
    vertical->addWidget(caption);
    return group;
}

QWidget *KPrFormatTab::createShapeStylesGroup()
{
    auto *group = new QWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    group->setProperty("ribbonGroupOrder", 10);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(3, 0, 3, 0);
    vertical->setSpacing(0);
    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    for (const ShapeStyle &style : shapeStyles) {
        auto *button = new QToolButton(group);
        button->setAutoRaise(true);
        button->setIcon(QIcon(stylePixmap(style, QSize(44, 48))));
        button->setIconSize(QSize(44, 48));
        button->setFixedSize(50, 73);
        button->setToolTip(QCoreApplication::translate("KPrFormatTab", style.name));
        const QColor fill = style.fill;
        const QColor stroke = style.stroke;
        const bool filled = style.filled;
        connect(button, &QToolButton::clicked, this, [this, fill, stroke, filled] { applyStyle(fill, stroke, filled); });
        row->addWidget(button);
    }
    vertical->addLayout(row);
    vertical->addStretch();
    auto *caption = new QLabel(i18n("Shape Styles"), group);
    caption->setProperty("ribbonGroupTitle", true);
    caption->setAlignment(Qt::AlignCenter);
    caption->setMinimumHeight(15);
    vertical->addWidget(caption);
    return group;
}

// Height and width of the selection, like the Size group of the PowerPoint
// Format tab. The fields resize the selection around its top left corner.
QWidget *KPrFormatTab::createSizeGroup()
{
    auto *group = new QWidget(m_page);
    group->setProperty("ribbonRoutedGroup", true);
    group->setProperty("ribbonGroupOrder", 40);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(3, 0, 3, 0);
    vertical->setSpacing(0);
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(2);
    grid->setVerticalSpacing(2);

    auto configureBox = [this](KoUnitDoubleSpinBox *box) {
        box->setDecimals(2);
        box->setMinimum(0.0);
        box->setMaximum(10000.0);
        box->setFixedWidth(74);
        box->setFixedHeight(21);
        box->setEnabled(false);
        if (KoPACanvasBase *canvas = m_view->kopaCanvas()) {
            box->setUnit(canvas->unit());
        }
        connect(box, &QAbstractSpinBox::editingFinished, this, &KPrFormatTab::applySize);
    };

    auto *heightIcon = new QLabel(group);
    heightIcon->setPixmap(sizeIconPixmap(true));
    m_heightBox = new KoUnitDoubleSpinBox(group);
    configureBox(m_heightBox);
    grid->addWidget(heightIcon, 0, 0);
    grid->addWidget(m_heightBox, 0, 1);

    auto *widthIcon = new QLabel(group);
    widthIcon->setPixmap(sizeIconPixmap(false));
    m_widthBox = new KoUnitDoubleSpinBox(group);
    configureBox(m_widthBox);
    grid->addWidget(widthIcon, 1, 0);
    grid->addWidget(m_widthBox, 1, 1);

    vertical->addLayout(grid);
    vertical->addStretch();
    auto *caption = new QLabel(i18n("Size"), group);
    caption->setProperty("ribbonGroupTitle", true);
    caption->setAlignment(Qt::AlignCenter);
    caption->setMinimumHeight(15);
    vertical->addWidget(caption);
    return group;
}

void KPrFormatTab::updateSizeFields()
{
    if (!m_heightBox || !m_widthBox) {
        return;
    }
    KoPACanvasBase *canvas = m_view ? m_view->kopaCanvas() : nullptr;
    KoSelection *selection = canvas ? canvas->shapeManager()->selection() : nullptr;
    const bool hasSelection = selection && selection->count() > 0;
    m_heightBox->setEnabled(hasSelection);
    m_widthBox->setEnabled(hasSelection);
    if (m_updatingSize) {
        return;
    }
    QScopedValueRollback<bool> updating(m_updatingSize, true);
    const QSizeF size = hasSelection ? selection->boundingRect().size() : QSizeF();
    m_heightBox->changeValue(size.height());
    m_widthBox->changeValue(size.width());
}

void KPrFormatTab::applySize()
{
    if (m_updatingSize) {
        return;
    }
    KoPACanvasBase *canvas = m_view ? m_view->kopaCanvas() : nullptr;
    KoSelection *selection = canvas ? canvas->shapeManager()->selection() : nullptr;
    if (!selection || selection->count() == 0) {
        return;
    }
    const QRectF rect = selection->boundingRect();
    const qreal width = m_widthBox->value();
    const qreal height = m_heightBox->value();
    if (width <= 0.0 || height <= 0.0
        || (qFuzzyCompare(width, rect.width()) && qFuzzyCompare(height, rect.height()))) {
        updateSizeFields();
        return;
    }
    QTransform resize;
    resize.translate(rect.left(), rect.top());
    resize.scale(rect.width() > 0.0 ? width / rect.width() : 1.0,
                 rect.height() > 0.0 ? height / rect.height() : 1.0);
    resize.translate(-rect.left(), -rect.top());

    const QList<KoShape *> shapes = selection->selectedShapes(KoFlake::TopLevelSelection);
    QVector<QTransform> oldTransforms;
    QVector<QTransform> newTransforms;
    oldTransforms.reserve(shapes.size());
    newTransforms.reserve(shapes.size());
    for (KoShape *shape : shapes) {
        oldTransforms.append(shape->transformation());
        shape->update();
        shape->applyAbsoluteTransformation(resize);
        shape->update();
        newTransforms.append(shape->transformation());
    }
    auto *command = new KoShapeTransformCommand(shapes, oldTransforms, newTransforms);
    command->setText(kundo2_i18n("Resize"));
    canvas->addCommand(command);
    canvas->canvasWidget()->update();
    updateSizeFields();
}

void KPrFormatTab::insertShape(const QString &preset)
{
    KoPACanvasBase *canvas = m_view ? m_view->kopaCanvas() : nullptr;
    if (!canvas) {
        return;
    }
    auto *properties = new KoProperties;
    properties->setProperty(QStringLiteral("officePreset"), preset);
    m_presetProperties.append(properties);
    // Create the shape with the same tool the Shapes gallery uses, so the
    // click on the slide places it in the page layer like any other shape.
    if (KoCreateShapesTool *tool = KoToolManager::instance()->shapeCreatorTool(canvas)) {
        tool->setShapeId(QStringLiteral("OfficeShape"));
        tool->setShapeProperties(properties);
        KoToolManager::instance()->switchToolRequested(KoCreateShapesTool_ID);
        canvas->canvasWidget()->setFocus(Qt::OtherFocusReason);
    }
}

void KPrFormatTab::applyStyle(const QColor &fill, const QColor &stroke, bool filled)
{
    KoPACanvasBase *canvas = m_view ? m_view->kopaCanvas() : nullptr;
    if (!canvas) {
        return;
    }
    const QList<KoShape *> shapes = canvas->shapeManager()->selection()->selectedShapes();
    if (shapes.isEmpty()) {
        return;
    }
    auto *macro = new KUndo2Command(kundo2_i18n("Change Shape Style"));
    if (filled) {
        QSharedPointer<KoShapeBackground> background(new KoColorBackground(fill));
        new KoShapeBackgroundCommand(shapes, background, macro);
    } else {
        new KoShapeBackgroundCommand(shapes, QSharedPointer<KoShapeBackground>(), macro);
    }
    QList<KoShapeStrokeModel *> strokes;
    for (KoShape *shape : shapes) {
        Q_UNUSED(shape);
        strokes.append(new KoShapeStroke(1.0, stroke));
    }
    new KoShapeStrokeCommand(shapes, strokes, macro);
    canvas->addCommand(macro);
    canvas->canvasWidget()->update();
}
