/*
 * This file is part of Office 2007 Filters for Calligra
 * SPDX-FileCopyrightText: 2002 Laurent Montel <lmontel@mandrakesoft.com>
 * SPDX-FileCopyrightText: 2003 David Faure <faure@kde.org>
 * SPDX-FileCopyrightText: 2002, 2003, 2004 Nicolas GOUTTE <goutte@kde.org>
 * SPDX-FileCopyrightText: 2009 Nokia Corporation and /or its subsidiary(-ies).
 *
 * Contact: Suresh Chande suresh.chande@nokia.com
 *
 * SPDX-License-Identifier: LGPL-2.1-only
 *
 */

#include "XlsxImport.h"
#include "XlsxUtils.h"
#include "XlsxXmlCommentsReader.h"
#include "XlsxXmlDocumentReader.h"
#include "XlsxXmlSharedStringsReader.h"
#include "XlsxXmlStylesReader.h"

#include "MsooXmlThemesReader.h"
#include <MsooXmlContentTypes.h>
#include <MsooXmlRelationships.h>
#include <MsooXmlSchemas.h>
#include <MsooXmlUtils.h>

#include <memory>

#include <QColor>
#include <QElapsedTimer>
#include <QBuffer>
#include <QFile>
#include <QFont>
#include <QImage>
#include <QPen>

#include <KPluginFactory>

#include <KoDocument.h>
#include <KoDocumentInfo.h>
#include <KoEmbeddedDocumentSaver.h>
#include <KoFilterChain.h>
#include <KoPageLayout.h>
#include <KoXmlWriter.h>

K_PLUGIN_FACTORY_WITH_JSON(XlsxImportFactory, "calligra_filter_xlsx2ods.json", registerPlugin<XlsxImport>();)

Q_LOGGING_CATEGORY(lcXlsxImport, "calligra.filter.xlsx2ods")
Q_LOGGING_CATEGORY(lcXlsxTiming, "calligra.filter.xlsx.timing")

enum XlsxDocumentType {
    XlsxDocument,
    XlsxTemplate,
    XlsxMacroDocument
};

class XlsxImport::Private
{
public:
    Private()
        : type(XlsxDocument)
        , macrosEnabled(false)
    {
    }

    const char *mainDocumentContentType() const
    {
        if (type == XlsxMacroDocument)
            return MSOOXML::ContentTypes::spreadsheetMacroDocument;
        if (type == XlsxTemplate)
            return MSOOXML::ContentTypes::spreadsheetTemplate;
        return MSOOXML::ContentTypes::spreadsheetDocument;
    }

    XlsxDocumentType type;
    bool macrosEnabled;
    QMap<int, QString> printAreas;
    QString dataValidationXml;
    QString namedExpressionsXml;
    QList<QPair<QString, QString>> tables;
    int dataValidationCount = 0;
};

XlsxImport::XlsxImport(QObject *parent, const QVariantList &)
    : MSOOXML::MsooXmlImport(QLatin1String("spreadsheet"), parent)
    , d(new Private)
{
}

KoFilter::ConversionStatus XlsxImport::convert(const QByteArray &from, const QByteArray &to)
{
    QElapsedTimer timer;
    timer.start();
    const KoFilter::ConversionStatus status = MSOOXML::MsooXmlImport::convert(from, to);
    qCDebug(lcXlsxTiming, "XLSX import: finished at %lld ms", (long long)timer.elapsed());
    return status;
}

QMap<int, QString> XlsxImport::printAreas() const
{
    return d->printAreas;
}

void XlsxImport::setPrintAreas(const QMap<int, QString> &printAreas)
{
    d->printAreas = printAreas;
}

QString XlsxImport::dataValidationXml() const
{
    return d->dataValidationXml;
}

void XlsxImport::appendDataValidationXml(const QString &xml)
{
    d->dataValidationXml += xml;
}

void XlsxImport::clearDataValidationXml()
{
    d->dataValidationXml.clear();
}

QString XlsxImport::nextDataValidationName()
{
    return QStringLiteral("val%1").arg(++d->dataValidationCount);
}

QString XlsxImport::namedExpressionsXml() const
{
    return d->namedExpressionsXml;
}

void XlsxImport::setNamedExpressionsXml(const QString &xml)
{
    d->namedExpressionsXml = xml;
}

QList<QPair<QString, QString>> XlsxImport::tables() const
{
    return d->tables;
}

