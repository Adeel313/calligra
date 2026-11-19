// SPDX-License-Identifier: LGPL-2.0-or-later
#include "PptEquation.h"

#include "pole.h"

#include <QTemporaryFile>
#include <QtEndian>
#include <QStringList>
#include <QXmlStreamReader>

#include <zlib.h>

namespace
{
constexpr int MaximumObjectSize = 2 * 1024 * 1024;
constexpr int MaximumEquationSize = 64 * 1024;

quint32 little32(QByteArrayView data, qsizetype offset)
{
    return qFromLittleEndian<quint32>(data.data() + offset);
}

QByteArray compoundStorage(QByteArrayView object)
{
    if (object.size() < 16 || object.size() > MaximumObjectSize) {
        return {};
    }
    const qsizetype offset = qsizetype(little32(object, 4)) + 8;
    if (offset > object.size() - 8 || qFromLittleEndian<quint16>(object.data() + offset + 2) != 0x1011
        || little32(object, offset + 4) != object.size() - offset - 8) {
        return {};
    }
    QByteArray compound;
    const auto flags = qFromLittleEndian<quint16>(object.data() + offset);
    if (flags == 0) {
        compound = object.sliced(offset + 8).toByteArray();
    } else if (flags == 0x10 && object.size() - offset >= 12) {
        const auto length = little32(object, offset + 8);
        if (!length || length > MaximumObjectSize) {
            return {};
        }
        // Some writers omit the zlib trailer. The decompressed length from
        // the PPT storage record still has to match exactly.
        compound.resize(length + 1);
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(object.data() + offset + 12));
        stream.avail_in = object.size() - offset - 12;
        stream.next_out = reinterpret_cast<Bytef *>(compound.data());
        stream.avail_out = compound.size();
        if (inflateInit(&stream) != Z_OK) {
            return {};
        }
        const int status = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if ((status != Z_STREAM_END && status != Z_BUF_ERROR) || stream.total_out != length || stream.avail_in) {
            return {};
        }
        compound.resize(length);
    } else {
        return {};
    }
    return compound;
}

QByteArray readStream(POLE::Storage &storage, const char *name)
{
    POLE::Stream stream(&storage, name);
    const auto length = stream.size();
    if (stream.fail() || !length || length > MaximumEquationSize) {
        return {};
    }
    QByteArray bytes(length, '\0');
    if (stream.read(reinterpret_cast<unsigned char *>(bytes.data()), length) != length) {
        return {};
    }
    return bytes;
}

bool isMathML(const QByteArray &bytes)
{
    QXmlStreamReader reader(bytes);
    bool foundRoot = false;
    int depth = 0;
    const auto mathNamespace = QLatin1StringView("http://www.w3.org/1998/Math/MathML");
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement:
            if (reader.namespaceUri() != mathNamespace || ++depth > 64) {
                return false;
            }
            if (!foundRoot) {
                if (reader.name() != QLatin1StringView("math")) {
                    return false;
                }
                foundRoot = true;
            }
            break;
        case QXmlStreamReader::EndElement:
            --depth;
            break;
        case QXmlStreamReader::DTD:
        case QXmlStreamReader::ProcessingInstruction:
        case QXmlStreamReader::EntityReference:
            return false;
        default:
            break;
        }
    }
    return foundRoot && !reader.hasError() && bytes.trimmed().startsWith("<math");
}

QByteArray equationStream(POLE::Storage &storage)
{
    const QByteArray equation = readStream(storage, "/Equation Native");
    if (equation.size() < 28) {
        return {};
    }
    const auto headerSize = little32(equation, 0);
    const auto equationSize = little32(equation, 8);
    if (headerSize < 28 || headerSize > equation.size() || equationSize > equation.size() - headerSize) {
        return {};
    }
    return equation.mid(headerSize, equationSize);
}

QString mathElement(QLatin1StringView tag, const QString &children)
{
    return QStringLiteral("<math:") + tag + QLatin1Char('>') + children + QStringLiteral("</math:") + tag + QLatin1Char('>');
}

// Design Science's MTEF v5 grammar. Unsupported constructs keep the original
// preview. A partially decoded expression must never replace the preview.
class EquationReader
{
public:
    explicit EquationReader(QByteArrayView bytes)
        : m_bytes(bytes)
    {
    }

