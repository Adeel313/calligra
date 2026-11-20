/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2026 Adeel <adeel@example.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "DocExport.h"
#include "Word97Writer.h"

#include <KPluginFactory>

#include <KoFilterChain.h>
#include <KoStore.h>

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNode>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringLiteral>

#include <cmath>

namespace {

constexpr const char *NS_OFFICE = "urn:oasis:names:tc:opendocument:xmlns:office:1.0";
constexpr const char *NS_TEXT = "urn:oasis:names:tc:opendocument:xmlns:text:1.0";
constexpr const char *NS_DRAW = "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0";
constexpr const char *NS_SVG = "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0";
constexpr const char *NS_XLINK = "http://www.w3.org/1999/xlink";
constexpr const char *NS_MANIFEST = "urn:oasis:names:tc:opendocument:xmlns:manifest:1.0";

bool isElement(const QDomNode &node, const char *ns, const char *name)
{
    const QDomElement e = node.toElement();
    return !e.isNull() && e.namespaceURI() == QLatin1String(ns) && e.localName() == QLatin1String(name);
}

QByteArray readStoreFile(KoStore *store, const QString &path)
{
    QString cleaned = path;
    while (cleaned.startsWith(QLatin1String("./"))) {
        cleaned.remove(0, 2);
    }
    if (!store->open(cleaned)) {
        qWarning() << "ODT->DOC: cannot open" << cleaned;
        return {};
    }
    const QByteArray data = store->read(store->size());
    store->close();
    return data;
}

qint64 lengthToTwips(const QString &value)
{
    const QString s = value.trimmed();
    if (s.isEmpty()) {
        return 0;
    }

    bool ok = false;
    const double number = s.left(s.size() - 2).toDouble(&ok);
    if (ok) {
        const QString unit = s.right(2);
        if (unit == QLatin1String("cm")) {
            return qint64(std::lround(number * 1440.0 / 2.54));
        }
        if (unit == QLatin1String("mm")) {
            return qint64(std::lround(number * 1440.0 / 25.4));
        }
        if (unit == QLatin1String("in")) {
            return qint64(std::lround(number * 1440.0));
        }
        if (unit == QLatin1String("pt")) {
            return qint64(std::lround(number * 20.0));
        }
        if (unit == QLatin1String("pc")) {
            return qint64(std::lround(number * 240.0));
        }
        if (unit == QLatin1String("px")) {
            return qint64(std::lround(number * 15.0));
        }
    }

    // Bare values in ODF lengths are in points.
    bool bareOk = false;
    const double bare = s.toDouble(&bareOk);
    return bareOk ? qint64(std::lround(bare * 20.0)) : 0;
}

void collectText(const QDomElement &element, QString &text)
{
    for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (node.isText() || node.nodeType() == QDomNode::CDATASectionNode) {
            text += node.nodeValue();
            continue;
        }
        if (!node.isElement()) {
            continue;
        }
        // The title of a frame is metadata, not visible paragraph text.
        if (isElement(node, NS_DRAW, "frame")) {
            continue;
        }
        if (isElement(node, NS_TEXT, "s")) {
            const QString count = node.toElement().attributeNS(QLatin1String(NS_TEXT), QLatin1String("c"));
            int n = count.isEmpty() ? 1 : count.toInt();
            if (n < 1) {
                n = 1;
            }
            text += QString(n, QLatin1Char(' '));
            continue;
        }
        if (isElement(node, NS_TEXT, "tab")) {
            text += QLatin1Char('\t');
            continue;
        }
        if (isElement(node, NS_TEXT, "line-break")) {
            text += QLatin1Char('\n');
            continue;
        }
        collectText(node.toElement(), text);
    }
}

void findFrames(const QDomElement &element, QList<QDomElement> &frames)
{
    for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (!node.isElement()) {
            continue;
        }
        if (isElement(node, NS_DRAW, "frame")) {
            frames.append(node.toElement());
        }
        findFrames(node.toElement(), frames);
    }
}

QString mimeTypeForPath(const QString &path, const QHash<QString, QString> &manifest)
{
    const QString key = QLatin1Char('/') + QDir::cleanPath(path);
    if (manifest.contains(key)) {
        return manifest.value(key);
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")) {
        return QStringLiteral("image/jpeg");
    }
    if (suffix == QLatin1String("png")) {
        return QStringLiteral("image/png");
    }
    if (suffix == QLatin1String("gif")) {
        return QStringLiteral("image/gif");
    }
    return {};
}

