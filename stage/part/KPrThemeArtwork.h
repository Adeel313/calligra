/* SPDX-License-Identifier: LGPL-2.0-or-later */
#ifndef KPRTHEMEARTWORK_H
#define KPRTHEMEARTWORK_H

#include <KLocalizedString>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

namespace KPrThemeArtwork
{
enum class Motif {
    Bands, Grid, Arcs, Columns, Diagonal, Dots, Current, Horizon, Prism,
    Mosaic, Circuit, Folio, Canopy, Peaks, Blueprint, Orbit, Ribbon, Halftone
};

struct Theme {
    QString name;
    QColor paper;
    QColor ink;
    QColor accent;
    Motif motif;
};

inline QList<Theme> themes()
{
    return {
        {i18n("Meridian"), QColor(248, 251, 253), QColor(20, 39, 63), QColor(15, 133, 147), Motif::Bands},
        {i18n("Ledger"), QColor(250, 250, 246), QColor(28, 53, 48), QColor(89, 133, 106), Motif::Grid},
        {i18n("Contour"), QColor(250, 248, 253), QColor(54, 38, 74), QColor(133, 98, 174), Motif::Arcs},
        {i18n("Editorial"), QColor(253, 250, 245), QColor(59, 42, 34), QColor(178, 99, 64), Motif::Columns},
        {i18n("Vector"), QColor(247, 249, 255), QColor(23, 38, 73), QColor(61, 93, 188), Motif::Diagonal},
        {i18n("Field Notes"), QColor(247, 251, 250), QColor(27, 52, 59), QColor(28, 133, 114), Motif::Dots},
        {i18n("Pacific"), QColor(241, 252, 253), QColor(11, 65, 89), QColor(0, 139, 169), Motif::Current},
        {i18n("Dusk"), QColor(251, 245, 252), QColor(62, 37, 92), QColor(201, 92, 138), Motif::Horizon},
        {i18n("Prism"), QColor(251, 249, 255), QColor(53, 38, 105), QColor(100, 83, 224), Motif::Prism},
        {i18n("Mosaic"), QColor(255, 250, 243), QColor(90, 51, 36), QColor(222, 112, 50), Motif::Mosaic},
        {i18n("Circuit"), QColor(240, 250, 247), QColor(12, 66, 61), QColor(12, 153, 107), Motif::Circuit},
        {i18n("Folio"), QColor(255, 252, 242), QColor(52, 44, 39), QColor(164, 103, 48), Motif::Folio},
        {i18n("Canopy"), QColor(244, 252, 242), QColor(23, 65, 46), QColor(71, 147, 76), Motif::Canopy},
        {i18n("Alpine"), QColor(244, 251, 255), QColor(37, 68, 100), QColor(59, 139, 182), Motif::Peaks},
        {i18n("Blueprint"), QColor(243, 248, 255), QColor(24, 58, 112), QColor(40, 111, 204), Motif::Blueprint},
        {i18n("Orbit"), QColor(246, 247, 255), QColor(29, 39, 100), QColor(113, 88, 220), Motif::Orbit},
        {i18n("Ribbon"), QColor(255, 247, 251), QColor(101, 30, 70), QColor(204, 72, 130), Motif::Ribbon},
        {i18n("Halftone"), QColor(255, 251, 240), QColor(83, 55, 24), QColor(226, 148, 35), Motif::Halftone},
        {i18n("Glacier"), QColor(238, 248, 255), QColor(20, 60, 95), QColor(55, 173, 218), Motif::Prism},
        {i18n("Ochre"), QColor(254, 247, 230), QColor(91, 51, 25), QColor(189, 121, 36), Motif::Horizon},
        {i18n("Terracotta"), QColor(255, 247, 241), QColor(93, 44, 43), QColor(192, 78, 61), Motif::Ribbon},
        {i18n("Indigo"), QColor(248, 247, 255), QColor(50, 41, 111), QColor(102, 87, 230), Motif::Circuit},
        {i18n("Rainforest"), QColor(237, 250, 245), QColor(6, 63, 58), QColor(9, 138, 113), Motif::Peaks},
        {i18n("Coral Coast"), QColor(255, 248, 246), QColor(118, 44, 55), QColor(226, 95, 94), Motif::Current},
    };
}

inline QImage artwork(const Theme &theme, const QSize &size, bool preview = false)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(theme.paper);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(size.width() / 960.0, size.height() / 540.0);
    QColor faint = theme.accent;
    faint.setAlpha(42);
    painter.setPen(Qt::NoPen);
    QPainterPath corner;
    switch (theme.motif) {
    case Motif::Bands:
        painter.fillRect(QRectF(0, 486, 960, 54), theme.ink);
        painter.fillRect(QRectF(0, 486, 960, 7), theme.accent);
        corner.addPolygon(QPolygonF{{640, 540}, {700, 493}, {752, 493}, {693, 540}});
        painter.fillPath(corner, theme.accent);
        break;
    case Motif::Grid:
        painter.fillRect(QRectF(0, 0, 12, 540), theme.ink);
        painter.fillRect(QRectF(48, 474, 864, 2), theme.accent);
        painter.setPen(QPen(faint, 2));
        for (int x = 688; x <= 904; x += 24)
            painter.drawLine(x, 430, x, 520);
        for (int y = 430; y <= 520; y += 18)
            painter.drawLine(688, y, 904, y);
        break;
    case Motif::Arcs:
        painter.fillRect(QRectF(0, 518, 960, 22), theme.ink);
        painter.setPen(QPen(faint, 3));
        for (int radius = 58; radius <= 174; radius += 29)
            painter.drawEllipse(QPointF(920, 540), radius, radius);
        painter.fillRect(QRectF(48, 44, 80, 5), theme.accent);
        break;
    case Motif::Columns:
        painter.fillRect(QRectF(48, 40, 864, 3), theme.ink);
        painter.fillRect(QRectF(48, 487, 864, 2), theme.ink);
        painter.fillRect(QRectF(48, 42, 104, 8), theme.accent);
        painter.fillRect(QRectF(760, 492, 152, 48), faint);
        break;
    case Motif::Diagonal:
        corner.addPolygon(QPolygonF{{778, 540}, {960, 378}, {960, 540}});
        painter.fillPath(corner, theme.ink);
        corner.clear();
        corner.addPolygon(QPolygonF{{741, 540}, {960, 345}, {960, 363}, {762, 540}});
        painter.fillPath(corner, theme.accent);
        break;
    case Motif::Dots:
        painter.fillRect(QRectF(0, 512, 960, 28), theme.accent);
        painter.setBrush(faint);
        for (int y = 406; y <= 496; y += 18) {
            for (int x = 750; x <= 912; x += 18)
                painter.drawEllipse(QPointF(x, y), 3, 3);
        }
        break;
    case Motif::Current: {
        QLinearGradient wash(0, 440, 960, 540);
        wash.setColorAt(0, theme.accent);
        wash.setColorAt(1, theme.ink);
        corner.moveTo(0, 496);
        corner.cubicTo(230, 435, 445, 565, 960, 427);
        corner.lineTo(960, 540);
        corner.lineTo(0, 540);
        corner.closeSubpath();
        painter.fillPath(corner, wash);
        break;
    }
    case Motif::Horizon: {
        QLinearGradient sky(0, 320, 0, 540);
        sky.setColorAt(0, theme.paper);
        sky.setColorAt(1, faint);
        painter.fillRect(QRectF(0, 315, 960, 225), sky);
        painter.setBrush(theme.accent);
        painter.drawEllipse(QPointF(842, 465), 42, 42);
        painter.fillRect(QRectF(0, 492, 960, 48), theme.ink);
        break;
    }
    case Motif::Prism:
        painter.setBrush(faint);
        painter.drawPolygon(QPolygonF{{680, 540}, {960, 264}, {960, 540}});
        painter.setBrush(theme.accent);
        painter.drawPolygon(QPolygonF{{792, 540}, {960, 357}, {960, 540}});
        painter.setBrush(theme.ink);
        painter.drawPolygon(QPolygonF{{855, 540}, {960, 421}, {960, 540}});
        break;
    case Motif::Mosaic:
        for (int column = 0; column < 12; ++column) {
            for (int row = 0; row < 2; ++row) {
                QColor square = (column + row) % 3 ? theme.accent : theme.ink;
                square.setAlpha(45 + ((column * 37 + row * 65) % 175));
                painter.fillRect(QRectF(480 + column * 40, 460 + row * 40, 38, 38), square);
            }
        }
        break;
    case Motif::Circuit:
        for (int i = 0; i < 5; ++i) {
            const int x = 670 + 48 * i;
            const int y = 426 + (i % 3) * 22;
            painter.setPen(QPen(i % 2 ? faint : theme.accent, 3));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(QPolygonF{{qreal(x - 100), 540}, {qreal(x - 45), qreal(y + 34)}, {qreal(x), qreal(y + 34)}, {qreal(x + 36), qreal(y)}});
            painter.drawEllipse(QPointF(x + 36, y), 6, 6);
        }
        break;
    case Motif::Folio:
        painter.fillRect(QRectF(0, 0, 32, 540), theme.accent);
        painter.setPen(QPen(faint, 3));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(53, 30, 874, 480));
        painter.fillRect(QRectF(745, 498, 182, 12), theme.ink);
        break;
    case Motif::Canopy: {
        painter.setBrush(faint);
        painter.drawEllipse(QRectF(732, -130, 370, 291));
        painter.drawEllipse(QRectF(676, 410, 340, 270));
        painter.setBrush(theme.accent);
        painter.drawEllipse(QRectF(818, 467, 220, 170));
        break;
    }
    case Motif::Peaks:
        painter.setBrush(faint);
        painter.drawPolygon(QPolygonF{{360, 540}, {561, 457}, {635, 483}, {774, 369}, {960, 540}});
        painter.setBrush(theme.accent);
        painter.drawPolygon(QPolygonF{{582, 540}, {734, 438}, {830, 488}, {918, 411}, {960, 441}, {960, 540}});
        painter.setBrush(theme.ink);
        painter.drawPolygon(QPolygonF{{680, 540}, {849, 500}, {960, 467}, {960, 540}});
        break;
    case Motif::Blueprint:
        painter.setPen(QPen(faint, 2));
        for (int i = 32; i < 960; i += 24)
            painter.drawLine(i, 430, i, 540);
        for (int i = 432; i < 540; i += 18)
            painter.drawLine(0, i, 960, i);
        painter.fillRect(QRectF(0, 532, 960, 8), theme.accent);
        break;
    case Motif::Orbit:
        painter.setPen(QPen(faint, 4));
        painter.setBrush(Qt::NoBrush);
        for (int i = 0; i < 4; ++i)
            painter.drawEllipse(QRectF(688 + i * 20, 338 + i * 24, 320 - i * 40, 210 - i * 30));
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.accent);
        painter.drawEllipse(QPointF(913, 391), 14, 14);
        break;
    case Motif::Ribbon:
        corner.moveTo(600, 540);
        corner.cubicTo(728, 488, 804, 452, 960, 453);
        corner.lineTo(960, 505);
        corner.cubicTo(808, 502, 754, 522, 707, 540);
        corner.closeSubpath();
        painter.fillPath(corner, theme.accent);
        painter.fillRect(QRectF(0, 534, 960, 6), theme.ink);
        break;
    case Motif::Halftone:
        painter.setBrush(theme.accent);
        for (int row = 0; row < 5; ++row) {
            for (int column = 0; column < 12; ++column) {
                const qreal radius = 2 + (row + column) * 0.38;
                painter.drawEllipse(QPointF(690 + column * 22, 434 + row * 22), radius, radius);
            }
        }
        break;
    }
    if (preview) {
        painter.setPen(theme.ink);
        QFont title = painter.font();
        title.setPixelSize(92);
        title.setWeight(QFont::DemiBold);
        painter.setFont(title);
        painter.drawText(QRectF(54, 126, 848, 128), Qt::AlignLeft | Qt::AlignVCenter, theme.name);
        painter.fillRect(QRectF(56, 289, 260, 9), theme.accent);
        painter.fillRect(QRectF(56, 323, 168, 7), faint);
    }
    return image;
}
}

#endif
