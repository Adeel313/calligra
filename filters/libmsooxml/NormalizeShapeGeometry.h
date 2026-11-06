/* SPDX-License-Identifier: LGPL-2.0-or-later */
#pragma once
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

// DrawingML guides are evaluated in order and may reassign a name. ODF
// equations are named expressions instead: duplicate names must be renamed,
// or presets such as gears acquire circular references and overflow the stack.
inline void normalizeShapeGeometry(QHash<QString, QString> &paths, QHash<QString, QString> &equations, QHash<QString, QString> &textAreas)
{
    static const QRegularExpression reference(QStringLiteral("\\?([A-Za-z_][A-Za-z0-9_]*)"));
    for (auto it = equations.begin(); it != equations.end(); ++it) {
        QHash<QString, QString> names;
        QHash<QString, int> versions;
        const auto resolve = [&names](QString expression) {
            QList<QPair<QPair<int, int>, QString>> replacements;
            auto matches = reference.globalMatch(expression);
            while (matches.hasNext()) {
                const auto match = matches.next();
                if (names.contains(match.captured(1))) {
                    replacements.append({{match.capturedStart(1), match.capturedLength(1)}, names.value(match.captured(1))});
                }
            }
            for (auto replacement = replacements.crbegin(); replacement != replacements.crend(); ++replacement) {
                expression.replace(replacement->first.first, replacement->first.second, replacement->second);
            }
            return expression;
        };
        const QString xml = QStringLiteral("<root xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\">") + it.value() + QStringLiteral("</root>");
        QXmlStreamReader reader(xml);
        QString rewritten;
        QXmlStreamWriter writer(&rewritten);
        while (!reader.atEnd()) {
            reader.readNext();
            if (!reader.isStartElement() || reader.name() != QLatin1String("equation")) { continue; }
            const QString original = reader.attributes().value(QStringLiteral("draw:name")).toString();
            const QString formula = resolve(reader.attributes().value(QStringLiteral("draw:formula")).toString());
            const int version = ++versions[original];
            const QString unique = version == 1 ? original : original + QStringLiteral("Version%1").arg(version);
            writer.writeEmptyElement(QStringLiteral("draw:equation"));
            writer.writeAttribute(QStringLiteral("draw:name"), unique);
            writer.writeAttribute(QStringLiteral("draw:formula"), formula);
            names.insert(original, unique);
        }
        writer.writeEndDocument();
        if (!reader.hasError()) {
            it.value() = rewritten;
            paths[it.key()] = resolve(paths.value(it.key()));
            textAreas[it.key()] = resolve(textAreas.value(it.key()));
        }
    }
}
