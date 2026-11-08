/* This file is part of the KDE project
   SPDX-FileCopyrightText: 1998, 1999 Reginald Stadlbauer <reggie@kde.org>
   SPDX-FileCopyrightText: 2000 Michael Johnson <mikej@xnet.com>
   SPDX-FileCopyrightText: 2001, 2002, 2004 Nicolas GOUTTE <goutte@kde.org>
   SPDX-FileCopyrightText: 2010-2011 Thorsten Zachmann <zachmann@kde.org>
   SPDX-FileCopyrightText: 2010 Christoph Cullmann <cullmann@kde.org>
   SPDX-FileCopyrightText: 2013-2014 Inge Wallin <inge@lysator.liu.se>

   SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "DocxExport.h"

#include <QFile>
#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QRegularExpression>
#include <QStringList>

// KF5
#include <KPluginFactory>

// Calligra
#include <KoFilterChain.h>
#include <KoStore.h>
#include <KoXmlWriter.h>

// Filter libraries
#include "OdtReader.h"
#include "OdfTextReader.h"

// This filter
#include "DocxExportDebug.h"
#include "DocxFile.h"
#include "DocxStyleWriter.h"
#include "OdfReaderDocxContext.h"
#include "OdfDrawReader.h"
#include "DocxDrawReaderBackend.h"
#include "OdfTextReaderDocxBackend.h"
#include "OdtReaderDocxBackend.h"

// Calligra
#include <KoUnit.h>
#include <KoXmlNS.h>
#include <KoXmlStreamReader.h>
#include <KoOdfStyle.h>
#include <KoOdfStyleManager.h>
#include <KoOdfStyleProperties.h>

K_PLUGIN_FACTORY_WITH_JSON(DocxExportFactory, "calligra_filter_odt2docx.json", registerPlugin<DocxExport>();)

// Needed to instantiate the plugin factory.
#include "DocxExport.moc"

namespace
{
// Page geometry collected from a <style:page-layout>, in points.
struct PageGeometry {    bool valid = false;
    qreal pageWidth = 0.0;
    qreal pageHeight = 0.0;
    qreal marginTop = 0.0;
    qreal marginRight = 0.0;
    qreal marginBottom = 0.0;
    qreal marginLeft = 0.0;
    qreal headerMinHeight = 0.0;
    qreal footerMinHeight = 0.0;
    bool landscape = false;
    QString numberFormat;
    QString sectionType = QStringLiteral("nextPage");
    int columns = 1;
    qreal columnGap = 0;
    QString borderTop;
    QString borderRight;
    QString borderBottom;
    QString borderLeft;
    QString backgroundImageHref;
};

int toTwips(qreal points)
{
    return qRound(points * 20.0);
}

QString wordBorderStyle(const QString &odfStyle)
{
    if (odfStyle == QLatin1String("none")) return QStringLiteral("nil");
    if (odfStyle == QLatin1String("dashed")) return QStringLiteral("dashed");
    if (odfStyle == QLatin1String("dotted")) return QStringLiteral("dotted");
    if (odfStyle == QLatin1String("double")) return QStringLiteral("double");
    return QStringLiteral("single");
}

struct ParsedBorder {
    bool visible = false;
    QString style = QStringLiteral("single");
    int size = 8;
    QString color = QStringLiteral("000000");
};

ParsedBorder parseOdfBorder(const QString &value)
{
    ParsedBorder border;
    const QStringList tokens = value.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) return border;

    qreal width = KoUnit::parseValue(tokens.first());
    if (width <= 0.0) {
        if (tokens.first() == QLatin1String("thin")) width = 0.75;
        else if (tokens.first() == QLatin1String("medium")) width = 1.5;
        else if (tokens.first() == QLatin1String("thick")) width = 2.25;
    }

    QString style = QStringLiteral("solid");
    for (int i = 1; i < tokens.size(); ++i) {
        const QString token = tokens.at(i);
        if (token == QLatin1String("none") || token == QLatin1String("solid")
            || token == QLatin1String("dashed") || token == QLatin1String("dotted")
            || token == QLatin1String("double")) {
            style = token;
        } else if (token.startsWith(QLatin1Char('#')) || QColor(token).isValid()) {
            const QColor parsed(token);
            if (parsed.isValid()) border.color = parsed.name(QColor::HexRgb).mid(1);
        }
    }

    border.visible = style != QLatin1String("none") && width > 0.0;
    border.style = wordBorderStyle(style);
    border.size = qBound(2, qRound(width * 8.0), 96);
    return border;
}

void writePageBorders(KoXmlWriter &writer, const PageGeometry &geometry)
{
    const ParsedBorder top = parseOdfBorder(geometry.borderTop);
    const ParsedBorder right = parseOdfBorder(geometry.borderRight);
    const ParsedBorder bottom = parseOdfBorder(geometry.borderBottom);
    const ParsedBorder left = parseOdfBorder(geometry.borderLeft);
    if (!top.visible && !right.visible && !bottom.visible && !left.visible) return;

    writer.startElement("w:pgBorders");
    writer.addAttribute("w:offsetFrom", "text");
    const auto writeSide = [&writer](const char *name, const ParsedBorder &border) {
        writer.startElement(name);
        writer.addAttribute("w:val", border.visible ? border.style : QStringLiteral("nil"));
        if (border.visible) {
            writer.addAttribute("w:sz", border.size);
            writer.addAttribute("w:space", 0);
            writer.addAttribute("w:color", border.color);
        }
        writer.endElement();
    };
    writeSide("w:top", top);
    writeSide("w:left", left);
    writeSide("w:bottom", bottom);
    writeSide("w:right", right);
    writer.endElement(); // w:pgBorders
}

QByteArray watermarkParagraph(DocxFile &docxFile, KoStore *store, const PageGeometry &geometry,
                              const QString &headerFile, int partNumber)
{
    if (geometry.backgroundImageHref.isEmpty()) return {};

    if (!store->open(geometry.backgroundImageHref)) return {};
    const QByteArray source = store->read(store->size());
    store->close();
    QImage image;
    if (!image.loadFromData(source)) return {};

    QByteArray png;
    QBuffer pngBuffer(&png);
    if (!pngBuffer.open(QIODevice::WriteOnly) || !image.save(&pngBuffer, "PNG")) return {};

    QString imageStem = headerFile;
    imageStem.remove(QLatin1String("header"));
    imageStem.remove(QLatin1String(".xml"));
    imageStem.remove(QLatin1Char('-'));
    const QString imageName = QStringLiteral("watermark%1-%2.png").arg(partNumber).arg(imageStem);
    const QString imageTarget = QStringLiteral("media/") + imageName;
    docxFile.addContentFile(QString(), QStringLiteral("/word/") + imageTarget, "image/png", png);
    const QString relationshipId = docxFile.addRelationship(
        QStringLiteral("word/") + headerFile,
        QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/image"),
        imageTarget);
    if (relationshipId.isEmpty()) return {};

    const qint64 cx = qRound64(geometry.pageWidth * 12700.0);
    const qint64 cy = qRound64(geometry.pageHeight * 12700.0);
    const int variantOffset = headerFile.contains(QLatin1String("first")) ? 2
        : headerFile.contains(QLatin1String("even")) ? 1 : 0;
    const int drawingId = 9000 + partNumber * 10 + variantOffset;

    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&buffer);
    writer.startElement("w:p");
    writer.startElement("w:r");
    writer.startElement("w:drawing");
    writer.addAttribute("xmlns:wp", "http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing");
    writer.addAttribute("xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main");
    writer.addAttribute("xmlns:pic", "http://schemas.openxmlformats.org/drawingml/2006/picture");
    writer.startElement("wp:anchor");
    writer.addAttribute("distT", 0);
    writer.addAttribute("distB", 0);
    writer.addAttribute("distL", 0);
    writer.addAttribute("distR", 0);
    writer.addAttribute("simplePos", 0);
    writer.addAttribute("relativeHeight", 0);
    writer.addAttribute("behindDoc", 1);
    writer.addAttribute("locked", 0);
    writer.addAttribute("layoutInCell", 1);
    writer.addAttribute("allowOverlap", 1);
    writer.startElement("wp:simplePos");
    writer.addAttribute("x", 0);
    writer.addAttribute("y", 0);
    writer.endElement();
    writer.startElement("wp:positionH");
    writer.addAttribute("relativeFrom", "page");
    writer.startElement("wp:posOffset");
    writer.addTextNode(QStringLiteral("0"));
    writer.endElement();
    writer.endElement();
    writer.startElement("wp:positionV");
    writer.addAttribute("relativeFrom", "page");
    writer.startElement("wp:posOffset");
    writer.addTextNode(QStringLiteral("0"));
    writer.endElement();
    writer.endElement();
    writer.startElement("wp:extent");
    writer.addAttribute("cx", QString::number(cx));
    writer.addAttribute("cy", QString::number(cy));
    writer.endElement();
    writer.startElement("wp:wrapNone");
    writer.endElement();
    writer.startElement("wp:docPr");
    writer.addAttribute("id", drawingId);
    writer.addAttribute("name", "Watermark");
    writer.endElement();
    writer.startElement("wp:cNvGraphicFramePr");
    writer.startElement("a:graphicFrameLocks");
    writer.addAttribute("noChangeAspect", 1);
    writer.endElement();
    writer.endElement();
    writer.startElement("a:graphic");
    writer.startElement("a:graphicData");
    writer.addAttribute("uri", "http://schemas.openxmlformats.org/drawingml/2006/picture");
    writer.startElement("pic:pic");
    writer.startElement("pic:nvPicPr");
    writer.startElement("pic:cNvPr");
    writer.addAttribute("id", 0);
    writer.addAttribute("name", "Watermark");
    writer.endElement();
    writer.startElement("pic:cNvPicPr");
    writer.endElement();
    writer.endElement();
    writer.startElement("pic:blipFill");
    writer.startElement("a:blip");
    writer.addAttribute("r:embed", relationshipId);
    writer.endElement();
    writer.startElement("a:stretch");
    writer.startElement("a:fillRect");
    writer.endElement();
    writer.endElement();
    writer.endElement();
    writer.startElement("pic:spPr");
    writer.startElement("a:xfrm");
    writer.startElement("a:off");
    writer.addAttribute("x", 0);
    writer.addAttribute("y", 0);
    writer.endElement();
    writer.startElement("a:ext");
    writer.addAttribute("cx", QString::number(cx));
    writer.addAttribute("cy", QString::number(cy));
    writer.endElement();
    writer.endElement();
    writer.startElement("a:prstGeom");
    writer.addAttribute("prst", "rect");
    writer.startElement("a:avLst");
    writer.endElement();
    writer.endElement();
    writer.endElement();
    writer.endElement();
    writer.endElement();
    writer.endElement();
    writer.endElement();
    writer.endElement();
    writer.endElement();
    writer.endElement();
    return result;
}

struct OdtMetadata {
    QString title;
    QString subject;
    QString description;
    QString keywords;
    QString initialCreator;
    QString creator;
    QString created;
    QString modified;
    QString revision;
    QString generator;
};

OdtMetadata readOdtMetadata(KoStore *store)
{
    OdtMetadata result;
    if (!store->open(QStringLiteral("meta.xml"))) {
        return result;
    }

    KoXmlStreamReader reader;
    prepareForOdf(reader);
    reader.setDevice(store->device());

    if (!reader.readNextStartElement() || reader.qualifiedName() != QLatin1StringView("office:document-meta")) {
        store->close();
        return result;
    }

    while (reader.readNextStartElement()) {
        if (reader.namespaceUri() == KoXmlNS::office && reader.name() == QLatin1StringView("meta")) {
            break;
        }
        reader.skipCurrentElement();
    }

    QStringList keywords;
    while (reader.readNextStartElement()) {
        const QString localName = reader.name().toString();
        const QString ns = reader.namespaceUri().toString();
        if (ns == KoXmlNS::dc && localName == QLatin1String("title")) {
            result.title = reader.readElementText().trimmed();
        } else if (ns == KoXmlNS::dc && localName == QLatin1String("subject")) {
            result.subject = reader.readElementText().trimmed();
        } else if (ns == KoXmlNS::dc && localName == QLatin1String("description")) {
            result.description = reader.readElementText().trimmed();
        } else if (ns == KoXmlNS::dc && localName == QLatin1String("creator")) {
            result.creator = reader.readElementText().trimmed();
        } else if (ns == KoXmlNS::dc && localName == QLatin1String("date")) {
            result.modified = reader.readElementText().trimmed();
        } else if (ns == KoXmlNS::meta && localName == QLatin1String("initial-creator")) {
            result.initialCreator = reader.readElementText().trimmed();
        } else if (ns == KoXmlNS::meta && localName == QLatin1String("creation-date")) {
            result.created = reader.readElementText().trimmed();
        } else if (ns == KoXmlNS::meta && localName == QLatin1String("editing-cycles")) {
            result.revision = reader.readElementText().trimmed();
        } else if (ns == KoXmlNS::meta && localName == QLatin1String("keyword")) {
            const QString keyword = reader.readElementText().trimmed();
            if (!keyword.isEmpty()) {
                keywords.append(keyword);
            }
        } else if (ns == KoXmlNS::meta && localName == QLatin1String("generator")) {
            result.generator = reader.readElementText().trimmed();
        } else {
            reader.skipCurrentElement();
        }
    }
    result.keywords = keywords.join(QStringLiteral(", "));
    store->close();
    return result;
}

QByteArray corePropertiesPart(const OdtMetadata &metadata)
{
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&buffer);
    writer.startDocument(nullptr);
    writer.startElement("cp:coreProperties");
    writer.addAttribute("xmlns:cp", "http://schemas.openxmlformats.org/package/2006/metadata/core-properties");
    writer.addAttribute("xmlns:dc", "http://purl.org/dc/elements/1.1/");
    writer.addAttribute("xmlns:dcterms", "http://purl.org/dc/terms/");
    writer.addAttribute("xmlns:dcmitype", "http://purl.org/dc/dcmitype/");
    writer.addAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");

    const auto addText = [&writer](const char *element, const QString &value) {
        if (!value.isEmpty()) {
            writer.startElement(element);
            writer.addTextNode(value);
            writer.endElement();
        }
    };
    addText("dc:title", metadata.title);
    addText("dc:subject", metadata.subject);
    addText("dc:description", metadata.description);
    addText("cp:keywords", metadata.keywords);
    addText("dc:creator", metadata.creator.isEmpty() ? metadata.initialCreator : metadata.creator);
    addText("cp:lastModifiedBy", metadata.creator);
    addText("cp:revision", metadata.revision);

    const auto addDate = [&writer](const char *element, const QString &value) {
        if (!value.isEmpty()) {
            writer.startElement(element);
            writer.addAttribute("xsi:type", "dcterms:W3CDTF");
            writer.addTextNode(value);
            writer.endElement();
        }
    };
    addDate("dcterms:created", metadata.created);
    addDate("dcterms:modified", metadata.modified);

    writer.endElement(); // cp:coreProperties
    writer.endDocument();
    return result;
}

QByteArray appPropertiesPart()
{
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&buffer);
    writer.startDocument(nullptr);
    writer.startElement("Properties");
    writer.addAttribute("xmlns", "http://schemas.openxmlformats.org/officeDocument/2006/extended-properties");
    writer.addAttribute("xmlns:vt", "http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes");
    writer.startElement("Application");
    writer.addTextNode(QStringLiteral("Calligra Words"));
    writer.endElement();
    writer.endElement(); // Properties
    writer.endDocument();
    return result;
}

QByteArray emptyCustomPropertiesPart()
{
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&buffer);
    writer.startDocument(nullptr);
    writer.startElement("Properties");
    writer.addAttribute("xmlns", "http://schemas.openxmlformats.org/officeDocument/2006/custom-properties");
    writer.addAttribute("xmlns:vt", "http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes");
    writer.endElement();
    writer.endDocument();
    return result;
}

QByteArray settingsPart(bool evenAndOddHeaders)
{
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&buffer);
    writer.startDocument(nullptr);
    writer.startElement("w:settings");
    writer.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    if (evenAndOddHeaders) {
        writer.startElement("w:evenAndOddHeaders");
        writer.endElement();
    }
    writer.startElement("w:compat");
    writer.startElement("w:compatSetting");
    writer.addAttribute("w:val", 15);
    writer.addAttribute("w:name", "compatibilityMode");
    writer.addAttribute("w:uri", "http://schemas.microsoft.com/office/word");
    writer.endElement();
    writer.endElement(); // w:compat
    writer.endElement(); // w:settings
    writer.endDocument();
    return result;
}

QByteArray emptyFontTablePart()
{
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&buffer);
    writer.startDocument(nullptr);
    writer.startElement("w:fonts");
    writer.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    writer.endElement();
    writer.endDocument();
    return result;
}

QByteArray emptyCommentsPart()
{
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&buffer);
    writer.startDocument(nullptr);
    writer.startElement("w:comments");
    writer.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    writer.endElement();
    writer.endDocument();
    return result;
}

QByteArray emptyNotesPart(const char *rootElement, const char *noteElement, const char *referenceName)
{
    QByteArray result;
    QBuffer buffer(&result);
    buffer.open(QIODevice::WriteOnly);
    KoXmlWriter writer(&buffer);
    writer.startDocument(nullptr);
    writer.startElement(rootElement);
    writer.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    const auto writeSeparator = [&writer, noteElement, referenceName](const char *type, int id, const char *separator) {
        writer.startElement(noteElement);
        writer.addAttribute("w:type", type);
        writer.addAttribute("w:id", id);
        writer.startElement("w:p");
        writer.startElement("w:r");
        writer.startElement("w:rPr");
        writer.startElement("w:rStyle");
        writer.addAttribute("w:val", referenceName);
        writer.endElement();
        writer.endElement(); // w:rPr
        writer.startElement(separator);
        writer.endElement();
        writer.endElement(); // w:r
        writer.endElement(); // w:p
        writer.endElement(); // note
    };
    writeSeparator("separator", -1, "separator");
    writeSeparator("continuationSeparator", 0, "continuationSeparator");
    writer.endElement(); // root
    writer.endDocument();
    return result;
}

void writePackageMetadata(DocxFile &docxFile, KoStore *store, bool evenAndOddHeaders,
                          bool hasComments, bool hasFootnotes, bool hasEndnotes)
{
    const OdtMetadata metadata = readOdtMetadata(store);

    docxFile.addRelationship(QString(),
                             QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"),
                             QStringLiteral("docProps/core.xml"));
    docxFile.addContentFile(QString(), QStringLiteral("/docProps/core.xml"),
                            QByteArrayLiteral("application/vnd.openxmlformats-package.core-properties+xml"),
                            corePropertiesPart(metadata));

    docxFile.addRelationship(QString(),
                             QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties"),
                             QStringLiteral("docProps/app.xml"));
    docxFile.addContentFile(QString(), QStringLiteral("/docProps/app.xml"),
                            QByteArrayLiteral("application/vnd.openxmlformats-officedocument.extended-properties+xml"),
                            appPropertiesPart());

    docxFile.addRelationship(QString(),
                             QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/custom-properties"),
                             QStringLiteral("docProps/custom.xml"));
    docxFile.addContentFile(QString(), QStringLiteral("/docProps/custom.xml"),
                            QByteArrayLiteral("application/vnd.openxmlformats-officedocument.custom-properties+xml"),
                            emptyCustomPropertiesPart());

    docxFile.addRelationship(QStringLiteral("word/document.xml"),
                             QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings"),
                             QStringLiteral("settings.xml"));
    docxFile.addContentFile(QString(), QStringLiteral("/word/settings.xml"),
                            QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.settings+xml"),
                            settingsPart(evenAndOddHeaders));

    docxFile.addRelationship(QStringLiteral("word/document.xml"),
                             QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/fontTable"),
                             QStringLiteral("fontTable.xml"));
    docxFile.addContentFile(QString(), QStringLiteral("/word/fontTable.xml"),
                            QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.fontTable+xml"),
                            emptyFontTablePart());

    if (!hasComments) {
        docxFile.addRelationship(QStringLiteral("word/document.xml"),
                                 QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments"),
                                 QStringLiteral("comments.xml"));
        docxFile.addContentFile(QString(), QStringLiteral("/word/comments.xml"),
                                QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml"),
                                emptyCommentsPart());
    }
    if (!hasFootnotes) {
        docxFile.addRelationship(QStringLiteral("word/document.xml"),
                                 QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes"),
                                 QStringLiteral("footnotes.xml"));
        docxFile.addContentFile(QString(), QStringLiteral("/word/footnotes.xml"),
                                QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml"),
                                emptyNotesPart("w:footnotes", "w:footnote", "FootnoteReference"));
    }
    if (!hasEndnotes) {
        docxFile.addRelationship(QStringLiteral("word/document.xml"),
                                 QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/endnotes"),
                                 QStringLiteral("endnotes.xml"));
        docxFile.addContentFile(QString(), QStringLiteral("/word/endnotes.xml"),
                                QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.endnotes+xml"),
                                emptyNotesPart("w:endnotes", "w:endnote", "EndnoteReference"));
    }
}

void resolveFontFamilies(OdfReaderDocxContext &context)
{
    QHash<QString, QString> families;
    KoStore *store = context.odfStore();
    for (const QString &part : {QStringLiteral("styles.xml"), QStringLiteral("content.xml")}) {
        if (!store->open(part)) continue;
        KoXmlStreamReader reader;
        prepareForOdf(reader);
        reader.setDevice(store->device());
        while (!reader.atEnd()) {
            reader.readNext();
            if (!reader.isStartElement() || reader.qualifiedName() != QLatin1StringView("style:font-face")) continue;
            const QString name = reader.attributes().value(QStringLiteral("style:name")).toString();
            QString family = reader.attributes().value(QStringLiteral("svg:font-family")).toString().section(QLatin1Char(','), 0, 0).trimmed();
            if (family.size() >= 2 && ((family.front() == QLatin1Char('\'') && family.back() == QLatin1Char('\''))
                || (family.front() == QLatin1Char('"') && family.back() == QLatin1Char('"'))))
                family = family.mid(1, family.size() - 2);
            if (!name.isEmpty() && !family.isEmpty()) families.insert(name, family);
        }
        store->close();
    }
    auto styles = context.styleManager()->styles();
    styles.append(context.styleManager()->defaultStyles());
    for (KoOdfStyle *style : styles) {
        auto *properties = style->properties("style:text-properties");
        if (!properties) continue;
        for (const QString &name : {QStringLiteral("style:font-name"), QStringLiteral("style:font-name-asian"), QStringLiteral("style:font-name-complex")}) {
            const QString alias = properties->attribute(name);
            if (families.contains(alias)) properties->setAttribute(name, families.value(alias));
        }
        if (properties->attribute("style:font-name").isEmpty() && !properties->attribute("fo:font-family").isEmpty())
            properties->setAttribute("style:font-name", properties->attribute("fo:font-family"));
    }
}

// Wraps a sequence of Word body-level elements into a complete header or
// footer part (word/header1.xml, word/footer1.xml).
QByteArray wrapHeaderFooterPart(const QByteArray &content, const char *rootElement)
{
    QByteArray result;
    QBuffer buffer(&result);
    KoXmlWriter writer(&buffer);
    writer.startDocument(nullptr);
    writer.startElement(rootElement);
    writer.addAttribute("xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    writer.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    writer.addCompleteElement(content.constData());
    writer.endElement(); // root element
    writer.endDocument();
    return result;
}

// Collect each master page's geometry and header/footer parts before writing
// the body. Paragraph master-page changes then select the matching section.
void exportMasterPageSections(OdfReaderDocxContext &context, OdfTextReader &textReader, DocxFile &docxFile,
                          const QString &selectedMaster = QString(), int partNumber = 1)
{
    if (!context.odfStore()) return;
    if (selectedMaster.isNull()) {
        QStringList masters;
        if (context.odfStore()->open(QStringLiteral("styles.xml"))) {
            KoXmlStreamReader scan;
            prepareForOdf(scan);
            scan.setDevice(context.odfStore()->device());
            while (!scan.atEnd()) {
                scan.readNext();
                if (scan.isStartElement() && scan.qualifiedName() == QLatin1StringView("style:master-page")) {
                    masters.append(scan.attributes().value(QStringLiteral("style:name")).toString());
                }
            }
            context.odfStore()->close();
        }
        for (int i = 0; i < masters.size(); ++i) {
            exportMasterPageSections(context, textReader, docxFile, masters[i], i + 1);
        }
        if (!masters.isEmpty()) {
            context.activeMasterPage = masters.first();
            context.setSectPrContent(context.masterPageSections.value(masters.first()));
        }
        return;
    }
    KoStore *store = context.odfStore();
    if (!store) {
        return;
    }

    struct HeaderFooterPart {
        bool present = false;
        QByteArray content;
        QString file;
        QString relationshipId;
    };

    const auto partFile = [partNumber](bool header, const QString &type) {
        QString suffix;
        if (type == QLatin1String("even")) {
            suffix = QStringLiteral("-even");
        } else if (type == QLatin1String("first")) {
            suffix = QStringLiteral("-first");
        }
        return QStringLiteral("%1%2%3.xml")
            .arg(header ? QStringLiteral("header") : QStringLiteral("footer"))
            .arg(partNumber)
            .arg(suffix);
    };

    QHash<QString, HeaderFooterPart> headers;
    QHash<QString, HeaderFooterPart> footers;
    QHash<QString, PageGeometry> pageLayouts;
    QString pageLayoutName;
    const QString masterPageName = selectedMaster;

    if (!store->open(QStringLiteral("styles.xml"))) {
        return;
    }
    KoXmlStreamReader reader;
    prepareForOdf(reader);
    reader.setDevice(store->device());

    bool masterPageDone = false;

    // Find the root element.  Calling qualifiedName() also triggers the
    // namespace soundness check, so attribute lookups by qualified name
    // work from here on.
    bool foundRoot = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.qualifiedName() == QLatin1StringView("office:document-styles")) {
            foundRoot = true;
            break;
        }
    }

    if (foundRoot) {
        while (reader.readNextStartElement()) {
            const QString tagName = reader.qualifiedName().toString();

            if (tagName == QLatin1StringView("office:automatic-styles")) {
                // Collect page layouts so geometry can be looked up after
                // the selected master page tells us which one is in use.
                while (reader.readNextStartElement()) {
                    if (reader.qualifiedName() != QLatin1StringView("style:page-layout")) {
                        reader.skipCurrentElement();
                        continue;
                    }
                    const QString layoutName = reader.attributes().value(QStringLiteral("style:name")).toString();
                    PageGeometry geometry;
                    const QString usage = reader.attributes().value(QStringLiteral("style:page-usage")).toString();
                    if (usage == QLatin1String("left")) geometry.sectionType = QStringLiteral("evenPage");
                    if (usage == QLatin1String("right")) geometry.sectionType = QStringLiteral("oddPage");
                    while (reader.readNextStartElement()) {
                        const QString childName = reader.qualifiedName().toString();
                        if (childName == QLatin1StringView("style:page-layout-properties")) {
                            const KoXmlStreamAttributes attrs = reader.attributes();
                            geometry.valid = true;
                            geometry.pageWidth = KoUnit::parseValue(attrs.value(QStringLiteral("fo:page-width")).toString());
                            geometry.pageHeight = KoUnit::parseValue(attrs.value(QStringLiteral("fo:page-height")).toString());
                            // Words uses fo:margin when all four page margins
                            // are equal. Individual sides override that value.
                            const qreal margin = KoUnit::parseValue(attrs.value(QStringLiteral("fo:margin")).toString());
                            const auto pageMargin = [&attrs, margin](const QString &name) {
                                const QString value = attrs.value(name).toString();
                                return value.isEmpty() ? margin : KoUnit::parseValue(value);
                            };
                            geometry.marginTop = pageMargin(QStringLiteral("fo:margin-top"));
                            geometry.marginRight = pageMargin(QStringLiteral("fo:margin-right"));
                            geometry.marginBottom = pageMargin(QStringLiteral("fo:margin-bottom"));
                            geometry.marginLeft = pageMargin(QStringLiteral("fo:margin-left"));
                            geometry.numberFormat = attrs.value(QStringLiteral("style:num-format")).toString();
                            geometry.landscape = attrs.value(QStringLiteral("style:print-orientation")) == QLatin1StringView("landscape");
                            const QString allBorder = attrs.value(QStringLiteral("fo:border")).toString();
                            geometry.borderTop = attrs.value(QStringLiteral("fo:border-top")).toString();
                            geometry.borderRight = attrs.value(QStringLiteral("fo:border-right")).toString();
                            geometry.borderBottom = attrs.value(QStringLiteral("fo:border-bottom")).toString();
                            geometry.borderLeft = attrs.value(QStringLiteral("fo:border-left")).toString();
                            if (geometry.borderTop.isEmpty()) geometry.borderTop = allBorder;
                            if (geometry.borderRight.isEmpty()) geometry.borderRight = allBorder;
                            if (geometry.borderBottom.isEmpty()) geometry.borderBottom = allBorder;
                            if (geometry.borderLeft.isEmpty()) geometry.borderLeft = allBorder;
                            while (reader.readNextStartElement()) {
                                if (reader.qualifiedName() == QLatin1StringView("style:columns")) {
                                    geometry.columns = qMax(1, reader.attributes().value(QStringLiteral("fo:column-count")).toInt());
                                    geometry.columnGap = KoUnit::parseValue(reader.attributes().value(QStringLiteral("fo:column-gap")).toString());
                                } else if (reader.qualifiedName() == QLatin1StringView("style:background-image")) {
                                    geometry.backgroundImageHref = reader.attributes().value(QStringLiteral("xlink:href")).toString();
                                }
                                reader.skipCurrentElement();
                            }
                            continue;
                        } else if (childName == QLatin1StringView("style:header-style") || childName == QLatin1StringView("style:footer-style")) {
                            const bool isHeaderStyle = childName == QLatin1StringView("style:header-style");
                            while (reader.readNextStartElement()) {
                                if (reader.qualifiedName() == QLatin1StringView("style:header-footer-properties")) {
                                    const qreal minHeight = KoUnit::parseValue(reader.attributes().value(QStringLiteral("fo:min-height")).toString());
                                    if (isHeaderStyle) {
                                        geometry.headerMinHeight = minHeight;
                                    } else {
                                        geometry.footerMinHeight = minHeight;
                                    }
                                }
                                reader.skipCurrentElement();
                            }
                            continue; // consumed completely above
                        }
                        reader.skipCurrentElement();
                    }
                    if (geometry.valid && !layoutName.isEmpty()) {
                        pageLayouts.insert(layoutName, geometry);
                    }
                }
            } else if (tagName == QLatin1StringView("office:master-styles")) {
                while (reader.readNextStartElement()) {
                    if (reader.qualifiedName() != QLatin1StringView("style:master-page")) {
                        reader.skipCurrentElement();
                        continue;
                    }
                    const QString thisMasterName = reader.attributes().value(QStringLiteral("style:name")).toString();
                    const QString thisLayoutName = reader.attributes().value(QStringLiteral("style:page-layout-name")).toString();
                    if (masterPageDone || (!masterPageName.isEmpty() && thisMasterName != masterPageName)) {
                        reader.skipCurrentElement();
                        continue;
                    }
                    masterPageDone = true;
                    pageLayoutName = thisLayoutName;

                    // Redirect the backends' output into the matching
                    // header/footer part while its content is traversed.
                    while (reader.readNextStartElement()) {
                        const QString childName = reader.qualifiedName().toString();
                        const bool isHeader = childName == QLatin1StringView("style:header")
                            || childName == QLatin1StringView("style:header-left")
                            || childName == QLatin1StringView("style:header-first");
                        const bool isFooter = childName == QLatin1StringView("style:footer")
                            || childName == QLatin1StringView("style:footer-left")
                            || childName == QLatin1StringView("style:footer-first");
                        if (!isHeader && !isFooter) {
                            reader.skipCurrentElement();
                            continue;
                        }

                        QString type = QStringLiteral("default");
                        if (childName.endsWith(QLatin1String("-left"))) {
                            type = QStringLiteral("even");
                        } else if (childName.endsWith(QLatin1String("-first"))) {
                            type = QStringLiteral("first");
                        }

                        HeaderFooterPart &part = (isHeader ? headers : footers)[type];
                        part.present = true;
                        part.file = partFile(isHeader, type);

                        QByteArray content;
                        QBuffer buffer(&content);
                        KoXmlWriter partWriter(&buffer);
                        KoXmlWriter *previousWriter = context.setDocumentWriter(&partWriter);
                        const QString previousPart = context.outputPart();
                        context.setOutputPart(QStringLiteral("word/") + part.file);

                        while (reader.readNextStartElement()) {
                            textReader.readTextLevelElement(reader);
                        }

                        context.setOutputPart(previousPart);
                        context.setDocumentWriter(previousWriter);
                        part.content = content;
                    }
                }
            } else {
                reader.skipCurrentElement();
            }
        }
    }

    store->close();

    const PageGeometry geometry = pageLayouts.value(pageLayoutName);

    // A page background image (watermark) has to be present in every header
    // variant, otherwise it disappears on first/even pages.  When there is no
    // header at all, create the default header for it.
    if (geometry.valid && !geometry.backgroundImageHref.isEmpty()) {
        if (headers.isEmpty()) {
            HeaderFooterPart &part = headers[QStringLiteral("default")];
            part.present = true;
            part.file = partFile(true, QStringLiteral("default"));
        }
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            const QByteArray watermark = watermarkParagraph(docxFile, store, geometry, it.value().file, partNumber);
            if (!watermark.isEmpty()) {
                it.value().content += watermark;
            }
        }
    }

    // Create the parts and the relationships from document.xml to them.
    const auto createHeaderFooterParts = [&docxFile](QHash<QString, HeaderFooterPart> &parts, bool header) {
        for (auto it = parts.begin(); it != parts.end(); ++it) {
            HeaderFooterPart &part = it.value();
            if (!part.present) {
                continue;
            }
            if (part.content.isEmpty()) {
                part.content = QByteArrayLiteral("<w:p/>");
            }
            part.relationshipId = docxFile.addRelationship(
                QStringLiteral("word/document.xml"),
                header ? QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/header")
                       : QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer"),
                part.file);
            docxFile.addContentFile(
                QString(),
                QStringLiteral("/word/") + part.file,
                header ? QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml")
                       : QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"),
                wrapHeaderFooterPart(part.content, header ? "w:hdr" : "w:ftr"));
        }
    };

    createHeaderFooterParts(headers, true);
    createHeaderFooterParts(footers, false);

    context.evenAndOddHeaders = headers.contains(QStringLiteral("even")) || footers.contains(QStringLiteral("even"));

    if (headers.isEmpty() && footers.isEmpty() && !geometry.valid) {
        return;
    }

    // Build the section properties for the end of the document body.
    QByteArray sectPr;
    QBuffer sectPrBuffer(&sectPr);
    KoXmlWriter writer(&sectPrBuffer);
    writer.startElement("w:sectPr");
    writer.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    writer.addAttribute("xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    const auto writeReferences = [&writer](const QHash<QString, HeaderFooterPart> &parts, const char *element) {
        for (const QString &type : {QStringLiteral("default"), QStringLiteral("even"), QStringLiteral("first")}) {
            const auto it = parts.constFind(type);
            if (it == parts.constEnd() || it.value().relationshipId.isEmpty()) {
                continue;
            }
            writer.startElement(element);
            writer.addAttribute("w:type", type);
            writer.addAttribute("r:id", it.value().relationshipId);
            writer.endElement();
        }
    };
    writeReferences(headers, "w:headerReference");
    writeReferences(footers, "w:footerReference");
    writer.startElement("w:type");
    writer.addAttribute("w:val", geometry.sectionType);
    writer.endElement(); // w:type
    if (geometry.valid) {
        writer.startElement("w:pgSz");
        writer.addAttribute("w:w", toTwips(geometry.pageWidth));
        writer.addAttribute("w:h", toTwips(geometry.pageHeight));
        if (geometry.landscape) {
            writer.addAttribute("w:orient", "landscape");
        }
        writer.endElement(); // w:pgSz

        // ODF margins locate the header/footer frames; Word's top/bottom
        // margins locate the body text. Include the space reserved by an
        // active header/footer, keeping its distance from the paper edge
        // separately in w:header/w:footer.
        writer.startElement("w:pgMar");
        writer.addAttribute("w:top", toTwips(geometry.marginTop + (headers.isEmpty() ? 0 : geometry.headerMinHeight)));
        writer.addAttribute("w:right", toTwips(geometry.marginRight));
        writer.addAttribute("w:bottom", toTwips(geometry.marginBottom + (footers.isEmpty() ? 0 : geometry.footerMinHeight)));
        writer.addAttribute("w:left", toTwips(geometry.marginLeft));
        writer.addAttribute("w:header", toTwips(geometry.marginTop));
        writer.addAttribute("w:footer", toTwips(geometry.marginBottom));
        writer.addAttribute("w:gutter", 0);
        writer.endElement(); // w:pgMar
        writePageBorders(writer, geometry);
    }
    if (!geometry.numberFormat.isEmpty()) {
        const QHash<QString, QString> formats = {{QStringLiteral("i"), QStringLiteral("lowerRoman")},
                                                {QStringLiteral("I"), QStringLiteral("upperRoman")},
                                                {QStringLiteral("a"), QStringLiteral("lowerLetter")},
                                                {QStringLiteral("A"), QStringLiteral("upperLetter")}};
        writer.startElement("w:pgNumType");
        writer.addAttribute("w:fmt", formats.value(geometry.numberFormat, QStringLiteral("decimal")));
        writer.endElement();
    }
    if (headers.contains(QStringLiteral("first")) || footers.contains(QStringLiteral("first"))) {
        writer.startElement("w:titlePg");
        writer.endElement();
    }
    if (geometry.columns > 1) {
        writer.startElement("w:cols");
        writer.addAttribute("w:num", geometry.columns);
        writer.addAttribute("w:space", toTwips(geometry.columnGap));
        writer.endElement();
    }
    writer.endElement(); // w:sectPr

    context.masterPageSections.insert(selectedMaster, sectPr);
    context.setSectPrContent(sectPr);
}

// ----------------------------------------------------------------
//         Style extras: tab stops and list numbering definitions

namespace
{
// Converts a point value to twips; returns false for negative input.
bool pointsToTwips(qreal points, qint64 *twips)
{
    if (points < 0) {
        return false;
    }
    *twips = qRound64(points * 20.0);
    return true;
}

struct ScannedTabStop {
    int position = 0; //!< twips
    QString type; //!< ODF style:type (left, right, center, char)
};

struct ScannedParagraphStyle {
    QString parent;
    QVector<ScannedTabStop> tabStops;
};

// Maps an ODF number format to the corresponding w:numFmt value.
QString wordNumFormat(const QString &odfFormat)
{
    if (odfFormat == QLatin1String("a")) {
        return QStringLiteral("lowerLetter");
    } else if (odfFormat == QLatin1String("A")) {
        return QStringLiteral("upperLetter");
    } else if (odfFormat == QLatin1String("i")) {
        return QStringLiteral("lowerRoman");
    } else if (odfFormat == QLatin1String("I")) {
        return QStringLiteral("upperRoman");
    } else if (odfFormat == QLatin1String("none")) {
        return QStringLiteral("none");
    } else if (odfFormat == QLatin1String("1")) {
        return QStringLiteral("decimal");
    }
    return QStringLiteral("decimal");
}

// Parses one <text:list-level-style-*> element into a NumberingLevel.
void readListLevelStyle(KoXmlStreamReader &reader, NumberingLevel *level)
{
    const QString qname = reader.qualifiedName().toString();
    const KoXmlStreamAttributes attributes = reader.attributes();
    const int odfLevel = attributes.value(QStringLiteral("text:level")).toInt();
    level->level = odfLevel > 0 ? odfLevel : 1;
    level->leftTwips = 720 * level->level;
    level->hangingTwips = 360;

    if (qname == QLatin1StringView("text:list-level-style-number")) {
        const QString format = attributes.value(QStringLiteral("style:num-format")).toString();
        const QString prefix = attributes.value(QStringLiteral("style:num-prefix")).toString();
        const QString suffix = attributes.value(QStringLiteral("style:num-suffix")).toString();
        const int start = attributes.value(QStringLiteral("text:start-value")).toInt();
        level->format = wordNumFormat(format);
        level->start = start > 0 ? start : 1;

        // LibreOffice records the exact label in loext:num-list-format using
        // %1% style placeholders.  It is authoritative and also covers
        // formats that cannot be reconstructed from text:display-levels
        // (for example "1.a." for a nested level).
        const QString loFormat = attributes.value(QStringLiteral("loext:num-list-format")).toString();
        if (!loFormat.isEmpty()) {
            // loext:num-list-format already contains the prefix and suffix
            // ("%1%."), so do not append style:num-prefix/suffix again.
            QString number = loFormat;
            number.replace(QRegularExpression(QStringLiteral("%(\\d+)%")), QStringLiteral("%\\1"));
            level->text = number;
        } else {
            // text:display-levels says how many levels of the number are shown
            // (e.g. "1.2.3" for level 3).  When it is absent only the current
            // level is shown, so a level-2 style must produce "%2", not "%1".
            int displayLevels = attributes.value(QStringLiteral("text:display-levels")).toInt();
            if (displayLevels < 1) {
                displayLevels = 1;
            }
            displayLevels = qMin(displayLevels, level->level);
            const int firstLevel = level->level - displayLevels + 1;
            QString number;
            for (int i = firstLevel; i <= level->level; ++i) {
                if (i > firstLevel) {
                    number += QLatin1Char('.');
                }
                number += QLatin1Char('%') + QString::number(i);
            }
            level->text = prefix + number + suffix;
        }
        if (level->format == QLatin1String("none")) {
            level->text.clear();
        }
    } else {
        // Bullet and image levels both become bullet definitions.
        QString bullet = attributes.value(QStringLiteral("text:bullet-char")).toString();
        if (!bullet.isEmpty()) {
            const ushort code = bullet.at(0).unicode();
            if ((code >= 0xE000 && code <= 0xF8FF) || code == 0xE00A) {
                // Bullet glyphs from the OpenSymbol font live in the Unicode
                // private use area and would show up as empty boxes in Word.
                bullet = QStringLiteral("\u2022");
            }
        }
        // A zero width space is the importer's marker for "the source had a
        // bullet, but its glyph could not be represented".  Exporting it
        // verbatim makes the bullet invisible, so use the normal bullet.
        if (bullet == QString(QChar(0x200B))) {
            bullet = QStringLiteral("\u2022");
        }
        if (bullet.isEmpty() || qname == QLatin1StringView("text:list-level-style-image")) {
            bullet = QStringLiteral("\u2022");
        }
        level->format = QStringLiteral("bullet");
        level->text = bullet;
    }

    while (reader.readNextStartElement()) {
        if (reader.qualifiedName() == QLatin1StringView("style:list-level-properties")) {
            const KoXmlStreamAttributes listProperties = reader.attributes();
            qint64 left = 0;
            qint64 hanging = 0;
            if (pointsToTwips(KoUnit::parseValue(listProperties.value(QStringLiteral("text:space-before")).toString(), -1.0), &left)) {
                level->leftTwips = static_cast<int>(left);
            }
            if (pointsToTwips(KoUnit::parseValue(listProperties.value(QStringLiteral("text:min-label-width")).toString(), -1.0), &hanging)) {
                level->hangingTwips = static_cast<int>(hanging);
            }
            while (reader.readNextStartElement()) {
                if (reader.qualifiedName() == QLatin1StringView("style:list-level-label-alignment")) {
                    qint64 indent = 0;
                    if (pointsToTwips(KoUnit::parseValue(reader.attributes().value(QStringLiteral("fo:text-indent")).toString(), -1.0), &indent)) {
                        level->leftTwips = static_cast<int>(qMax<qint64>(0, level->leftTwips + indent));
                    }
                }
                reader.skipCurrentElement();
            }
            continue;
        }
        reader.skipCurrentElement();
    }
}

// Reads the interesting style information from one package part (styles.xml or
// content.xml): paragraph tab stops and list style definitions.
void scanStylePart(KoXmlStreamReader &reader,
                   QHash<QString, ScannedParagraphStyle> *paragraphStyles,
                   DocxNumbering *numbering)
{
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement()) {
            continue;
        }
        reader.qualifiedName(); // validate prefixes for attribute lookup
        const QString rootName = reader.qualifiedName().toString();
        if (rootName == QLatin1StringView("office:document-styles") || rootName == QLatin1StringView("office:document-content")) {
            break;
        }
    }

    while (reader.readNextStartElement()) {
        const QString sectionName = reader.qualifiedName().toString();
        if (sectionName != QLatin1StringView("office:automatic-styles") && sectionName != QLatin1StringView("office:styles")) {
            reader.skipCurrentElement();
            continue;
        }
        while (reader.readNextStartElement()) {
            const QString qname = reader.qualifiedName().toString();
            if (qname == QLatin1StringView("style:style")) {
                const KoXmlStreamAttributes attributes = reader.attributes();
                const QString name = attributes.value(QStringLiteral("style:name")).toString();
                const QString family = attributes.value(QStringLiteral("style:family")).toString();
                const QString parent = attributes.value(QStringLiteral("style:parent-style-name")).toString();
                ScannedParagraphStyle scanned;
                scanned.parent = parent;
                // Tab stops can be a direct child of style:style (in some
                // documents) or nested in style:paragraph-properties.
                auto readTabStops = [&reader, &scanned]() {
                    while (reader.readNextStartElement()) {
                        if (reader.qualifiedName() == QLatin1StringView("style:tab-stop")) {
                            ScannedTabStop tab;
                            const qreal points = KoUnit::parseValue(reader.attributes().value(QStringLiteral("style:position")).toString(), -1.0);
                            if (points >= 0) {
                                tab.position = qRound(points * 20.0);
                                tab.type = reader.attributes().value(QStringLiteral("style:type")).toString();
                                scanned.tabStops.append(tab);
                            }
                        }
                        reader.skipCurrentElement();
                    }
                };
                while (reader.readNextStartElement()) {
                    if (reader.qualifiedName() == QLatin1StringView("style:paragraph-properties")) {
                        while (reader.readNextStartElement()) {
                            if (reader.qualifiedName() == QLatin1StringView("style:tab-stops")) {
                                readTabStops();
                            } else {
                                reader.skipCurrentElement();
                            }
                        }
                        continue;
                    }
                    if (reader.qualifiedName() == QLatin1StringView("style:tab-stops")) {
                        readTabStops();
                        continue;
                    }
                    reader.skipCurrentElement();
                }
                if (family == QLatin1StringView("paragraph") && !name.isEmpty() && !scanned.tabStops.isEmpty()) {
                    paragraphStyles->insert(name, scanned);
                }
            } else if (qname == QLatin1StringView("text:list-style")) {
                const QString name = reader.attributes().value(QStringLiteral("style:name")).toString();
                QVector<NumberingLevel> levels;
                while (reader.readNextStartElement()) {
                    const QString levelName = reader.qualifiedName().toString();
                    if (levelName.startsWith(QLatin1StringView("text:list-level-style"))) {
                        NumberingLevel level;
                        level.level = levels.size() + 1;
                        readListLevelStyle(reader, &level);
                        levels.append(level);
                    } else {
                        reader.skipCurrentElement();
                    }
                }
                if (!name.isEmpty() && !levels.isEmpty()) {
                    numbering->setLevels(name, levels);
                }
            } else {
                reader.skipCurrentElement();
            }
        }
    }
}

// Serializes a set of tab stops as a <w:tabs> element.
QByteArray tabStopsXml(const QVector<ScannedTabStop> &tabStops)
{
    QByteArray result;
    QBuffer buffer(&result);
    KoXmlWriter writer(&buffer);
    writer.startElement("w:tabs");
    for (const ScannedTabStop &tab : tabStops) {
        writer.startElement("w:tab");
        if (tab.type == QLatin1String("right")) {
            writer.addAttribute("w:val", "right");
        } else if (tab.type == QLatin1String("center")) {
            writer.addAttribute("w:val", "center");
        } else if (tab.type == QLatin1String("char")) {
            writer.addAttribute("w:val", "left");
        } else {
            writer.addAttribute("w:val", "left");
        }
        writer.addAttribute("w:pos", tab.position);
        writer.endElement(); // w:tab
    }
    writer.endElement(); // w:tabs
    return result;
}

// Collects tab stops (with inheritance resolved) and list numbering
// definitions from both styles.xml and content.xml.
void collectStyleExtras(OdfReaderDocxContext &context)
{
    KoStore *store = context.odfStore();
    if (!store) {
        return;
    }

    QHash<QString, ScannedParagraphStyle> paragraphStyles;
    for (const QString &part : {QStringLiteral("styles.xml"), QStringLiteral("content.xml")}) {
        if (!store->open(part)) {
            continue;
        }
        KoXmlStreamReader reader;
        prepareForOdf(reader);
        reader.setDevice(store->device());
        scanStylePart(reader, &paragraphStyles, &context.numbering());
        store->close();
    }

    // Resolve inherited tab stops: a paragraph style without its own stops
    // uses the ones of its ancestor.
    for (auto it = paragraphStyles.constBegin(); it != paragraphStyles.constEnd(); ++it) {
        QVector<ScannedTabStop> tabs = it->tabStops;
        QString parent = it->parent;
        int guard = 0;
        while (tabs.isEmpty() && !parent.isEmpty() && guard++ < 16) {
            const auto parentIt = paragraphStyles.constFind(parent);
            if (parentIt == paragraphStyles.constEnd()) {
                break;
            }
            tabs = parentIt->tabStops;
            parent = parentIt->parent;
        }
        if (!tabs.isEmpty()) {
            context.setTabStopsForStyle(it.key(), tabStopsXml(tabs));
        }
    }
}

} // namespace

} // namespace

DocxExport::DocxExport(QObject *parent, const QVariantList &)
    : KoFilter(parent)
{
}
DocxExport::~DocxExport() = default;

KoFilter::ConversionStatus DocxExport::convert(const QByteArray &from, const QByteArray &to)
{
    // Check for types
    if (from != "application/vnd.oasis.opendocument.text" || to != "application/vnd.openxmlformats-officedocument.wordprocessingml.document") {
        return KoFilter::NotImplemented;
    }

    // Open the infile and return an error if it fails.
    KoStore *odfStore = KoStore::createStore(m_chain->inputFile(), KoStore::Read, "", KoStore::Auto);
    if (!odfStore->open("mimetype")) {
        errorDocx << "Unable to open input file!" << Qt::endl;
        delete odfStore;
        return KoFilter::FileNotFound;
    }
    odfStore->close();

    // Start the conversion

    // Collects all the parts of the docx file and writes the result at the end.
    DocxFile docxFile;

    OdfReaderDocxContext docxBackendContext(odfStore, &docxFile);

    // The backends
    OdtReaderDocxBackend docxBackend;
    DocxDrawReaderBackend docxDrawBackend;
    OdfTextReaderDocxBackend docxTextBackend;

    // The readers
    OdtReader odtReader;
    OdfDrawReader drawReader;
    drawReader.setBackend(&docxDrawBackend);
    drawReader.setContext(&docxBackendContext);
    odtReader.setDrawReader(&drawReader);
    OdfTextReader odfTextReader;
    odfTextReader.setBackend(&docxTextBackend);
    odtReader.setTextReader(&odfTextReader);

    if (!odtReader.analyzeContent(&docxBackendContext)) {
        return KoFilter::ParsingError;
    }

    // After analyzeContent: the context manifest and styles are only available
    // once analyzeOdfFile() ran, and the store must be free to open content.xml.
    docxDrawBackend.setContext(&docxBackendContext);

    // Collect tab stops and list definitions from the package.  The text
    // backend needs both while it generates the document (and the header and
    // footer parts, which are written below).
    collectStyleExtras(docxBackendContext);
    resolveFontFamilies(docxBackendContext);

    DocxStyleWriter styleWriter(&docxBackendContext);
    styleWriter.read();

    // Add the styles to the docx file.
    docxFile.addContentFile("", "/word/styles.xml", "application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml", styleWriter.documentContent());

    // Export the first master page's header/footer (letterhead, page footer)
    // and record the section properties for the end of the document body.
    // This has to happen before readContent(): the sectPr is emitted when
    // office:body is closed.  The text reader needs its context set here
    // already because readContent() would only set it later.
    odfTextReader.setContext(&docxBackendContext);
    exportMasterPageSections(docxBackendContext, odfTextReader, docxFile);

    if (!odtReader.readContent(&docxBackend, &docxBackendContext)) {
        return KoFilter::ParsingError;
    }

    bool commentsExist = !docxBackendContext.commentsContent().isEmpty();

    // list numbering definitions are only complete after the body has been
    // traversed, since only then are all used list styles known.
    if (!docxBackendContext.numbering().isEmpty()) {
        docxFile.addContentFile("",
                                "/word/numbering.xml",
                                "application/vnd.openxmlformats-officedocument.wordprocessingml.numbering+xml",
                                docxBackendContext.numbering().toXml());
    }

    if (commentsExist) {
        // Add comments file
        QByteArray tempArray;
        QBuffer tempBuffer(&tempArray);
        KoXmlWriter commentWriter(&tempBuffer);
        commentWriter.startDocument(nullptr);
        commentWriter.startElement("w:comments");
        commentWriter.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
        commentWriter.addAttribute("xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
        commentWriter.addCompleteElement(docxBackendContext.commentsContent());
        commentWriter.endElement(); // w:comments
        commentWriter.endDocument();
        docxFile.addContentFile("", "/word/comments.xml", "application/vnd.openxmlformats-officedocument.wordprocessingml.comments+xml", tempArray);
    }

    // Footnotes and endnotes collected by the text backend.  The buffers
    // hold bare <w:footnote>/<w:endnote> fragments; Word also requires the
    // separator and continuation-separator entries with the reserved ids -1
    // and 0, so add them while wrapping the fragments in the part root.
    if (!docxBackendContext.footnotesContent().isEmpty()) {
        QByteArray tempArray;
        QBuffer tempBuffer(&tempArray);
        KoXmlWriter notesWriter(&tempBuffer);
        notesWriter.startDocument(nullptr);
        notesWriter.startElement("w:footnotes");
        notesWriter.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
        notesWriter.addCompleteElement(
            QByteArray("<w:footnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:footnote>"
                       "<w:footnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:footnote>"));
        notesWriter.addCompleteElement(docxBackendContext.footnotesContent());
        notesWriter.endElement(); // w:footnotes
        notesWriter.endDocument();
        docxFile.addRelationship(QStringLiteral("word/document.xml"),
                                 QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/footnotes"),
                                 QStringLiteral("footnotes.xml"));
        docxFile.addContentFile("", "/word/footnotes.xml", "application/vnd.openxmlformats-officedocument.wordprocessingml.footnotes+xml", tempArray);
    }
    if (!docxBackendContext.endnotesContent().isEmpty()) {
        QByteArray tempArray;
        QBuffer tempBuffer(&tempArray);
        KoXmlWriter notesWriter(&tempBuffer);
        notesWriter.startDocument(nullptr);
        notesWriter.startElement("w:endnotes");
        notesWriter.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
        notesWriter.addCompleteElement(
            QByteArray("<w:endnote w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:endnote>"
                       "<w:endnote w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r><w:continuationSeparator/></w:r></w:p></w:endnote>"));
        notesWriter.addCompleteElement(docxBackendContext.endnotesContent());
        notesWriter.endElement(); // w:endnotes
        notesWriter.endDocument();
        docxFile.addRelationship(QStringLiteral("word/document.xml"),
                                 QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/endnotes"),
                                 QStringLiteral("endnotes.xml"));
        docxFile.addContentFile("", "/word/endnotes.xml", "application/vnd.openxmlformats-officedocument.wordprocessingml.endnotes+xml", tempArray);
    }

    // Emit the package metadata and the standard supporting parts.  Callers
    // that inspect a DOCX package expect these parts even when the document
    // has no comments, notes, custom properties, or explicit settings.
    writePackageMetadata(docxFile, odfStore, docxBackendContext.evenAndOddHeaders,
                         commentsExist,
                         !docxBackendContext.footnotesContent().isEmpty(),
                         !docxBackendContext.endnotesContent().isEmpty());

    // Add the newly converted document contents to the docx file.
    docxFile.addContentFile("",
                            "/word/document.xml",
                            "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml",
                            docxBackendContext.documentContent());

    // Write the output file.
    if (docxDrawBackend.hasDrawingError()) {
        return KoFilter::NotImplemented;
    }
    return docxFile.writeDocx(m_chain->outputFile(), to, docxBackendContext, commentsExist);
}