void XlsxImport::appendTable(const QString &name, const QString &range)
{
    d->tables.append(qMakePair(name, range));
}

XlsxImport::~XlsxImport()
{
    delete d;
}

bool XlsxImport::acceptsSourceMimeType(const QByteArray &mime) const
{
    qCDebug(lcXlsxImport) << "Entering XLSX Import filter: from " << mime;
    if (mime == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") {
        d->type = XlsxDocument;
        d->macrosEnabled = false;
    } else if (mime == "application/vnd.openxmlformats-officedocument.spreadsheetml.template") {
        d->type = XlsxTemplate;
        d->macrosEnabled = false;
    } else if (mime == "application/vnd.ms-excel.sheet.macroEnabled") {
        d->type = XlsxMacroDocument;
        d->macrosEnabled = true;
    } else if (mime == "application/vnd.ms-excel.sheet.macroEnabled.12") {
        d->type = XlsxDocument;
        d->macrosEnabled = true;
    } else if (mime == "application/vnd.ms-excel.template.macroEnabled.12") {
        d->type = XlsxTemplate;
        d->macrosEnabled = true;
    } else {
        return false;
    }
    return true;
}

bool XlsxImport::acceptsDestinationMimeType(const QByteArray &mime) const
{
    qCDebug(lcXlsxImport) << "Entering XLSX Import filter: to " << mime;
    return mime == "application/vnd.oasis.opendocument.spreadsheet";
}

namespace
{
//! Reads workbook.xml to collect the sheet print areas, which the worksheet reader emits as
//! table:print-ranges. They have to be known before the sheets are parsed.
class PrintAreaReader : public MSOOXML::MsooXmlReader
{
public:
    explicit PrintAreaReader(KoOdfWriters *writers)
        : MsooXmlReader(writers)
    {
    }

    KoFilter::ConversionStatus read(MSOOXML::MsooXmlReaderContext *context = nullptr) override
    {
        Q_UNUSED(context)
        readNext();
        if (!isStartDocument()) {
            return KoFilter::WrongFormat;
        }
        readNext();
        if (!expectEl("workbook")) {
            return KoFilter::WrongFormat;
        }
        while (!atEnd()) {
            readNext();
            if (isEndElement()) {
                break;
            }
            if (!isStartElement()) {
                continue;
            }
            if (name() == QLatin1StringView("definedNames")) {
                readDefinedNames();
            } else {
                skipCurrentElement();
            }
        }
        return KoFilter::OK;
    }

    //! Print areas in OpenDocument notation, keyed by the zero-based sheet index.
    QMap<int, QString> printAreas;
    //! Workbook level names, converted to OpenDocument notation.
    QList<QPair<QString, QString>> definedNames;

private:
    void readDefinedNames()
    {
        while (!atEnd()) {
            readNext();
            if (isEndElement() && name() == QLatin1StringView("definedNames")) {
                break;
            }
            if (!isStartElement()) {
                continue;
            }
            if (name() != QLatin1StringView("definedName")) {
                skipCurrentElement();
                continue;
            }
            const auto attrs = attributes();
            const QString name = attrs.value(QLatin1StringView("name")).toString();
            if (name == QLatin1StringView("_xlnm.Print_Area")) {
                const int sheet = attrs.value(QLatin1StringView("localSheetId")).toInt();
                const QString area = convertPrintArea(readElementText().trimmed());
                if (!area.isEmpty()) {
                    printAreas.insert(sheet, area);
                }
                continue;
            }
            if (!name.isEmpty() && !name.startsWith(QLatin1StringView("_xlnm."))) {
                const QString range = convertPrintArea(readElementText().trimmed());
                if (!range.isEmpty()) {
                    definedNames.append(qMakePair(name, range));
                }
                continue;
            }
            skipCurrentElement();
        }
    }

