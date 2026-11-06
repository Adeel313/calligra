/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2011 Jan Hambrecht <jaham@gmx.net>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "SvgShapeFactory.h"
#include "KoShapeGroup.h"
#include "KoShapeGroupCommand.h"
#include "KoShapeLoadingContext.h"
#include "KoShapeRegistry.h"
#include "SvgParser.h"
#include <KLocalizedString>
#include <KoOdfLoadingContext.h>
#include <KoStore.h>
#include <KoStoreDevice.h>
#include <KoStyleStack.h>
#include <KoUnit.h>
#include <KoXmlNS.h>

#define SVGSHAPEFACTORYID "SvgShapeFactory"

namespace {
class SvgFrameGroup : public KoShapeGroup
{
public:
    QTransform framePlacement(const KoXmlElement &frame, KoShapeLoadingContext &context)
    {
        QTransform placement;
        placement.translate(KoUnit::parseValue(frame.attributeNS(KoXmlNS::svg, "x")),
                            KoUnit::parseValue(frame.attributeNS(KoXmlNS::svg, "y")));
        return placement * parseOdfTransform(frame.attributeNS(KoXmlNS::draw, "transform"), context);
    }

    void loadFrame(const KoXmlElement &frame, KoShapeLoadingContext &context)
    {
        loadOdfAttributes(frame, context, OdfMandatories | OdfAdditionalAttributes | OdfCommonChildElements);
    }
};
}

SvgShapeFactory::SvgShapeFactory()
    : KoShapeFactoryBase(SVGSHAPEFACTORYID, i18n("Embedded svg shape"))
{
    setLoadingPriority(4);
    setXmlElementNames(QString(KoXmlNS::draw), QStringList("image"));
    // hide from add shapes docker as the shape is not able to be dragged onto
    // the canvas as createDefaultShape returns 0.
    setHidden(true);
}

SvgShapeFactory::~SvgShapeFactory() = default;

void SvgShapeFactory::addToRegistry()
{
    KoShapeRegistry *registry = KoShapeRegistry::instance();
    if (!registry->contains(QString(SVGSHAPEFACTORYID))) {
        registry->addFactory(new SvgShapeFactory);
    }
}

bool SvgShapeFactory::supports(const KoXmlElement &element, KoShapeLoadingContext &context) const
{
    if (element.localName() == "image" && element.namespaceURI() == KoXmlNS::draw) {
        QString href = element.attribute("href");
        if (href.isEmpty())
            return false;

        // check the mimetype
        if (href.startsWith(QLatin1String("./"))) {
            href.remove(0, 2);
        }

        QString mimetype = context.odfLoadingContext().mimeTypeForPath(href, true);
        return (mimetype == "image/svg+xml");
    }

    return false;
}

KoShape *SvgShapeFactory::createShapeFromOdf(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    const KoXmlElement &imageElement(KoXml::namedItemNS(element, KoXmlNS::draw, "image"));
    if (imageElement.isNull()) {
        errorFlake << "svg image element not found";
        return nullptr;
    }

    if (imageElement.tagName() == "image") {
        debugFlake << "trying to create shapes from svg image";
        QString href = imageElement.attribute("href");
        if (href.isEmpty())
            return nullptr;

        // check the mimetype
        if (href.startsWith(QLatin1String("./"))) {
            href.remove(0, 2);
        }
        QString mimetype = context.odfLoadingContext().mimeTypeForPath(href);
        debugFlake << mimetype;
        if (mimetype != "image/svg+xml")
            return nullptr;

        if (!context.odfLoadingContext().store()->open(href))
            return nullptr;

        KoStoreDevice dev(context.odfLoadingContext().store());
        KoXmlDocument xmlDoc;

        int line, col;
        QString errormessage;

        const bool parsed = xmlDoc.setContent(&dev, &errormessage, &line, &col);

        context.odfLoadingContext().store()->close();

        if (!parsed) {
            errorFlake << "Error while parsing file: "
                       << "at line " << line << " column: " << col << " message: " << errormessage << Qt::endl;
            return nullptr;
        }

        SvgParser parser(context.documentResourceManager());

        QSizeF viewport;
        QList<KoShape *> shapes = parser.parseSvg(xmlDoc.documentElement(), &viewport);
        if (shapes.isEmpty())
            return nullptr;

        int zIndex = 0;
        if (element.hasAttributeNS(KoXmlNS::draw, "z-index")) {
            zIndex = element.attributeNS(KoXmlNS::draw, "z-index").toInt();
        } else {
            zIndex = context.zIndex();
        }

        // Embedded SVG coordinates belong to the SVG viewport, not to the
        // slide. Fit the entire fragment, including its margins, to its frame.
        // Reloading a single parsed path from draw:frame also loses its SVG data.
        auto *svgGroup = new SvgFrameGroup;
        const QSizeF frameSize(KoUnit::parseValue(element.attributeNS(KoXmlNS::svg, "width")),
                               KoUnit::parseValue(element.attributeNS(KoXmlNS::svg, "height")));
        QTransform fit;
        if (viewport.width() > 0 && viewport.height() > 0 && frameSize.width() > 0 && frameSize.height() > 0) {
            fit.scale(frameSize.width() / viewport.width(), frameSize.height() / viewport.height());
        }
        fit *= svgGroup->framePlacement(element, context);
        // Ordinary shape groups do not propagate scale/rotation to children.
        // Transform the parsed leaves before grouping them in slide coordinates.
        const auto place = [&](const auto &self, KoShape *shape) -> void {
            if (auto *group = dynamic_cast<KoShapeGroup *>(shape)) {
                for (KoShape *child : group->shapes()) self(self, child);
            } else {
                shape->applyAbsoluteTransformation(fit);
            }
        };
        for (KoShape *shape : shapes) place(place, shape);
        KoShapeGroupCommand cmd(svgGroup, shapes);
        cmd.redo();
        svgGroup->loadFrame(element, context);
        svgGroup->setZIndex(zIndex);

        return svgGroup;
    }

    return nullptr;
}
