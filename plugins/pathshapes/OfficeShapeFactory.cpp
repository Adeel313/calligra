/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "OfficeShapeFactory.h"
#include "../../filters/libmsooxml/NormalizeShapeGeometry.h"
#include "enhancedpath/EnhancedPathShape.h"

#include <KoColorBackground.h>
#include <KoOdfLoadingContext.h>
#include <KoPathShape.h>
#include <KoProperties.h>
#include <KoShapeLoadingContext.h>
#include <KoShapeStroke.h>
#include <KoStyleStack.h>
#include <KoXmlNS.h>
#include <KLocalizedString>
#include <QHash>
#include <QXmlStreamReader>

namespace
{
struct PresetGeometry {
    QHash<QString, QString> attributes;
    QHash<QString, QString> equations;
    QHash<QString, QString> textareas;
    PresetGeometry()
    {
        // Share the importer's existing, generated Office geometry. No filter
        // library is needed at runtime, so the native shapes remain standalone.
#include "../../filters/libmsooxml/generated/generatedShapes.h"
        normalizeShapeGeometry(attributes, equations, textareas);
    }
};
const PresetGeometry &geometry()
{
    static const PresetGeometry presets;
    return presets;
}
struct Entry { const char *preset; KLocalizedString name; const char *family; };
const Entry entries[] = {
    {"accentBorderCallout1", ki18n("Accent Border Callout 1"), "office-callouts"},
    {"accentBorderCallout2", ki18n("Accent Border Callout 2"), "office-callouts"},
    {"accentBorderCallout3", ki18n("Accent Border Callout 3"), "office-callouts"},
    {"accentCallout1", ki18n("Accent Callout 1"), "office-callouts"},
    {"accentCallout2", ki18n("Accent Callout 2"), "office-callouts"},
    {"accentCallout3", ki18n("Accent Callout 3"), "office-callouts"},
    {"actionButtonBackPrevious", ki18n("Action Button: Back Previous"), "office-buttons"},
    {"actionButtonBeginning", ki18n("Action Button: Beginning"), "office-buttons"},
    {"actionButtonBlank", ki18n("Action Button: Blank"), "office-buttons"},
    {"actionButtonDocument", ki18n("Action Button: Document"), "office-buttons"},
    {"actionButtonEnd", ki18n("Action Button: End"), "office-buttons"},
    {"actionButtonForwardNext", ki18n("Action Button: Forward Next"), "office-buttons"},
    {"actionButtonHelp", ki18n("Action Button: Help"), "office-buttons"},
    {"actionButtonHome", ki18n("Action Button: Home"), "office-buttons"},
    {"actionButtonInformation", ki18n("Action Button: Information"), "office-buttons"},
    {"actionButtonMovie", ki18n("Action Button: Movie"), "office-buttons"},
    {"actionButtonReturn", ki18n("Action Button: Return"), "office-buttons"},
    {"actionButtonSound", ki18n("Action Button: Sound"), "office-buttons"},
    {"arc", ki18n("Arc"), "office-lines"},
    {"bentArrow", ki18n("Bent Arrow"), "office-arrows"},
    {"bentConnector2", ki18n("Bent Connector 2"), "office-lines"},
    {"bentConnector3", ki18n("Elbow Connector"), "office-lines"},
    {"bentConnector4", ki18n("Bent Connector 4"), "office-lines"},
    {"bentConnector5", ki18n("Bent Connector 5"), "office-lines"},
    {"bentUpArrow", ki18n("Bent Up Arrow"), "office-arrows"},
    {"bevel", ki18n("Bevel"), "office-basic"},
    {"blockArc", ki18n("Block Arc"), "office-basic"},
    {"borderCallout1", ki18n("Border Callout 1"), "office-callouts"},
    {"borderCallout2", ki18n("Border Callout 2"), "office-callouts"},
    {"borderCallout3", ki18n("Border Callout 3"), "office-callouts"},
    {"bracePair", ki18n("Brace Pair"), "office-basic"},
    {"bracketPair", ki18n("Bracket Pair"), "office-basic"},
    {"callout1", ki18n("Callout 1"), "office-callouts"},
    {"callout2", ki18n("Callout 2"), "office-callouts"},
    {"callout3", ki18n("Callout 3"), "office-callouts"},
    {"can", ki18n("Cylinder"), "office-basic"},
    {"chartPlus", ki18n("Chart Plus"), "office-basic"},
    {"chartStar", ki18n("Chart Star"), "office-basic"},
    {"chartX", ki18n("Chart X"), "office-basic"},
    {"chevron", ki18n("Chevron"), "office-arrows"},
    {"chord", ki18n("Chord"), "office-basic"},
    {"circularArrow", ki18n("Circular Arrow"), "office-arrows"},
    {"cloud", ki18n("Cloud"), "office-basic"},
    {"cloudCallout", ki18n("Cloud Callout"), "office-callouts"},
    {"corner", ki18n("Corner"), "office-basic"},
    {"cornerTabs", ki18n("Corner Tabs"), "office-basic"},
    {"cube", ki18n("Cube"), "office-basic"},
    {"curvedConnector2", ki18n("Curved Connector 2"), "office-lines"},
    {"curvedConnector3", ki18n("Curved Connector"), "office-lines"},
    {"curvedConnector4", ki18n("Curved Connector 4"), "office-lines"},
    {"curvedConnector5", ki18n("Curved Connector 5"), "office-lines"},
    {"curvedDownArrow", ki18n("Curved Down Arrow"), "office-arrows"},
    {"curvedLeftArrow", ki18n("Curved Left Arrow"), "office-arrows"},
    {"curvedRightArrow", ki18n("Curved Right Arrow"), "office-arrows"},
    {"curvedUpArrow", ki18n("Curved Up Arrow"), "office-arrows"},
    {"decagon", ki18n("Decagon"), "office-basic"},
    {"diagStripe", ki18n("Diag Stripe"), "office-basic"},
    {"diamond", ki18n("Diamond"), "office-basic"},
    {"dodecagon", ki18n("Dodecagon"), "office-basic"},
    {"donut", ki18n("Donut"), "office-basic"},
    {"doubleWave", ki18n("Double Wave"), "office-stars"},
    {"downArrow", ki18n("Down Arrow"), "office-arrows"},
    {"downArrowCallout", ki18n("Down Arrow Callout"), "office-callouts"},
    {"ellipse", ki18n("Oval"), "office-basic"},
    {"ellipseRibbon", ki18n("Ellipse Ribbon"), "office-stars"},
    {"ellipseRibbon2", ki18n("Up Curved Ribbon"), "office-stars"},
    {"flowChartAlternateProcess", ki18n("Flowchart: Alternate Process"), "office-flowchart"},
    {"flowChartCollate", ki18n("Flowchart: Collate"), "office-flowchart"},
    {"flowChartConnector", ki18n("Flowchart: Connector"), "office-flowchart"},
    {"flowChartDecision", ki18n("Flowchart: Decision"), "office-flowchart"},
    {"flowChartDelay", ki18n("Flowchart: Delay"), "office-flowchart"},
    {"flowChartDisplay", ki18n("Flowchart: Display"), "office-flowchart"},
    {"flowChartDocument", ki18n("Flowchart: Document"), "office-flowchart"},
    {"flowChartExtract", ki18n("Flowchart: Extract"), "office-flowchart"},
    {"flowChartInputOutput", ki18n("Flowchart: Data"), "office-flowchart"},
    {"flowChartInternalStorage", ki18n("Flowchart: Internal Storage"), "office-flowchart"},
    {"flowChartMagneticDisk", ki18n("Flowchart: Database"), "office-flowchart"},
    {"flowChartMagneticDrum", ki18n("Flowchart: Magnetic Drum"), "office-flowchart"},
    {"flowChartMagneticTape", ki18n("Flowchart: Magnetic Tape"), "office-flowchart"},
    {"flowChartManualInput", ki18n("Flowchart: Manual Input"), "office-flowchart"},
    {"flowChartManualOperation", ki18n("Flowchart: Manual Operation"), "office-flowchart"},
    {"flowChartMerge", ki18n("Flowchart: Merge"), "office-flowchart"},
    {"flowChartMultidocument", ki18n("Flowchart: Multiple Documents"), "office-flowchart"},
    {"flowChartOfflineStorage", ki18n("Flowchart: Offline Storage"), "office-flowchart"},
    {"flowChartOffpageConnector", ki18n("Flowchart: Offpage Connector"), "office-flowchart"},
    {"flowChartOnlineStorage", ki18n("Flowchart: Online Storage"), "office-flowchart"},
    {"flowChartOr", ki18n("Flowchart: Or"), "office-flowchart"},
    {"flowChartPredefinedProcess", ki18n("Flowchart: Predefined Process"), "office-flowchart"},
    {"flowChartPreparation", ki18n("Flowchart: Preparation"), "office-flowchart"},
    {"flowChartProcess", ki18n("Flowchart: Process"), "office-flowchart"},
    {"flowChartPunchedCard", ki18n("Flowchart: Punched Card"), "office-flowchart"},
    {"flowChartPunchedTape", ki18n("Flowchart: Punched Tape"), "office-flowchart"},
    {"flowChartSort", ki18n("Flowchart: Sort"), "office-flowchart"},
    {"flowChartSummingJunction", ki18n("Flowchart: Summing Junction"), "office-flowchart"},
    {"flowChartTerminator", ki18n("Flowchart: Terminator"), "office-flowchart"},
    {"foldedCorner", ki18n("Folded Corner"), "office-basic"},
    {"frame", ki18n("Frame"), "office-basic"},
    {"funnel", ki18n("Funnel"), "office-basic"},
    {"gear6", ki18n("Gear 6"), "office-basic"},
    {"gear9", ki18n("Gear 9"), "office-basic"},
    {"halfFrame", ki18n("Half Frame"), "office-basic"},
    {"heart", ki18n("Heart"), "office-basic"},
    {"heptagon", ki18n("Heptagon"), "office-basic"},
    {"hexagon", ki18n("Hexagon"), "office-basic"},
    {"homePlate", ki18n("Home Plate"), "office-basic"},
    {"horizontalScroll", ki18n("Horizontal Scroll"), "office-stars"},
    {"irregularSeal1", ki18n("Explosion 1"), "office-stars"},
    {"irregularSeal2", ki18n("Explosion 2"), "office-stars"},
    {"leftArrow", ki18n("Left Arrow"), "office-arrows"},
    {"leftArrowCallout", ki18n("Left Arrow Callout"), "office-callouts"},
    {"leftBrace", ki18n("Left Brace"), "office-basic"},
    {"leftBracket", ki18n("Left Bracket"), "office-basic"},
    {"leftCircularArrow", ki18n("Left Circular Arrow"), "office-arrows"},
    {"leftRightArrow", ki18n("Left Right Arrow"), "office-arrows"},
    {"leftRightArrowCallout", ki18n("Left Right Arrow Callout"), "office-callouts"},
    {"leftRightCircularArrow", ki18n("Left Right Circular Arrow"), "office-arrows"},
    {"leftRightRibbon", ki18n("Left Right Ribbon"), "office-stars"},
    {"leftRightUpArrow", ki18n("Left Right Up Arrow"), "office-arrows"},
    {"leftUpArrow", ki18n("Left Up Arrow"), "office-arrows"},
    {"lightningBolt", ki18n("Lightning Bolt"), "office-basic"},
    {"line", ki18n("Line"), "office-lines"},
    {"lineInv", ki18n("Inverse Line"), "office-lines"},
    {"mathDivide", ki18n("Divide"), "office-equations"},
    {"mathEqual", ki18n("Equal"), "office-equations"},
    {"mathMinus", ki18n("Minus"), "office-equations"},
    {"mathMultiply", ki18n("Multiply"), "office-equations"},
    {"mathNotEqual", ki18n("Not Equal"), "office-equations"},
    {"mathPlus", ki18n("Plus"), "office-equations"},
    {"moon", ki18n("Moon"), "office-basic"},
    {"nonIsoscelesTrapezoid", ki18n("Non Isosceles Trapezoid"), "office-basic"},
    {"noSmoking", ki18n("No Symbol"), "office-basic"},
    {"notchedRightArrow", ki18n("Notched Right Arrow"), "office-arrows"},
    {"octagon", ki18n("Octagon"), "office-basic"},
    {"parallelogram", ki18n("Parallelogram"), "office-basic"},
    {"pentagon", ki18n("Pentagon"), "office-basic"},
    {"pie", ki18n("Pie"), "office-basic"},
    {"pieWedge", ki18n("Pie Wedge"), "office-basic"},
    {"plaque", ki18n("Plaque"), "office-basic"},
    {"plaqueTabs", ki18n("Plaque Tabs"), "office-basic"},
    {"plus", ki18n("Plus"), "office-basic"},
    {"quadArrow", ki18n("Quad Arrow"), "office-arrows"},
    {"quadArrowCallout", ki18n("Quad Arrow Callout"), "office-callouts"},
    {"rect", ki18n("Rectangle"), "office-rectangles"},
    {"ribbon", ki18n("Ribbon"), "office-stars"},
    {"ribbon2", ki18n("Up Ribbon"), "office-stars"},
    {"rightArrow", ki18n("Right Arrow"), "office-arrows"},
    {"rightArrowCallout", ki18n("Right Arrow Callout"), "office-callouts"},
    {"rightBrace", ki18n("Right Brace"), "office-basic"},
    {"rightBracket", ki18n("Right Bracket"), "office-basic"},
    {"round1Rect", ki18n("Round Single Corner Rectangle"), "office-rectangles"},
    {"round2DiagRect", ki18n("Round Diagonal Corner Rectangle"), "office-rectangles"},
    {"round2SameRect", ki18n("Round Same Side Corner Rectangle"), "office-rectangles"},
    {"roundRect", ki18n("Rounded Rectangle"), "office-rectangles"},
    {"rtTriangle", ki18n("Right Triangle"), "office-basic"},
    {"smileyFace", ki18n("Smiley Face"), "office-basic"},
    {"snip1Rect", ki18n("Snip Single Corner Rectangle"), "office-rectangles"},
    {"snip2DiagRect", ki18n("Snip Diagonal Corner Rectangle"), "office-rectangles"},
    {"snip2SameRect", ki18n("Snip Same Side Corner Rectangle"), "office-rectangles"},
    {"snipRoundRect", ki18n("Snip and Round Single Corner Rectangle"), "office-rectangles"},
    {"squareTabs", ki18n("Square Tabs"), "office-basic"},
    {"star10", ki18n("Star 10"), "office-stars"},
    {"star12", ki18n("Star 12"), "office-stars"},
    {"star16", ki18n("Star 16"), "office-stars"},
    {"star24", ki18n("Star 24"), "office-stars"},
    {"star32", ki18n("Star 32"), "office-stars"},
    {"star4", ki18n("Star 4"), "office-stars"},
    {"star5", ki18n("Star 5"), "office-stars"},
    {"star6", ki18n("Star 6"), "office-stars"},
    {"star7", ki18n("Star 7"), "office-stars"},
    {"star8", ki18n("Star 8"), "office-stars"},
    {"straightConnector1", ki18n("Straight Connector"), "office-lines"},
    {"stripedRightArrow", ki18n("Striped Right Arrow"), "office-arrows"},
    {"sun", ki18n("Sun"), "office-basic"},
    {"swooshArrow", ki18n("Swoosh Arrow"), "office-arrows"},
    {"teardrop", ki18n("Teardrop"), "office-basic"},
    {"trapezoid", ki18n("Trapezoid"), "office-basic"},
    {"triangle", ki18n("Triangle"), "office-basic"},
    {"upArrowCallout", ki18n("Up Arrow Callout"), "office-callouts"},
    {"upArrow", ki18n("Up Arrow"), "office-arrows"},
    {"upDownArrow", ki18n("Up Down Arrow"), "office-arrows"},
    {"upDownArrowCallout", ki18n("Up Down Arrow Callout"), "office-callouts"},
    {"uturnArrow", ki18n("U-Turn Arrow"), "office-arrows"},
    {"verticalScroll", ki18n("Vertical Scroll"), "office-stars"},
    {"wave", ki18n("Wave"), "office-stars"},
    {"wedgeEllipseCallout", ki18n("Oval Callout"), "office-callouts"},
    {"wedgeRectCallout", ki18n("Rectangular Callout"), "office-callouts"},
    {"wedgeRoundRectCallout", ki18n("Rounded Rectangular Callout"), "office-callouts"},
};
}