    //! Converts an Excel print area ('Sheet'!$A$1:$L$100) into OpenDocument print-ranges notation.
    static QString convertPrintArea(const QString &expression)
    {
        QStringList areas;
        QString current;
        bool inQuotes = false;
        for (const QChar c : expression) {
            if (c == QLatin1Char('\'')) {
                inQuotes = !inQuotes;
            }
            if (c == QLatin1Char(',') && !inQuotes) {
                areas << current;
                current.clear();
                continue;
            }
            current += c;
        }
        areas << current;

        QStringList result;
        for (QString area : std::as_const(areas)) {
            area = area.trimmed();
            const int bang = area.lastIndexOf(QLatin1Char('!'));
            if (bang <= 0 || bang + 1 >= area.size()) {
                continue;
            }
            QString sheetName = area.left(bang).trimmed();
            if (sheetName.size() > 1 && sheetName.startsWith(QLatin1Char('\'')) && sheetName.endsWith(QLatin1Char('\''))) {
                sheetName = sheetName.mid(1, sheetName.size() - 2);
                sheetName.replace(QLatin1String("''"), QLatin1String("'"));
            }
            const QStringList cells = area.mid(bang + 1).trimmed().split(QLatin1Char(':'));
            if (cells.isEmpty() || cells.first().isEmpty()) {
                continue;
            }
            QString odfSheetName = sheetName;
            odfSheetName.replace(QLatin1Char('\''), QLatin1String("''"));
            if (odfSheetName.contains(QLatin1Char(' ')) || odfSheetName.contains(QLatin1Char('.')) || odfSheetName.contains(QLatin1Char(';'))
                || odfSheetName.contains(QLatin1Char('!')) || odfSheetName.contains(QLatin1Char('$')) || odfSheetName.contains(QLatin1Char(']'))) {
                odfSheetName = QLatin1Char('\'') + odfSheetName + QLatin1Char('\'');
            }
            QString odfArea = odfSheetName + QLatin1Char('.') + cells.first();
            if (cells.size() > 1 && !cells.at(1).isEmpty()) {
                odfArea += QLatin1Char(':') + odfSheetName + QLatin1Char('.') + cells.at(1);
            }
            result << odfArea;
        }
        return result.join(QLatin1Char(' '));
    }
};
} // namespace

