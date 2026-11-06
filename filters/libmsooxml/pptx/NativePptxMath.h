/* SPDX-License-Identifier: LGPL-2.0-or-later */
// Office Math with an empty fallback is dropped by the conversion backend.
// Supply editable DrawingML runs in the disposable PPTX for these expressions.
bool preparePptxMath(QDomDocument &document)
{
    const QString ans = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
    const QString mns = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/math");
    const QString mc = QStringLiteral("http://schemas.openxmlformats.org/markup-compatibility/2006");
    const QString a14 = QStringLiteral("http://schemas.microsoft.com/office/drawing/2010/main");
    const auto child = [&](QDomElement node, const QString &name) {
        for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            if (c.namespaceURI() == mns && c.localName() == name) return c;
        return QDomElement();
    };
    const auto literal = [&](const QString &text, int level) {
        auto run = document.createElementNS(ans, QStringLiteral("a:r"));
        auto props = document.createElementNS(ans, QStringLiteral("a:rPr"));
        if (level) props.setAttribute(QStringLiteral("baseline"), level > 0 ? 30000 : -25000);
        auto font = document.createElementNS(ans, QStringLiteral("a:latin"));
        font.setAttribute(QStringLiteral("typeface"), QStringLiteral("Cambria Math"));
        props.appendChild(font); run.appendChild(props);
        auto t = document.createElementNS(ans, QStringLiteral("a:t"));
        t.appendChild(document.createTextNode(text)); run.appendChild(t);
        return run;
    };
    const auto convert = [&](const auto &self, QDomElement node, int level) -> QList<QDomNode> {
        QList<QDomNode> out;
        if (node.isNull()) return out;
        const QString tag = node.localName();
        if (tag.endsWith(QLatin1String("Pr"))) return out;
        if (tag == QLatin1String("r")) {
            auto run = literal(child(node, QStringLiteral("t")).text(), level);
            const auto properties = node.elementsByTagNameNS(ans, QStringLiteral("rPr"));
            if (!properties.isEmpty()) {
                auto props = properties.at(0).cloneNode(true).toElement();
                if (level) props.setAttribute(QStringLiteral("baseline"), level > 0 ? 30000 : -25000);
                run.replaceChild(props, run.firstChild());
            }
            out.append(run);
        } else if (tag == QLatin1String("sSup") || tag == QLatin1String("sSub")) {
            out += self(self, child(node, QStringLiteral("e")), level);
            const bool upper = tag == QLatin1String("sSup");
            out += self(self, child(node, upper ? QStringLiteral("sup") : QStringLiteral("sub")), upper ? 1 : -1);
        } else if (tag == QLatin1String("limLow")) {
            out += self(self, child(node, QStringLiteral("e")), level);
            out += self(self, child(node, QStringLiteral("lim")), -1);
        } else if (tag == QLatin1String("acc")) {
            out += self(self, child(node, QStringLiteral("e")), level);
            const auto accent = child(child(node, QStringLiteral("accPr")), QStringLiteral("chr"));
            out.append(literal(accent.isNull() ? QString(QChar(0x0302)) : accent.attributeNS(mns, QStringLiteral("val")), level));
        } else if (tag == QLatin1String("d")) {
            const auto props = child(node, QStringLiteral("dPr"));
            const auto begin = child(props, QStringLiteral("begChr"));
            const auto end = child(props, QStringLiteral("endChr"));
            out.append(literal(begin.isNull() ? QStringLiteral("(") : begin.attributeNS(mns, QStringLiteral("val")), level));
            for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
                if (c.localName() == QLatin1String("e")) out += self(self, c, level);
            out.append(literal(end.isNull() ? QStringLiteral(")") : end.attributeNS(mns, QStringLiteral("val")), level));
        } else if (tag == QLatin1String("groupChr")) {
            const auto symbol = child(child(node, QStringLiteral("groupChrPr")), QStringLiteral("chr"));
            out += self(self, child(node, QStringLiteral("e")), level);
            out.append(literal(symbol.isNull() ? QString::fromUtf8("⏟") : symbol.attributeNS(mns, QStringLiteral("val")), level));
        } else if (tag == QLatin1String("nary")) {
            const auto symbol = child(child(node, QStringLiteral("naryPr")), QStringLiteral("chr"));
            out.append(literal(symbol.isNull() ? QString::fromUtf8("∑") : symbol.attributeNS(mns, QStringLiteral("val")), level));
            out += self(self, child(node, QStringLiteral("sub")), -1);
            out += self(self, child(node, QStringLiteral("sup")), 1);
            out += self(self, child(node, QStringLiteral("e")), level);
        } else if (tag == QLatin1String("f")) {
            out.append(literal(QStringLiteral("("), level));
            out += self(self, child(node, QStringLiteral("num")), level);
            out.append(literal(QStringLiteral(")/("), level));
            out += self(self, child(node, QStringLiteral("den")), level);
            out.append(literal(QStringLiteral(")"), level));
        } else {
            for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) out += self(self, c, level);
        }
        return out;
    };
    bool changed = false;
    const auto expressions = document.elementsByTagNameNS(a14, QStringLiteral("m"));
    for (int i = expressions.size() - 1; i >= 0; --i) {
        const auto expression = expressions.at(i).toElement();
        // Unhandled structures must not silently lose their meaning.
        bool supported = true;
        const QStringList structures = {QStringLiteral("oMathPara"), QStringLiteral("oMath"), QStringLiteral("r"), QStringLiteral("t"),
            QStringLiteral("sSup"), QStringLiteral("sSub"), QStringLiteral("sup"), QStringLiteral("sub"), QStringLiteral("e"),
            QStringLiteral("d"), QStringLiteral("nary"), QStringLiteral("func"), QStringLiteral("fName"), QStringLiteral("f"), QStringLiteral("num"), QStringLiteral("den"),
            QStringLiteral("limLow"), QStringLiteral("lim"), QStringLiteral("acc"), QStringLiteral("groupChr")};
        const auto check = [&](const auto &self, QDomElement node) -> void {
            if (node.namespaceURI() == mns) {
                if (node.localName().endsWith(QLatin1String("Pr"))) return;
                if (!structures.contains(node.localName())) supported = false;
            }
            for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) self(self, c);
        };
        check(check, expression);
        if (!supported) continue;
        const auto runs = convert(convert, expression, 0);
        auto parent = expression.parentNode();
        for (const auto &run : runs) parent.insertBefore(run, expression);
        parent.removeChild(expression); changed = true;
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