OfficeShapeFactory::OfficeShapeFactory()
    : KoShapeFactoryBase(QStringLiteral("OfficeShape"), i18n("Office Shapes"))
{
    setFamily(QStringLiteral("office"));
    setToolTip(i18n("Insert an Office preset shape"));
    setIconName(QStringLiteral("shape-choose-symbolic"));
    setXmlElementNames(KoXmlNS::draw, {QStringLiteral("custom-shape")});
    setLoadingPriority(2);
    const QStringList familiarOrder = QStringLiteral(
        "line,lineInv,straightConnector1,bentConnector3,curvedConnector3,bentConnector2,curvedConnector2,bentConnector4,curvedConnector4,bentConnector5,curvedConnector5,arc,"
        "rect,roundRect,snip1Rect,snip2SameRect,snip2DiagRect,snipRoundRect,round1Rect,round2SameRect,round2DiagRect,"
        "ellipse,triangle,rtTriangle,parallelogram,trapezoid,nonIsoscelesTrapezoid,diamond,pentagon,hexagon,heptagon,octagon,decagon,dodecagon,"
        "pie,pieWedge,chord,teardrop,frame,halfFrame,corner,diagStripe,plus,plaque,can,cube,bevel,donut,noSmoking,blockArc,foldedCorner,smileyFace,heart,lightningBolt,sun,moon,cloud,"
        "rightArrow,leftArrow,upArrow,downArrow,leftRightArrow,upDownArrow,quadArrow,leftRightUpArrow,bentArrow,uturnArrow,leftUpArrow,bentUpArrow,"
        "mathPlus,mathMinus,mathMultiply,mathDivide,mathEqual,mathNotEqual,"
        "flowChartProcess,flowChartAlternateProcess,flowChartDecision,flowChartInputOutput,flowChartPredefinedProcess,flowChartInternalStorage,flowChartDocument,flowChartMultidocument,flowChartTerminator,"
        "star4,star5,star6,star7,star8,star10,star12,star16,star24,star32,irregularSeal1,irregularSeal2,ribbon,ribbon2,ellipseRibbon,ellipseRibbon2,"
        "wedgeRectCallout,wedgeRoundRectCallout,wedgeEllipseCallout,cloudCallout,callout1,callout2,callout3,borderCallout1,borderCallout2,borderCallout3"
    ).split(QLatin1Char(','));
    int order = 0;
    for (const Entry &entry : entries) {
        KoShapeTemplate item;
        item.templateId = QString::fromLatin1(entry.preset);
        item.name = entry.name.toString();
        item.toolTip = item.name;
        item.family = QString::fromLatin1(entry.family);
        const int familiar = familiarOrder.indexOf(item.templateId);
        item.order = familiar >= 0 ? familiar : 1000 + order;
        ++order;
        auto *properties = new KoProperties;
        properties->setProperty(QStringLiteral("officePreset"), item.templateId);
        item.properties = properties;
        addTemplate(item);
    }
}