KoFilter::ConversionStatus XlsxImport::parseParts(KoOdfWriters *writers, MSOOXML::MsooXmlRelationships *relationships, QString &errorMessage)
{
    // more here...
    // 0. temporary styles
    //! @todo create styles in XlsxXmlDocumentReader (XLSX defines styles in workbook.xml)

    writers->mainStyles->insertRawOdfStyles(
        KoGenStyles::DocumentStyles,
        "    <!-- COPIED -->"
        "\n    <style:default-style style:family=\"table-cell\">"
        "\n      <style:table-cell-properties />"
        "\n      <style:paragraph-properties style:tab-stop-distance=\"1.27cm\"/>"
        "\n      <style:text-properties style:font-name=\"Albany AMT\" fo:language=\"en\" fo:country=\"US\" style:font-name-asian=\"Arial\" "
        "style:language-asian=\"zxx\" style:country-asian=\"none\" style:font-name-complex=\"Tahoma\" style:language-complex=\"zxx\" "
        "style:country-complex=\"none\"/>"
        "\n    </style:default-style>"
        "\n    <number:number-style style:name=\"N0\">"
        "\n      <number:number number:min-integer-digits=\"1\"/>"
        "\n    </number:number-style>"
        "\n    <number:currency-style style:name=\"N104P0\" style:volatile=\"true\">"
        "\n      <number:currency-symbol number:language=\"en\" number:country=\"US\">$</number:currency-symbol>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N104\">"
        "\n      <style:text-properties fo:color=\"#ff0000\"/>"
        "\n      <number:text>-</number:text>"
        "\n      <number:currency-symbol number:language=\"en\" number:country=\"US\">$</number:currency-symbol>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N104P0\"/>"
        "\n    </number:currency-style>"
        "\n    <number:number-style style:name=\"N5000\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:number number:min-integer-digits=\"1\"/>"
        "\n    </number:number-style>"
        "\n    <number:currency-style style:name=\"N5116P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5116\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text>(</number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N5116P0\"/>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5117P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5117\" number:language=\"en\" number:country=\"US\">"
        "\n      <style:text-properties fo:color=\"#ff0000\"/>"
        "\n      <number:text>(</number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N5117P0\"/>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5118P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5118\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text>(</number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N5118P0\"/>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5119P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5119\" number:language=\"en\" number:country=\"US\">"
        "\n      <style:text-properties fo:color=\"#ff0000\"/>"
        "\n      <number:text>(</number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N5119P0\"/>"
        "\n    </number:currency-style>"
        "\n    <number:date-style style:name=\"N5120\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:month/>"
        "\n      <number:text>/</number:text>"
        "\n      <number:day/>"
        "\n      <number:text>/</number:text>"
        "\n      <number:year number:style=\"long\"/>"
        "\n    </number:date-style>"
        "\n    <number:date-style style:name=\"N5121\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:day/>"
        "\n      <number:text>-</number:text>"
        "\n      <number:month number:textual=\"true\"/>"
        "\n      <number:text>-</number:text>"
        "\n      <number:year/>"
        "\n    </number:date-style>"
        "\n    <number:date-style style:name=\"N5122\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:day/>"
        "\n      <number:text>-</number:text>"
        "\n      <number:month number:textual=\"true\"/>"
        "\n    </number:date-style>"
        "\n    <number:date-style style:name=\"N5123\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:month number:textual=\"true\"/>"
        "\n      <number:text>-</number:text>"
        "\n      <number:year/>"
        "\n    </number:date-style>"
        "\n    <number:time-style style:name=\"N5124\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:hours/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:minutes number:style=\"long\"/>"
        "\n      <number:text> </number:text>"
        "\n      <number:am-pm/>"
        "\n    </number:time-style>"
        "\n    <number:time-style style:name=\"N5125\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:hours/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:minutes number:style=\"long\"/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:seconds number:style=\"long\"/>"
        "\n      <number:text> </number:text>"
        "\n      <number:am-pm/>"
        "\n    </number:time-style>"
        "\n    <number:time-style style:name=\"N5126\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:hours/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:minutes number:style=\"long\"/>"
        "\n    </number:time-style>"
        "\n    <number:time-style style:name=\"N5127\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:hours/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:minutes number:style=\"long\"/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:seconds number:style=\"long\"/>"
        "\n    </number:time-style>"
        "\n    <number:date-style style:name=\"N5128\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:month/>"
        "\n      <number:text>/</number:text>"
        "\n      <number:day/>"
        "\n      <number:text>/</number:text>"
        "\n      <number:year number:style=\"long\"/>"
        "\n      <number:text> </number:text>"
        "\n      <number:hours/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:minutes number:style=\"long\"/>"
        "\n    </number:date-style>"
        "\n    <number:number-style style:name=\"N5129P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5129\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text>(</number:text>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N5129P0\"/>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5130P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5130\" number:language=\"en\" number:country=\"US\">"
        "\n      <style:text-properties fo:color=\"#ff0000\"/>"
        "\n      <number:text>(</number:text>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N5130P0\"/>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5131P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5131\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text>(</number:text>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N5131P0\"/>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5132P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5132\" number:language=\"en\" number:country=\"US\">"
        "\n      <style:text-properties fo:color=\"#ff0000\"/>"
        "\n      <number:text>(</number:text>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n      <style:map style:condition=\"value()&gt;=0\" style:apply-style-name=\"N5132P0\"/>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5133P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5133P1\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> (</number:text>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5133P2\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> - </number:text>"
        "\n    </number:number-style>"
        "\n    <number:text-style style:name=\"N5133\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:text-content/>"
        "\n      <number:text> </number:text>"
        "\n      <style:map style:condition=\"value()&gt;0\" style:apply-style-name=\"N5133P0\"/>"
        "\n      <style:map style:condition=\"value()&lt;0\" style:apply-style-name=\"N5133P1\"/>"
        "\n      <style:map style:condition=\"value()=0\" style:apply-style-name=\"N5133P2\"/>"
        "\n    </number:text-style>"
        "\n    <number:currency-style style:name=\"N5134P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5134P1\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:text>(</number:text>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5134P2\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:text>- </number:text>"
        "\n    </number:currency-style>"
        "\n    <number:text-style style:name=\"N5134\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:text-content/>"
        "\n      <number:text> </number:text>"
        "\n      <style:map style:condition=\"value()&gt;0\" style:apply-style-name=\"N5134P0\"/>"
        "\n      <style:map style:condition=\"value()&lt;0\" style:apply-style-name=\"N5134P1\"/>"
        "\n      <style:map style:condition=\"value()=0\" style:apply-style-name=\"N5134P2\"/>"
        "\n    </number:text-style>"
        "\n    <number:number-style style:name=\"N5135P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5135P1\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> (</number:text>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n    </number:number-style>"
        "\n    <number:number-style style:name=\"N5135P2\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> -</number:text>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"0\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:number-style>"
        "\n    <number:text-style style:name=\"N5135\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:text-content/>"
        "\n      <number:text> </number:text>"
        "\n      <style:map style:condition=\"value()&gt;0\" style:apply-style-name=\"N5135P0\"/>"
        "\n      <style:map style:condition=\"value()&lt;0\" style:apply-style-name=\"N5135P1\"/>"
        "\n      <style:map style:condition=\"value()=0\" style:apply-style-name=\"N5135P2\"/>"
        "\n    </number:text-style>"
        "\n    <number:currency-style style:name=\"N5136P0\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5136P1\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:text>(</number:text>"
        "\n      <number:number number:decimal-places=\"2\" number:min-integer-digits=\"1\" number:grouping=\"true\"/>"
        "\n      <number:text>)</number:text>"
        "\n    </number:currency-style>"
        "\n    <number:currency-style style:name=\"N5136P2\" style:volatile=\"true\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:currency-symbol/>"
        "\n      <number:text>-</number:text>"
        "\n      <number:number number:decimal-places=\"0\" number:min-integer-digits=\"0\"/>"
        "\n      <number:text> </number:text>"
        "\n    </number:currency-style>"
        "\n    <number:text-style style:name=\"N5136\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:text> </number:text>"
        "\n      <number:text-content/>"
        "\n      <number:text> </number:text>"
        "\n      <style:map style:condition=\"value()&gt;0\" style:apply-style-name=\"N5136P0\"/>"
        "\n      <style:map style:condition=\"value()&lt;0\" style:apply-style-name=\"N5136P1\"/>"
        "\n      <style:map style:condition=\"value()=0\" style:apply-style-name=\"N5136P2\"/>"
        "\n    </number:text-style>"
        "\n    <number:time-style style:name=\"N5137\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:minutes number:style=\"long\"/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:seconds number:style=\"long\"/>"
        "\n    </number:time-style>"
        "\n    <number:time-style style:name=\"N5138\" number:language=\"en\" number:country=\"US\" number:truncate-on-overflow=\"false\">"
        "\n      <number:hours/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:minutes number:style=\"long\"/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:seconds number:style=\"long\"/>"
        "\n    </number:time-style>"
        "\n    <number:time-style style:name=\"N5139\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:minutes number:style=\"long\"/>"
        "\n      <number:text>:</number:text>"
        "\n      <number:seconds number:style=\"long\" number:decimal-places=\"1\"/>"
        "\n    </number:time-style>"
        "\n    <number:number-style style:name=\"N5140\" number:language=\"en\" number:country=\"US\">"
        "\n      <number:scientific-number number:decimal-places=\"1\" number:min-integer-digits=\"3\" number:min-exponent-digits=\"1\"/>"
        "\n    </number:number-style>"
        "\n    <style:style style:name=\"Default\" style:family=\"table-cell\"/>"
        "\n    <style:style style:name=\"Result\" style:family=\"table-cell\" style:parent-style-name=\"Default\">"
        "\n      <style:text-properties fo:font-style=\"italic\" style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" "
        "style:text-underline-color=\"font-color\" fo:font-weight=\"bold\"/>"
        "\n    </style:style>"
        "\n    <style:style style:name=\"Result2\" style:family=\"table-cell\" style:parent-style-name=\"Result\" style:data-style-name=\"N104\"/>"
        "\n    <style:style style:name=\"Heading\" style:family=\"table-cell\" style:parent-style-name=\"Default\">"
        "\n      <style:table-cell-properties style:text-align-source=\"fix\" style:repeat-content=\"false\"/>"
        "\n      <style:paragraph-properties fo:text-align=\"center\"/>"
        "\n      <style:text-properties fo:font-size=\"16pt\" fo:font-style=\"italic\" fo:font-weight=\"bold\"/>"
        "\n    </style:style>"
        "\n    <style:style style:name=\"Heading1\" style:family=\"table-cell\" style:parent-style-name=\"Heading\">"
        "\n      <style:table-cell-properties style:rotation-angle=\"90\"/>"
        "\n    </style:style>"
        "\n    <style:style style:name=\"Excel_20_Built-in_20_Normal\" style:display-name=\"Excel Built-in Normal\" style:family=\"table-cell\" "
        "style:parent-style-name=\"Default\">"
        "\n      <style:table-cell-properties style:cell-protect=\"protected\" style:print-content=\"true\" style:diagonal-bl-tr=\"none\" "
        "style:diagonal-tl-br=\"none\" style:text-align-source=\"value-type\" style:repeat-content=\"false\" fo:background-color=\"transparent\" "
        "fo:wrap-option=\"no-wrap\" fo:border=\"none\" style:direction=\"ltr\" style:rotation-angle=\"0\" style:rotation-align=\"none\" "
        "style:shrink-to-fit=\"false\" style:vertical-align=\"bottom\"/>"
        "\n      <style:paragraph-properties fo:margin-left=\"0cm\" style:writing-mode=\"page\"/>"
        "\n      <style:text-properties fo:color=\"#000000\" style:text-outline=\"false\" style:text-line-through-style=\"none\" style:font-name=\"Calibri\" "
        "fo:font-size=\"11pt\" fo:font-style=\"normal\" fo:text-shadow=\"none\" style:text-underline-style=\"none\" fo:font-weight=\"normal\" "
        "style:font-size-asian=\"11pt\" style:font-style-asian=\"normal\" style:font-weight-asian=\"normal\" style:font-size-complex=\"11pt\" "
        "style:font-style-complex=\"normal\" style:font-weight-complex=\"normal\"/>"
        "\n    </style:style>"
        "\n    <!-- /COPIED -->");

    writers->mainStyles->insertRawOdfStyles(KoGenStyles::MasterStyles,
                                            "    <!-- COPIED -->"
                                            "\n    <style:master-page style:name=\"Default\" style:page-layout-name=\"pm1\">"
                                            "\n      <style:header>"
                                            "\n        <text:p>"
                                            "\n          <text:sheet-name>"
                                            "???"
                                            "</text:sheet-name>"
                                            "\n        </text:p>"
                                            "\n      </style:header>"
                                            "\n      <style:header-left style:display=\"false\"/>"
                                            "\n      <style:footer>"
                                            "\n        <text:p>Page <text:page-number>1</text:page-number></text:p>"
                                            "\n      </style:footer>"
                                            "\n      <style:footer-left style:display=\"false\"/>"
                                            "\n    </style:master-page>"
                                            "\n    <style:master-page style:name=\"Report\" style:page-layout-name=\"pm2\">"
                                            "\n      <style:header>"
                                            "\n        <style:region-left>"
                                            "\n          <text:p><text:sheet-name>"
                                            "???"
                                            "</text:sheet-name> (<text:title>"
                                            "???"
                                            "</text:title>)</text:p>"
                                            "\n        </style:region-left>"
                                            "\n        <style:region-right>"
                                            "\n          <text:p><text:date style:data-style-name=\"N2\" "
                                            "text:date-value=\"2009-07-18\">07/18/2009</text:date>, <text:time>21:48:12</text:time></text:p>"
                                            "\n        </style:region-right>"
                                            "\n      </style:header>"
                                            "\n      <style:header-left style:display=\"false\"/>"
                                            "\n      <style:footer>"
                                            "\n        <text:p>Page <text:page-number>1</text:page-number> / <text:page-count>99</text:page-count></text:p>"
                                            "\n      </style:footer>"
                                            "\n      <style:footer-left style:display=\"false\"/>"
                                            "\n    </style:master-page>"
                                            "\n    <style:master-page style:name=\"PageStyle_5f_Test_20_sheet_20__5f_1\" style:display-name=\"PageStyle_Test "
                                            "sheet _1\" style:page-layout-name=\"pm3\">"
                                            "\n      <style:header style:display=\"false\"/>"
                                            "\n      <style:header-left style:display=\"false\"/>"
                                            "\n      <style:footer style:display=\"false\"/>"
                                            "\n      <style:footer-left style:display=\"false\"/>"
                                            "\n    </style:master-page>"
                                            "\n    <style:master-page style:name=\"PageStyle_5f_Test_20_sheet_20__5f_2\" style:display-name=\"PageStyle_Test "
                                            "sheet _2\" style:page-layout-name=\"pm4\">"
                                            "\n      <style:header style:display=\"false\"/>"
                                            "\n      <style:header-left style:display=\"false\"/>"
                                            "\n      <style:footer style:display=\"false\"/>"
                                            "\n      <style:footer-left style:display=\"false\"/>"
                                            "\n    </style:master-page>"
                                            "\n    <!-- COPIED -->");

    writers->mainStyles->insertRawOdfStyles(
        KoGenStyles::StylesXmlAutomaticStyles,
        "    <!-- COPIED -->"
        "\n    <style:page-layout style:name=\"pm1\">"
        "\n      <style:page-layout-properties style:first-page-number=\"continue\" style:writing-mode=\"lr-tb\"/>"
        "\n      <style:header-style>"
        "\n        <style:header-footer-properties fo:min-height=\"0.751cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:margin-bottom=\"0.25cm\"/>"
        "\n      </style:header-style>"
        "\n      <style:footer-style>"
        "\n        <style:header-footer-properties fo:min-height=\"0.751cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:margin-top=\"0.25cm\"/>"
        "\n      </style:footer-style>"
        "\n    </style:page-layout>"
        "\n    <style:page-layout style:name=\"pm2\">"
        "\n      <style:page-layout-properties style:writing-mode=\"lr-tb\"/>"
        "\n      <style:header-style>"
        "\n        <style:header-footer-properties fo:min-height=\"0.751cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:margin-bottom=\"0.25cm\" "
        "fo:border=\"0.088cm solid #000000\" fo:padding=\"0.018cm\" fo:background-color=\"#c0c0c0\">"
        "\n          <style:background-image/>"
        "\n        </style:header-footer-properties>"
        "\n      </style:header-style>"
        "\n      <style:footer-style>"
        "\n        <style:header-footer-properties fo:min-height=\"0.751cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:margin-top=\"0.25cm\" "
        "fo:border=\"0.088cm solid #000000\" fo:padding=\"0.018cm\" fo:background-color=\"#c0c0c0\">"
        "\n          <style:background-image/>"
        "\n        </style:header-footer-properties>"
        "\n      </style:footer-style>"
        "\n    </style:page-layout>"
        "\n    <style:page-layout style:name=\"pm3\">"
        "\n      <style:page-layout-properties fo:page-width=\"21.001cm\" fo:page-height=\"29.7cm\" style:num-format=\"1\" "
        "style:print-orientation=\"portrait\" fo:margin-top=\"1.905cm\" fo:margin-bottom=\"1.905cm\" fo:margin-left=\"1.778cm\" fo:margin-right=\"1.778cm\" "
        "style:print-page-order=\"ttb\" style:first-page-number=\"continue\" style:scale-to=\"100%\" style:writing-mode=\"lr-tb\" style:print=\"charts "
        "drawings objects zero-values\"/>"
        "\n      <style:header-style>"
        "\n        <style:header-footer-properties fo:min-height=\"0.75cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:margin-bottom=\"0.25cm\"/>"
        "\n      </style:header-style>"
        "\n      <style:footer-style>"
        "\n        <style:header-footer-properties fo:min-height=\"0.75cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:margin-top=\"0.25cm\"/>"
        "\n      </style:footer-style>"
        "\n    </style:page-layout>"
        "\n    <style:page-layout style:name=\"pm4\">"
        "\n      <style:page-layout-properties fo:page-width=\"21.59cm\" fo:page-height=\"27.94cm\" style:num-format=\"1\" "
        "style:print-orientation=\"portrait\" fo:margin-top=\"1.905cm\" fo:margin-bottom=\"1.905cm\" fo:margin-left=\"1.778cm\" fo:margin-right=\"1.778cm\" "
        "style:print-page-order=\"ttb\" style:first-page-number=\"continue\" style:scale-to=\"100%\" style:writing-mode=\"lr-tb\" style:print=\"charts "
        "drawings objects zero-values\"/>"
        "\n      <style:header-style>"
        "\n        <style:header-footer-properties fo:min-height=\"0.75cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:margin-bottom=\"0.25cm\"/>"
        "\n      </style:header-style>"
        "\n      <style:footer-style>"
        "\n        <style:header-footer-properties fo:min-height=\"0.75cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:margin-top=\"0.25cm\"/>"
        "\n      </style:footer-style>"
        "\n    </style:page-layout>"
        "\n    <!-- /COPIED -->");

    // Todo, find out whether these are defaults for xlsx or whether they can be read somewhere
    writers->body->startElement("table:calculation-settings");
    writers->body->addAttribute("table:case-sensitive", "false");
    writers->body->addAttribute("table:automatic-find-labels", "false");
    writers->body->addAttribute("table:use-regular-expressions", "false");
    writers->body->addAttribute("table:use-wildcards", "true");
    writers->body->endElement(); // table:calculation-settings

    // 1. parse themes
    QList<QByteArray> partNames = this->partNames(d->mainDocumentContentType());

    // following is a workaround till the patch at https://bugs.freedesktop.org/show_bug.cgi?id=30417 is applied
    // so we are able to proper handle this case already before using the mimetype.
    if (partNames.isEmpty() && d->type != XlsxMacroDocument) {
        QList<QByteArray> macroPartNames = this->partNames(MSOOXML::ContentTypes::spreadsheetMacroDocument);
        if (macroPartNames.count() == 1 && acceptsSourceMimeType("application/vnd.ms-excel.sheet.macroEnabled")) {
            partNames = macroPartNames;
        }
    }

    if (partNames.count() != 1) {
        errorMessage = i18n("Unable to find part for type %1", d->mainDocumentContentType());
        return KoFilter::WrongFormat;
    }
    const QString spreadPathAndFile(partNames.first());
    QString spreadPath, spreadFile;
    MSOOXML::Utils::splitPathAndFile(spreadPathAndFile, &spreadPath, &spreadFile);

    MSOOXML::DrawingMLTheme themes;
    const QString spreadThemePathAndFile(
        relationships->targetForType(spreadPath, spreadFile, QLatin1String(MSOOXML::Schemas::officeDocument::relationships) + "/theme"));
    qCDebug(lcXlsxImport) << QLatin1String(MSOOXML::Schemas::officeDocument::relationships) + "/theme";

    QString spreadThemePath, spreadThemeFile;
    MSOOXML::Utils::splitPathAndFile(spreadThemePathAndFile, &spreadThemePath, &spreadThemeFile);

    MSOOXML::MsooXmlThemesReader themesReader(writers);
    MSOOXML::MsooXmlThemesReaderContext themecontext(themes, relationships, (MSOOXML::MsooXmlImport *)this, spreadThemePath, spreadThemeFile);

    KoFilter::ConversionStatus status = loadAndParseDocument(&themesReader, spreadThemePathAndFile, errorMessage, &themecontext);
    Q_UNUSED(status);
    reportProgress(5);

    // 2. parse styles
    XlsxStyles styles;
    XlsxXmlStylesReaderContext colorContext(styles, true, this, &themes);
    {
        // In first round we read color overrides, in 2nd round we can actually use them.
        XlsxXmlStylesReader stylesReader(writers);
        RETURN_IF_ERROR(loadAndParseDocumentIfExists(MSOOXML::ContentTypes::spreadsheetStyles, &stylesReader, writers, errorMessage, &colorContext))
        reportProgress(15);
        XlsxXmlStylesReaderContext context2(styles, false, this, &themes);
        context2.colorIndices = colorContext.colorIndices; // Overriding default colors potentially
        RETURN_IF_ERROR(loadAndParseDocumentIfExists(MSOOXML::ContentTypes::spreadsheetStyles, &stylesReader, writers, errorMessage, &context2))
    }

    reportProgress(30);

    // 3. parse shared strings
    QVector<QString> sharedStrings;
    {
        XlsxXmlSharedStringsReader sharedStringsReader(writers);
        XlsxXmlSharedStringsReaderContext context(sharedStrings, &themes, colorContext.colorIndices);
        RETURN_IF_ERROR(loadAndParseDocumentIfExists(MSOOXML::ContentTypes::spreadsheetSharedStrings, &sharedStringsReader, writers, errorMessage, &context))
    }

    reportProgress(35);

    // 3b. collect the sheet print areas, they are needed while the sheets are read
    {
        PrintAreaReader printAreaReader(writers);
        RETURN_IF_ERROR(loadAndParseDocument(d->mainDocumentContentType(), &printAreaReader, writers, errorMessage, nullptr))
        setPrintAreas(printAreaReader.printAreas);
        if (!printAreaReader.definedNames.isEmpty()) {
            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            KoXmlWriter namedWriter(&buffer);
            namedWriter.startElement("table:named-expressions");
            for (const auto &name : printAreaReader.definedNames) {
                namedWriter.startElement("table:named-range");
                namedWriter.addAttribute("table:name", name.first);
                namedWriter.addAttribute("table:base-cell-address", name.second.section(QLatin1Char(':'), 0, 0));
                namedWriter.addAttribute("table:cell-range-address", name.second);
                namedWriter.endElement();
            }
            namedWriter.endElement();
            setNamedExpressionsXml(QString::fromUtf8(buffer.data()));
        }
    }

    // 4. parse document; the comments parts are found through the sheet
    // relationships while the sheets are read
    reportProgress(40);
    XlsxComments comments;
    {
        XlsxXmlDocumentReaderContext context(*this, &themes, sharedStrings, comments, styles, colorContext.colorIndices, *relationships, "workbook.xml", "xl");
        XlsxXmlDocumentReader documentReader(writers);
        RETURN_IF_ERROR(loadAndParseDocument(d->mainDocumentContentType(), &documentReader, writers, errorMessage, &context))
    }

    reportProgress(100);

    // more here...
    return KoFilter::OK;
}

#include "XlsxImport.moc"
