/* SPDX-License-Identifier: LGPL-2.0-or-later */
// Mapping between the legacy binary PPT SlideShowSlideInfoAtom transition
// values and the ODF smil:* / presentation:* draw-page properties used by
// Stage.  Shared by PptExport.cpp and PptToOdp.cpp so both directions agree.
#pragma once

#include <QString>

namespace PptTransition {

// MS-PPT effectType values (SlideShowSlideInfoAtom).
enum EffectType {
    Cut = 0,
    Random = 1,
    Blinds = 2,
    Checker = 3,
    Cover = 4,
    Dissolve = 5,
    Fade = 6,
    Uncover = 7,
    RandomBars = 8,
    Strips = 9,
    Wipe = 10,
    Box = 11,
    Split = 13,
    Diamond = 17,
    Plus = 18,
    Wedge = 19,
    Push = 20,
    Comb = 21,
    Newsflash = 22,
    AlphaFade = 23,
    Wheel = 26,
    Circle = 27
};

// Binary effectDirection uses 0=left, 1=top, 2=right, 3=bottom for the
// directional effects (<-> ODF "from*" / "to*" subtypes).
struct Mapped {
    quint8 effectType = Cut;
    quint8 direction = 0;
};

inline Mapped fromOdf(const QString &type, const QString &subtype, bool reverse)
{
    Mapped m;
    if (type == QLatin1String("fade")) {
        m.effectType = subtype == QLatin1String("fadeOverColor") ? Newsflash : Fade;
    } else if (type == QLatin1String("barWipe")) {
        m.effectType = Wipe;
        // Stage's barWipe names where the new page enters; binary Wipe uses
        // 0=left, 1=top, 2=right, 3=bottom.
        m.direction = (subtype == QLatin1String("leftToRight")) ? (reverse ? 2 : 0)
                                                               : (reverse ? 3 : 1);
    } else if (type == QLatin1String("slideWipe")) {
        m.effectType = Push;
        QString dir = subtype;
        if (reverse) {
            if (dir == QLatin1String("fromLeft")) dir = QStringLiteral("fromRight");
            else if (dir == QLatin1String("fromRight")) dir = QStringLiteral("fromLeft");
            else if (dir == QLatin1String("fromTop")) dir = QStringLiteral("fromBottom");
            else if (dir == QLatin1String("fromBottom")) dir = QStringLiteral("fromTop");
        }
        if (dir == QLatin1String("fromLeft")) m.direction = 0;
        else if (dir == QLatin1String("fromTop")) m.direction = 1;
        else if (dir == QLatin1String("fromRight")) m.direction = 2;
        else if (dir == QLatin1String("fromBottom")) m.direction = 3;
    } else if (type == QLatin1String("fourBoxWipe")
               || type == QLatin1String("boxWipe")) {
        m.effectType = Box;
    } else if (type == QLatin1String("irisWipe")) {
        m.effectType = (subtype == QLatin1String("diamond")) ? Diamond : Box;
    } else if (type == QLatin1String("triangleWipe")) {
        m.effectType = Wedge;
    } else if (type == QLatin1String("pinWheelWipe")) {
        m.effectType = Wheel;
    } else if (type == QLatin1String("ellipseWipe")) {
        m.effectType = Circle;
    } else if (type == QLatin1String("snakeWipe")) {
        m.effectType = Comb;
    } else if (type == QLatin1String("clockWipe")) {
        m.effectType = Wheel;
    }
    return m;
}

struct OdfMapped {
    QString type;
    QString subtype;
    bool reverse = false;
};

inline OdfMapped toOdf(quint8 effectType, quint8 direction)
{
    OdfMapped m;
    switch (effectType) {
    case Fade:
        m.type = QStringLiteral("fade");
        m.subtype = QStringLiteral("crossfade");
        break;
    case Newsflash:
        m.type = QStringLiteral("fade");
        m.subtype = QStringLiteral("fadeOverColor");
        break;
    case Wipe:
        m.type = QStringLiteral("barWipe");
        if (direction == 0) m.subtype = QStringLiteral("leftToRight");
        else if (direction == 2) { m.subtype = QStringLiteral("leftToRight"); m.reverse = true; }
        else if (direction == 3) { m.subtype = QStringLiteral("topToBottom"); m.reverse = true; }
        else m.subtype = QStringLiteral("topToBottom");
        break;
    case Push:
    case Cover:
    case Uncover:
        m.type = QStringLiteral("slideWipe");
        if (direction == 0) m.subtype = QStringLiteral("fromLeft");
        else if (direction == 1) m.subtype = QStringLiteral("fromTop");
        else if (direction == 2) m.subtype = QStringLiteral("fromRight");
        else m.subtype = QStringLiteral("fromBottom");
        m.reverse = (effectType == Uncover);
        break;
    case Box:
    case Checker:
        m.type = QStringLiteral("fourBoxWipe");
        m.subtype = QStringLiteral("cornersIn");
        break;
    case Diamond:
        m.type = QStringLiteral("irisWipe");
        m.subtype = QStringLiteral("diamond");
        break;
    case Circle:
        m.type = QStringLiteral("ellipseWipe");
        m.subtype = QStringLiteral("circle");
        break;
    case Wedge:
        m.type = QStringLiteral("triangleWipe");
        m.subtype = QStringLiteral("down");
        break;
    case Wheel:
        m.type = QStringLiteral("pinWheelWipe");
        m.subtype = QStringLiteral("fourBlade");
        break;
    case Comb:
    case Split:
        m.type = QStringLiteral("snakeWipe");
        m.subtype = QStringLiteral("topToBottom");
        break;
    case Blinds:
    case RandomBars:
    case Strips:
        m.type = QStringLiteral("barWipe");
        m.subtype = QStringLiteral("topToBottom");
        break;
    default:
        // Random, Dissolve, Plus and Cut have no exact Stage equivalent.
        break;
    }
    return m;
}

// speed byte <-> smil:dur seconds (binary: 0=0.75s, 1=0.5s, 2=0.25s).
inline quint8 speedFromSeconds(double seconds)
{
    if (seconds >= 0.75) return 0;
    if (seconds >= 0.5) return 1;
    return 2;
}
inline QString secondsFromSpeed(quint8 speed)
{
    return speed == 0 ? QStringLiteral("0.75")
         : speed == 1 ? QStringLiteral("0.5") : QStringLiteral("0.25");
}

} // namespace PptTransition