KoShape *OfficeShapeFactory::createDefaultShape(KoDocumentResourceManager *resources) const
{
    KoProperties properties;
    properties.setProperty(QStringLiteral("officePreset"), QStringLiteral("rect"));
    return createShape(&properties, resources);
}

KoShape *OfficeShapeFactory::createShape(const KoProperties *properties, KoDocumentResourceManager *resources) const
{
    const QString preset = properties ? properties->stringProperty(QStringLiteral("officePreset"), QStringLiteral("rect")) : QStringLiteral("rect");
    const PresetGeometry &data = geometry();
    if (!data.attributes.contains(preset)) { return nullptr; }
    QMap<QString, QString> formulae;
    const QString xml = QStringLiteral("<root xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\">")
        + data.equations.value(preset) + QStringLiteral("</root>");
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("equation")) {
            const auto attrs = reader.attributes();
            formulae.insert(attrs.value(KoXmlNS::draw, QStringLiteral("name")).toString(),
                            attrs.value(KoXmlNS::draw, QStringLiteral("formula")).toString());
        }
    }
    if (reader.hasError()) { return nullptr; }
    auto *shape = new EnhancedPathShape(QRect(0, 0, 21600, 21600));
    shape->setShapeId(KoPathShapeId);
    shape->setStroke(new KoShapeStroke(1.0));
    shape->setBackground(QSharedPointer<KoColorBackground>(new KoColorBackground(QColor(QStringLiteral("#5b9bd5")))));
    if (preset.contains(QLatin1String("Connector")) || preset == QLatin1String("line") || preset == QLatin1String("lineInv") || preset == QLatin1String("arc")) {
        shape->setBackground(QSharedPointer<KoShapeBackground>());
    }
    if (resources && !data.textareas.value(preset).isEmpty()) { shape->createTextShape(resources); }
    shape->setPresetGeometry(preset, data.attributes.value(preset), resources ? data.textareas.value(preset) : QString(), formulae);
    shape->setSize(QSizeF(120, 90));
    return shape;
}

