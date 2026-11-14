/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2008 Thorsten Zachmann <zachmann@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KPrPlaceholderStrategy.h"

#include "KPrPlaceholderPictureStrategy.h"
#include "KPrPlaceholderTextStrategy.h"
#include "StageDebug.h"

#include <QPainter>
#include <QPen>
#include <QTextOption>

#include <KLazyLocalizedString>
#include <KLocalizedString>

#include <KoShape.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeLoadingContext.h>
#include <KoShapeRegistry.h>
#include <KoShapeSavingContext.h>
#include <KoXmlWriter.h>

static const class PlaceholderData
{
public:
    const char *m_presentationClass;
    const char *m_shapeId;
    const char *m_xmlElement;
    const KLazyLocalizedString m_text;
} placeholderData[] = {{"title", "TextShapeID", "<draw:text-box/>", kli18n("Click to add a title")},
                       {"outline", "TextShapeID", "<draw:text-box/>", kli18n("Click to add content")},
                       {"subtitle", "TextShapeID", "<draw:text-box/>", kli18n("Click to add a subtitle")},
                       {"text", "TextShapeID", "<draw:text-box/>", kli18n("Click to add text")},
                       {"notes", "TextShapeID", "<draw:text-box/>", kli18n("Click to add notes")},
                       {"date-time", "TextShapeID", "<draw:text-box/>", kli18n( "Double click to add data/time" ) },
                       {"footer", "TextShapeID", "<draw:text-box/>", kli18n( "Double click to add footer" ) },
                       {"header", "TextShapeID", "<draw:text-box/>", kli18n( "Double click to add header" ) },
                       {"page-number", "TextShapeID", "<draw:text-box/>", kli18n( "Double click to add page number" ) },
                       {"graphic",
                        "PictureShape",
                        "<draw:image xlink:href=\"\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>",
                        kli18n("Double click to add a picture")},
                       {"chart",
                        "ChartShape",
                        "<draw:object xlink:href=\"\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>",
                        kli18n("Double click to add a chart")},
                       {"object",
                        "ChartShape",
                        "<draw:object xlink:href=\"\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>",
                        kli18n("Double click to add an object")}};

static QMap<QString, const PlaceholderData *> s_placeholderMap;

void fillPlaceholderMap()
{
    const unsigned int numPlaceholderData = sizeof(placeholderData) / sizeof(*placeholderData);
    for (unsigned int i = 0; i < numPlaceholderData; ++i) {
        s_placeholderMap.insert(placeholderData[i].m_presentationClass, &placeholderData[i]);
    }
}

KPrPlaceholderStrategy *KPrPlaceholderStrategy::create(const QString &presentationClass)
{
    if (s_placeholderMap.isEmpty()) {
        fillPlaceholderMap();
    }

    KPrPlaceholderStrategy *strategy = nullptr;
    if (presentationClass == "graphic") {
        strategy = new KPrPlaceholderPictureStrategy();
    }
    // TODO make nice
    else if (presentationClass == "outline" || presentationClass == "title" || presentationClass == "subtitle") {
        strategy = new KPrPlaceholderTextStrategy(presentationClass);
    } else {
        if (s_placeholderMap.contains(presentationClass)) {
            strategy = new KPrPlaceholderStrategy(presentationClass);
        } else {
            warnStage << "Unsupported placeholder strategy:" << presentationClass;
        }
    }
    return strategy;
}

bool KPrPlaceholderStrategy::supported(const QString &presentationClass)
{
    if (s_placeholderMap.isEmpty()) {
        fillPlaceholderMap();
    }

    return s_placeholderMap.contains(presentationClass);
}

KPrPlaceholderStrategy::KPrPlaceholderStrategy(const QString &presentationClass)
    : m_placeholderData(s_placeholderMap[presentationClass])
{
}

KPrPlaceholderStrategy::~KPrPlaceholderStrategy() = default;

KoShape *KPrPlaceholderStrategy::createShape(KoDocumentResourceManager *rm)
{
    KoShape *shape = nullptr;
    KoShapeFactoryBase *factory = KoShapeRegistry::instance()->value(m_placeholderData->m_shapeId);
    Q_ASSERT(factory);
    if (factory) {
        shape = factory->createDefaultShape(rm);
    } else {
        warnStage << "no factory found for placeholder";
    }
    return shape;
}

void KPrPlaceholderStrategy::paint(QPainter &painter, const KoViewConverter &converter, const QRectF &rect, KoShapePaintingContext & /*paintcontext*/)
{
    painter.save();
    KoShape::applyConversion(painter, converter);
    QFont font = painter.font();
    const int promptSize = qstrcmp(m_placeholderData->m_presentationClass, "title") == 0 ? 48 : 36;
    font.setPixelSize(qMax(14, qMin(promptSize, qRound(rect.height() * 0.65))));
    font.setBold(true);
    painter.setFont(font);
    QTextOption options(Qt::AlignCenter);
    options.setWrapMode(QTextOption::WordWrap);
    const QRectF textRect = rect.adjusted(8, 4, -8, -4);
    painter.setPen(QColor(0, 0, 0, 200));
    painter.drawText(textRect.translated(1.5, 1.5), text(), options);
    painter.setPen(Qt::white);
    painter.drawText(textRect, text(), options);

    // Cosmetic widths keep the outline legible at every slide zoom level.
    QPen edge(QColor(0, 0, 0, 180), 4);
    edge.setCosmetic(true);
    painter.setPen(edge);
    painter.drawRect(rect);
    QPen outline(Qt::white, 2);
    outline.setCosmetic(true);
    painter.setPen(outline);
    painter.drawRect(rect);
    painter.restore();
}

void KPrPlaceholderStrategy::saveOdf(KoShapeSavingContext &context)
{
    KoXmlWriter &writer = context.xmlWriter();
    writer.addCompleteElement(m_placeholderData->m_xmlElement);
}

bool KPrPlaceholderStrategy::loadOdf(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    Q_UNUSED(element);
    Q_UNUSED(context);
    return true;
}

QString KPrPlaceholderStrategy::text() const
{
    return m_placeholderData->m_text.toString();
}

void KPrPlaceholderStrategy::init(KoDocumentResourceManager *)
{
}

KoShapeUserData *KPrPlaceholderStrategy::userData() const
{
    return nullptr;
}
