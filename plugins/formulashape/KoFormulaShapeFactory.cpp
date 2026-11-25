/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2006 Martin Pfeiffer <hubipete@gmx.net>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "KoFormulaShapeFactory.h"

#include <KLocalizedString>

#include <KoIcon.h>
#include <KoProperties.h>
#include <KoOdfLoadingContext.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeLoadingContext.h>
#include <KoXmlNS.h>
#include <KoXmlReader.h>

#include "FormulaDebug.h"
#include "KoFormulaShape.h"
#include "FormulaData.h"

KoFormulaShapeFactory::KoFormulaShapeFactory()
    : KoShapeFactoryBase(KoFormulaShapeId, i18n("Formula"))
{
    setToolTip(i18n("A formula"));
    setIconName(koIconName("x-shape-formula"));

    // The following lines let the formula shape load both embedded and
    // inline formulas.
    //
    // The line below tells the shape registry which XML elements that
    // the shape supports.
    //
    // FIXME: Find out if inline formulas are supported by ODF.

    QList<QPair<QString, QStringList>> elementNamesList;
    elementNamesList.append(qMakePair(QString(KoXmlNS::draw), QStringList("object")));
    elementNamesList.append(qMakePair(QString(KoXmlNS::math), QStringList("math")));
    setXmlElements(elementNamesList);

    setLoadingPriority(1);
    /*    KoShapeTemplate t;
        t.id = KoFormulaShapeId;
        t.name = i18n("Formula");
        t.toolTip = i18n("A formula");
        t.icon = ""; //TODO add it
        props = new KoProperties();
        t.properties = props;
        addTemplate( t );*/
}

KoFormulaShapeFactory::~KoFormulaShapeFactory() = default;

KoShape *KoFormulaShapeFactory::createDefaultShape(KoDocumentResourceManager *resourceManager) const
{
    return createShape(nullptr, resourceManager);
}

KoShape *KoFormulaShapeFactory::createShape(const KoProperties *params, KoDocumentResourceManager *resourceManager) const
{
    KoFormulaShape *formula = new KoFormulaShape(resourceManager);
    formula->setShapeId(KoFormulaShapeId);
    const bool standalone = params && params->boolProperty("standalone");
    const QString size = standalone ? QStringLiteral("14pt") : QStringLiteral("12pt");
    auto *root = formula->formulaData()->formulaElement();
    root->setAttribute(QStringLiteral("fontsize"), size);
    root->setAttribute(QStringLiteral("mathsize"), size);
    root->setAttribute(QStringLiteral("display"), standalone ? QStringLiteral("block") : QStringLiteral("inline"));
    root->setAttribute(QStringLiteral("displaystyle"), standalone ? QStringLiteral("true") : QStringLiteral("false"));
    // Keep the natural formula size. Shrinking the empty placeholder's
    // 30pt frame to 20pt also shrinks the text typed into the equation.
    formula->updateLayout();
    return formula;
}

bool KoFormulaShapeFactory::supports(const KoXmlElement &e, KoShapeLoadingContext &context) const
{
    Q_UNUSED(context);
    if ((e.localName() == "math" && e.namespaceURI() == KoXmlNS::math)) {
        return true;
    }

    if (e.localName() == "object" && e.namespaceURI() == KoXmlNS::draw) {
        if (!KoXml::namedItemNS(e, KoXmlNS::math, "math").isNull()) {
            return true;
        }
        QString href = e.attributeNS(KoXmlNS::xlink, "href");
        if (!href.isEmpty()) {
            // check the mimetype
            if (href.startsWith(QLatin1String("./"))) {
                href.remove(0, 2);
            }

            const QString mimetype = context.odfLoadingContext().mimeTypeForPath(href);
            return mimetype.isEmpty() || mimetype == "application/vnd.oasis.opendocument.formula";
        }
    }

    return false;
}
