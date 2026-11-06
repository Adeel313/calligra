/* SPDX-License-Identifier: LGPL-2.0-or-later */
// Flatten cardinal rotation on empty gradient rectangles. ODF consumers differ
// on whether opacity angles rotate with a text frame; express the same visual
// rectangle without that ambiguity. Text-bearing and nonrectangular shapes keep
// their original geometry. The caller only modifies a disposable source copy.
bool preparePptxGradientRectangles(QDomDocument &document)
{
    const QString pns = QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main");
    const QString ans = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
    const auto child = [](QDomElement node, const QString &ns, const QString &name) {
        for (auto c = node.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
            if (c.namespaceURI() == ns && c.localName() == name) return c;
        return QDomElement();
    };
    bool changed = false;
    const auto shapes = document.elementsByTagNameNS(pns, QStringLiteral("sp"));
    for (int i = 0; i < shapes.size(); ++i) {
        const auto shape = shapes.at(i).toElement();
        const auto props = child(shape, pns, QStringLiteral("spPr"));
        const auto geometry = child(props, ans, QStringLiteral("prstGeom"));
        auto transform = child(props, ans, QStringLiteral("xfrm"));
        const auto gradient = child(props, ans, QStringLiteral("gradFill"));
        auto linear = child(gradient, ans, QStringLiteral("lin"));
        constexpr qint64 quarterTurn = 90 * 60000;
        constexpr qint64 fullTurn = 4 * quarterTurn;
        const qint64 rotation = transform.attribute(QStringLiteral("rot")).toLongLong();
        if (geometry.attribute(QStringLiteral("prst")) != QLatin1String("rect") || linear.isNull()
            || rotation % quarterTurn || rotation % fullTurn == 0
            || transform.attribute(QStringLiteral("flipH")) == QLatin1String("1")
            || transform.attribute(QStringLiteral("flipH")) == QLatin1String("true")
            || transform.attribute(QStringLiteral("flipV")) == QLatin1String("1")
            || transform.attribute(QStringLiteral("flipV")) == QLatin1String("true")
            || gradient.attribute(QStringLiteral("rotWithShape")) == QLatin1String("0")
            || gradient.attribute(QStringLiteral("rotWithShape")) == QLatin1String("false")) continue;
        bool hasText = false;
        const auto texts = shape.elementsByTagNameNS(ans, QStringLiteral("t"));
        for (int t = 0; t < texts.size(); ++t) hasText |= !texts.at(t).toElement().text().trimmed().isEmpty();
        if (hasText) continue;
        auto offset = child(transform, ans, QStringLiteral("off"));
        auto extent = child(transform, ans, QStringLiteral("ext"));
        if (offset.isNull() || extent.isNull()) continue;
        const qint64 width = extent.attribute(QStringLiteral("cx")).toLongLong();
        const qint64 height = extent.attribute(QStringLiteral("cy")).toLongLong();
        if (width <= 0 || height <= 0) continue;
        if ((rotation / quarterTurn) % 2) {
            offset.setAttribute(QStringLiteral("x"), offset.attribute(QStringLiteral("x")).toLongLong() + (width - height) / 2);
            offset.setAttribute(QStringLiteral("y"), offset.attribute(QStringLiteral("y")).toLongLong() + (height - width) / 2);
            extent.setAttribute(QStringLiteral("cx"), height);
            extent.setAttribute(QStringLiteral("cy"), width);
        }
        const qint64 angle = linear.attribute(QStringLiteral("ang")).toLongLong() + rotation;
        linear.setAttribute(QStringLiteral("ang"), (angle % fullTurn + fullTurn) % fullTurn);
        transform.removeAttribute(QStringLiteral("rot"));
        changed = true;
    }
    return changed;
}
