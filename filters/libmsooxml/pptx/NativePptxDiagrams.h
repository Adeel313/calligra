/* SPDX-License-Identifier: LGPL-2.0-or-later */
// Included in the native importer’s private namespace after rewritePackage.
// Use Office's saved SmartArt drawing rather than asking another layout engine
// to regenerate it. Only the disposable conversion input is modified.

#include "NativePptxMath.h"
#include "NativePptxGradients.h"

bool preparePptxDiagrams(const QString &file)
{
    const QString pns = QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main");
    const QString ans = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
    const QString rns = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    const QString dns = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/diagram");
    const QString dsp = QStringLiteral("http://schemas.microsoft.com/office/drawing/2008/diagram");
    KZip archive(file);
    if (!archive.open(QIODevice::ReadOnly)) return false;
    const auto read = [&](const QString &name) {
        const auto *entry = dynamic_cast<const KArchiveFile *>(archive.directory()->entry(name));
        return entry ? entry->data() : QByteArray();
    };
    const auto parse = [](const QByteArray &bytes) {
        QDomDocument doc;
        doc.setContent(bytes, QDomDocument::ParseOptions(QDomDocument::ParseOption::UseNamespaceProcessing) | QDomDocument::ParseOption::PreserveSpacingOnlyNodes);
        return doc;
    };
    const auto first = [](const QDomNode &parent, const QString &ns, const QString &name) {
        for (auto node = parent.firstChildElement(); !node.isNull(); node = node.nextSiblingElement())
            if (node.namespaceURI() == ns && node.localName() == name) return node;
        return QDomElement();
    };
    const auto resolve = [](const QString &part, const QString &target) {
        return target.startsWith(QLatin1Char('/')) ? target.mid(1) : QDir::cleanPath(QFileInfo(part).path() + QLatin1Char('/') + target);
    };
    QMap<QString, QByteArray> updates;
    const auto *slides = dynamic_cast<const KArchiveDirectory *>(archive.directory()->entry(QStringLiteral("ppt/slides")));
    if (!slides) return true;
    for (const QString &name : slides->entries()) {
        if (!name.endsWith(QLatin1String(".xml"))) continue;
        const QString part = QStringLiteral("ppt/slides/") + name;
        const QString relpart = QStringLiteral("ppt/slides/_rels/") + name + QStringLiteral(".rels");
        const QByteArray slideBytes = read(part);
        // Most slides need no normalization. Avoid building a DOM and reading
        // relationships for ordinary text, pictures, tables and charts. Use
        // expanded names so alternate namespace prefixes work too.
        QXmlStreamReader probe(slideBytes);
        bool needsPreparation = false;
        while (!probe.atEnd()) {
            probe.readNext();
            if (!probe.isStartElement()) continue;
            if ((probe.namespaceUri() == ans && probe.name() == QLatin1String("gradFill"))
                || (probe.namespaceUri() == dns && probe.name() == QLatin1String("relIds"))
                || (probe.namespaceUri() == QLatin1String("http://schemas.microsoft.com/office/drawing/2010/main") && probe.name() == QLatin1String("m"))
                || (probe.namespaceUri() == QLatin1String("http://schemas.openxmlformats.org/markup-compatibility/2006") && probe.name() == QLatin1String("AlternateContent"))) {
                needsPreparation = true;
                break;
            }
        }
        if (probe.hasError()) return false;
        if (!needsPreparation) continue;
        auto slide = parse(slideBytes);
        auto rels = parse(read(relpart));
        QMap<QString, QDomElement> relationships;
        for (auto rel = rels.documentElement().firstChildElement(); !rel.isNull(); rel = rel.nextSiblingElement())
            relationships.insert(rel.attribute(QStringLiteral("Id")), rel);
        uint nextId = 0;
        const auto existingIds = slide.elementsByTagNameNS(pns, QStringLiteral("cNvPr"));
        for (int i = 0; i < existingIds.size(); ++i) nextId = qMax(nextId, existingIds.at(i).toElement().attribute(QStringLiteral("id")).toUInt());
        bool changed = preparePptxMath(slide);
        changed = preparePptxGradientRectangles(slide) || changed;
        bool relationshipsChanged = false;
        const auto frames = slide.elementsByTagNameNS(pns, QStringLiteral("graphicFrame"));
        for (int i = frames.size() - 1; i >= 0; --i) {
            const auto frame = frames.at(i).toElement();
            const auto ids = frame.elementsByTagNameNS(dns, QStringLiteral("relIds"));
            if (ids.isEmpty()) continue;
            const auto dataRel = relationships.value(ids.at(0).toElement().attributeNS(rns, QStringLiteral("dm")));
            auto data = parse(read(resolve(part, dataRel.attribute(QStringLiteral("Target")))));
            const auto extension = data.elementsByTagNameNS(dsp, QStringLiteral("dataModelExt"));
            if (extension.isEmpty()) continue;
            const auto drawingRel = relationships.value(extension.at(0).toElement().attribute(QStringLiteral("relId")));
            if (!drawingRel.attribute(QStringLiteral("Type")).endsWith(QLatin1String("/diagramDrawing"))) continue;
            const QString drawingPart = resolve(part, drawingRel.attribute(QStringLiteral("Target")));
            auto drawing = parse(read(drawingPart));
            const auto tree = first(drawing.documentElement(), dsp, QStringLiteral("spTree"));
            const auto transform = first(frame, pns, QStringLiteral("xfrm"));
            if (tree.isNull() || transform.isNull()) continue;
            auto group = slide.createElementNS(pns, QStringLiteral("p:grpSp"));
            const auto copy = [&](const auto &self, const QDomNode &node) -> QDomNode {
                if (!node.isElement()) return slide.importNode(node, true);
                auto original = node.toElement();
                if (original.namespaceURI() == dsp && original.localName() == QLatin1String("txXfrm")) return {};
                auto result = original.namespaceURI() == dsp
                    ? slide.createElementNS(pns, QStringLiteral("p:") + original.localName())
                    : slide.createElementNS(original.namespaceURI(), original.nodeName());
                const auto attributes = original.attributes();
                for (int k = 0; k < attributes.size(); ++k) {
                    const auto attr = attributes.item(k).toAttr();
                    if (attr.name() == QLatin1String("modelId")) continue;
                    if (attr.namespaceURI().isEmpty()) result.setAttribute(attr.name(), attr.value());
                    else result.setAttributeNS(attr.namespaceURI(), attr.nodeName(), attr.value());
                }
                for (auto child = node.firstChild(); !child.isNull(); child = child.nextSibling()) {
                    const auto cloned = self(self, child);
                    if (!cloned.isNull()) result.appendChild(cloned);
                }
                if (result.namespaceURI() == pns && result.localName() == QLatin1String("cNvPr"))
                    result.setAttribute(QStringLiteral("id"), ++nextId);
                if (result.namespaceURI() == pns && (result.localName() == QLatin1String("nvSpPr") || result.localName() == QLatin1String("nvGrpSpPr")))
                    if (first(result, pns, QStringLiteral("nvPr")).isNull()) result.appendChild(slide.createElementNS(pns, QStringLiteral("p:nvPr")));
                return result;
            };
            for (auto child = tree.firstChild(); !child.isNull(); child = child.nextSibling()) group.appendChild(copy(copy, child));
            auto groupTransform = first(first(group, pns, QStringLiteral("grpSpPr")), ans, QStringLiteral("xfrm"));
            if (groupTransform.isNull()) {
                // Office commonly omits the cached tree's identity transform.
                // The children then use the frame's extent as their canvas.
                auto properties = first(group, pns, QStringLiteral("grpSpPr"));
                if (properties.isNull()) continue;
                groupTransform = slide.createElementNS(ans, QStringLiteral("a:xfrm"));
                properties.appendChild(groupTransform);
                for (const QString &tag : {QStringLiteral("off"), QStringLiteral("ext")}) {
                    const auto value = first(transform, ans, tag);
                    if (!value.isNull()) groupTransform.appendChild(value.cloneNode(true));
                }
                auto childOffset = slide.createElementNS(ans, QStringLiteral("a:chOff"));
                childOffset.setAttribute(QStringLiteral("x"), 0); childOffset.setAttribute(QStringLiteral("y"), 0);
                auto childExtent = slide.createElementNS(ans, QStringLiteral("a:chExt"));
                const auto extent = first(transform, ans, QStringLiteral("ext"));
                childExtent.setAttribute(QStringLiteral("cx"), extent.attribute(QStringLiteral("cx")));
                childExtent.setAttribute(QStringLiteral("cy"), extent.attribute(QStringLiteral("cy")));
                groupTransform.appendChild(childOffset); groupTransform.appendChild(childExtent);
            }
            for (const QString &tag : {QStringLiteral("off"), QStringLiteral("ext")}) {
                const auto value = first(transform, ans, tag);
                if (!value.isNull()) {
                    const auto previous = first(groupTransform, ans, tag);
                    if (previous.isNull()) groupTransform.insertBefore(value.cloneNode(true),groupTransform.firstChild());
                    else groupTransform.replaceChild(value.cloneNode(true), previous);
                }
            }
            // Drawing relationships are scoped to the diagram part. Move them
            // with the native shapes, preserving images and external links.
            const QString drawingRels = QFileInfo(drawingPart).path() + QStringLiteral("/_rels/") + QFileInfo(drawingPart).fileName() + QStringLiteral(".rels");
            auto resources = parse(read(drawingRels));
            for (auto rel = resources.documentElement().firstChildElement(); !rel.isNull(); rel = rel.nextSiblingElement()) {
                const QString oldId = rel.attribute(QStringLiteral("Id"));
                QString newId;
                do { newId = QStringLiteral("rIdDiagram%1").arg(++nextId); } while (relationships.contains(newId));
                auto imported = rels.importNode(rel, true).toElement();
                imported.setAttribute(QStringLiteral("Id"), newId);
                if (rel.attribute(QStringLiteral("TargetMode")) != QLatin1String("External"))
                    imported.setAttribute(QStringLiteral("Target"), QDir(QStringLiteral("/ppt/slides")).relativeFilePath(QLatin1Char('/') + resolve(drawingPart, rel.attribute(QStringLiteral("Target")))));
                rels.documentElement().appendChild(imported);
                relationships.insert(newId, imported);
                relationshipsChanged = true;
                const auto remap = [&](const auto &self, QDomElement node) -> void {
                    for (const QString &attr : {QStringLiteral("embed"), QStringLiteral("link"), QStringLiteral("id")})
                        if (node.attributeNS(rns, attr) == oldId) node.setAttributeNS(rns, QStringLiteral("r:") + attr, newId);
                    for (auto child = node.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) self(self, child);
                };
                remap(remap, group);
            }
            frame.parentNode().replaceChild(group, frame);
            changed = true;
        }
        // Cached diagram shapes can themselves contain Office Math.
        // Normalize again after inserting them into the slide tree.
        changed = preparePptxMath(slide) || changed;
        changed = preparePptxGradientRectangles(slide) || changed;
        if (changed) updates.insert(part, slide.toByteArray());
        // Math and AlternateContent normalization does not alter relationships.
        // In particular, do not create an empty XML part when none existed.
        if (relationshipsChanged) updates.insert(relpart, rels.toByteArray());
    }
    // Notes, layouts and masters can carry Office Math as well. Normalize them
    // the same way so an expression is spelled alike everywhere it appears.
    for (const QString &directory : {QStringLiteral("ppt/notesSlides"), QStringLiteral("ppt/slideLayouts"), QStringLiteral("ppt/slideMasters")}) {
        const auto *parts = dynamic_cast<const KArchiveDirectory *>(archive.directory()->entry(directory));
        if (!parts) continue;
        for (const QString &name : parts->entries()) {
            if (!name.endsWith(QLatin1String(".xml"))) continue;
            const QString part = directory + QLatin1Char('/') + name;
            const QByteArray bytes = read(part);
            QXmlStreamReader probe(bytes);
            bool needsPreparation = false;
            while (!probe.atEnd()) {
                probe.readNext();
                if (!probe.isStartElement()) continue;
                if ((probe.namespaceUri() == ans && probe.name() == QLatin1String("gradFill"))
                    || (probe.namespaceUri() == QStringLiteral("http://schemas.microsoft.com/office/drawing/2010/main") && probe.name() == QLatin1String("m"))
                    || (probe.namespaceUri() == QStringLiteral("http://schemas.openxmlformats.org/markup-compatibility/2006") && probe.name() == QLatin1String("AlternateContent"))) {
                    needsPreparation = true;
                    break;
                }
            }
            if (probe.hasError() || !needsPreparation) continue;
            auto document = parse(bytes);
            bool changed = preparePptxMath(document);
            changed = preparePptxGradientRectangles(document) || changed;
            if (changed) updates.insert(part, document.toByteArray());
        }
    }
    archive.close();
    return updates.isEmpty() || rewritePackage(file, updates);
}
