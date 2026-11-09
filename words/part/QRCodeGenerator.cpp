/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2026 Calligra developers
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "QRCodeGenerator.h"

#include <KLocalizedString>

#include <QPainter>

namespace
{

//! Error correction codewords per block, indexed by level and version.
const int EccPerBlock[4][11] = {
    {0, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18}, // Low
    {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26}, // Medium
    {0, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24}, // Quartile
    {0, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28}, // High
};

//! Number of error correction blocks, indexed by level and version.
const int NumBlocks[4][11] = {
    {0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4}, // Low
    {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5}, // Medium
    {0, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8}, // Quartile
    {0, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8}, // High
};

//! Format information bits for the error correction level.
const int EclFormatBits[4] = {1, 0, 3, 2};

const int MaxVersion = 10;

//! Modules available for data (and error correction) bits.
int rawDataModules(int version)
{
    int result = (16 * version + 128) * version + 64;
    if (version >= 2) {
        const int numAlign = version / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (version >= 7) {
            result -= 36;
        }
    }
    return result;
}

int dataCodewords(int version, int level)
{
    return rawDataModules(version) / 8 - EccPerBlock[level][version] * NumBlocks[level][version];
}

//! Centre positions of the alignment patterns, from smallest to largest.
QVector<int> alignmentPositions(int version)
{
    if (version == 1) {
        return {};
    }
    const int numAlign = version / 7 + 2;
    const int step = (version == 32) ? 26 : (version * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
    QVector<int> result;
    for (int i = 0, pos = version * 4 + 17 - 7; i < numAlign - 1; ++i, pos -= step) {
        result.prepend(pos);
    }
    result.prepend(6);
    return result;
}

//! Multiplication in GF(256) with the polynomial x^8 + x^4 + x^3 + x^2 + 1.
quint8 gfMultiply(quint8 x, quint8 y)
{
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return quint8(z);
}

QVector<quint8> reedSolomonDivisor(int degree)
{
    QVector<quint8> result(degree);
    result[degree - 1] = 1;
    quint8 root = 1;
    for (int i = 0; i < degree; ++i) {
        for (int j = 0; j < degree; ++j) {
            result[j] = gfMultiply(result[j], root);
            if (j + 1 < degree) {
                result[j] ^= result[j + 1];
            }
        }
        root = gfMultiply(root, 0x02);
    }
    return result;
}

QVector<quint8> reedSolomonRemainder(const QVector<quint8> &data, const QVector<quint8> &divisor)
{
    QVector<quint8> result(divisor.size());
    for (const quint8 value : data) {
        const quint8 factor = value ^ result[0];
        for (int i = 0; i < result.size() - 1; ++i) {
            result[i] = result[i + 1];
        }
        result[result.size() - 1] = 0;
        for (int i = 0; i < result.size(); ++i) {
            result[i] ^= gfMultiply(divisor[i], factor);
        }
    }
    return result;
}

//! The data codewords for \a text, padded as the standard requires.
QVector<quint8> encodeData(const QByteArray &text, int version, int level)
{
    QVector<bool> bits;
    const auto appendBits = [&bits](int value, int length) {
        for (int i = length - 1; i >= 0; --i) {
            bits.append(((value >> i) & 1) != 0);
        }
    };

    appendBits(0x4, 4); // byte mode
    appendBits(text.size(), version < 10 ? 8 : 16);
    for (const char byte : text) {
        appendBits(quint8(byte), 8);
    }

    const int capacityBits = dataCodewords(version, level) * 8;
    appendBits(0, qMin(4, capacityBits - bits.size()));
    appendBits(0, (8 - bits.size() % 8) % 8);
    for (int pad = 0xEC; bits.size() < capacityBits; pad ^= 0xEC ^ 0x11) {
        appendBits(pad, 8);
    }

    QVector<quint8> data(bits.size() / 8);
    for (int i = 0; i < bits.size(); ++i) {
        if (bits.at(i)) {
            data[i >> 3] |= quint8(0x80 >> (i & 7));
        }
    }
    return data;
}

//! Splits the data into blocks, appends the error correction and interleaves it.
QVector<quint8> addErrorCorrection(const QVector<quint8> &data, int version, int level)
{
    const int numBlocks = NumBlocks[level][version];
    const int blockEccLen = EccPerBlock[level][version];
    const int rawCodewords = rawDataModules(version) / 8;
    const int numShortBlocks = numBlocks - rawCodewords % numBlocks;
    const int shortBlockLen = rawCodewords / numBlocks;

    const QVector<quint8> divisor = reedSolomonDivisor(blockEccLen);
    QVector<QVector<quint8>> blocks;
    for (int i = 0, offset = 0; i < numBlocks; ++i) {
        const int dataLen = shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1);
        QVector<quint8> block = data.mid(offset, dataLen);
        offset += dataLen;
        const QVector<quint8> ecc = reedSolomonRemainder(block, divisor);
        if (i < numShortBlocks) {
            block.append(0); // placeholder, kept for the interleaving below
        }
        block += ecc;
        blocks.append(block);
    }

    QVector<quint8> result;
    result.reserve(rawCodewords);
    for (int i = 0; i < blocks.first().size(); ++i) {
        for (int j = 0; j < blocks.size(); ++j) {
            if (i != shortBlockLen - blockEccLen || j >= numShortBlocks) {
                result.append(blocks.at(j).at(i));
            }
        }
    }
    return result;
}

//! The 15 bit format information for a level and mask, BCH protected.
int formatBits(int level, int mask)
{
    const int data = (EclFormatBits[level] << 3) | mask;
    int remainder = data;
    for (int i = 0; i < 10; ++i) {
        remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
    }
    return ((data << 10) | remainder) ^ 0x5412;
}

//! The 18 bit version information for versions 7 and up, BCH protected.
int versionBits(int version)
{
    int remainder = version;
    for (int i = 0; i < 12; ++i) {
        remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);
    }
    return (version << 12) | remainder;
}