    QByteArray read()
    {
        try {
            if (byte() != 5) {
                return {};
            }
            skip(4);
            skipString();
            skip(1);
            const QString content = records(0).join(QString());
            if (content.isEmpty() || !m_hasCharacter) {
                return {};
            }
            return (QStringLiteral("<math:math xmlns:math=\"http://www.w3.org/1998/Math/MathML\">") + content + QStringLiteral("</math:math>")).toUtf8();
        } catch (const InvalidEquation &) {
            return {};
        }
    }

private:
    struct InvalidEquation {};

    void skip(qsizetype count)
    {
        if (count < 0 || count > m_bytes.size() - m_pos) {
            throw InvalidEquation{};
        }
        m_pos += count;
    }

    int byte()
    {
        skip(1);
        return quint8(m_bytes[m_pos - 1]);
    }

    int word()
    {
        skip(2);
        return qFromLittleEndian<quint16>(m_bytes.data() + m_pos - 2);
    }

    int variableInteger()
    {
        const int first = byte();
        return first == 255 ? word() : first;
    }

    void skipString()
    {
        const qsizetype end = m_bytes.indexOf('\0', m_pos);
        if (end < 0) {
            throw InvalidEquation{};
        }
        m_pos = end + 1;
    }

    void skipDimensions()
    {
        int remaining = byte();
        while (remaining > 0) {
            const int pair = byte();
            remaining -= ((pair >> 4) == 15) + ((pair & 15) == 15);
        }
        if (remaining < 0) {
            throw InvalidEquation{};
        }
    }

    void skipRuler()
    {
        if (byte() != 7) {
            throw InvalidEquation{};
        }
        skip(byte() * 3);
    }

    QString character(int options)
    {
        if (options & 0x21) {
            throw InvalidEquation{};
        }
        const int face = variableInteger();
        const QChar glyph(word());
        if (options & 0x04) {
            skip(1);
        }
        if (options & 0x10) {
            skip(2);
        }
        if (glyph.unicode() >= 0xeb00 && glyph.unicode() <= 0xeb05) {
            return QStringLiteral("<math:mspace width=\"0.2em\"/>");
        }
        if (glyph.isSurrogate() || glyph.category() == QChar::Other_PrivateUse || glyph.category() == QChar::Other_Control
            || glyph.unicode() >= 0xfffe) {
            throw InvalidEquation{};
        }
        m_hasCharacter = true;
        const auto tag = glyph.isDigit() ? QLatin1StringView("mn")
            : glyph.isLetter() ? QLatin1StringView("mi") : QLatin1StringView("mo");
        if (face == 129 && glyph.isLetter()) {
            return mathElement(QLatin1StringView("mtext"), QString(glyph).toHtmlEscaped());
        }
        return mathElement(tag, QString(glyph).toHtmlEscaped());
    }

    QString readTemplate(QStringList &siblings, int depth)
    {
        const int selector = byte();
        int variation = byte();
        if (variation & 0x80) {
            variation = (variation & 0x7f) | (byte() << 8);
        }
        skip(1);
        const QStringList slots = records(depth + 1);
        if (selector <= 8 && (variation == 1 || variation == 2 || variation == 3)
            && slots.size() == (variation == 3 ? 3 : 2)) {
            const auto fence = [](const QString &op) {
                const QString start = QStringLiteral("<math:mo>");
                const QString end = QStringLiteral("</math:mo>");
                if (!op.startsWith(start) || !op.endsWith(end)) {
                    throw InvalidEquation{};
                }
                return op.mid(start.size(), op.size() - start.size() - end.size());
            };
            // Fence records store the enclosed slot before either delimiter.
            const QString left = variation & 1 ? fence(slots.at(1)) : QString();
            const QString right = variation & 2 ? fence(slots.last()) : QString();
            return QStringLiteral(R"(<math:mfenced open=")") + left + QStringLiteral(R"(" close=")") + right
                + QStringLiteral(R"(" separators="">)") + slots.first() + QStringLiteral("</math:mfenced>");
        } else if (selector == 10 && variation == 0 && slots.size() == 1) {
            return mathElement(QLatin1StringView("msqrt"), slots.first());
        } else if ((selector == 10 || selector == 11) && slots.size() == 2) {
            return mathElement(selector == 10 ? QLatin1StringView("mroot") : QLatin1StringView("mfrac"), slots.join(QString()));
        } else if ((selector == 12 || selector == 13) && variation == 0 && slots.size() == 1) {
            return mathElement(selector == 12 ? QLatin1StringView("munder") : QLatin1StringView("mover"),
                               slots.first() + QStringLiteral("<math:mo stretchy=\"true\">¯</math:mo>"));
        } else if (selector >= 27 && selector <= 29 && variation == 0 && slots.size() == 2 && !siblings.isEmpty()) {
            const QString base = siblings.takeLast();
            if (selector == 27) {
                return mathElement(QLatin1StringView("msub"), base + slots.at(0));
            }
            if (selector == 28) {
                return mathElement(QLatin1StringView("msup"), base + slots.at(1));
            }
            return mathElement(QLatin1StringView("msubsup"), base + slots.join(QString()));
        }
        throw InvalidEquation{};
    }

