/*
  SPDX-FileCopyrightText: 2008 Brad Hards <bradh@frogmouth.net>
  SPDX-FileCopyrightText: 2009, 2011 Inge Wallin <inge@lysator.liu.se>

  SPDX-License-Identifier: LGPL-2.1-or-later
*/

#ifndef EMFOBJECTS_H
#define EMFOBJECTS_H

#include <QDataStream>
#include <QList>
#include <QRect> // also provides QSize
#include <QString>

/**
   Namespace for Enhanced Metafile (EMF) classes
*/
namespace Libemf
{

// There are a number of objects defined in the EMF
// specification. These objects are common to several different EMF
// records.  An example would be the EmfTextObject which is used in
// among others the ExtTextOutA and ExtTextOutW records.
//
// All these objects are defined in sections 2.2.x in [MS-EMF].pdf
//
// Note: Not all objects in section 2.2 are implemented yet.

// ================================================================
//                         class EmrTextObject

/**
 * Simple representation of an EmrText object
 *
 *  See MS-EMF Section 2.2.5 for details
 */
class EmrTextObject
{
public:
    /**
      The type of text to read
      */
    enum TextType {
        EightBitChars,
        SixteenBitChars
    };

    /**
       Constructor for EmrText object

       \param stream the stream to read the record structure from
       \param size the number of bytes in this record
       \param textType whether the text is normal (EightBitChars) or wide
       characters (SixteenBitChars)
    */
    EmrTextObject(QDataStream &stream, quint32 size, TextType textType);
    ~EmrTextObject();

    /// The reference point for the text output
    [[nodiscard]] QPoint referencePoint() const;

    /// Options for rectangle
    [[nodiscard]] quint32 options() const;

    // Clipping and/or opaquing rectangle
    [[nodiscard]] QRect rectangle() const;

    /// The text to be output
    [[nodiscard]] QString textString() const;

    const QList<qint32> &advances() const { return m_advances; }

private:
    QPoint m_referencePoint; // reference point used to position the string
    quint32 m_options = 0; // specifies how to use the rectangle
    QRect m_rectangle; // clipping and/or opaquing rectangle
    QString m_textString; // the text string to output
    QList<qint32> m_advances;
};

}

#endif
