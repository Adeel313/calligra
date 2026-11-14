/* SPDX-License-Identifier: LGPL-2.0-or-later */
// Office Math with an empty fallback is dropped by the conversion backend.
// Supply editable DrawingML runs in the disposable PPTX for these expressions.
//
// PresentationML text cannot carry a formula object, so stacked Office Math is
// spelled out linearly: scripts keep the smaller size PowerPoint gives them so
// an expression still reads like the formula it came from, fractions become
// "a/b", roots "√(x)" and matrices "1, 2; 3, 4".  Unknown constructs are
// recursed into rather than skipped: losing a term is worse than a plain
// linear spelling, and a dropped <a14:m> removed the whole expression from the
// slide because the text importer has no handler for it.
bool preparePptxMath(QDomDocument &document)
{
    const QString ans = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
    const QString mns = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/math");
    const QString mc = QStringLiteral("http://schemas.openxmlformats.org/markup-compatibility/2006");
    const QString a14 = QStringLiteral("http://schemas.microsoft.com/office/drawing/2010/main");
    // Baseline shift in thousandths of a percent, as DrawingML stores it.
    const int superscript = 30000;
    const int subscript = -25000;

    struct Flattener {
        QDomDocument *document;
        QString ans;
        QString mns;
        int superscript;
        int subscript;
        // Run properties shared by the whole expression. Literal punctuation
        // the flattener inserts ("(", ")", "/", separators) must look like the
        // math around it instead of falling back to the paragraph's default
        // 18 pt plain text.
        QDomElement base;
        // Math that shares its paragraph with text takes that text's size.
        // A pasted expression can carry a stale size of its own (32 pt inside
        // a 28 pt sentence), which reads as a different font on the slide.
        bool inlineMath;
        int inlineSize;

        // PowerPoint renders script arguments at 65% of the base size.
        int scriptSize(int size) const { return size > 0 ? qMax(1, qRound(size * 0.65)) : size; }

        QDomElement child(QDomElement node, const QString &name) const
        {
            for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
                if (c.namespaceURI() == mns && c.localName() == name) return c;
            return QDomElement();
        }
        QString attribute(QDomElement node, const QString &fallback = QString()) const
        {
            if (node.isNull()) return fallback;
            const QString value = node.attributeNS(mns, QStringLiteral("val"));
            return value.isEmpty() ? fallback : value;
        }
        QString property(QDomElement node, const QString &properties, const QString &name, const QString &fallback = QString()) const
        {
            return attribute(child(child(node, properties), name), fallback);
        }
        bool hidden(QDomElement node, const QString &name) const
        {
            // <m:naryPr><m:supHide m:val="1"/></m:naryPr> hides that script.
            const auto flag = child(child(node, QStringLiteral("naryPr")), name);
            if (flag.isNull()) return false;
            const QString value = attribute(flag, QStringLiteral("1"));
            return value != QLatin1String("0") && value != QLatin1String("false") && value != QLatin1String("off");
        }
        QList<QDomElement> elements(QDomElement node, const QString &name) const
        {
            QList<QDomElement> result;
            for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
                if (c.namespaceURI() == mns && c.localName() == name) result.append(c);
            return result;
        }
        QDomElement runProperties(QDomElement node) const
        {
            for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
                if (c.namespaceURI() == ans && c.localName() == QStringLiteral("rPr")) return c;
            return QDomElement();
        }
        // A single short token needs no parentheses when it becomes a/b.
        bool simple(QDomElement container) const
        {
            if (container.isNull()) return false;
            int runs = 0;
            QString text;
            for (auto c = container.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
                if (c.namespaceURI() != mns || c.localName() != QLatin1String("r")) return false;
                ++runs;
                text += child(c, QStringLiteral("t")).text();
            }
            return runs == 1 && text.size() <= 2 && !text.contains(QLatin1Char(' '));
        }
        QDomElement makeRun(const QString &text, QDomElement source, int baseline, int size) const
        {
            auto run = document->createElementNS(ans, QStringLiteral("a:r"));
            const bool literal = source.isNull();
            auto properties = literal ? QDomElement() : source.cloneNode(true).toElement();
            if (properties.isNull() && !base.isNull())
                properties = base.cloneNode(true).toElement();
            if (properties.isNull()) {
                properties = document->createElementNS(ans, QStringLiteral("a:rPr"));
                auto font = document->createElementNS(ans, QStringLiteral("a:latin"));
                font.setAttribute(QStringLiteral("typeface"), QStringLiteral("Cambria Math"));
                properties.appendChild(font);
            }
            properties.removeAttribute(QStringLiteral("baseline"));
            if (baseline) properties.setAttribute(QStringLiteral("baseline"), QString::number(baseline));
            // Scripts shrink to the size PowerPoint uses for them. Literal
            // punctuation has no run of its own, so it takes the expression's
            // size; runs from the source keep whatever they already specify.
            if (inlineMath && inlineSize > 0) {
                // Inline math follows the size of the sentence it sits in, so
                // the expression is not visibly larger than the words around
                // it even when the source carries a stale size of its own.
                properties.setAttribute(QStringLiteral("sz"),
                                        QString::number(baseline ? scriptSize(inlineSize) : inlineSize));
            } else if (inlineMath) {
                // The sentence inherits its size from the placeholder. Let the
                // expression inherit it too instead of pinning a stale size.
                if (baseline) {
                    const QString own = properties.attribute(QStringLiteral("sz"));
                    if (!own.isEmpty()) properties.setAttribute(QStringLiteral("sz"), QString::number(scriptSize(own.toInt())));
                } else {
                    properties.removeAttribute(QStringLiteral("sz"));
                }
            } else if (baseline) {
                int scaled = size;
                if (scaled <= 0) {
                    const QString own = properties.attribute(QStringLiteral("sz"));
                    if (!own.isEmpty()) scaled = scriptSize(own.toInt());
                }
                if (scaled > 0) properties.setAttribute(QStringLiteral("sz"), QString::number(scaled));
            } else if (literal && size > 0) {
                properties.setAttribute(QStringLiteral("sz"), QString::number(size));
            }
            run.appendChild(properties);
            auto textNode = document->createElementNS(ans, QStringLiteral("a:t"));
            textNode.appendChild(document->createTextNode(text));
            run.appendChild(textNode);
            return run;
        }
        QList<QDomNode> literal(const QString &text, int baseline, int size) const
        {
            if (text.isEmpty()) return {};
            return {makeRun(text, QDomElement(), baseline, size)};
        }
        QList<QDomNode> convert(QDomElement node, int baseline, int size) const
        {
            QList<QDomNode> out;
            if (node.isNull()) return out;
            const QString tag = node.localName();
            if (tag.endsWith(QLatin1String("Pr"))) return out;
            if (node.namespaceURI() != mns) {
                // Host wrappers such as a14:m only hold the expression.
                for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) out += convert(c, baseline, size);
                return out;
            }
            const auto child = [this](QDomElement parent, const QString &name) { return this->child(parent, name); };
            const auto run = [this](QDomElement r) { return this->runProperties(r); };
            const auto recurse = [&](QDomElement c, int shift, int scale) {
                if (c.isNull()) return;
                out += convert(c, shift ? shift : baseline, scale ? this->scriptSize(size) : size);
            };
            if (tag == QLatin1String("r")) {
                const QString text = child(node, QStringLiteral("t")).text();
                if (!text.isEmpty()) out.append(makeRun(text, run(node), baseline, size));
            } else if (tag == QLatin1String("t")) {
                const QString text = node.text();
                if (!text.isEmpty()) out.append(makeRun(text, QDomElement(), baseline, size));
            } else if (tag == QLatin1String("sSup") || tag == QLatin1String("sSub")) {
                const bool upper = tag == QLatin1String("sSup");
                recurse(child(node, QStringLiteral("e")), 0, 0);
                recurse(child(node, upper ? QStringLiteral("sup") : QStringLiteral("sub")), upper ? superscript : subscript, 1);
            } else if (tag == QLatin1String("sSubSup")) {
                recurse(child(node, QStringLiteral("e")), 0, 0);
                recurse(child(node, QStringLiteral("sub")), subscript, 1);
                recurse(child(node, QStringLiteral("sup")), superscript, 1);
            } else if (tag == QLatin1String("sPre")) {
                recurse(child(node, QStringLiteral("sub")), subscript, 1);
                recurse(child(node, QStringLiteral("sup")), superscript, 1);
                recurse(child(node, QStringLiteral("e")), 0, 0);
            } else if (tag == QLatin1String("f")) {
                const auto numerator = child(node, QStringLiteral("num"));
                const auto denominator = child(node, QStringLiteral("den"));
                const bool bare = simple(numerator) && simple(denominator);
                if (!bare) out += literal(QStringLiteral("("), baseline, size);
                recurse(numerator, 0, 0);
                if (!bare) out += literal(QStringLiteral(")"), baseline, size);
                out += literal(QStringLiteral("/"), baseline, size);
                if (!bare) out += literal(QStringLiteral("("), baseline, size);
                recurse(denominator, 0, 0);
                if (!bare) out += literal(QStringLiteral(")"), baseline, size);
            } else if (tag == QLatin1String("rad")) {
                const auto degree = child(node, QStringLiteral("deg"));
                QString radical = QString::fromUtf8("√");
                const QString index = degree.isNull() ? QString() : degree.text().trimmed();
                if (degree.hasChildNodes() && index == QLatin1String("3")) radical = QString::fromUtf8("∛");
                else if (degree.hasChildNodes() && index == QLatin1String("4")) radical = QString::fromUtf8("∜");
                else if (degree.hasChildNodes()) {
                    out += literal(index, superscript, scriptSize(size));
                }
                out += literal(radical, baseline, size);
                out += literal(QStringLiteral("("), baseline, size);
                recurse(child(node, QStringLiteral("e")), 0, 0);
                out += literal(QStringLiteral(")"), baseline, size);
            } else if (tag == QLatin1String("nary")) {
                const QString symbol = attribute(child(child(node, QStringLiteral("naryPr")), QStringLiteral("chr")),
                                                QString::fromUtf8("∫"));
                out += literal(symbol, baseline, size);
                if (!hidden(node, QStringLiteral("subHide")) && child(node, QStringLiteral("sub")).hasChildNodes())
                    recurse(child(node, QStringLiteral("sub")), subscript, 1);
                if (!hidden(node, QStringLiteral("supHide")) && child(node, QStringLiteral("sup")).hasChildNodes())
                    recurse(child(node, QStringLiteral("sup")), superscript, 1);
                const auto body = child(node, QStringLiteral("e"));
                if (body.hasChildNodes()) out += literal(QStringLiteral(" "), baseline, size);
                recurse(body, 0, 0);
            } else if (tag == QLatin1String("limLow") || tag == QLatin1String("limUpp")) {
                const bool low = tag == QLatin1String("limLow");
                recurse(child(node, QStringLiteral("e")), 0, 0);
                recurse(child(node, QStringLiteral("lim")), low ? subscript : superscript, 1);
            } else if (tag == QLatin1String("func")) {
                recurse(child(node, QStringLiteral("fName")), 0, 0);
                out += literal(QStringLiteral(" "), baseline, size);
                recurse(child(node, QStringLiteral("e")), 0, 0);
            } else if (tag == QLatin1String("d")) {
                const auto props = child(node, QStringLiteral("dPr"));
                const QString begin = attribute(child(props, QStringLiteral("begChr")), QStringLiteral("("));
                const QString end = attribute(child(props, QStringLiteral("endChr")), QStringLiteral(")"));
                const QString separator = attribute(child(props, QStringLiteral("sepChr")), QStringLiteral("|"));
                out += literal(begin, baseline, size);
                QList<QDomElement> items;
                for (const auto &item : elements(node, QStringLiteral("e")))
                    if (item.hasChildNodes() || !item.text().trimmed().isEmpty()) items.append(item);
                for (int item = 0; item < items.size(); ++item) {
                    if (item) out += literal(separator, baseline, size);
                    recurse(items.at(item), 0, 0);
                }
                out += literal(end, baseline, size);
            } else if (tag == QLatin1String("acc")) {
                recurse(child(node, QStringLiteral("e")), 0, 0);
                const QString accent = property(node, QStringLiteral("accPr"), QStringLiteral("chr"), QString(QChar(0x0302)));
                out += literal(accent, baseline, size);
            } else if (tag == QLatin1String("bar")) {
                recurse(child(node, QStringLiteral("e")), 0, 0);
                const bool below = property(node, QStringLiteral("barPr"), QStringLiteral("pos")) == QLatin1String("bot");
                out += literal(QString(QChar(below ? 0x0332 : 0x0304)), baseline, size);
            } else if (tag == QLatin1String("groupChr")) {
                recurse(child(node, QStringLiteral("e")), 0, 0);
                out += literal(property(node, QStringLiteral("groupChrPr"), QStringLiteral("chr"), QString(QChar(0x23DF))), baseline, size);
            } else if (tag == QLatin1String("m")) {
                bool firstRow = true;
                for (const auto &row : elements(node, QStringLiteral("mr"))) {
                    if (!firstRow) out += literal(QStringLiteral("; "), baseline, size);
                    firstRow = false;
                    bool firstCell = true;
                    for (const auto &cell : elements(row, QStringLiteral("e"))) {
                        if (!firstCell) out += literal(QStringLiteral(", "), baseline, size);
                        firstCell = false;
                        recurse(cell, 0, 0);
                    }
                }
            } else if (tag == QLatin1String("eqArr")) {
                bool first = true;
                for (const auto &line : elements(node, QStringLiteral("e"))) {
                    if (!first) out += literal(QStringLiteral("; "), baseline, size);
                    first = false;
                    recurse(line, 0, 0);
                }
            } else {
                // Functions, boxes, phantom runs and anything we do not model
                // keep their content in document order. Office also writes raw
                // text directly inside containers such as m:num, m:den and m:e,
                // so text nodes must be kept instead of skipped.
                for (auto c = node.firstChild(); !c.isNull(); c = c.nextSibling()) {
                    if (c.isText()) {
                        const QString text = c.nodeValue();
                        if (!text.isEmpty()) out += literal(text, baseline, size);
                    } else if (c.isElement()) {
                        out += convert(c.toElement(), baseline, size);
                    }
                }
            }
            return out;
        }
    };

    Flattener flattener{&document, ans, mns, superscript, subscript, QDomElement(), false, 0};

    bool changed = false;
    const auto expressions = document.elementsByTagNameNS(a14, QStringLiteral("m"));
    for (int i = expressions.size() - 1; i >= 0; --i) {
        const auto expression = expressions.at(i).toElement();
        // Scripts scale from the size the paragraph's own runs use. Sizes from
        // an enclosing shape or placeholder must not leak into the expression,
        // and neither may the size of a script we already flattened.
        int size = 0;
        QDomElement base;
        int inlineSize = 0;
        bool inlineMath = false;
        const auto paragraph = expression.parentNode().toElement();
        const auto runSize = [&](const QDomElement &run) {
            if (run.isNull()) return 0;
            for (auto c = run.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
                if (c.namespaceURI() != ans || c.localName() != QLatin1String("rPr")) continue;
                const QString value = c.attribute(QStringLiteral("sz"));
                if (!value.isEmpty()) return value.toInt();
            }
            return 0;
        };
        if (!paragraph.isNull()) {
            // Text beside the expression marks it as inline math. Remember the
            // nearest runs on each side: their size is the sentence's size.
            QDomElement before;
            QDomElement after;
            bool seenExpression = false;
            for (auto node = paragraph.firstChildElement(); !node.isNull(); node = node.nextSiblingElement()) {
                if (node == expression) {
                    seenExpression = true;
                    continue;
                }
                if (node.namespaceURI() != ans
                    || (node.localName() != QLatin1String("r") && node.localName() != QLatin1String("fld")))
                    continue;
                // Whitespace runs often carry a stray size (an enlarged space
                // in the source); they do not describe the sentence.
                QString text;
                for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
                    if (c.namespaceURI() == ans && c.localName() == QLatin1String("t")) text += c.text();
                if (text.trimmed().isEmpty()) continue;
                if (!seenExpression) before = node;
                else if (after.isNull()) after = node;
            }
            inlineMath = !before.isNull() || !after.isNull();
            if (inlineMath) {
                inlineSize = runSize(before);
                if (inlineSize <= 0) inlineSize = runSize(after);
            }
            // Prefer the expression's own formatting: a formula keeps the font,
            // weight, slant and colour its runs specify, so the punctuation the
            // flattener adds is not plainer than the terms beside it.
            const auto ownProperties = expression.elementsByTagNameNS(ans, QStringLiteral("rPr"));
            const auto properties = paragraph.elementsByTagNameNS(ans, QStringLiteral("rPr"));
            // m:ctrlPr carries hints such as panose and pitchFamily that the
            // actual m:r runs do not; take the look from a real run first.
            for (int p = 0; p < ownProperties.size() && base.isNull(); ++p) {
                const auto run = ownProperties.at(p).toElement();
                if (!run.hasAttribute(QStringLiteral("baseline"))
                    && run.parentNode().toElement().localName() == QLatin1String("r"))
                    base = run.cloneNode(true).toElement();
            }
            for (int p = 0; p < ownProperties.size() && base.isNull(); ++p) {
                const auto run = ownProperties.at(p).toElement();
                if (!run.hasAttribute(QStringLiteral("baseline"))) base = run.cloneNode(true).toElement();
            }
            for (int p = 0; p < properties.size() && !size; ++p) {
                const auto run = properties.at(p).toElement();
                const QString value = run.attribute(QStringLiteral("sz"));
                if (!value.isEmpty() && !run.hasAttribute(QStringLiteral("baseline"))) size = value.toInt();
            }
            // A formula without run properties takes the look of the text it
            // sits in, so inline math matches the paragraph around it.
            if (base.isNull()) {
                for (int p = 0; p < properties.size() && base.isNull(); ++p) {
                    const auto run = properties.at(p).toElement();
                    if (!run.hasAttribute(QStringLiteral("baseline"))) base = run.cloneNode(true).toElement();
                }
            }
            if (base.isNull()) {
                // A paragraph whose only content is math still records its
                // intended look in endParaRPr (or the paragraph default).
                const auto endProperties = paragraph.elementsByTagNameNS(ans, QStringLiteral("endParaRPr"));
                if (!endProperties.isEmpty()) {
                    base = endProperties.at(0).toElement().cloneNode(true).toElement();
                    base.setTagName(QStringLiteral("a:rPr"));
                }
            }
            if (!base.isNull()) base.removeAttribute(QStringLiteral("baseline"));
        }
        if (inlineMath && inlineSize <= 0 && !paragraph.isNull()) {
            // A paragraph that records its intended size only in endParaRPr
            // still tells us how large the sentence is.
            const auto endProperties = paragraph.elementsByTagNameNS(ans, QStringLiteral("endParaRPr"));
            if (!endProperties.isEmpty()) {
                const QString value = endProperties.at(0).toElement().attribute(QStringLiteral("sz"));
                if (!value.isEmpty()) inlineSize = value.toInt();
            }
        }
        if (size == 0 && !base.isNull()) {
            const QString value = base.attribute(QStringLiteral("sz"));
            if (!value.isEmpty()) size = value.toInt();
        }
        if (inlineMath && inlineSize > 0) size = inlineSize;
        flattener.base = base;
        flattener.inlineMath = inlineMath;
        flattener.inlineSize = inlineSize;
        const auto runs = flattener.convert(expression, 0, size);
        auto parent = expression.parentNode();
        for (const auto &run : runs) parent.insertBefore(run, expression);
        parent.removeChild(expression);
        changed = true;
    }
    // Expose supported PresentationML shapes directly. Their alternatives
    // otherwise share reader state with nested choices and can lose text.
    const auto alternatives = document.elementsByTagNameNS(mc, QStringLiteral("AlternateContent"));
    for (int i = alternatives.size() - 1; i >= 0; --i) {
        const auto alternate = alternatives.at(i).toElement();
        auto choice = alternate.firstChildElement();
        if (choice.localName() != QLatin1String("Choice") || choice.attribute(QStringLiteral("Requires")) == QLatin1String("v")) continue;
        const auto shape = choice.firstChildElement();
        const bool presentationShape = shape.namespaceURI() == QLatin1String("http://schemas.openxmlformats.org/presentationml/2006/main")
            && QStringList{QStringLiteral("sp"), QStringLiteral("grpSp"), QStringLiteral("pic"), QStringLiteral("graphicFrame")}.contains(shape.localName());
        const bool textRun = shape.namespaceURI() == ans
            && QStringList{QStringLiteral("r"), QStringLiteral("br"), QStringLiteral("fld")}.contains(shape.localName());
        if (!presentationShape && !textRun) continue;
        auto parent = alternate.parentNode();
        while (!choice.firstChild().isNull()) parent.insertBefore(choice.firstChild(), alternate);
        parent.removeChild(alternate);
        changed = true;
    }
    return changed;
}
