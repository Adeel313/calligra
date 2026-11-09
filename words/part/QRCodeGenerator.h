/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra developers
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef QRCODEGENERATOR_H
#define QRCODEGENERATOR_H

#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>

/**
 * Generates QR codes (ISO/IEC 18004) in byte mode, the mode that carries UTF-8
 * text unchanged. Versions 1 to 10 are supported, which is enough for a few
 * hundred characters, the range a code that is printed on a page is readable in.
 */
class QRCodeGenerator
{
public:
    enum ErrorCorrection {
        Low, ///< about 7% of the codewords can be restored
        Medium, ///< about 15%
        Quartile, ///< about 25%
        High ///< about 30%
    };

    /// Number of data bytes that fit in a version for the given level.
    static int capacity(int version, ErrorCorrection level);

    /// Smallest version that holds \a byteCount bytes, or 0 when nothing fits.
    static int versionFor(int byteCount, ErrorCorrection level);

    /// Highest version this generator knows about.
    static int maxVersion();

    /**
     * Renders \a text as a QR code.
     *
     * @param text the text to encode, as UTF-8
     * @param level error correction level
     * @param pixelsAcross size of the returned image in pixels
     * @param margin quiet zone in modules (4 is the value the standard asks for)
     * @param dark colour of the modules
     * @param light colour of the background
     * @param error set to a translated message when the text does not fit
     */
    static QImage render(const QString &text,
                         ErrorCorrection level,
                         int pixelsAcross,
                         int margin,
                         const QColor &dark,
                         const QColor &light,
                         QString *error = nullptr);

    /// The module grid of \a text, row by row, true being a dark module.
    /// Empty when the text does not fit.
    static QVector<QVector<bool>> modules(const QString &text, ErrorCorrection level);
};

#endif