class Matrix
{
public:
    Matrix(int version)
        : m_version(version)
        , m_size(version * 4 + 17)
        , m_modules(m_size, QVector<bool>(m_size, false))
        , m_function(m_size, QVector<bool>(m_size, false))
    {
    }

    void drawFunctionPatterns()
    {
        for (int i = 0; i < m_size; ++i) {
            setFunction(6, i, i % 2 == 0);
            setFunction(i, 6, i % 2 == 0);
        }

        drawFinder(3, 3);
        drawFinder(m_size - 4, 3);
        drawFinder(3, m_size - 4);

        const QVector<int> positions = alignmentPositions(m_version);
        for (int i = 0; i < positions.size(); ++i) {
            for (int j = 0; j < positions.size(); ++j) {
                // The three corners already hold finder patterns.
                const bool corner = (i == 0 && j == 0) || (i == 0 && j == positions.size() - 1)
                    || (i == positions.size() - 1 && j == 0);
                if (!corner) {
                    drawAlignment(positions.at(i), positions.at(j));
                }
            }
        }

        // Reserve the format and version areas; the real bits are drawn later.
        drawFormatBits(0, 0);
        drawVersionBits();
    }

    void drawFormatBits(int level, int mask)
    {
        const int bits = formatBits(level, mask);
        for (int i = 0; i <= 5; ++i) {
            setFunction(8, i, ((bits >> i) & 1) != 0);
        }
        setFunction(8, 7, ((bits >> 6) & 1) != 0);
        setFunction(8, 8, ((bits >> 7) & 1) != 0);
        setFunction(7, 8, ((bits >> 8) & 1) != 0);
        for (int i = 9; i < 15; ++i) {
            setFunction(14 - i, 8, ((bits >> i) & 1) != 0);
        }

        for (int i = 0; i < 8; ++i) {
            setFunction(m_size - 1 - i, 8, ((bits >> i) & 1) != 0);
        }
        for (int i = 8; i < 15; ++i) {
            setFunction(8, m_size - 15 + i, ((bits >> i) & 1) != 0);
        }
        setFunction(8, m_size - 8, true); // the dark module
    }

