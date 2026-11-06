/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2008 Jan Hambrecht <jaham@gmx.net>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KoGradientBackground.h"
#include "KoFlake.h"
#include "KoShapeBackground_p.h"
#include <KoOdfGraphicStyles.h>
#include <KoOdfLoadingContext.h>
#include <KoOdfStylesReader.h>
#include <KoShapeSavingContext.h>
#include <KoStyleStack.h>
#include <KoXmlNS.h>
#include <KoXmlReader.h>
#include <KoUnit.h>

#include <FlakeDebug.h>

#include <QBrush>
#include <QLineF>
#include <QPainter>
#include <QSharedPointer>
#include <algorithm>
#include <cmath>

namespace {
QColor colorAt(const QGradientStops &stops, qreal position)
{
    if (position <= stops.first().first)
        return stops.first().second;
    for (int i = 1; i < stops.size(); ++i) {
        if (position <= stops[i].first) {
            const auto &a = stops[i - 1];
            const auto &b = stops[i];
            const qreal t = (position - a.first) / (b.first - a.first);
            return QColor::fromRgbF(a.second.redF() + t * (b.second.redF() - a.second.redF()),
                                   a.second.greenF() + t * (b.second.greenF() - a.second.greenF()),
                                   a.second.blueF() + t * (b.second.blueF() - a.second.blueF()),
                                   a.second.alphaF() + t * (b.second.alphaF() - a.second.alphaF()));
        }
    }
    return stops.last().second;
}

QGradient *withOpacityGradient(const QGradient &colorGradient, KoOdfLoadingContext &context)
{
    const QString name = context.styleStack().property(KoXmlNS::draw, "opacity-name");
    const auto *opacity = context.stylesReader().drawStyles("opacity").value(name);
    if (!opacity || colorGradient.type() != QGradient::LinearGradient)
        return nullptr;
    const QString opacityStyle = opacity->attributeNS(KoXmlNS::draw, "style");
    if (opacityStyle != QLatin1String("linear") && opacityStyle != QLatin1String("axial"))
        return nullptr;

    const QString angleValue = opacity->attributeNS(KoXmlNS::draw, "angle", "0");
    bool legacyAngle = false;
    const qreal tenths = angleValue.toDouble(&legacyAngle);
    const qreal angle = (90 + (legacyAngle ? tenths / 10 : KoUnit::parseAngle(angleValue))) * M_PI / 180;
    const qreal radius = (qAbs(std::cos(angle)) + qAbs(std::sin(angle))) / 2;
    const QPointF delta(std::cos(angle) * radius, -std::sin(angle) * radius);
    QLinearGradient mask(QPointF(0.5, 0.5) + delta, QPointF(0.5, 0.5) - delta);
    mask.setCoordinateMode(QGradient::ObjectBoundingMode);
    const auto colors = colorGradient.stops();
    const bool constantColor = std::all_of(colors.cbegin(), colors.cend(), [&](const QGradientStop &stop) {
        return stop.second == colors.first().second;
    });
    const auto &linear = static_cast<const QLinearGradient &>(colorGradient);
    if (!constantColor && (QLineF(linear.start(), mask.start()).length() > 1e-6
                          || QLineF(linear.finalStop(), mask.finalStop()).length() > 1e-6))
        return nullptr;

    QGradientStops alpha;
    KoXmlElement stop;
    forEachElement(stop, (*opacity)) {
        if (stop.namespaceURI() != QLatin1String("urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0")
            || stop.localName() != QLatin1String("opacity-stop"))
            continue;
        QString offset = stop.attributeNS(KoXmlNS::svg, "offset", "0");
        const bool percent = offset.endsWith(QLatin1Char('%'));
        offset.remove(QLatin1Char('%'));
        QColor color(Qt::white);
        color.setAlphaF(qBound(0.0, stop.attributeNS(KoXmlNS::svg, "stop-opacity", "1").toDouble(), 1.0));
        alpha.append({qBound(0.0, offset.toDouble() / (percent ? 100 : 1), 1.0), color});
    }
    if (alpha.isEmpty()) {
        const qreal border = qBound(0.0, opacity->attributeNS(KoXmlNS::draw, "border", "0%").remove('%').toDouble() / 100, 1.0);
        QColor first(Qt::white), last(Qt::white);
        first.setAlphaF(qBound(0.0, opacity->attributeNS(KoXmlNS::draw, "start", "100%").remove('%').toDouble() / 100, 1.0));
        last.setAlphaF(qBound(0.0, opacity->attributeNS(KoXmlNS::draw, "end", "100%").remove('%').toDouble() / 100, 1.0));
        alpha = {{border, first}, {1.0, last}};
    }
    if (opacityStyle == QLatin1String("axial")) {
        // ODF axial stops describe the center-to-edge half of a symmetric
        // gradient. Mirror them into the full linear axis used by Qt/OOXML.
        QGradientStops mirrored;
        for (const auto &stop : alpha) {
            mirrored.append({(1.0 - stop.first) / 2, stop.second});
            mirrored.append({(1.0 + stop.first) / 2, stop.second});
        }
        alpha = mirrored;
    }
    std::stable_sort(alpha.begin(), alpha.end(), [](const QGradientStop &a, const QGradientStop &b) { return a.first < b.first; });
    QList<qreal> positions;
    for (const auto &stop : colors) positions.append(stop.first);
    for (const auto &stop : alpha) positions.append(stop.first);
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    QGradientStops combined;
    for (qreal position : positions) {
        QColor color = colorAt(colors, position);
        color.setAlphaF(color.alphaF() * colorAt(alpha, position).alphaF());
        combined.append({position, color});
    }
    auto *result = new QLinearGradient(constantColor ? mask : linear);
    result->setStops(combined);
    return result;
}
}

