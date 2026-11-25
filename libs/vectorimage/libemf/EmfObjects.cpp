/*
  SPDX-FileCopyrightText: 2008 Brad Hards <bradh@frogmouth.net>
  SPDX-FileCopyrightText: 2009, 2011 Inge Wallin <inge@lysator.liu.se>

  SPDX-License-Identifier: LGPL-2.1-or-later
*/

#include "EmfObjects.h"
#include "EmfEnums.h"

#include <QIODevice>

namespace
{

// Convenience function to handle a 2-byte wide char stream
QString recordWChars(QDataStream &stream, quint32 numChars)
{
    QString text;
    QChar myChar;
    for (quint32 i = 0; i < numChars; ++i) {
        stream >> myChar;
        text.append(myChar);
    }
    return text;
}

// Convenience function to handle a 1-byte wide char stream
QString recordChars(QDataStream &stream, quint32 numChars)
{
    QString text;
    quint8 myChar;
    for (quint32 i = 0; i < numChars; ++i) {
        stream >> myChar;
        text.append(QChar(myChar));
    }
    return text;
}
}

namespace Libemf
{

// ================================================================
//                         class EmrTextObject
//

// See MS-EMF section 2.2.5.

EmrTextObject::EmrTextObject(QDataStream &stream, quint32 size, TextType textType)
{
    constexpr quint32 recordPrefix = 36;
    constexpr quint32 textHeader = 40;
    QIODevice *device = stream.device();
    const qint64 recordStart = device->pos() - recordPrefix;
    const quint64 recordSize = quint64(size) + recordPrefix;
    const qint64 recordEnd = recordStart + recordSize;
    if (size < textHeader) {
        device->seek(recordEnd);
        return;
    }

    quint32 charCount, stringOffset, advanceOffset;
    stream >> m_referencePoint >> charCount >> stringOffset >> m_options >> m_rectangle >> advanceOffset;
    const bool wide = textType == SixteenBitChars;
    if (charCount && stringOffset >= recordPrefix + textHeader
        && quint64(stringOffset) + quint64(charCount) * (wide ? 2 : 1) <= recordSize
        && device->seek(recordStart + stringOffset)) {
        m_textString = wide ? recordWChars(stream, charCount) : recordChars(stream, charCount);

        // TextOutPairedAdvances stores pairs of horizontal and vertical advances.
        // Ordinary ExtTextOut records only store horizontal advances.
        if (!(m_options & TextOutPairedAdvances) && advanceOffset >= recordPrefix + textHeader
            && quint64(advanceOffset) + quint64(charCount) * sizeof(qint32) <= recordSize
            && device->seek(recordStart + advanceOffset)) {
            m_advances.reserve(charCount);
            for (quint32 i = 0; i < charCount; ++i) {
                qint32 advance;
                stream >> advance;
                m_advances.append(advance);
            }
        }
    }
    device->seek(recordEnd);
}

EmrTextObject::~EmrTextObject() = default;

QPoint EmrTextObject::referencePoint() const
{
    return m_referencePoint;
}

QString EmrTextObject::textString() const
{
    return m_textString;
}

quint32 EmrTextObject::options() const
{
    return m_options;
}

QRect EmrTextObject::rectangle() const
{
    return m_rectangle;
}
}
