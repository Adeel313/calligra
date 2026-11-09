/* This file is part of the KDE project
 *
 * SPDX-FileCopyrightText: 2007, 2010 Inge Wallin <inge@lysator.liu.se>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

// Own
#include "ChartTool.h"

// Qt
#include <QAction>
#include <QBrush>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

// KF5
#include <KLocalizedString>

// Calligra
#include <KoCanvasBase.h>
#include <KoFlake.h>
#include <KoInteractionTool.h>
#include <KoPointerEvent.h>
#include <KoRibbonIcons.h>
#include <KoRibbonLayout.h>
#include <KoSelection.h>
#include <KoShapeManager.h>
#include <KoTextShapeData.h>
#include <KoViewConverter.h>

// KChart
#include <KChartAbstractCartesianDiagram>
#include <KChartCartesianAxis>
#include <KChartCartesianCoordinatePlane>
#include <KChartChart>
#include <KChartGridAttributes>
#include <KChartPieAttributes>
#include <KChartPosition>

// KoChart
#include "AxesConfigWidget.h"
#include "Axis.h"
#include "BubbleDataEditor.h"
#include "ChartDebug.h"
#include "ChartTableModel.h"
#include "ChartLayout.h"
#include "ChartProxyModel.h"
#include "DataSet.h"
#include "DataSetConfigWidget.h"
#include "KChartConvertions.h"
#include "Legend.h"
#include "LegendConfigWidget.h"
#include "PieConfigWidget.h"
#include "PieDataEditor.h"
#include "PlotArea.h"
#include "PlotAreaConfigWidget.h"
#include "ScatterDataEditor.h"
#include "RadarDataSetConfigWidget.h"
#include "RingConfigWidget.h"
#include "StockDataEditor.h"
#include "Surface.h"
#include "TableEditorDialog.h"
#include "TitlesConfigWidget.h"
#include "commands/AddRemoveAxisCommand.h"
#include "commands/AxisCommand.h"
#include "commands/ChartTextShapeCommand.h"
#include "commands/ChartTypeCommand.h"
#include "commands/DatasetCommand.h"
#include "commands/GapCommand.h"
#include "commands/LegendCommand.h"
#include "commands/PlotAreaCommand.h"

using namespace KoChart;

class ChartTool::Private
{
public:
    Private();
    ~Private();

    ChartShape *shape;
    QModelIndex datasetSelection;
    QPen datasetSelectionPen;
    QBrush datasetSelectionBrush;
};

ChartTool::Private::Private()
    : shape(nullptr)
{
}

ChartTool::Private::~Private() = default;

ChartTool::ChartTool(KoCanvasBase *canvas)
    : KoToolBase(canvas)
    , d(new Private())
{
    // Create QActions here.
#if 0
    QActionGroup *group = new QActionGroup(this);
    m_foo  = new QAction(koIcon("this-action"), i18n("Do something"), this);
    m_foo->setCheckable(true);
    group->addAction(m_foo);
    connect(m_foo, SIGNAL(toggled(bool)), this, SLOT(catchFoo(bool)));

    m_bar  = new QAction(koIcon("that-action"), i18n("Do something else"), this);
    m_bar->setCheckable(true);
    group->addAction(m_bar);
    connect(m_foo, SIGNAL(toggled(bool)), this, SLOT(catchBar(bool)));

#endif
    connect(canvas->shapeManager(), &KoShapeManager::selectionChanged, this, &ChartTool::shapeSelectionChanged);
}

ChartTool::~ChartTool()
{
    delete d;
}

void ChartTool::shapeSelectionChanged()
{
    // When this chart tool is activated with one chart and a new chart is created,
    // the new chart is selected but the deactivate method is not called,
    // so this tool will still operate on the old chart.
    // We activate the default tool to rectify this.
    // FIXME: could probably be done in KoToolBase (or wherever appropriate)

    if (!d->shape) {
        return;
    }
    QList<KoShape *> lst = canvas()->shapeManager()->selection()->selectedShapes(KoFlake::StrippedSelection);
    if (lst.contains(d->shape)) {
        return;
    }
    for (KoShape *s : lst) {
        ChartShape *chart = dynamic_cast<ChartShape *>(s);
        if (chart && chart != d->shape) {
            activateTool(KoInteractionTool_ID);
        }
    }
}

void ChartTool::paint(QPainter &painter, const KoViewConverter &converter)
{
    if (d->shape) {
        QPen pen;
        // Use the #00adf5 color with 50% opacity
        pen.setColor(QColor(0, 173, 245, 127));
        pen.setWidth(qMax((uint)1, handleRadius() / 2));
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);

        QTransform painterMatrix = painter.worldTransform();
        painter.setWorldTransform(d->shape->absoluteTransformation(&converter) * painterMatrix);
        KoShape::applyConversion(painter, converter);
        painter.drawRect(QRectF(QPointF(), d->shape->size()));
    }
}

void ChartTool::mousePressEvent(KoPointerEvent *event)
{
#if 1 // disabled
    Q_UNUSED(event);
    return;
#else
    // Select dataset
    if (!d->shape || !d->shape->kdChart() || !d->shape->kdChart()->coordinatePlane() || !d->shape->kdChart()->coordinatePlane()->diagram())
        return;
    QPointF point = event->point - d->shape->position();
    QModelIndex selection = d->shape->kdChart()->coordinatePlane()->diagram()->indexAt(point.toPoint());
    // Note: the dataset will always stay column() due to the transformations being
    // done internally by the ChartProxyModel
    int dataset = selection.column();

    if (d->datasetSelection.isValid()) {
        d->shape->kdChart()->coordinatePlane()->diagram()->setPen(d->datasetSelection.column(), d->datasetSelectionPen);
        // d->shape->kdChart()->coordinatePlane()->diagram()->setBrush(d->datasetSelection, d->datasetSelectionBrush);
    }
    if (selection.isValid()) {
        d->datasetSelection = selection;

        QPen pen(Qt::DotLine);
        pen.setColor(Qt::darkGray);
        pen.setWidth(1);

        d->datasetSelectionBrush = d->shape->kdChart()->coordinatePlane()->diagram()->brush(selection);
        d->datasetSelectionPen = d->shape->kdChart()->coordinatePlane()->diagram()->pen(dataset);

        d->shape->kdChart()->coordinatePlane()->diagram()->setPen(dataset, pen);
        // d->shape->kdChart()->coordinatePlane()->diagram()->setBrush(selection, QBrush(Qt::lightGray));
    }
    ((ChartConfigWidget *)optionWidget())->selectDataset(dataset);

    d->shape->update();
#endif
}

void ChartTool::mouseMoveEvent(KoPointerEvent *event)
{
    event->ignore();
}

void ChartTool::mouseReleaseEvent(KoPointerEvent *event)
{
    event->ignore();
}

void ChartTool::activate(ToolActivation, const QSet<KoShape *> &shapes)
{
    debugChartTool << shapes;
    d->shape = nullptr;
    for (KoShape *s : shapes) {
        d->shape = dynamic_cast<ChartShape *>(s);
        if (!d->shape) {
            for (KoShape *parent = s->parent(); parent; parent = parent->parent()) {
                d->shape = dynamic_cast<ChartShape *>(parent);
                if (d->shape) {
                    break;
                }
            }
        }
        if (d->shape) {
            break;
        }
    }
    debugChartTool << shapes << d->shape;
    if (!d->shape) {
        Q_EMIT done();
        return;
    }
    useCursor(Qt::ArrowCursor);

    foreach (QWidget *w, optionWidgets()) {
        ConfigWidgetBase *widget = dynamic_cast<ConfigWidgetBase *>(w);
        Q_ASSERT(widget);
        if (widget) {
            widget->open(d->shape);
        }
    }
    foreach (QWidget *w, optionWidgets()) {
        ConfigWidgetBase *widget = dynamic_cast<ConfigWidgetBase *>(w);
        Q_ASSERT(widget);
        if (widget) {
            widget->updateData();
        }
    }
    d->shape->update(); // to paint decoration
}

void ChartTool::deactivate()
{
    debugChartTool << d->shape;
    if (!d->shape) {
        // activated without shape
        return;
    }
    foreach (QWidget *w, optionWidgets()) {
        ConfigWidgetBase *configWidget = dynamic_cast<ConfigWidgetBase *>(w);
        if (configWidget)
            configWidget->deactivate();
    }
    if (d->shape) {
        d->shape->update(); // to get rid of decoration
    }
    d->shape = nullptr;
}

QList<QPointer<QWidget>> ChartTool::createOptionWidgets()
{
    QList<QPointer<QWidget>> widgets;

    TitlesConfigWidget *titles = new TitlesConfigWidget();
    titles->setWindowTitle(i18n("Titles"));
    widgets.append(titles);
    connect(titles->ui.showTitle, &QAbstractButton::toggled, this, &ChartTool::setShowTitle);
    connect(titles->ui.titlePositioning, &QComboBox::currentIndexChanged, this, &ChartTool::setTitlePositioning);
    connect(titles->ui.titleResize, &QComboBox::currentIndexChanged, this, &ChartTool::setTitleResize);
    connect(titles->ui.showSubTitle, &QAbstractButton::toggled, this, &ChartTool::setShowSubTitle);
    connect(titles->ui.subtitlePositioning, &QComboBox::currentIndexChanged, this, &ChartTool::setSubTitlePositioning);
    connect(titles->ui.subtitleResize, &QComboBox::currentIndexChanged, this, &ChartTool::setSubTitleResize);
    connect(titles->ui.showFooter, &QAbstractButton::toggled, this, &ChartTool::setShowFooter);
    connect(titles->ui.footerPositioning, &QComboBox::currentIndexChanged, this, &ChartTool::setFooterPositioning);
    connect(titles->ui.footerResize, &QComboBox::currentIndexChanged, this, &ChartTool::setFooterResize);

    // The tool may be created before it is activated on a chart, and then the
    // panels are opened by activate(). Do not connect a chart that is not there.
    if (d->shape) {
        connect(d->shape, &KoChart::ChartShape::updateConfigWidget, titles, QOverload<>::of(&TitlesConfigWidget::updateData));
    }

    LegendConfigWidget *legend = new LegendConfigWidget();
    legend->setWindowTitle(i18n("Legend"));
    widgets.append(legend);
    connect(legend, &LegendConfigWidget::showLegendChanged, this, &ChartTool::setShowLegend);
    connect(legend, &LegendConfigWidget::legendTitleChanged, this, &ChartTool::setLegendTitle);
    connect(legend, &LegendConfigWidget::legendFontChanged, this, &ChartTool::setLegendFont);
    connect(legend, &LegendConfigWidget::legendFontSizeChanged, this, &ChartTool::setLegendFontSize);
    connect(legend, &LegendConfigWidget::legendOrientationChanged, this, &ChartTool::setLegendOrientation);
    connect(legend, &LegendConfigWidget::legendPositionChanged, this, &ChartTool::setLegendPosition);
    connect(legend, &LegendConfigWidget::legendAlignmentChanged, this, &ChartTool::setLegendAlignment);

    if (d->shape && d->shape->legend()) {
        connect(d->shape->legend(), &KoChart::Legend::updateConfigWidget, legend, QOverload<>::of(&LegendConfigWidget::updateData));
    }

    PlotAreaConfigWidget *plotarea = new PlotAreaConfigWidget();
    plotarea->setWindowTitle(i18n("Plot Area"));
    widgets.append(plotarea);

    connect(plotarea, &PlotAreaConfigWidget::chartTypeChanged, this, &ChartTool::setChartType);
    connect(plotarea, &PlotAreaConfigWidget::chartSubTypeChanged, this, &ChartTool::setChartSubType);
    connect(plotarea, &PlotAreaConfigWidget::threeDModeToggled, this, &ChartTool::setThreeDMode);
    connect(plotarea, &PlotAreaConfigWidget::chartOrientationChanged, this, &ChartTool::setChartOrientation);

    // data set edit dialog
    connect(plotarea, &PlotAreaConfigWidget::dataSetXDataRegionChanged, this, &ChartTool::setDataSetXDataRegion);
    connect(plotarea, &PlotAreaConfigWidget::dataSetYDataRegionChanged, this, &ChartTool::setDataSetYDataRegion);
    connect(plotarea, &PlotAreaConfigWidget::dataSetCustomDataRegionChanged, this, &ChartTool::setDataSetCustomDataRegion);
    connect(plotarea, &PlotAreaConfigWidget::dataSetLabelDataRegionChanged, this, &ChartTool::setDataSetLabelDataRegion);
    connect(plotarea, &PlotAreaConfigWidget::dataSetCategoryDataRegionChanged, this, &ChartTool::setDataSetCategoryDataRegion);

    AxesConfigWidget *axes = plotarea->cartesianAxesConfigWidget();
    connect(axes, &AxesConfigWidget::axisAdded, this, &ChartTool::addAxis);
    connect(axes, &AxesConfigWidget::axisRemoved, this, &ChartTool::removeAxis);
    connect(axes, &AxesConfigWidget::axisShowTitleChanged, this, &ChartTool::setAxisShowTitle);
    connect(axes, &AxesConfigWidget::axisShowChanged, this, &ChartTool::setShowAxis);
    connect(axes, &AxesConfigWidget::axisPositionChanged, this, &ChartTool::setAxisPosition);
    connect(axes, &AxesConfigWidget::axisLabelsPositionChanged, this, &ChartTool::setAxisLabelsPosition);
    connect(axes, &AxesConfigWidget::axisShowLabelsChanged, this, &ChartTool::setAxisShowLabels);

    connect(axes, &AxesConfigWidget::axisShowMajorGridLinesChanged, this, &ChartTool::setAxisShowMajorGridLines);
    connect(axes, &AxesConfigWidget::axisShowMinorGridLinesChanged, this, &ChartTool::setAxisShowMinorGridLines);
    // scaling dialog
    connect(axes, &AxesConfigWidget::axisUseLogarithmicScalingChanged, this, &ChartTool::setAxisUseLogarithmicScaling);
    connect(axes, &AxesConfigWidget::axisStepWidthChanged, this, &ChartTool::setAxisStepWidth);
    connect(axes, &AxesConfigWidget::axisSubStepWidthChanged, this, &ChartTool::setAxisSubStepWidth);
    connect(axes, &AxesConfigWidget::axisUseAutomaticStepWidthChanged, this, &ChartTool::setAxisUseAutomaticStepWidth);
    connect(axes, &AxesConfigWidget::axisUseAutomaticSubStepWidthChanged, this, &ChartTool::setAxisUseAutomaticSubStepWidth);
    // font dialog
    connect(axes, &AxesConfigWidget::axisLabelsFontChanged, this, &ChartTool::setAxisLabelsFont);

    connect(axes, &AxesConfigWidget::gapBetweenBarsChanged, this, &ChartTool::setGapBetweenBars);
    connect(axes, &AxesConfigWidget::gapBetweenSetsChanged, this, &ChartTool::setGapBetweenSets);

    DataSetConfigWidget *dataset = plotarea->cartesianDataSetConfigWidget();
    connect(dataset, &DataSetConfigWidget::dataSetChartTypeChanged, this, &ChartTool::setDataSetChartType);
    connect(dataset, &DataSetConfigWidget::datasetBrushChanged, this, &ChartTool::setDataSetBrush);
    connect(dataset, &DataSetConfigWidget::dataSetMarkerChanged, this, &ChartTool::setDataSetMarker);
    connect(dataset, &DataSetConfigWidget::datasetPenChanged, this, &ChartTool::setDataSetPen);
    connect(dataset, &DataSetConfigWidget::datasetShowCategoryChanged, this, &ChartTool::setDataSetShowCategory);
    connect(dataset, &DataSetConfigWidget::dataSetShowNumberChanged, this, &ChartTool::setDataSetShowNumber);
    connect(dataset, &DataSetConfigWidget::datasetShowPercentChanged, this, &ChartTool::setDataSetShowPercent);
    connect(dataset, &DataSetConfigWidget::datasetShowSymbolChanged, this, &ChartTool::setDataSetShowSymbol);
    connect(dataset, &DataSetConfigWidget::dataSetAxisChanged, this, &ChartTool::setDataSetAxis);
    connect(dataset, &DataSetConfigWidget::axisAdded, this, &ChartTool::addAxis);

    PieConfigWidget *pie = plotarea->pieConfigWidget();
    connect(pie, QOverload<DataSet *, int, int>::of(&PieConfigWidget::explodeFactorChanged), this, &ChartTool::setPieExplodeFactor);
    connect(pie, QOverload<DataSet *, const QColor &, int>::of(&PieConfigWidget::brushChanged), this, &ChartTool::setDataSetBrush);
    connect(pie, QOverload<DataSet *, const QColor &, int>::of(&PieConfigWidget::penChanged), this, &ChartTool::setDataSetPen);
    connect(pie, QOverload<DataSet *, bool, int>::of(&PieConfigWidget::showCategoryChanged), this, &ChartTool::setDataSetShowCategory);
    connect(pie, QOverload<DataSet *, bool, int>::of(&PieConfigWidget::showNumberChanged), this, &ChartTool::setDataSetShowNumber);
    connect(pie, QOverload<DataSet *, bool, int>::of(&PieConfigWidget::showPercentChanged), this, &ChartTool::setDataSetShowPercent);

    RingConfigWidget *ring = plotarea->ringConfigWidget();
    connect(ring, QOverload<DataSet *, int, int>::of(&RingConfigWidget::explodeFactorChanged), this, &ChartTool::setPieExplodeFactor);
    connect(ring, QOverload<DataSet *, const QColor &, int>::of(&RingConfigWidget::brushChanged), this, &ChartTool::setDataSetBrush);
    connect(ring, QOverload<DataSet *, const QColor &, int>::of(&RingConfigWidget::penChanged), this, &ChartTool::setDataSetPen);
    connect(ring, QOverload<DataSet *, bool, int>::of(&RingConfigWidget::showCategoryChanged), this, &ChartTool::setDataSetShowCategory);
    connect(ring, QOverload<DataSet *, bool, int>::of(&RingConfigWidget::showNumberChanged), this, &ChartTool::setDataSetShowNumber);
    connect(ring, QOverload<DataSet *, bool, int>::of(&RingConfigWidget::showPercentChanged), this, &ChartTool::setDataSetShowPercent);

    axes = plotarea->stockAxesConfigWidget();
    connect(axes, &AxesConfigWidget::axisAdded, this, &ChartTool::addAxis);
    connect(axes, &AxesConfigWidget::axisRemoved, this, &ChartTool::removeAxis);
    connect(axes, &AxesConfigWidget::axisShowTitleChanged, this, &ChartTool::setAxisShowTitle);
    connect(axes, &AxesConfigWidget::axisShowChanged, this, &ChartTool::setShowAxis);
    connect(axes, &AxesConfigWidget::axisPositionChanged, this, &ChartTool::setAxisPosition);
    connect(axes, &AxesConfigWidget::axisLabelsPositionChanged, this, &ChartTool::setAxisLabelsPosition);
    connect(axes, &AxesConfigWidget::axisShowLabelsChanged, this, &ChartTool::setAxisShowLabels);

    connect(axes, &AxesConfigWidget::axisShowMajorGridLinesChanged, this, &ChartTool::setAxisShowMajorGridLines);
    connect(axes, &AxesConfigWidget::axisShowMinorGridLinesChanged, this, &ChartTool::setAxisShowMinorGridLines);
    // scaling dialog
    connect(axes, &AxesConfigWidget::axisUseLogarithmicScalingChanged, this, &ChartTool::setAxisUseLogarithmicScaling);
    connect(axes, &AxesConfigWidget::axisStepWidthChanged, this, &ChartTool::setAxisStepWidth);
    connect(axes, &AxesConfigWidget::axisSubStepWidthChanged, this, &ChartTool::setAxisSubStepWidth);
    connect(axes, &AxesConfigWidget::axisUseAutomaticStepWidthChanged, this, &ChartTool::setAxisUseAutomaticStepWidth);
    connect(axes, &AxesConfigWidget::axisUseAutomaticSubStepWidthChanged, this, &ChartTool::setAxisUseAutomaticSubStepWidth);
    // font dialog
    connect(axes, &AxesConfigWidget::axisLabelsFontChanged, this, &ChartTool::setAxisLabelsFont);

    // Radar
    RadarDataSetConfigWidget *rdataset = plotarea->radarDataSetConfigWidget();
    connect(rdataset, &RadarDataSetConfigWidget::datasetBrushChanged, this, &ChartTool::setDataSetBrush);
    connect(rdataset, &RadarDataSetConfigWidget::dataSetMarkerChanged, this, &ChartTool::setDataSetMarker);
    connect(rdataset, &RadarDataSetConfigWidget::datasetPenChanged, this, &ChartTool::setDataSetPen);
    connect(rdataset, &RadarDataSetConfigWidget::datasetShowCategoryChanged, this, &ChartTool::setDataSetShowCategory);
    connect(rdataset, &RadarDataSetConfigWidget::dataSetShowNumberChanged, this, &ChartTool::setDataSetShowNumber);
    connect(rdataset, &RadarDataSetConfigWidget::datasetShowPercentChanged, this, &ChartTool::setDataSetShowPercent);
    connect(rdataset, &RadarDataSetConfigWidget::datasetShowSymbolChanged, this, &ChartTool::setDataSetShowSymbol);

    if (d->shape) {
        connect(d->shape, &ChartShape::updateConfigWidget, plotarea, QOverload<>::of(&PlotAreaConfigWidget::updateData));
    }

    return widgets;
}

// ================================================================
//          The Chart Tools "Design" tab of the Stage ribbon

namespace
{
struct ColorScheme {
    const char *name;
    const char *colors[6];
};

// The palettes of the Change Colors gallery.
const ColorScheme colorSchemes[] = {
    {QT_TRANSLATE_NOOP("ChartTool", "Colorful"), {"#4472c4", "#ed7d31", "#a5a5a5", "#ffc000", "#5b9bd5", "#70ad47"}},
    {QT_TRANSLATE_NOOP("ChartTool", "Blue"), {"#1f3864", "#2f5597", "#4472c4", "#8faadc", "#b4c7e7", "#d9e2f3"}},
    {QT_TRANSLATE_NOOP("ChartTool", "Orange"), {"#843c0c", "#c55a11", "#ed7d31", "#f4b183", "#f8cbad", "#fbe5d6"}},
    {QT_TRANSLATE_NOOP("ChartTool", "Green"), {"#375623", "#548235", "#70ad47", "#a9d08e", "#c6e0b4", "#e2efda"}},
    {QT_TRANSLATE_NOOP("ChartTool", "Gray"), {"#262626", "#595959", "#808080", "#a6a6a6", "#bfbfbf", "#d9d9d9"}},
    {QT_TRANSLATE_NOOP("ChartTool", "Dark"), {"#1f2430", "#2e3b4e", "#4472c4", "#7f9fc4", "#b4c7e7", "#e8eef7"}},
};

int colorSchemeCount()
{
    return int(sizeof(colorSchemes) / sizeof(colorSchemes[0]));
}

QList<QColor> colorSchemeColors(int scheme)
{
    QList<QColor> colors;
    const ColorScheme &entry = colorSchemes[qBound(0, scheme, colorSchemeCount() - 1)];
    for (const char *color : entry.colors) {
        colors.append(QColor(QLatin1String(color)));
    }
    return colors;
}

// The styles of the Chart Styles gallery: a palette together with a layout.
struct StylePreset {
    int scheme;
    bool gridLines;
    bool valueLabels;
    bool legend;
    Position legendPosition;
};

const StylePreset stylePresets[] = {
    {0, true, false, true, EndPosition},
    {0, false, true, true, BottomPosition},
    {1, true, false, true, BottomPosition},
    {3, true, false, true, EndPosition},
    {5, false, true, true, EndPosition},
    {2, true, true, false, BottomPosition},
};

int stylePresetCount()
{
    return int(sizeof(stylePresets) / sizeof(stylePresets[0]));
}

// The previews of the chart style gallery: bars in the colors of the style.
QPixmap chartPreview(const QList<QColor> &colors, bool gridLines, bool valueLabels, const QSize &size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::white);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF plot(3.0, 5.0, size.width() - 6.0, size.height() - 9.0);
    if (gridLines) {
        painter.setPen(QPen(QColor(0xd6, 0xdc, 0xe5), 0.7));
        for (int i = 1; i < 4; ++i) {
            const qreal y = plot.top() + plot.height() * i / 4.0;
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }
    }
    const int bars = qMin(3, int(colors.count()));
    const qreal barWidth = plot.width() / (bars * 2.0 + 1.0);
    for (int i = 0; i < bars; ++i) {
        const qreal height = plot.height() * (0.9 - 0.24 * i);
        const QRectF bar(plot.left() + barWidth * (1.0 + 2.0 * i), plot.bottom() - height, barWidth, height);
        painter.fillRect(bar, colors.at(i));
        if (valueLabels) {
            painter.setPen(QColor(0x44, 0x4a, 0x55));
            painter.drawText(bar.adjusted(0, -8, 0, -1), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("1"));
        }
    }
    painter.setPen(QPen(QColor(0x9a, 0xa2, 0xad), 0.7));
    painter.drawLine(QPointF(plot.left(), plot.bottom()), QPointF(plot.right(), plot.bottom()));
    return pixmap;
}

// The preview of a color scheme: the colors side by side.
QPixmap colorPreview(const QList<QColor> &colors, const QSize &size)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    const qreal width = qreal(size.width()) / qMax(1, int(colors.count()));
    for (int i = 0; i < colors.count(); ++i) {
        painter.fillRect(QRectF(i * width, 0, width + 0.5, size.height()), colors.at(i));
    }
    return pixmap;
}

// A labelled ribbon group of Stage: its commands above the caption.
QWidget *ribbonGroup(QWidget *page, const QString &title, int order, QWidget **content)
{
    auto *group = new QWidget(page);
    group->setProperty("ribbonRoutedGroup", true);
    group->setProperty("ribbonGroupOrder", order);
    auto *vertical = new QVBoxLayout(group);
    vertical->setContentsMargins(3, 0, 3, 0);
    vertical->setSpacing(0);
    auto *row = new QWidget(group);
    vertical->addWidget(row, 0, Qt::AlignTop);
    vertical->addStretch();
    auto *caption = new QLabel(title, group);
    caption->setProperty("ribbonGroupTitle", true);
    caption->setAlignment(Qt::AlignCenter);
    caption->setMinimumHeight(15);
    vertical->addWidget(caption);
    *content = row;
    return group;
}

QToolButton *ribbonButton(QWidget *parent, const QString &text, const QString &iconName)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(text);
    button->setIcon(KoRibbonIcons::forThemeIcon(iconName));
    KoRibbonLayout::configureButton(button, Qt::ToolButtonTextUnderIcon);
    return button;
}

QToolButton *ribbonMenuButton(QWidget *parent, const QString &text, const QString &iconName, QMenu *menu)
{
    QToolButton *button = ribbonButton(parent, text, iconName);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
    return button;
}

QHBoxLayout *ribbonRow(QWidget *content)
{
    auto *layout = new QHBoxLayout(content);
    layout->setContentsMargins(2, 1, 2, 1);
    layout->setSpacing(4);
    layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    return layout;
}
}

void ChartTool::configureRibbon(QWidget *page)
{
    if (!page) {
        return;
    }
    auto *layout = qobject_cast<QHBoxLayout *>(page->layout());
    if (!layout) {
        layout = new QHBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    }
    // Inserted right to left so the tab keeps the order of PowerPoint.
    layout->insertWidget(0, createTypeGroup(page));
    layout->insertWidget(0, createDataGroup(page));
    layout->insertWidget(0, createStylesGroup(page));
    layout->insertWidget(0, createLayoutsGroup(page));
}

ChartShape *ChartTool::currentChart() const
{
    if (d->shape) {
        return d->shape;
    }
    // Stage builds the tab before the first chart is activated, so fall back to
    // the chart in the canvas selection.
    if (!canvas() || !canvas()->shapeManager()) {
        return nullptr;
    }
    const QList<KoShape *> selected = canvas()->shapeManager()->selection()->selectedShapes(KoFlake::StrippedSelection);
    for (KoShape *shape : selected) {
        for (KoShape *candidate = shape; candidate; candidate = candidate->parent()) {
            if (auto *chart = dynamic_cast<ChartShape *>(candidate)) {
                return chart;
            }
        }
    }
    return nullptr;
}

QList<Axis *> ChartTool::chartAxes(ChartShape *chart) const
{
    if (!chart || !chart->plotArea()) {
        return {};
    }
    return chart->plotArea()->axes();
}

QWidget *ChartTool::createLayoutsGroup(QWidget *page)
{
    QWidget *content = nullptr;
    QWidget *group = ribbonGroup(page, i18n("Chart Layouts"), 10, &content);
    QHBoxLayout *layout = ribbonRow(content);
    layout->addWidget(ribbonMenuButton(content, i18n("Add Chart Element"), QStringLiteral("category"), createAddElementMenu(content)));
    layout->addWidget(ribbonMenuButton(content, i18n("Quick Layout"), QStringLiteral("view_column"), createQuickLayoutMenu(content)));
    return group;
}

QWidget *ChartTool::createStylesGroup(QWidget *page)
{
    QWidget *content = nullptr;
    QWidget *group = ribbonGroup(page, i18n("Chart Styles"), 20, &content);
    QHBoxLayout *layout = ribbonRow(content);
    layout->addWidget(ribbonMenuButton(content, i18n("Change Colors"), QStringLiteral("format_color_fill"), createColorSchemeMenu(content)));

    // The gallery of the group: the styles side by side, in two rows.
    auto *gallery = new QWidget(content);
    auto *grid = new QGridLayout(gallery);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    for (int style = 0; style < stylePresetCount(); ++style) {
        const StylePreset &preset = stylePresets[style];
        auto *button = new QToolButton(gallery);
        button->setAutoRaise(true);
        const QList<QColor> colors = colorSchemeColors(preset.scheme).mid(0, 3);
        button->setIcon(QIcon(chartPreview(colors, preset.gridLines, preset.valueLabels, QSize(34, 22))));
        button->setIconSize(QSize(34, 22));
        button->setFixedSize(36, 24);
        button->setToolTip(i18n("Chart Style %1", style + 1));
        connect(button, &QToolButton::clicked, this, [this, style] { applyStylePreset(style); });
        grid->addWidget(button, style / 3, style % 3);
    }
    layout->addWidget(gallery, 0, Qt::AlignTop);
    return group;
}

QWidget *ChartTool::createDataGroup(QWidget *page)
{
    QWidget *content = nullptr;
    QWidget *group = ribbonGroup(page, i18n("Data"), 30, &content);
    QHBoxLayout *layout = ribbonRow(content);

    QToolButton *switchButton = ribbonButton(content, i18n("Switch Row/Column"), QStringLiteral("splitscreen"));
    connect(switchButton, &QToolButton::clicked, this, [this] {
        ChartShape *chart = currentChart();
        if (!chart || !chart->proxyModel()) {
            return;
        }
        ChartProxyModel *model = chart->proxyModel();
        model->setDataDirection(model->dataDirection() == Qt::Horizontal ? Qt::Vertical : Qt::Horizontal);
        chart->relayout();
        chart->update();
    });
    layout->addWidget(switchButton);

    QToolButton *editButton = ribbonButton(content, i18n("Edit Data"), QStringLiteral("table_chart"));
    editButton->setToolTip(i18n("Edit the data of the chart"));
    connect(editButton, &QToolButton::clicked, this, &ChartTool::showDataEditor);
    layout->addWidget(editButton);

    QToolButton *refreshButton = ribbonButton(content, i18n("Refresh Data"), QStringLiteral("refresh"));
    connect(refreshButton, &QToolButton::clicked, this, [this] {
        ChartShape *chart = currentChart();
        if (!chart || !chart->proxyModel()) {
            return;
        }
        chart->proxyModel()->dataChanged();
        chart->relayout();
        chart->update();
    });
    layout->addWidget(refreshButton);
    return group;
}

QWidget *ChartTool::createTypeGroup(QWidget *page)
{
    QWidget *content = nullptr;
    QWidget *group = ribbonGroup(page, i18n("Type"), 40, &content);
    QHBoxLayout *layout = ribbonRow(content);

    layout->addWidget(ribbonMenuButton(content, i18n("Change Chart Type"), QStringLiteral("bar_chart"), createChartTypeMenu(content)));
    return group;
}

QMenu *ChartTool::createAddElementMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);

    QAction *title = menu->addAction(i18n("Chart Title"));
    title->setCheckable(true);
    connect(title, &QAction::triggered, this, [this](bool show) {
        if (ChartShape *chart = currentChart(); chart && chart->title()) {
            canvas()->addCommand(new ChartTextShapeCommand(chart->title(), chart, show));
        }
    });

    QAction *subTitle = menu->addAction(i18n("Chart Subtitle"));
    subTitle->setCheckable(true);
    connect(subTitle, &QAction::triggered, this, [this](bool show) {
        if (ChartShape *chart = currentChart(); chart && chart->subTitle()) {
            canvas()->addCommand(new ChartTextShapeCommand(chart->subTitle(), chart, show));
        }
    });

    QAction *footer = menu->addAction(i18n("Chart Footer"));
    footer->setCheckable(true);
    connect(footer, &QAction::triggered, this, [this](bool show) {
        if (ChartShape *chart = currentChart(); chart && chart->footer()) {
            canvas()->addCommand(new ChartTextShapeCommand(chart->footer(), chart, show));
        }
    });

    menu->addSeparator();

    QAction *legend = menu->addAction(i18n("Legend"));
    legend->setCheckable(true);
    connect(legend, &QAction::triggered, this, [this](bool show) {
        if (ChartShape *chart = currentChart(); chart && chart->legend()) {
            canvas()->addCommand(new ChartTextShapeCommand(chart->legend(), chart, show));
        }
    });

    QAction *axisTitles = menu->addAction(i18n("Axis Titles"));
    axisTitles->setCheckable(true);
    connect(axisTitles, &QAction::triggered, this, [this](bool show) {
        ChartShape *chart = currentChart();
        if (!chart) {
            return;
        }
        auto *macro = new KUndo2Command(kundo2_i18n(show ? "Show Axis Titles" : "Hide Axis Titles"));
        for (Axis *axis : chartAxes(chart)) {
            auto *command = new AxisCommand(axis, chart, macro);
            command->setAxisShowTitle(show);
        }
        canvas()->addCommand(macro);
    });

    QAction *labels = menu->addAction(i18n("Data Labels"));
    labels->setCheckable(true);
    connect(labels, &QAction::triggered, this, [this](bool show) { setDataLabels(show); });

    QAction *gridLines = menu->addAction(i18n("Major Gridlines"));
    gridLines->setCheckable(true);
    connect(gridLines, &QAction::triggered, this, [this](bool show) { setMajorGridLines(show); });

    // The chart changes while the tab stays: refresh the marks when it opens.
    connect(menu, &QMenu::aboutToShow, this, [this, title, subTitle, footer, legend, axisTitles, labels, gridLines] {
        const auto setChecked = [](QAction *action, bool checked) {
            const QSignalBlocker blocker(action);
            action->setChecked(checked);
        };
        ChartShape *chart = currentChart();
        if (!chart) {
            for (QAction *action : {title, subTitle, footer, legend, axisTitles, labels, gridLines}) {
                setChecked(action, false);
            }
            return;
        }
        setChecked(title, chart->title() && chart->title()->isVisible());
        setChecked(subTitle, chart->subTitle() && chart->subTitle()->isVisible());
        setChecked(footer, chart->footer() && chart->footer()->isVisible());
        setChecked(legend, chart->legend() && chart->legend()->isVisible());
        bool anyAxisTitle = false;
        bool anyGridLines = false;
        for (Axis *axis : chartAxes(chart)) {
            anyAxisTitle = anyAxisTitle || (axis->title() && axis->title()->isVisible());
            anyGridLines = anyGridLines || axis->showMajorGrid();
        }
        bool anyLabels = false;
        if (chart->plotArea()) {
            for (DataSet *dataSet : chart->plotArea()->dataSets()) {
                anyLabels = anyLabels || dataSet->valueLabelType().number;
            }
        }
        setChecked(axisTitles, anyAxisTitle);
        setChecked(labels, anyLabels);
        setChecked(gridLines, anyGridLines);
        axisTitles->setEnabled(isCartesian(chart->chartType()));
        gridLines->setEnabled(isCartesian(chart->chartType()));
    });
    return menu;
}

QMenu *ChartTool::createQuickLayoutMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);
    const char *const layouts[] = {
        QT_TRANSLATE_NOOP("ChartTool", "Title and Legend"),
        QT_TRANSLATE_NOOP("ChartTool", "Title and Data Labels"),
        QT_TRANSLATE_NOOP("ChartTool", "Title, Legend and Data Labels"),
        QT_TRANSLATE_NOOP("ChartTool", "Title Only"),
        QT_TRANSLATE_NOOP("ChartTool", "Chart Only"),
    };
    for (int layout = 0; layout < int(sizeof(layouts) / sizeof(layouts[0])); ++layout) {
        QAction *action = menu->addAction(i18n(layouts[layout]));
        connect(action, &QAction::triggered, this, [this, layout] { applyQuickLayout(layout); });
    }
    return menu;
}

QMenu *ChartTool::createColorSchemeMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);
    for (int scheme = 0; scheme < colorSchemeCount(); ++scheme) {
        const ColorScheme &entry = colorSchemes[scheme];
        QAction *action = menu->addAction(QIcon(colorPreview(colorSchemeColors(scheme), QSize(60, 12))), i18n(entry.name));
        connect(action, &QAction::triggered, this, [this, scheme] { applyColorScheme(scheme); });
    }
    return menu;
}

QMenu *ChartTool::createChartTypeMenu(QWidget *parent)
{
    auto *menu = new QMenu(parent);
    const auto addType = [this](QMenu *target, ChartType type, ChartSubtype subtype, const QString &label) {
        QAction *action = target->addAction(ICON2(type, subtype), label);
        connect(action, &QAction::triggered, this, [this, type, subtype] { changeChartType(type, subtype); });
    };

    QMenu *bar = menu->addMenu(ICON1(BarChartType), i18n("Bar Chart"));
    addType(bar, BarChartType, NormalChartSubtype, i18n("Normal"));
    addType(bar, BarChartType, StackedChartSubtype, i18n("Stacked"));
    addType(bar, BarChartType, PercentChartSubtype, i18n("Percent"));

    QMenu *line = menu->addMenu(ICON1(LineChartType), i18n("Line Chart"));
    addType(line, LineChartType, NormalChartSubtype, i18n("Normal"));
    addType(line, LineChartType, StackedChartSubtype, i18n("Stacked"));
    addType(line, LineChartType, PercentChartSubtype, i18n("Percent"));

    QMenu *area = menu->addMenu(ICON1(AreaChartType), i18n("Area Chart"));
    addType(area, AreaChartType, NormalChartSubtype, i18n("Normal"));
    addType(area, AreaChartType, StackedChartSubtype, i18n("Stacked"));
    addType(area, AreaChartType, PercentChartSubtype, i18n("Percent"));

    menu->addSeparator();
    addType(menu, CircleChartType, NoChartSubtype, i18n("Pie Chart"));
    addType(menu, RingChartType, NoChartSubtype, i18n("Ring Chart"));

    menu->addSeparator();
    QMenu *radar = menu->addMenu(ICON1(RadarChartType), i18n("Polar Chart"));
    addType(radar, RadarChartType, NoChartSubtype, i18n("Normal"));
    addType(radar, FilledRadarChartType, NoChartSubtype, i18n("Filled"));

    menu->addSeparator();
    addType(menu, ScatterChartType, NoChartSubtype, i18n("Scatter Chart"));
    addType(menu, BubbleChartType, NoChartSubtype, i18n("Bubble Chart"));

    menu->addSeparator();
    QMenu *stock = menu->addMenu(ICON1(StockChartType), i18n("Stock Chart"));
    addType(stock, StockChartType, CandlestickChartSubtype, i18n("Candlestick"));
    addType(stock, StockChartType, OpenHighLowCloseChartSubtype, i18n("Open-High-Low-Close"));
    addType(stock, StockChartType, HighLowCloseChartSubtype, i18n("High-Low-Close"));
    return menu;
}

void ChartTool::changeChartType(ChartType type, ChartSubtype subtype)
{
    ChartShape *chart = currentChart();
    if (!chart) {
        return;
    }
    auto *command = new ChartTypeCommand(chart);
    command->setChartType(type, subtype);
    canvas()->addCommand(command);
}

void ChartTool::showDataEditor()
{
    ChartShape *chart = currentChart();
    if (!chart || !canvas()) {
        return;
    }
    QWidget *parent = canvas()->canvasWidget();
    QDialog *editor = nullptr;
    // Pie, bubble, scatter and stock charts have editors of their own; every
    // other type is edited in the data table.
    switch (chart->chartType()) {
    case CircleChartType: {
        auto *dialog = new PieDataEditor(parent);
        if (chart->internalModel()) {
            dialog->setModel(chart->internalModel());
        }
        editor = dialog;
        break;
    }
    case BubbleChartType:
        editor = new BubbleDataEditor(chart, parent);
        break;
    case ScatterChartType:
        editor = new ScatterDataEditor(chart, parent);
        break;
    case StockChartType:
        editor = new StockDataEditor(chart, parent);
        break;
    default: {
        auto *dialog = new TableEditorDialog;
        dialog->setProxyModel(chart->proxyModel());
        if (chart->internalModel()) {
            dialog->setModel(chart->internalModel());
        }
        editor = dialog;
        break;
    }
    }
    if (!editor) {
        return;
    }
    editor->setAttribute(Qt::WA_DeleteOnClose);
    editor->show();
    editor->raise();
}

void ChartTool::applyColorScheme(int scheme)
{
    ChartShape *chart = currentChart();
    if (!chart || !chart->plotArea()) {
        return;
    }
    const QList<QColor> colors = colorSchemeColors(scheme);
    const QList<DataSet *> dataSets = chart->plotArea()->dataSets();
    if (colors.isEmpty() || dataSets.isEmpty()) {
        return;
    }
    auto *macro = new KUndo2Command(kundo2_i18n("Change Chart Colors"));
    const ChartType type = chart->chartType();
    if (type == CircleChartType || type == RingChartType) {
        // Every section of a pie or ring is painted separately.
        DataSet *dataSet = dataSets.first();
        for (int section = 0; section < dataSet->size(); ++section) {
            auto *command = new DatasetCommand(dataSet, chart, section, macro);
            command->setDataSetBrush(colors.at(section % colors.count()));
        }
    } else {
        for (int i = 0; i < dataSets.count(); ++i) {
            auto *command = new DatasetCommand(dataSets.at(i), chart, -1, macro);
            command->setDataSetBrush(colors.at(i % colors.count()));
        }
    }
    canvas()->addCommand(macro);
}

void ChartTool::setDataLabels(bool show)
{
    ChartShape *chart = currentChart();
    if (!chart || !chart->plotArea()) {
        return;
    }
    const QList<DataSet *> dataSets = chart->plotArea()->dataSets();
    if (dataSets.isEmpty()) {
        return;
    }
    auto *macro = new KUndo2Command(kundo2_i18n(show ? "Show Data Labels" : "Hide Data Labels"));
    for (DataSet *dataSet : dataSets) {
        auto *command = new DatasetCommand(dataSet, chart, -1, macro);
        command->setDataSetShowNumber(show);
    }
    canvas()->addCommand(macro);
}

void ChartTool::setMajorGridLines(bool show)
{
    ChartShape *chart = currentChart();
    if (!chart) {
        return;
    }
    auto *macro = new KUndo2Command(kundo2_i18n(show ? "Show Gridlines" : "Hide Gridlines"));
    for (Axis *axis : chartAxes(chart)) {
        auto *command = new AxisCommand(axis, chart, macro);
        command->setAxisShowMajorGridLines(show);
    }
    canvas()->addCommand(macro);
}

void ChartTool::applyQuickLayout(int layout)
{
    ChartShape *chart = currentChart();
    if (!chart) {
        return;
    }
    const bool showTitle = layout != 4;
    const bool showLegend = layout == 0 || layout == 2;
    const bool showLabels = layout == 1 || layout == 2;
    if (chart->legend()) {
        chart->legend()->setLegendPosition(layout == 1 ? BottomPosition : EndPosition);
    }
    auto *macro = new KUndo2Command(kundo2_i18n("Change Chart Layout"));
    if (chart->title()) {
        new ChartTextShapeCommand(chart->title(), chart, showTitle, macro);
    }
    if (chart->legend()) {
        new ChartTextShapeCommand(chart->legend(), chart, showLegend, macro);
    }
    canvas()->addCommand(macro);
    setDataLabels(showLabels);
}

void ChartTool::applyStylePreset(int style)
{
    ChartShape *chart = currentChart();
    if (!chart) {
        return;
    }
    const StylePreset &preset = stylePresets[qBound(0, style, stylePresetCount() - 1)];
    if (chart->legend()) {
        chart->legend()->setLegendPosition(preset.legendPosition);
    }
    auto *macro = new KUndo2Command(kundo2_i18n("Change Chart Style"));
    if (chart->legend()) {
        new ChartTextShapeCommand(chart->legend(), chart, preset.legend, macro);
    }
    canvas()->addCommand(macro);
    applyColorScheme(preset.scheme);
    setMajorGridLines(preset.gridLines);
    setDataLabels(preset.valueLabels);
    chart->relayout();
    chart->update();
}

void ChartTool::setChartType(ChartType type, ChartSubtype subtype)
{
    Q_ASSERT(d->shape);
    if (!d->shape) {
        return;
    }
    ChartTypeCommand *command = new ChartTypeCommand(d->shape);
    if (command != nullptr) {
        command->setChartType(type, subtype);
        canvas()->addCommand(command);
    }
    foreach (QWidget *w, optionWidgets()) {
        ConfigWidgetBase *cw = dynamic_cast<ConfigWidgetBase *>(w);
        if (cw) {
            cw->updateData();
        }
    }
}

void ChartTool::setChartSubType(ChartSubtype subtype)
{
    Q_ASSERT(d->shape);
    if (!d->shape)
        return;

    d->shape->setChartSubType(subtype);
    d->shape->update();
}

void ChartTool::setDataSetXDataRegion(DataSet *dataSet, const CellRegion &region)
{
    debugChartTool << dataSet << region.toString();
    if (!dataSet)
        return;

    dataSet->setXDataRegion(region);
    d->shape->update();
}

void ChartTool::setDataSetYDataRegion(DataSet *dataSet, const CellRegion &region)
{
    if (!dataSet)
        return;

    dataSet->setYDataRegion(region);
    d->shape->update();
}

void ChartTool::setDataSetCustomDataRegion(DataSet *dataSet, const CellRegion &region)
{
    if (!dataSet)
        return;

    dataSet->setCustomDataRegion(region);
}

void ChartTool::setDataSetLabelDataRegion(DataSet *dataSet, const CellRegion &region)
{
    if (!dataSet)
        return;

    dataSet->setLabelDataRegion(region);
    d->shape->update();
    d->shape->legend()->update();
}

void ChartTool::setDataSetCategoryDataRegion(DataSet *dataSet, const CellRegion &region)
{
    if (!dataSet) {
        return;
    }
    if (isCartesian(d->shape->chartType())) {
        // FIXME: Seems strange the way things are stored in multiple places
        // Categories are labels on the categories axis
        dataSet->setCategoryDataRegion(region); // probably should not be stored here, as datasets cannot have individual categories
        d->shape->plotArea()->proxyModel()->setCategoryDataRegion(region); // this seems to be for odf only!?
    } else {
        // Categories are legend texts
        dataSet->setCategoryDataRegion(region);
    }
    d->shape->update();
    d->shape->legend()->update();
}

void ChartTool::setDataSetChartType(DataSet *dataSet, ChartType type, ChartSubtype subType)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(dataSet);
    if (dataSet) {
        DatasetCommand *cmd = new DatasetCommand(dataSet, d->shape);
        cmd->setDataSetChartType(type, subType);
        canvas()->addCommand(cmd);
    }
    d->shape->update();
    d->shape->legend()->update();
}

void ChartTool::setDataSetBrush(DataSet *dataSet, const QColor &color, int section)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(dataSet || section >= 0);
    debugChartTool << dataSet << color << section;
    if (!dataSet) {
        QList<DataSet *> lst = d->shape->proxyModel()->dataSets();
        if (lst.isEmpty()) {
            return;
        }
        // we set brush for section in all datasets
        KUndo2Command *command = new KUndo2Command();
        for (int i = 0; i < lst.count(); ++i) {
            DatasetCommand *cmd = new DatasetCommand(lst.at(i), d->shape, section, command);
            cmd->setDataSetBrush(color);
            command->setText(cmd->text());
        }
        canvas()->addCommand(command);
    } else {
        DatasetCommand *command = new DatasetCommand(dataSet, d->shape, section);
        command->setDataSetBrush(color);
        canvas()->addCommand(command);
    }
}

void ChartTool::setDataSetPen(DataSet *dataSet, const QColor &color, int section)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(dataSet || section >= 0);
    debugChartTool << color << section;
    if (!dataSet) {
        QList<DataSet *> lst = d->shape->proxyModel()->dataSets();
        if (lst.isEmpty()) {
            return;
        }
        // we set brush for section in all datasets
        KUndo2Command *command = new KUndo2Command();
        for (int i = 0; i < lst.count(); ++i) {
            DatasetCommand *cmd = new DatasetCommand(lst.at(i), d->shape, section, command);
            cmd->setDataSetPen(color);
            command->setText(cmd->text());
        }
        canvas()->addCommand(command);
    } else {
        DatasetCommand *command = new DatasetCommand(dataSet, d->shape, section);
        command->setDataSetPen(color);
        canvas()->addCommand(command);
    }
}

void ChartTool::setDataSetMarker(DataSet *dataSet, OdfSymbolType type, OdfMarkerStyle style)
{
    Q_ASSERT(d->shape);
    if (!dataSet) {
        return;
    }
    DatasetCommand *command = new DatasetCommand(dataSet, d->shape);
    command->setDataSetMarker(type, style);
    canvas()->addCommand(command);
}
void ChartTool::setDataSetAxis(DataSet *dataSet, Axis *axis)
{
    Q_ASSERT(d->shape);
    if (!dataSet || !axis)
        return;

    DatasetCommand *command = new DatasetCommand(dataSet, d->shape);
    command->setDataSetAxis(axis);
    canvas()->addCommand(command);
}

void ChartTool::setDataSetShowCategory(DataSet *dataSet, bool b, int section)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(dataSet || section >= 0);
    if (!dataSet) {
        QList<DataSet *> lst = d->shape->proxyModel()->dataSets();
        if (lst.isEmpty()) {
            return;
        }
        // we set brush for section in all datasets
        KUndo2Command *command = new KUndo2Command();
        for (int i = 0; i < lst.count(); ++i) {
            DatasetCommand *cmd = new DatasetCommand(lst.at(i), d->shape, section, command);
            cmd->setDataSetShowCategory(b);
            command->setText(cmd->text());
        }
        canvas()->addCommand(command);
    } else {
        DatasetCommand *command = new DatasetCommand(dataSet, d->shape, section);
        command->setDataSetShowCategory(b);
        canvas()->addCommand(command);
    }

    debugChartTool << section << b << ':' << dataSet->valueLabelType(section).category;
}

void ChartTool::setDataSetShowNumber(DataSet *dataSet, bool b, int section)
{
    debugChartTool << b << section << dataSet;
    Q_ASSERT(d->shape);
    Q_ASSERT(dataSet || section >= 0);
    if (!dataSet) {
        QList<DataSet *> lst = d->shape->proxyModel()->dataSets();
        if (lst.isEmpty()) {
            return;
        }
        // we set brush for section in all datasets
        KUndo2Command *command = new KUndo2Command();
        for (int i = 0; i < lst.count(); ++i) {
            DatasetCommand *cmd = new DatasetCommand(lst.at(i), d->shape, section, command);
            cmd->setDataSetShowNumber(b);
            command->setText(cmd->text());
        }
        canvas()->addCommand(command);
    } else {
        DatasetCommand *command = new DatasetCommand(dataSet, d->shape, section);
        command->setDataSetShowNumber(b);
        canvas()->addCommand(command);
    }
    debugChartTool << section << b << ':' << dataSet->valueLabelType(section).number;
}

void ChartTool::setDataSetShowPercent(DataSet *dataSet, bool b, int section)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(dataSet || section >= 0);
    if (!dataSet) {
        QList<DataSet *> lst = d->shape->proxyModel()->dataSets();
        if (lst.isEmpty()) {
            return;
        }
        // we set brush for section in all datasets
        KUndo2Command *command = new KUndo2Command();
        for (int i = 0; i < lst.count(); ++i) {
            DatasetCommand *cmd = new DatasetCommand(lst.at(i), d->shape, section, command);
            cmd->setDataSetShowPercent(b);
            command->setText(cmd->text());
        }
        canvas()->addCommand(command);
    } else {
        DatasetCommand *command = new DatasetCommand(dataSet, d->shape, section);
        command->setDataSetShowPercent(b);
        canvas()->addCommand(command);
    }

    debugChartTool << section << b << ':' << dataSet->valueLabelType(section).percentage;
}

void ChartTool::setDataSetShowSymbol(DataSet *dataSet, bool b, int section)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(dataSet || section >= 0);
    if (!dataSet) {
        QList<DataSet *> lst = d->shape->proxyModel()->dataSets();
        if (lst.isEmpty()) {
            return;
        }
        // we set brush for section in all datasets
        KUndo2Command *command = new KUndo2Command();
        for (int i = 0; i < lst.count(); ++i) {
            DatasetCommand *cmd = new DatasetCommand(lst.at(i), d->shape, section, command);
            cmd->setDataSetShowSymbol(b);
            command->setText(cmd->text());
        }
        canvas()->addCommand(command);
    } else {
        DatasetCommand *command = new DatasetCommand(dataSet, d->shape, section);
        command->setDataSetShowSymbol(b);
        canvas()->addCommand(command);
    }
    debugChartTool << section << b << ':' << dataSet->valueLabelType(section).symbol;
}

void ChartTool::setThreeDMode(bool threeD)
{
    Q_ASSERT(d->shape);
    if (!d->shape)
        return;

    d->shape->setThreeD(threeD);
    d->shape->update();
}

void ChartTool::setShowTitle(bool show)
{
    Q_ASSERT(d->shape);
    if (!d->shape)
        return;

    ChartTextShapeCommand *command = new ChartTextShapeCommand(d->shape->title(), d->shape, show);
    canvas()->addCommand(command);
}

void ChartTool::setTitlePositioning(int index)
{
    Q_ASSERT(d->shape);
    if (!d->shape) {
        return;
    }
    // TODD: undo command
    d->shape->title()->setAdditionalStyleAttribute("chart:auto-position", index == 0 ? "true" : "false");
    d->shape->layout()->scheduleRelayout();
    d->shape->layout()->layout();
    d->shape->update();
}

void ChartTool::setTitleResize(int index)
{
    Q_ASSERT(d->shape);
    if (!d->shape) {
        return;
    }
    // TODD: undo command
    TextLabelData *labelData = dynamic_cast<TextLabelData *>(d->shape->title()->userData());
    if (labelData == nullptr) {
        return;
    }
    labelData->setResizeMethod(index == 0 ? KoTextShapeDataBase::AutoResize : KoTextShapeDataBase::NoResize);
    d->shape->layout()->scheduleRelayout();
    d->shape->layout()->layout();
    d->shape->update();
}

void ChartTool::setShowSubTitle(bool show)
{
    Q_ASSERT(d->shape);
    if (!d->shape)
        return;

    ChartTextShapeCommand *command = new ChartTextShapeCommand(d->shape->subTitle(), d->shape, show);
    canvas()->addCommand(command);
}

void ChartTool::setSubTitlePositioning(int index)
{
    Q_ASSERT(d->shape);
    if (!d->shape) {
        return;
    }
    // TODD: undo command
    d->shape->subTitle()->setAdditionalStyleAttribute("chart:auto-position", index == 0 ? "true" : "false");
    d->shape->layout()->scheduleRelayout();
    d->shape->layout()->layout();
    d->shape->update();
}

void ChartTool::setSubTitleResize(int index)
{
    Q_ASSERT(d->shape);
    if (!d->shape) {
        return;
    }
    // TODD: undo command
    TextLabelData *labelData = dynamic_cast<TextLabelData *>(d->shape->subTitle()->userData());
    if (labelData == nullptr) {
        return;
    }
    labelData->setResizeMethod(index == 0 ? KoTextShapeDataBase::AutoResize : KoTextShapeDataBase::NoResize);
    d->shape->layout()->scheduleRelayout();
    d->shape->layout()->layout();
    d->shape->update();
}

void ChartTool::setShowFooter(bool show)
{
    Q_ASSERT(d->shape);
    if (!d->shape)
        return;

    ChartTextShapeCommand *command = new ChartTextShapeCommand(d->shape->footer(), d->shape, show);
    canvas()->addCommand(command);
}

void ChartTool::setFooterPositioning(int index)
{
    Q_ASSERT(d->shape);
    if (!d->shape) {
        return;
    }
    // TODD: undo command
    d->shape->footer()->setAdditionalStyleAttribute("chart:auto-position", index == 0 ? "true" : "false");
    d->shape->layout()->scheduleRelayout();
    d->shape->layout()->layout();
    d->shape->update();
}

void ChartTool::setFooterResize(int index)
{
    Q_ASSERT(d->shape);
    if (!d->shape) {
        return;
    }
    // TODD: undo command
    TextLabelData *labelData = dynamic_cast<TextLabelData *>(d->shape->footer()->userData());
    if (labelData == nullptr) {
        return;
    }
    labelData->setResizeMethod(index == 0 ? KoTextShapeDataBase::AutoResize : KoTextShapeDataBase::NoResize);
    d->shape->layout()->scheduleRelayout();
    d->shape->layout()->layout();
    d->shape->update();
}

void ChartTool::setDataDirection(Qt::Orientation direction)
{
    Q_ASSERT(d->shape);
    if (!d->shape)
        return;

    d->shape->proxyModel()->setDataDirection(direction);
    d->shape->relayout();
}

void ChartTool::setChartOrientation(Qt::Orientation direction)
{
    Q_ASSERT(d->shape);
    if (!d->shape) {
        return;
    }
    PlotAreaCommand *command = new PlotAreaCommand(d->shape->plotArea());
    command->setOrientation(direction);
    canvas()->addCommand(command);
}

void ChartTool::setLegendTitle(const QString &title)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(d->shape->legend());

    LegendCommand *command = new LegendCommand(d->shape->legend());
    command->setLegendTitle(title);
    canvas()->addCommand(command);
}

void ChartTool::setLegendFont(const QFont &font)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(d->shape->legend());

    // There only is a general font, for the legend items and the legend title
    LegendCommand *command = new LegendCommand(d->shape->legend());
    command->setLegendFont(font);
    canvas()->addCommand(command);
}

void ChartTool::setLegendFontSize(int size)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(d->shape->legend());

    LegendCommand *command = new LegendCommand(d->shape->legend());
    command->setLegendFontSize(size);
    canvas()->addCommand(command);
}

void ChartTool::setLegendOrientation(Qt::Orientation orientation)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(d->shape->legend());

    LegendCommand *command = new LegendCommand(d->shape->legend());
    command->setLegendExpansion(QtOrientationToLegendExpansion(orientation));
    canvas()->addCommand(command);
}

void ChartTool::setLegendPosition(Position pos)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(d->shape->legend());

    // TODO undo command
    d->shape->legend()->setLegendPosition(pos);
    d->shape->legend()->update();
    d->shape->layout()->scheduleRelayout();
    d->shape->layout()->layout();
}

void ChartTool::setLegendAlignment(Qt::Alignment alignment)
{
    Q_ASSERT(d->shape);
    Q_ASSERT(d->shape->legend());

    // TODO undo command
    d->shape->legend()->setAlignment(alignment);
    d->shape->legend()->update();
    d->shape->layout()->scheduleRelayout();
    d->shape->layout()->layout();
}

void ChartTool::addAxis(AxisDimension dimension, const QString &title)
{
    Q_ASSERT(d->shape);

    Axis *axis = new Axis(d->shape->plotArea(), dimension); // automatically adds axis to plot area
    if (axis == d->shape->plotArea()->secondaryYAxis()) {
        axis->setOdfAxisPosition("end"); // right
    } else if (axis == d->shape->plotArea()->secondaryXAxis()) {
        axis->setOdfAxisPosition("end"); // top
        axis->updateKChartAxisPosition();
    }
    d->shape->plotArea()->takeAxis(axis); // so we remove it again, sigh
    axis->setTitleText(title);
    AddRemoveAxisCommand *command = new AddRemoveAxisCommand(axis, d->shape, true, canvas()->shapeManager());
    canvas()->addCommand(command);
}

void ChartTool::removeAxis(Axis *axis)
{
    Q_ASSERT(d->shape);

    AddRemoveAxisCommand *command = new AddRemoveAxisCommand(axis, d->shape, false, canvas()->shapeManager());
    canvas()->addCommand(command);
}

void ChartTool::setAxisShowTitle(Axis *axis, bool show)
{
    Q_ASSERT(d->shape);
    if (show && axis->titleText().isEmpty()) {
        axis->setTitleText(i18n("Axistitle"));
    }
    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisShowTitle(show);
    canvas()->addCommand(command);
}

void ChartTool::setShowAxis(Axis *axis, bool show)
{
    Q_ASSERT(d->shape);
    debugChartTool << axis << show;
    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setShowAxis(show);
    canvas()->addCommand(command);
}

void ChartTool::setAxisPosition(Axis *axis, const QString &pos)
{
    Q_ASSERT(d->shape);
    debugChartTool << axis << pos;
    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisPosition(pos);
    canvas()->addCommand(command);
}

void ChartTool::setAxisLabelsPosition(Axis *axis, const QString &pos)
{
    Q_ASSERT(d->shape);
    debugChartTool << axis << pos;
    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisLabelsPosition(pos);
    canvas()->addCommand(command);
}

void ChartTool::setAxisShowLabels(Axis *axis, bool b)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisShowLabels(b);
    canvas()->addCommand(command);
}

void ChartTool::setAxisShowMajorGridLines(Axis *axis, bool b)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisShowMajorGridLines(b);
    canvas()->addCommand(command);
}

void ChartTool::setAxisShowMinorGridLines(Axis *axis, bool b)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisShowMinorGridLines(b);
    canvas()->addCommand(command);
}

void ChartTool::setAxisUseLogarithmicScaling(Axis *axis, bool b)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisUseLogarithmicScaling(b);
    canvas()->addCommand(command);
}

void ChartTool::setAxisStepWidth(Axis *axis, qreal width)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisStepWidth(width);
    canvas()->addCommand(command);
}

void ChartTool::setAxisSubStepWidth(Axis *axis, qreal width)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisSubStepWidth(width);
    canvas()->addCommand(command);
}

void ChartTool::setAxisUseAutomaticStepWidth(Axis *axis, bool automatic)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisUseAutomaticStepWidth(automatic);
    canvas()->addCommand(command);
}

void ChartTool::setAxisUseAutomaticSubStepWidth(Axis *axis, bool automatic)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisUseAutomaticSubStepWidth(automatic);
    canvas()->addCommand(command);
}

void ChartTool::setAxisLabelsFont(Axis *axis, const QFont &font)
{
    Q_ASSERT(d->shape);

    AxisCommand *command = new AxisCommand(axis, d->shape);
    command->setAxisLabelsFont(font);
    canvas()->addCommand(command);
}

void ChartTool::setGapBetweenBars(Axis *axis, int percent)
{
    Q_ASSERT(d->shape);
    debugChartTool << axis << percent;
    GapCommand *command = new GapCommand(axis, d->shape);
    command->setGapBetweenBars(percent);
    canvas()->addCommand(command);
}

void ChartTool::setGapBetweenSets(Axis *axis, int percent)
{
    Q_ASSERT(d->shape);
    debugChartTool << axis << percent;
    GapCommand *command = new GapCommand(axis, d->shape);
    command->setGapBetweenSets(percent);
    canvas()->addCommand(command);
}

void ChartTool::setPieExplodeFactor(DataSet *dataSet, int section, int percent)
{
    Q_ASSERT(d->shape);

    dataSet->setPieExplodeFactor(section, percent);
    d->shape->update();
}

void ChartTool::setShowLegend(bool show)
{
    Q_ASSERT(d->shape);

    ChartTextShapeCommand *command = new ChartTextShapeCommand(d->shape->legend(), d->shape, show);
    if (show) {
        command->setText(kundo2_i18n("Show Legend"));
    } else {
        command->setText(kundo2_i18n("Hide Legend"));
    }
    canvas()->addCommand(command);
}