    QString table(int tag, int options, int depth)
    {
        int rows = 0;
        int columns = 1;
        if (tag == 4) {
            skip(2);
            if (options & 2) {
                skipRuler();
            }
        } else {
            skip(3);
            rows = byte();
            columns = byte();
            if (!rows || !columns) {
                throw InvalidEquation{};
            }
            skip((rows + 4) / 4 + (columns + 4) / 4);
        }
        const auto cells = records(depth + 1);
        if (tag == 5 && cells.size() != rows * columns) {
            throw InvalidEquation{};
        }
        QString outer;
        for (qsizetype i = 0; i < cells.size(); i += columns) {
            QString row;
            for (int col = 0; col < columns; ++col) {
                row += mathElement(QLatin1StringView("mtd"), cells.at(i + col));
            }
            outer += mathElement(QLatin1StringView("mtr"), row);
        }
        return mathElement(QLatin1StringView("mtable"), outer);
    }

    QStringList records(int depth)
    {
        if (depth > 32) {
            throw InvalidEquation{};
        }
        QStringList result;
        while (const int tag = byte()) {
            int options = 0;
            if (tag >= 1 && tag <= 6) {
                options = byte();
                if ((options & 8) && word() == 0x8080) {
                    skip(4);
                }
            }
            switch (tag) {
            case 1:
                if (options & 4) {
                    skip(2);
                }
                if (options & 2) {
                    skipRuler();
                }
                result.append(mathElement(QLatin1StringView("mrow"), options & 1 ? QString() : records(depth + 1).join(QString())));
                break;
            case 2: result.append(character(options)); break;
            case 3: result.append(readTemplate(result, depth)); break;
            case 4:
            case 5: result.append(table(tag, options, depth)); break;
            case 7: skip(byte() * 3); break;
            case 8: variableInteger(); skip(1); break;
            case 9: {
                const int size = byte();
                skip(size == 100 ? 3 : size == 101 ? 2 : 1);
                break;
            }
            case 10: case 11: case 12: case 13: case 14: break;
            case 15: variableInteger(); break;
            case 16: {
                const int flags = byte();
                skip(flags & 1 ? 8 : 6);
                if (flags & 4) {
                    skipString();
                }
                break;
            }
            case 17: variableInteger(); skipString(); break;
            case 18: {
                skip(1);
                skipDimensions();
                skipDimensions();
                const int styles = byte();
                for (int i = 0; i < styles; ++i) {
                    if (variableInteger()) {
                        skip(1);
                    }
                }
                break;
            }
            case 19: skipString(); break;
            default:
                if (tag < 100) {
                    throw InvalidEquation{};
                }
                skip(variableInteger());
                break;
            }
        }
        return result;
    }

    QByteArrayView m_bytes;
    qsizetype m_pos = 0;
    bool m_hasCharacter = false;
};
}

QByteArray PptEquation::mathML(const QByteArray &embeddedObject)
{
    const QByteArray compound = compoundStorage(embeddedObject);
    QTemporaryFile file;
    if (compound.isEmpty() || !file.open() || file.write(compound) != compound.size() || !file.flush()) {
        return {};
    }
    const QByteArray path = QFile::encodeName(file.fileName());
    POLE::Storage storage(path.constData());
    if (!storage.open()) {
        return {};
    }
    const QByteArray math = readStream(storage, "/Calligra MathML");
    if (isMathML(math)) {
        return math;
    }
    const QByteArray equation = equationStream(storage);
    return EquationReader(equation).read();
}