void collectImages(KoStore *store,
                   const QDomElement &paragraph,
                   const QHash<QString, QString> &manifest,
                   Word97::Document &document)
{
    QList<QDomElement> frames;
    findFrames(paragraph, frames);
    for (const QDomElement &frame : std::as_const(frames)) {
        QDomNode imageNode = frame.firstChild();
        while (!imageNode.isNull() && !isElement(imageNode, NS_DRAW, "image")) {
            imageNode = imageNode.nextSibling();
        }
        if (imageNode.isNull()) {
            continue;
        }

        const QString href = imageNode.toElement().attributeNS(QLatin1String(NS_XLINK), QLatin1String("href"));
        if (href.isEmpty()) {
            continue;
        }
        const QByteArray data = readStoreFile(store, href);
        if (data.isEmpty()) {
            continue;
        }

        Word97::Image image;
        image.data = data;
        image.mimeType = mimeTypeForPath(href, manifest);
        image.width = lengthToTwips(frame.attributeNS(QLatin1String(NS_SVG), QLatin1String("width")));
        image.height = lengthToTwips(frame.attributeNS(QLatin1String(NS_SVG), QLatin1String("height")));
        image.posX = lengthToTwips(frame.attributeNS(QLatin1String(NS_SVG), QLatin1String("x")));
        image.posY = lengthToTwips(frame.attributeNS(QLatin1String(NS_SVG), QLatin1String("y")));
        if (image.width <= 0 || image.height <= 0) {
            continue;
        }
        document.images.append(image);
    }
}

void walkContent(KoStore *store,
                 const QDomElement &element,
                 const QHash<QString, QString> &manifest,
                 Word97::Document &document)
{
    for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (!node.isElement()) {
            continue;
        }
        const QDomElement child = node.toElement();
        if (isElement(child, NS_TEXT, "p") || isElement(child, NS_TEXT, "h")) {
            Word97::Paragraph paragraph;
            collectText(child, paragraph.text);
            collectImages(store, child, manifest, document);
            document.paragraphs.append(paragraph);
            continue;
        }
        walkContent(store, child, manifest, document);
    }
}

QHash<QString, QString> readManifest(KoStore *store)
{
    QHash<QString, QString> manifest;
    const QByteArray data = readStoreFile(store, QStringLiteral("META-INF/manifest.xml"));
    QDomDocument document;
    if (data.isEmpty() || !document.setContent(data, true)) {
        return manifest;
    }
    const QDomNodeList entries = document.elementsByTagNameNS(QLatin1String(NS_MANIFEST), QLatin1String("file-entry"));
    for (int i = 0; i < entries.size(); ++i) {
        const QDomElement e = entries.at(i).toElement();
        const QString path = e.attributeNS(QLatin1String(NS_MANIFEST), QLatin1String("full-path"));
        const QString type = e.attributeNS(QLatin1String(NS_MANIFEST), QLatin1String("media-type"));
        if (!path.isEmpty() && !type.isEmpty()) {
            manifest.insert(path, type);
        }
    }
    return manifest;
}

} // namespace

K_PLUGIN_FACTORY_WITH_JSON(DocExportFactory, "calligra_filter_odt2doc.json", registerPlugin<DocExport>();)

DocExport::DocExport(QObject *parent, const QVariantList &)
    : KoFilter(parent)
{
}

DocExport::~DocExport() = default;

KoFilter::ConversionStatus DocExport::convert(const QByteArray &from, const QByteArray &to)
{
    if (from != "application/vnd.oasis.opendocument.text" || to != "application/msword") {
        return KoFilter::NotImplemented;
    }

    KoStore *store = KoStore::createStore(m_chain->inputFile(), KoStore::Read, QByteArray(), KoStore::Auto);
    if (!store || !store->open(QStringLiteral("mimetype"))) {
        qWarning() << "ODT->DOC: unable to open input file";
        delete store;
        return KoFilter::FileNotFound;
    }
    store->close();

    const QByteArray contentData = readStoreFile(store, QStringLiteral("content.xml"));
    if (contentData.isEmpty()) {
        delete store;
        return KoFilter::ParsingError;
    }

    QDomDocument content;
    if (!content.setContent(contentData, true)) {
        qWarning() << "ODT->DOC: invalid content.xml";
        delete store;
        return KoFilter::ParsingError;
    }

    const QHash<QString, QString> manifest = readManifest(store);
    Word97::Document document;
    const QDomElement root = content.documentElement();
    const QDomNodeList bodies = root.elementsByTagNameNS(QLatin1String(NS_OFFICE), QLatin1String("text"));
    if (bodies.size() > 0) {
        walkContent(store, bodies.at(0).toElement(), manifest, document);
    }
    delete store;

    QFile output(m_chain->outputFile());
    // CFBWriter opens the device itself.
    if (!Word97::writeDocument(&output, document)) {
        qWarning() << "ODT->DOC: failed to write binary Word document";
        return KoFilter::CreationError;
    }
    output.close();
    return KoFilter::OK;
}

#include "DocExport.moc"