    void drawVersionBits()
    {
        if (m_version < 7) {
            return;
        }
        const int bits = versionBits(m_version);
        for (int i = 0; i < 18; ++i) {
            const bool value = ((bits >> i) & 1) != 0;
            const int x = m_size - 11 + i % 3;
            const int y = i / 3;
            setFunction(x, y, value);
            setFunction(y, x, value);
        }
    }

    void drawCodewords(const QVector<quint8> &data)
    {
        int i = 0;
        for (int right = m_size - 1; right >= 1; right -= 2) {
            if (right == 6) {
                right = 5; // the vertical timing pattern is skipped
            }
            for (int vertical = 0; vertical < m_size; ++vertical) {
                for (int j = 0; j < 2; ++j) {
                    const int x = right - j;
                    const bool upward = ((right + 1) & 2) == 0;
                    const int y = upward ? m_size - 1 - vertical : vertical;
                    if (!m_function.at(y).at(x) && i < data.size() * 8) {
                        m_modules[y][x] = ((data.at(i >> 3) >> (7 - (i & 7))) & 1) != 0;
                        ++i;
                    }
                }
            }
        }
    }

    void applyMask(int mask)
    {
        for (int y = 0; y < m_size; ++y) {
            for (int x = 0; x < m_size; ++x) {
                if (m_function.at(y).at(x)) {
                    continue;
                }
                bool invert = false;
                switch (mask) {
                case 0: invert = (x + y) % 2 == 0; break;
                case 1: invert = y % 2 == 0; break;
                case 2: invert = x % 3 == 0; break;
                case 3: invert = (x + y) % 3 == 0; break;
                case 4: invert = (x / 3 + y / 2) % 2 == 0; break;
                case 5: invert = x * y % 2 + x * y % 3 == 0; break;
                case 6: invert = (x * y % 2 + x * y % 3) % 2 == 0; break;
                case 7: invert = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
                default: break;
                }
                if (invert) {
                    m_modules[y][x] = !m_modules[y][x];
                }
            }
        }
    }

    //! The penalty the standard uses to pick the mask that is easiest to read.
    int penalty() const
    {
        int result = 0;

        // Runs of five or more modules of the same colour, in rows and columns.
        for (int y = 0; y < m_size; ++y) {
            for (int x = 0; x < m_size;) {
                int run = 1;
                while (x + run < m_size && m_modules[y][x + run] == m_modules[y][x]) {
                    ++run;
                }
                if (run >= 5) {
                    result += run - 2;
                }
                x += run;
            }
        }
        for (int x = 0; x < m_size; ++x) {
            for (int y = 0; y < m_size;) {
                int run = 1;
                while (y + run < m_size && m_modules[y + run][x] == m_modules[y][x]) {
                    ++run;
                }
                if (run >= 5) {
                    result += run - 2;
                }
                y += run;
            }
        }

        // Blocks of the same colour.
        for (int y = 0; y < m_size - 1; ++y) {
            for (int x = 0; x < m_size - 1; ++x) {
                const bool value = m_modules[y][x];
                if (value == m_modules[y][x + 1] && value == m_modules[y + 1][x] && value == m_modules[y + 1][x + 1]) {
                    result += 3;
                }
            }
        }

        // Patterns that look like a finder pattern.
        const QVector<bool> finder = {true, false, true, true, true, false, true, false, false, false, false};
        for (int y = 0; y < m_size; ++y) {
            for (int x = 0; x + 11 <= m_size; ++x) {
                bool matches = true;
                for (int i = 0; i < 11 && matches; ++i) {
                    matches = m_modules[y][x + i] == finder.at(i);
                }
                if (matches) {
                    result += 40;
                }
                matches = true;
                for (int i = 0; i < 11 && matches; ++i) {
                    matches = m_modules[y][x + i] == finder.at(10 - i);
                }
                if (matches) {
                    result += 40;
                }
            }
        }
        for (int x = 0; x < m_size; ++x) {
            for (int y = 0; y + 11 <= m_size; ++y) {
                bool matches = true;
                for (int i = 0; i < 11 && matches; ++i) {
                    matches = m_modules[y + i][x] == finder.at(i);
                }
                if (matches) {
                    result += 40;
                }
                matches = true;
                for (int i = 0; i < 11 && matches; ++i) {
                    matches = m_modules[y + i][x] == finder.at(10 - i);
                }
                if (matches) {
                    result += 40;
                }
            }
        }

        // Balance of dark and light modules.
        int dark = 0;
        for (const QVector<bool> &row : m_modules) {
            dark += std::count(row.begin(), row.end(), true);
        }
        const int total = m_size * m_size;
        const int k = (qAbs(dark * 20 - total * 10) + total - 1) / total - 1;
        result += k * 10;

        return result;
    }

