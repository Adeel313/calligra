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
#include <QRegularExpression>

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
};

int toTwips(qreal points)
{
    return qRound(points * 20.0);
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

    const QString headerFile = QStringLiteral("header%1.xml").arg(partNumber);
    const QString footerFile = QStringLiteral("footer%1.xml").arg(partNumber);
    QHash<QString, PageGeometry> pageLayouts;
    QString pageLayoutName;
    const QString masterPageName = selectedMaster;

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
                            while (reader.readNextStartElement()) {
                                if (reader.qualifiedName() == QLatin1StringView("style:columns")) {
                                    geometry.columns = qMax(1, reader.attributes().value(QStringLiteral("fo:column-count")).toInt());
                                    geometry.columnGap = KoUnit::parseValue(reader.attributes().value(QStringLiteral("fo:column-gap")).toString());
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
                        context.setOutputPart(isHeader ? QStringLiteral("word/") + headerFile : QStringLiteral("word/") + footerFile);

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
                                               headerFile);
        docxFile.addContentFile(QString(),
                                QStringLiteral("/word/") + headerFile,
                                QByteArrayLiteral("application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"),
                                wrapHeaderFooterPart(headerContent, "w:hdr"));
    }

    QString footerRelId;
    if (!footerContent.isEmpty()) {
        footerRelId = docxFile.addRelationship(QStringLiteral("word/document.xml"),
                                               QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer"),
                                               footerFile);
        docxFile.addContentFile(QString(),
                                QStringLiteral("/word/") + footerFile,
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
        writer.addAttribute("w:top", toTwips(geometry.marginTop + (headerRelId.isEmpty() ? 0 : geometry.headerMinHeight)));
        writer.addAttribute("w:right", toTwips(geometry.marginRight));
        writer.addAttribute("w:bottom", toTwips(geometry.marginBottom + (footerRelId.isEmpty() ? 0 : geometry.footerMinHeight)));
        writer.addAttribute("w:left", toTwips(geometry.marginLeft));
        writer.addAttribute("w:header", toTwips(geometry.marginTop));
        writer.addAttribute("w:footer", toTwips(geometry.marginBottom));
        writer.addAttribute("w:gutter", 0);
        writer.endElement(); // w:pgMar
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
