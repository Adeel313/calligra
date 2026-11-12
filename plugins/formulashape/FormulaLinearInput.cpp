/* This file is part of the KDE project
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "FormulaLinearInput.h"

#include <QList>
#include <QStringList>

namespace
{
QString escapeXml(const QString &text)
{
    QString escaped;
    escaped.reserve(text.size());
    for (const QChar character : text) {
        switch (character.unicode()) {
        case '&':
            escaped += QLatin1String("&amp;");
            break;
        case '<':
            escaped += QLatin1String("&lt;");
            break;
        case '>':
            escaped += QLatin1String("&gt;");
            break;
        default:
            escaped += character;
        }
    }
    return escaped;
}

QString mrow(const QString &content)
{
    return QStringLiteral("<mrow>") + content + QStringLiteral("</mrow>");
}

QString mi(const QString &text)
{
    return QStringLiteral("<mi>") + escapeXml(text) + QStringLiteral("</mi>");
}

QString mn(const QString &text)
{
    return QStringLiteral("<mn>") + escapeXml(text) + QStringLiteral("</mn>");
}

QString mo(const QString &text)
{
    return QStringLiteral("<mo>") + escapeXml(text) + QStringLiteral("</mo>");
}

QString msqrt(const QString &content)
{
    return QStringLiteral("<msqrt>") + mrow(content) + QStringLiteral("</msqrt>");
}

QString mfrac(const QString &numerator, const QString &denominator)
{
    return QStringLiteral("<mfrac>") + mrow(numerator) + mrow(denominator) + QStringLiteral("</mfrac>");
}

struct SymbolDefinition {
    const char *name;
    const char *unicode;
    bool operatorSymbol;
};

// The names people type after a backslash, as in LaTeX and in the Office
// equation editor's Unicode mode.
const SymbolDefinition Symbols[] = {
    {"alpha", "α", false}, {"beta", "β", false},     {"gamma", "γ", false},  {"delta", "δ", false},   {"epsilon", "ε", false},
    {"varepsilon", "ε", false}, {"zeta", "ζ", false}, {"eta", "η", false},  {"theta", "θ", false},   {"vartheta", "ϑ", false},
    {"iota", "ι", false},  {"kappa", "κ", false},   {"lambda", "λ", false}, {"mu", "μ", false},      {"nu", "ν", false},
    {"xi", "ξ", false},    {"omicron", "ο", false}, {"pi", "π", false},     {"rho", "ρ", false},     {"sigma", "σ", false},
    {"tau", "τ", false},   {"upsilon", "υ", false}, {"phi", "φ", false},    {"varphi", "φ", false},  {"chi", "χ", false},
    {"psi", "ψ", false},   {"omega", "ω", false},   {"Gamma", "Γ", false},  {"Delta", "Δ", false},   {"Theta", "Θ", false},
    {"Lambda", "Λ", false}, {"Xi", "Ξ", false},     {"Pi", "Π", false},     {"Sigma", "Σ", false},   {"Phi", "Φ", false},
    {"Psi", "Ψ", false},   {"Omega", "Ω", false},   {"infty", "∞", false},  {"partial", "∂", false}, {"nabla", "∇", false},
    {"times", "×", true},  {"cdot", "·", true},     {"div", "÷", true},     {"pm", "±", true},       {"mp", "∓", true},
    {"le", "≤", true},     {"leq", "≤", true},      {"ge", "≥", true},      {"geq", "≥", true},      {"ne", "≠", true},
    {"neq", "≠", true},    {"approx", "≈", true},   {"equiv", "≡", true},   {"rightarrow", "→", true}, {"to", "→", true},
    {"leftarrow", "←", true}, {"sum", "∑", true},   {"prod", "∏", true},    {"int", "∫", true},      {"oint", "∮", true},
    {"ldots", "…", true},  {"cdots", "⋯", true},    {"degree", "°", true},  {"prime", "′", true},    {"angle", "∠", true},
    {"perp", "⊥", true},   {"parallel", "∥", true}, {"subset", "⊂", true},  {"supset", "⊃", true},   {"in", "∈", true},
};

// Names that denote a function; they stay one token instead of being split
// into single letters.
const QStringList Functions = {
    QStringLiteral("sin"),  QStringLiteral("cos"),   QStringLiteral("tan"),   QStringLiteral("cot"),  QStringLiteral("sec"),
    QStringLiteral("csc"),  QStringLiteral("arcsin"), QStringLiteral("arccos"), QStringLiteral("arctan"), QStringLiteral("sinh"),
    QStringLiteral("cosh"), QStringLiteral("tanh"),  QStringLiteral("log"),   QStringLiteral("ln"),   QStringLiteral("lg"),
    QStringLiteral("exp"),  QStringLiteral("lim"),   QStringLiteral("max"),   QStringLiteral("min"),  QStringLiteral("gcd"),
    QStringLiteral("det"),  QStringLiteral("mod"),   QStringLiteral("deg"),
};

class Parser
{
public:
    explicit Parser(const QString &text)
        : m_text(text)
    {
    }

    QString parse();

private:
    QChar peek(int offset = 0) const
    {
        const int index = m_pos + offset;
        return index < m_text.size() ? m_text.at(index) : QChar();
    }

    bool atEnd() const
    {
        return m_pos >= m_text.size();
    }

    void skipSpaces()
    {
        while (!atEnd() && m_text.at(m_pos).isSpace()) {
            ++m_pos;
        }
    }

    /// Read a sequence of terms up to @p closing or the end of the text
    QString parseSequence(const QString &closing);
    QString parseProduct();
    QString parseScript();
    QString parseAtom();
    QString parseBackslashName();

    const QString &m_text;
    int m_pos = 0;
};

QString Parser::parse()
{
    const QString content = parseSequence(QString());
    if (content.isEmpty()) {
        return QString();
    }
    return QStringLiteral("<math xmlns=\"http://www.w3.org/1998/Math/MathML\">") + mrow(content) + QStringLiteral("</math>");
}

QString Parser::parseSequence(const QString &closing)
{
    QString content;
    while (true) {
        skipSpaces();
        if (atEnd()) {
            break;
        }
        const QChar character = m_text.at(m_pos);
        if (!closing.isEmpty() && character == closing) {
            break;
        }
        // A stray closing bracket ends the expression as well.
        if (character == QLatin1Char(')') || character == QLatin1Char('}') || character == QLatin1Char(']')) {
            break;
        }
        if (character == QLatin1Char('+') || character == QLatin1Char('-') || character == QLatin1Char('=')
            || character == QLatin1Char('<') || character == QLatin1Char('>')) {
            content += mo(QString(character));
            ++m_pos;
            continue;
        }
        content += parseProduct();
    }
    return content;
}

QString Parser::parseProduct()
{
    QString numerator = parseScript();
    while (true) {
        skipSpaces();
        if (peek() != QLatin1Char('/')) {
            break;
        }
        ++m_pos;
        const QString denominator = parseScript();
        numerator = mfrac(numerator, denominator);
    }
    return numerator;
}

QString Parser::parseScript()
{
    QString base = parseAtom();
    QString subscript;
    QString superscript;
    bool hasSubscript = false;
    bool hasSuperscript = false;
    while (true) {
        skipSpaces();
        const QChar character = peek();
        if (character == QLatin1Char('^') && !hasSuperscript) {
            ++m_pos;
            superscript = parseAtom();
            hasSuperscript = true;
        } else if (character == QLatin1Char('_') && !hasSubscript) {
            ++m_pos;
            subscript = parseAtom();
            hasSubscript = true;
        } else {
            break;
        }
    }
    if (hasSubscript && hasSuperscript) {
        return QStringLiteral("<msubsup>") + mrow(base) + mrow(subscript) + mrow(superscript) + QStringLiteral("</msubsup>");
    }
    if (hasSuperscript) {
        return QStringLiteral("<msup>") + mrow(base) + mrow(superscript) + QStringLiteral("</msup>");
    }
    if (hasSubscript) {
        return QStringLiteral("<msub>") + mrow(base) + mrow(subscript) + QStringLiteral("</msub>");
    }
    return base;
}

QString Parser::parseBackslashName()
{
    ++m_pos; // the backslash
    QString name;
    while (!atEnd() && m_text.at(m_pos).isLetter()) {
        name += m_text.at(m_pos);
        ++m_pos;
    }
    if (name.isEmpty()) {
        return mo(QStringLiteral("\\"));
    }
    if (name == QLatin1String("sqrt")) {
        return msqrt(parseAtom());
    }
    if (name == QLatin1String("frac")) {
        const QString numerator = parseAtom();
        const QString denominator = parseAtom();
        return mfrac(numerator, denominator);
    }
    for (const SymbolDefinition &symbol : Symbols) {
        if (name == QLatin1String(symbol.name)) {
            const QString unicode = QString::fromUtf8(symbol.unicode);
            return symbol.operatorSymbol ? mo(unicode) : mi(unicode);
        }
    }
    // An unknown name is most likely a function, so keep it in one piece.
    return mi(name);
}

QString Parser::parseAtom()
{
    skipSpaces();
    if (atEnd()) {
        return QString();
    }
    const QChar character = m_text.at(m_pos);

    if (character == QLatin1Char('(') || character == QLatin1Char('{') || character == QLatin1Char('[')) {
        const QChar closing = character == QLatin1Char('(') ? QLatin1Char(')')
            : character == QLatin1Char('{')                        ? QLatin1Char('}')
                                                                   : QLatin1Char(']');
        ++m_pos;
        const QString content = parseSequence(QString(closing));
        skipSpaces();
        if (peek() == closing) {
            ++m_pos;
        }
        // Brackets only group here; the rounding ones are symbols the user can
        // also type, but a group is what an expression needs.
        return content;
    }
    if (character == QLatin1Char('\\')) {
        return parseBackslashName();
    }
    if (character == QChar(0x221A)) { // √
        ++m_pos;
        return msqrt(parseAtom());
    }
    if (character == QLatin1Char('-') || character == QLatin1Char('+')) {
        ++m_pos;
        return mo(QString(character)) + parseAtom();
    }
    if (character.isDigit() || (character == QLatin1Char('.') && peek(1).isDigit())) {
        QString number;
        while (!atEnd() && (m_text.at(m_pos).isDigit() || m_text.at(m_pos) == QLatin1Char('.'))) {
            number += m_text.at(m_pos);
            ++m_pos;
        }
        return mn(number);
    }
    if (character.isLetter()) {
        QString word;
        int offset = m_pos;
        while (offset < m_text.size() && m_text.at(offset).isLetter()) {
            word += m_text.at(offset);
            ++offset;
        }
        if (word.length() > 1 && Functions.contains(word)) {
            m_pos = offset;
            return mi(word);
        }
        // A run of letters is a product of variables, as in a formula.
        ++m_pos;
        return mi(QString(character));
    }
    // Everything else is an operator or a symbol typed as is.
    ++m_pos;
    return mo(QString(character));
}
} // namespace

bool FormulaLinearInput::hasLinearSyntax(const QString &text)
{
    return text.contains(QLatin1Char('^')) || text.contains(QLatin1Char('_')) || text.contains(QLatin1Char('/'))
        || text.contains(QLatin1Char('\\')) || text.contains(QChar(0x221A)) || text.contains(QLatin1String("sqrt"))
        || text.contains(QLatin1String("frac"));
}

QString FormulaLinearInput::toMathML(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return QString();
    }
    Parser parser(text);
    return parser.parse();
}