    QVector<QVector<bool>> takeModules() const
    {
        return m_modules;
    }

    int size() const
    {
        return m_size;
    }

private:
    void setFunction(int x, int y, bool value)
    {
        m_modules[y][x] = value;
        m_function[y][x] = true;
    }

    void drawFinder(int cx, int cy)
    {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x < 0 || y < 0 || x >= m_size || y >= m_size) {
                    continue;
                }
                const int distance = qMax(qAbs(dx), qAbs(dy));
                setFunction(x, y, distance != 2 && distance != 4);
            }
        }
    }

    void drawAlignment(int cx, int cy)
    {
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                setFunction(cx + dx, cy + dy, qMax(qAbs(dx), qAbs(dy)) != 1);
            }
        }
    }

    int m_version;
    int m_size;
    QVector<QVector<bool>> m_modules;
    QVector<QVector<bool>> m_function;
};

} // namespace

int QRCodeGenerator::capacity(int version, ErrorCorrection level)
{
    if (version < 1 || version > MaxVersion) {
        return 0;
    }
    // Two bytes are needed for the mode and character count of short data.
    const int headerBytes = version < 10 ? 2 : 3;
    return qMax(0, dataCodewords(version, int(level)) - headerBytes);
}

int QRCodeGenerator::versionFor(int byteCount, ErrorCorrection level)
{
    for (int version = 1; version <= MaxVersion; ++version) {
        if (byteCount <= capacity(version, level)) {
            return version;
        }
    }
    return 0;
}

int QRCodeGenerator::maxVersion()
{
    return MaxVersion;
}

QVector<QVector<bool>> QRCodeGenerator::modules(const QString &text, ErrorCorrection level)
{
    const QByteArray bytes = text.toUtf8();
    const int version = versionFor(bytes.size(), level);
    if (version == 0) {
        return {};
    }

    const QVector<quint8> data = encodeData(bytes, version, int(level));
    const QVector<quint8> codewords = addErrorCorrection(data, version, int(level));

    Matrix best(version);
    int bestPenalty = -1;
    for (int mask = 0; mask < 8; ++mask) {
        Matrix candidate(version);
        candidate.drawFunctionPatterns();
        candidate.drawCodewords(codewords);
        candidate.applyMask(mask);
        candidate.drawFormatBits(int(level), mask);
        const int penalty = candidate.penalty();
        if (bestPenalty < 0 || penalty < bestPenalty) {
            bestPenalty = penalty;
            best = candidate;
        }
    }
    return best.takeModules();
}

QImage QRCodeGenerator::render(const QString &text,
                               ErrorCorrection level,
                               int pixelsAcross,
                               int margin,
                               const QColor &dark,
                               const QColor &light,
                               QString *error)
{
    const QVector<QVector<bool>> grid = modules(text, level);
    if (grid.isEmpty()) {
        if (error) {
            *error = i18n("The text is too long for a QR code. Shorten it, or choose a lower error correction level.");
        }
        return QImage();
    }

    const int count = grid.size();
    const int total = count + 2 * margin;
    const int scale = qMax(1, pixelsAcross / total);
    const int pixelSize = total * scale;

    QImage image(pixelSize, pixelSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(light);

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(dark);
    for (int y = 0; y < count; ++y) {
        for (int x = 0; x < count; ++x) {
            if (grid.at(y).at(x)) {
                painter.drawRect((x + margin) * scale, (y + margin) * scale, scale, scale);
            }
        }
    }
    painter.end();
    return image;
}