KoShape *OfficeShapeFactory::createShapeFromOdf(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    // EnhancedPathShape::loadOdf() calls reset() and re-reads
    // draw:enhanced-geometry from the document, so building the default "rect"
    // preset first (KoShapeFactoryBase::createShapeFromOdf does that through
    // createDefaultShape()) would parse the preset geometry only to throw it
    // away. Loading a page with hundreds of preset shapes spends most of its
    // time there, so construct the bare shape and load it directly.
    auto *shape = new EnhancedPathShape(QRect(0, 0, 21600, 21600));
    shape->setShapeId(KoPathShapeId);
    shape->setStroke(new KoShapeStroke(1.0));
    shape->setBackground(QSharedPointer<KoColorBackground>(new KoColorBackground(QColor(QStringLiteral("#5b9bd5")))));

    context.odfLoadingContext().styleStack().save();
    const bool loaded = shape->loadOdfDirect(element, context);
    context.odfLoadingContext().styleStack().restore();

    if (!loaded) {
        delete shape;
        return nullptr;
    }
    return shape;
}

bool OfficeShapeFactory::supports(const KoXmlElement &element, KoShapeLoadingContext &) const
{
    const KoXmlElement enhanced = KoXml::namedItemNS(element, KoXmlNS::draw, QStringLiteral("enhanced-geometry"));
    return element.namespaceURI() == KoXmlNS::draw && element.localName() == QLatin1String("custom-shape")
        && enhanced.attributeNS(KoXmlNS::draw, QStringLiteral("type")).startsWith(QLatin1String("ooxml-"));
}
