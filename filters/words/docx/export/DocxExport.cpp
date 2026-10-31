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
#include <KoXmlStreamReader.h>

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
};

int toTwips(qreal points)
{
    return qRound(points * 20.0);
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

// Traverses styles.xml to export the first paragraph's selected master page's
// <style:header> and <style:footer> as word/header1.xml / word/footer1.xml and to collect the
// page geometry of the referenced page layout.  The resulting section
// properties (<w:sectPr>: header/footer references plus page size and
// margins) are stored in the context, where OdtReaderDocxBackend appends
// them when office:body is closed.
//
// Must run before readContent(): the sectPr is needed while the document
// body is written.
void exportHeaderAndFooter(OdfReaderDocxContext &context, OdfTextReader &textReader, DocxFile &docxFile)
{
    KoStore *store = context.odfStore();
    if (!store) {
        return;
    }

    QHash<QString, PageGeometry> pageLayouts;
    QString pageLayoutName;
    QString masterPageName;
    QString firstParagraphStyle;
    if (store->open(QStringLiteral("content.xml"))) {
        KoXmlStreamReader contentReader;
        prepareForOdf(contentReader);
        contentReader.setDevice(store->device());
        while (!contentReader.atEnd()) {
            contentReader.readNext();
            if (!contentReader.isStartElement()) {
                continue;
            }
            contentReader.qualifiedName(); // validate prefixes for attribute lookup
            if (contentReader.qualifiedName() != QLatin1StringView("text:p")) {
                continue;
            }

            firstParagraphStyle = contentReader.attributes().value(QStringLiteral("text:style-name")).toString();
            break;
        }
        store->close();
    }

    // Automatic paragraph styles in content.xml may directly select a master
    // page; resolve the first paragraph's style in that same package part.
    if (!firstParagraphStyle.isEmpty() && store->open(QStringLiteral("content.xml"))) {
        KoXmlStreamReader contentStylesReader;
        prepareForOdf(contentStylesReader);
        contentStylesReader.setDevice(store->device());
        while (!contentStylesReader.atEnd()) {
            contentStylesReader.readNext();
            if (contentStylesReader.isStartElement() && contentStylesReader.qualifiedName() == QLatin1StringView("office:document-content")) {
                break;
            }
        }
        while (contentStylesReader.readNextStartElement()) {
            if (contentStylesReader.qualifiedName() != QLatin1StringView("office:automatic-styles")) {
                contentStylesReader.skipCurrentElement();
                continue;
            }
            while (contentStylesReader.readNextStartElement()) {
                if (contentStylesReader.qualifiedName() == QLatin1StringView("style:style")
                    && contentStylesReader.attributes().value(QStringLiteral("style:name")) == firstParagraphStyle) {
                    masterPageName = contentStylesReader.attributes().value(QStringLiteral("style:master-page-name")).toString();
                    if (masterPageName.isEmpty()) {
                        const QString parentStyleName = contentStylesReader.attributes().value(QStringLiteral("style:parent-style-name")).toString();
                        contentStylesReader.skipCurrentElement();
                        while (contentStylesReader.readNextStartElement()) {
                            if (contentStylesReader.qualifiedName() != QLatin1StringView("office:styles")) {
                                contentStylesReader.skipCurrentElement();
                                continue;
                            }
                            while (contentStylesReader.readNextStartElement()) {
                                if (contentStylesReader.qualifiedName() == QLatin1StringView("style:style")
                                    && contentStylesReader.attributes().value(QStringLiteral("style:name")) == parentStyleName) {
                                    masterPageName = contentStylesReader.attributes().value(QStringLiteral("style:master-page-name")).toString();
                                    break;
                                }
                                contentStylesReader.skipCurrentElement();
                            }
                            break;
                        }
                    }
                    break;
                }
                contentStylesReader.skipCurrentElement();
            }
            break;
        }
        store->close();
    }

    if (!store->open(QStringLiteral("styles.xml"))) {
        return;
    }
    KoXmlStreamReader reader;
    prepareForOdf(reader);
    reader.setDevice(store->device());

    QByteArray headerContent;
    QByteArray footerContent;
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
                    while (reader.readNextStartElement()) {
                        const QString childName = reader.qualifiedName().toString();
                        if (childName == QLatin1StringView("style:page-layout-properties")) {
                            const KoXmlStreamAttributes attrs = reader.attributes();
                            geometry.valid = true;
                            geometry.pageWidth = KoUnit::parseValue(attrs.value(QStringLiteral("fo:page-width")).toString());
                            geometry.pageHeight = KoUnit::parseValue(attrs.value(QStringLiteral("fo:page-height")).toString());
                            geometry.marginTop = KoUnit::parseValue(attrs.value(QStringLiteral("fo:margin-top")).toString());
                            geometry.marginRight = KoUnit::parseValue(attrs.value(QStringLiteral("fo:margin-right")).toString());
                            geometry.marginBottom = KoUnit::parseValue(attrs.value(QStringLiteral("fo:margin-bottom")).toString());
                            geometry.marginLeft = KoUnit::parseValue(attrs.value(QStringLiteral("fo:margin-left")).toString());
                            geometry.landscape = attrs.value(QStringLiteral("style:print-orientation")) == QLatin1StringView("landscape");
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

                    // Redirect the backends' output into the header/footer
                    // part while its content is traversed.
                    while (reader.readNextStartElement()) {
                        const QString childName = reader.qualifiedName().toString();
                        if (childName != QLatin1StringView("style:header") && childName != QLatin1StringView("style:footer")) {
                            reader.skipCurrentElement(); // first/left variants are not exported yet
                            continue;
                        }
                        const bool isHeader = childName == QLatin1StringView("style:header");

                        QByteArray content;
                        QBuffer buffer(&content);
                        KoXmlWriter partWriter(&buffer);
                        KoXmlWriter *previousWriter = context.setDocumentWriter(&partWriter);
                        const QString previousPart = context.outputPart();
                        context.setOutputPart(isHeader ? QStringLiteral("word/header1.xml") : QStringLiteral("word/footer1.xml"));

                        while (reader.readNextStartElement()) {
                            textReader.readTextLevelElement(reader);
                        }

                        context.setOutputPart(previousPart);
                        context.setDocumentWriter(previousWriter);

                        if (isHeader) {
                            headerContent = content;
                        } else {
                            footerContent = content;
                        }
                    }
                }
            } else if (tagName == QLatin1StringView("office:styles")) {
                // A named paragraph style may select a master page for the
                // document's initial text. Choose the first explicit selection;
                // otherwise use the first master-page definition as default.
                while (reader.readNextStartElement()) {
                    if (reader.qualifiedName() == QLatin1StringView("style:style")
                        && reader.attributes().value(QStringLiteral("style:family")) == QLatin1StringView("paragraph")) {
                        const QString candidate = reader.attributes().value(QStringLiteral("style:master-page-name")).toString();
                        if (!candidate.isEmpty()) {
                            masterPageName = candidate;
                            break;
                        }
                    }
                    reader.skipCurrentElement();
                }
            } else {
                reader.skipCurrentElement();
            }
        }
    }

    store->close();

    // Create the parts and the relationships from document.xml to them.
    QString headerRelId;
    if (!headerContent.isEmpty()) {
        headerRelId = docxFile.addRelationship(QStringLiteral("word/document.xml"),
                                               QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/header"),
                                               QStringLiteral("header1.xml"));
        docxFile.addContentFile(QString(),
                                QStringLiteral("/word/header1.xml"),
                                QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"),
                                wrapHeaderFooterPart(headerContent, "w:hdr"));
    }

    QString footerRelId;
    if (!footerContent.isEmpty()) {
        footerRelId = docxFile.addRelationship(QStringLiteral("word/document.xml"),
                                               QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer"),
                                               QStringLiteral("footer1.xml"));
        docxFile.addContentFile(QString(),
                                QStringLiteral("/word/footer1.xml"),
                                QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"),
                                wrapHeaderFooterPart(footerContent, "w:ftr"));
    }

    const PageGeometry geometry = pageLayouts.value(pageLayoutName);
    if (headerRelId.isEmpty() && footerRelId.isEmpty() && !geometry.valid) {
        return;
    }

    // Build the section properties for the end of the document body.
    QByteArray sectPr;
    QBuffer sectPrBuffer(&sectPr);
    KoXmlWriter writer(&sectPrBuffer);
    writer.startElement("w:sectPr");
    writer.addAttribute("xmlns:w", "http://schemas.openxmlformats.org/wordprocessingml/2006/main");
    writer.addAttribute("xmlns:r", "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    if (!headerRelId.isEmpty()) {
        writer.startElement("w:headerReference");
        writer.addAttribute("w:type", "default");
        writer.addAttribute("r:id", headerRelId);
        writer.endElement(); // w:headerReference
    }
    if (!footerRelId.isEmpty()) {
        writer.startElement("w:footerReference");
        writer.addAttribute("w:type", "default");
        writer.addAttribute("r:id", footerRelId);
        writer.endElement(); // w:footerReference
    }
    writer.startElement("w:type");
    writer.addAttribute("w:val", "nextPage");
    writer.endElement(); // w:type
    if (geometry.valid) {
        writer.startElement("w:pgSz");
        writer.addAttribute("w:w", toTwips(geometry.pageWidth));
        writer.addAttribute("w:h", toTwips(geometry.pageHeight));
        if (geometry.landscape) {
            writer.addAttribute("w:orient", "landscape");
        }
        writer.endElement(); // w:pgSz

        // Reserve room for the header/footer content on top of the body
        // margins, the same way LibreOffice maps these values.
        const qreal headerReserve = headerRelId.isEmpty() ? 0.0 : geometry.headerMinHeight;
        const qreal footerReserve = footerRelId.isEmpty() ? 0.0 : geometry.footerMinHeight;
        writer.startElement("w:pgMar");
        writer.addAttribute("w:top", toTwips(geometry.marginTop + headerReserve));
        writer.addAttribute("w:right", toTwips(geometry.marginRight));
        writer.addAttribute("w:bottom", toTwips(geometry.marginBottom + footerReserve));
        writer.addAttribute("w:left", toTwips(geometry.marginLeft));
        writer.addAttribute("w:header", toTwips(geometry.marginTop));
        writer.addAttribute("w:footer", toTwips(geometry.marginBottom));
        writer.addAttribute("w:gutter", 0);
        writer.endElement(); // w:pgMar
    }
    writer.endElement(); // w:sectPr

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

        // text:display-levels says how many levels of the number are shown
        // (e.g. "1.2.3" for level 3); %1..%N reference the level counters.
        int displayLevels = attributes.value(QStringLiteral("text:display-levels")).toInt();
        if (displayLevels < 1) {
            displayLevels = 1;
        }
        displayLevels = qMin(displayLevels, level->level);
        QString number;
        for (int i = 1; i <= displayLevels; ++i) {
            if (i > 1) {
                number += QLatin1Char('.');
            }
            number += QLatin1Char('%') + QString::number(i);
        }
        level->text = prefix + number + suffix;
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
    exportHeaderAndFooter(docxBackendContext, odfTextReader, docxFile);

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

    // Add the newly converted document contents to the docx file.
    docxFile.addContentFile("",
                            "/word/document.xml",
                            "application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml",
                            docxBackendContext.documentContent());

    // Write the output file.
    return docxFile.writeDocx(m_chain->outputFile(), to, docxBackendContext, commentsExist);
}