class KoGradientBackgroundPrivate : public KoShapeBackgroundPrivate
{
public:
    KoGradientBackgroundPrivate()
        : gradient(nullptr)
    {
    }

    QGradient *gradient;
    QTransform matrix;
};

KoGradientBackground::KoGradientBackground(QGradient *gradient, const QTransform &matrix)
    : KoShapeBackground(*(new KoGradientBackgroundPrivate()))
{
    Q_D(KoGradientBackground);
    d->gradient = gradient;
    d->matrix = matrix;
    Q_ASSERT(d->gradient);
    Q_ASSERT(d->gradient->coordinateMode() == QGradient::ObjectBoundingMode);
}

KoGradientBackground::KoGradientBackground(const QGradient &gradient, const QTransform &matrix)
    : KoShapeBackground(*(new KoGradientBackgroundPrivate()))
{
    Q_D(KoGradientBackground);
    d->gradient = KoFlake::cloneGradient(&gradient);
    d->matrix = matrix;
    Q_ASSERT(d->gradient);
    Q_ASSERT(d->gradient->coordinateMode() == QGradient::ObjectBoundingMode);
}

KoGradientBackground::~KoGradientBackground()
{
    Q_D(KoGradientBackground);
    delete d->gradient;
}

void KoGradientBackground::setTransform(const QTransform &matrix)
{
    Q_D(KoGradientBackground);
    d->matrix = matrix;
}

QTransform KoGradientBackground::transform() const
{
    Q_D(const KoGradientBackground);
    return d->matrix;
}

void KoGradientBackground::setGradient(const QGradient &gradient)
{
    Q_D(KoGradientBackground);
    delete d->gradient;

    d->gradient = KoFlake::cloneGradient(&gradient);
    Q_ASSERT(d->gradient);
    Q_ASSERT(d->gradient->coordinateMode() == QGradient::ObjectBoundingMode);
}

const QGradient *KoGradientBackground::gradient() const
{
    Q_D(const KoGradientBackground);
    return d->gradient;
}

void KoGradientBackground::paint(QPainter &painter,
                                 const KoViewConverter & /*converter*/,
                                 KoShapePaintingContext & /*context*/,
                                 const QPainterPath &fillPath) const
{
    Q_D(const KoGradientBackground);
    if (!d->gradient)
        return;
    QBrush brush(*d->gradient);
    brush.setTransform(d->matrix);

    painter.setBrush(brush);
    painter.drawPath(fillPath);
}

void KoGradientBackground::fillStyle(KoGenStyle &style, KoShapeSavingContext &context)
{
    Q_D(KoGradientBackground);
    if (!d->gradient)
        return;
    QBrush brush(*d->gradient);
    brush.setTransform(d->matrix);
    KoOdfGraphicStyles::saveOdfFillStyle(style, context.mainStyles(), brush);
}

bool KoGradientBackground::loadStyle(KoOdfLoadingContext &context, const QSizeF &shapeSize)
{
    Q_D(KoGradientBackground);
    KoStyleStack &styleStack = context.styleStack();
    if (!styleStack.hasProperty(KoXmlNS::draw, "fill"))
        return false;

    QString fillStyle = styleStack.property(KoXmlNS::draw, "fill");
    if (fillStyle == "gradient") {
        QBrush brush = KoOdfGraphicStyles::loadOdfGradientStyle(styleStack, context.stylesReader(), shapeSize);
        const QGradient *gradient = brush.gradient();
        if (gradient) {
            d->gradient = KoFlake::cloneGradient(gradient);
            d->matrix = brush.transform();

            // Gopalakrishna Bhat: If the brush has transparency then we ignore the draw:opacity property and use the brush transparency.
            //  Brush will have transparency if the svg:linearGradient stop point has stop-opacity property otherwise it is opaque
            if (brush.isOpaque() && styleStack.hasProperty(KoXmlNS::draw, "opacity")) {
                QString opacityPercent = styleStack.property(KoXmlNS::draw, "opacity");
                if (!opacityPercent.isEmpty() && opacityPercent.right(1) == "%") {
                    float opacity = qMin(QStringView{opacityPercent}.left(opacityPercent.length() - 1).toDouble(), 100.0) / 100;
                    QGradientStops stops;
                    foreach (QGradientStop stop, d->gradient->stops()) {
                        stop.second.setAlphaF(opacity);
                        stops << stop;
                    }
                    d->gradient->setStops(stops);
                }
            }

            if (QGradient *combined = withOpacityGradient(*d->gradient, context)) {
                delete d->gradient;
                d->gradient = combined;
            }

            return true;
        }
    }
    return false;
}
