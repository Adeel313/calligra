/* SPDX-License-Identifier: LGPL-2.0-or-later */
#pragma once

// Preserve the rectangular geometry while using the text-frame importer for
// fitted text boxes. The custom-shape importer does not implement text fitting.
inline bool normalizeRectangularTextFrames(QDomDocument &document, const QDomDocument &otherStyles)
{
    const QString draw = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:drawing:1.0");
    const QString style = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:style:1.0");
    const QString text = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0");
    QMap<QString, QDomElement> styles;
    for (auto source : {otherStyles, document}) {
        const auto nodes = source.elementsByTagNameNS(style, QStringLiteral("style"));
        for (int i = 0; i < nodes.size(); ++i) {
            const auto node = nodes.at(i).toElement();
            styles.insert(node.attributeNS(style, QStringLiteral("name")), node);
        }
    }
    const auto property = [&](const auto &self, const QString &name, const QString &ns, const QString &attr, int depth) -> QString {
        if (name.isEmpty() || depth == 0 || !styles.contains(name)) return {};
        const auto node = styles.value(name);
        for (auto p = node.firstChildElement(); !p.isNull(); p = p.nextSiblingElement())
            if (p.namespaceURI() == style && p.localName() == QLatin1String("graphic-properties") && p.hasAttributeNS(ns, attr))
                return p.attributeNS(ns, attr);
        return self(self, node.attributeNS(style, QStringLiteral("parent-style-name")), ns, attr, depth - 1);
    };
    const QString svg = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0");
    const QString fo = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0");
    QMap<QString, QString> masterLayouts;
    QMap<QString, QString> layoutHeights;
    for (auto source : {otherStyles, document}) {
        const auto masters = source.elementsByTagNameNS(style, QStringLiteral("master-page"));
        for (int i = 0; i < masters.size(); ++i) {
            const auto master = masters.at(i).toElement();
            masterLayouts.insert(master.attributeNS(style, QStringLiteral("name")), master.attributeNS(style, QStringLiteral("page-layout-name")));
        }
        const auto layouts = source.elementsByTagNameNS(style, QStringLiteral("page-layout"));
        for (int i = 0; i < layouts.size(); ++i) {
            const auto layout = layouts.at(i).toElement();
            const auto props = layout.elementsByTagNameNS(style, QStringLiteral("page-layout-properties"));
            if (!props.isEmpty()) layoutHeights.insert(layout.attributeNS(style, QStringLiteral("name")), props.at(0).toElement().attributeNS(fo, QStringLiteral("page-height")));
        }
    }
    const auto points = [](QString value) {
        const QMap<QString, qreal> units = {{QStringLiteral("cm"), 72.0 / 2.54}, {QStringLiteral("mm"), 72.0 / 25.4},
            {QStringLiteral("in"), 72.0}, {QStringLiteral("pt"), 1.0}, {QStringLiteral("pc"), 12.0}, {QStringLiteral("px"), 0.75}};
        for (auto unit = units.cbegin(); unit != units.cend(); ++unit) {
            if (value.endsWith(unit.key())) { value.chop(unit.key().size()); bool ok; const qreal number = value.toDouble(&ok); return ok ? number * unit.value() : -1.0; }
        }
        return -1.0;
    };
    bool changed = false;
    QMap<QString, QString> fixedStyles;
    const auto fixedStyle = [&](const QString &name) {
        QString frameStyle = fixedStyles.value(name);
        if (frameStyle.isEmpty()) {
            int suffix = styles.size();
            do { frameStyle = QStringLiteral("CalligraTextFit%1").arg(++suffix); } while (styles.contains(frameStyle));
            auto fitted = document.importNode(styles.value(name), true).toElement();
            fitted.setAttributeNS(style, QStringLiteral("style:name"), frameStyle);
            QDomElement props;
            for (auto c = fitted.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
                if (c.namespaceURI() == style && c.localName() == QLatin1String("graphic-properties")) props = c;
            if (props.isNull()) { props = document.createElementNS(style, QStringLiteral("style:graphic-properties")); fitted.appendChild(props); }
            props.setAttributeNS(draw, QStringLiteral("draw:auto-grow-height"), QStringLiteral("false"));
            props.setAttributeNS(draw, QStringLiteral("draw:auto-grow-width"), QStringLiteral("false"));
            props.setAttributeNS(draw, QStringLiteral("draw:fit-to-size"), QStringLiteral("true"));
            props.setAttributeNS(style, QStringLiteral("style:shrink-to-fit"), QStringLiteral("true"));
            const QString office = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:office:1.0");
            auto automatic = document.elementsByTagNameNS(office, QStringLiteral("automatic-styles")).at(0).toElement();
            if (automatic.isNull()) { automatic = document.createElementNS(office, QStringLiteral("office:automatic-styles")); document.documentElement().insertBefore(automatic, document.documentElement().firstChild()); }
            automatic.appendChild(fitted);
            styles.insert(frameStyle, fitted);
            fixedStyles.insert(name, frameStyle);
        }
        return frameStyle;
    };
    const auto shapes = document.elementsByTagNameNS(draw, QStringLiteral("custom-shape"));
    for (int i = shapes.size() - 1; i >= 0; --i) {
        const auto shape = shapes.at(i).toElement();
        QDomElement geometry;
        QList<QDomNode> paragraphs;
        for (auto child = shape.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
            if (child.namespaceURI() == draw && child.localName() == QLatin1String("enhanced-geometry")) geometry = child;
            if (child.namespaceURI() == text && (child.localName() == QLatin1String("p") || child.localName() == QLatin1String("list")
                                               || child.localName() == QLatin1String("h"))) paragraphs.append(child);
        }
        if (geometry.attributeNS(draw, QStringLiteral("type")) != QLatin1String("ooxml-rect") || paragraphs.isEmpty()) continue;
        const QString name = shape.attributeNS(draw, QStringLiteral("style-name"));
        const QString fit = property(property, name, draw, QStringLiteral("fit-to-size"), 32);
        if (fit != QLatin1String("true") && fit != QLatin1String("shrink-to-fit")
            && property(property, name, style, QStringLiteral("shrink-to-fit"), 32) != QLatin1String("true")
            && property(property, name, draw, QStringLiteral("auto-grow-height"), 32) != QLatin1String("true")) continue;
        QString frameStyle = name;
        if (fit != QLatin1String("true") && fit != QLatin1String("shrink-to-fit")
            && property(property, name, style, QStringLiteral("shrink-to-fit"), 32) != QLatin1String("true")) {
            // The saved PPTX dimensions are the already auto-sized geometry.
            // Keep those dimensions on import, fitting any metric differences
            // rather than growing text beyond its saved frame or slide edge.
            frameStyle = fixedStyle(name);
        }
        auto frame = document.createElementNS(draw, QStringLiteral("draw:frame"));
        const auto attributes = shape.attributes();
        for (int j = 0; j < attributes.size(); ++j) {
            const auto attr = attributes.item(j).toAttr();
            if (attr.namespaceURI().isEmpty()) frame.setAttribute(attr.nodeName(), attr.value());
            else frame.setAttributeNS(attr.namespaceURI(), attr.nodeName(), attr.value());
        }
        frame.setAttributeNS(draw, QStringLiteral("draw:style-name"), frameStyle);
        auto box = document.createElementNS(draw, QStringLiteral("draw:text-box"));
        frame.appendChild(box);
        for (const auto &paragraph : paragraphs) box.appendChild(paragraph);
        for (auto child = shape.firstChild(); !child.isNull();) {
            auto next = child.nextSibling();
            if (child != geometry) frame.appendChild(child);
            child = next;
        }
        shape.parentNode().replaceChild(frame, shape);
        changed = true;
    }
    // Ordinary draw:frame text boxes can carry the same auto-size flags.
    // Preserve their saved geometry too, including inherited presentation styles.
    const QString presentation = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:presentation:1.0");
    const auto frames = document.elementsByTagNameNS(draw, QStringLiteral("frame"));
    for (int i = 0; i < frames.size(); ++i) {
        auto frame = frames.at(i).toElement();
        const auto boxes = frame.elementsByTagNameNS(draw, QStringLiteral("text-box"));
        if (boxes.isEmpty() || boxes.at(0).toElement().text().trimmed().isEmpty()) continue;
        const QString ns = frame.hasAttributeNS(presentation, QStringLiteral("style-name")) ? presentation : draw;
        const QString name = frame.attributeNS(ns, QStringLiteral("style-name"));
        const QString fit = property(property, name, draw, QStringLiteral("fit-to-size"), 32);
        const bool fitted = fit == QLatin1String("true") || fit == QLatin1String("shrink-to-fit")
            || property(property, name, style, QStringLiteral("shrink-to-fit"), 32) == QLatin1String("true");
        if (fitted && !frame.hasAttributeNS(draw, QStringLiteral("transform"))) {
            auto ancestor = frame.parentNode();
            if (ancestor.namespaceURI() == draw && ancestor.localName() == QLatin1String("page")) {
                const QString master = ancestor.toElement().attributeNS(draw, QStringLiteral("master-page-name"));
                const qreal pageHeight = points(layoutHeights.value(masterLayouts.value(master)));
                const qreal top = points(frame.attributeNS(svg, QStringLiteral("y")));
                const qreal height = points(frame.attributeNS(svg, QStringLiteral("height")));
                // A fitted text frame may be saved partly outside the slide.
                // Fit its visible content to the part that can actually render.
                if (pageHeight > 0 && top >= 0 && top < pageHeight && height > pageHeight - top + 0.1) {
                    frame.setAttributeNS(svg, QStringLiteral("svg:height"), QString::number(pageHeight - top, 'g', 16) + QStringLiteral("pt"));
                    changed = true;
                }
            }
        }
        if (fitted) continue;
        if (property(property, name, draw, QStringLiteral("auto-grow-height"), 32) != QLatin1String("true")) continue;
        frame.setAttributeNS(ns, (ns == draw ? QStringLiteral("draw:") : QStringLiteral("presentation:")) + QStringLiteral("style-name"), fixedStyle(name));
        changed = true;
    }
    return changed;
}
