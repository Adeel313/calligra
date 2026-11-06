/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QCommandLineParser>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KZip>
#include <cstddef>
#include <cstdlib>

namespace
{
// The stable prefix of the public LibreOfficeKit C ABI. No unstable rendering
// API or LibreOffice headers are required. Run it in this disposable process,
// never in the application's GUI process. See include/LibreOfficeKit/LibreOfficeKit.h.
struct Office;
struct Document;
struct OfficeClass {
    size_t size;
    void (*destroy)(Office *);
    Document *(*load)(Office *, const char *);
    char *(*error)(Office *);
    Document *(*loadOptions)(Office *, const char *, const char *);
    void (*freeError)(char *);
};
struct DocumentClass {
    size_t size;
    void (*destroy)(Document *);
    int (*save)(Document *, const char *, const char *, const char *);
};
struct Office { OfficeClass *api; };
struct Document { DocumentClass *api; };

int fail(int code, const QString &message)
{
    QTextStream(stderr) << message << Qt::endl;
    return code;
}

QString family(const QString &format)
{
    if (format == QLatin1String("docx") || format == QLatin1String("odt")) return QStringLiteral("text");
    if (format == QLatin1String("xlsx") || format == QLatin1String("ods")) return QStringLiteral("spreadsheet");
    if (format == QLatin1String("pptx") || format == QLatin1String("odp")) return QStringLiteral("presentation");
    return {};
}

bool validPackage(const QString &file, const QString &format)
{
    KZip zip(file);
    if (!zip.open(QIODevice::ReadOnly)) return false;
    const bool ooxml = format.endsWith(QLatin1Char('x'));
    QString part = QStringLiteral("content.xml");
    QString rootName = QStringLiteral("document-content");
    QString xmlNamespace = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:office:1.0");
    QString strictNamespace;
    if (format == QLatin1String("docx")) {
        part = QStringLiteral("word/document.xml"); rootName = QStringLiteral("document");
        xmlNamespace = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
        strictNamespace = QStringLiteral("http://purl.oclc.org/ooxml/wordprocessingml/main");
    } else if (format == QLatin1String("xlsx")) {
        part = QStringLiteral("xl/workbook.xml"); rootName = QStringLiteral("workbook");
        xmlNamespace = QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
        strictNamespace = QStringLiteral("http://purl.oclc.org/ooxml/spreadsheetml/main");
    } else if (format == QLatin1String("pptx")) {
        part = QStringLiteral("ppt/presentation.xml"); rootName = QStringLiteral("presentation");
        xmlNamespace = QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main");
        strictNamespace = QStringLiteral("http://purl.oclc.org/ooxml/presentationml/main");
    }
    const auto *entry = dynamic_cast<const KArchiveFile *>(zip.directory()->entry(part));
    if (!entry) return false;
    QXmlStreamReader xml(entry->data());
    if (!xml.readNextStartElement() || xml.name() != rootName
        || (xml.namespaceUri() != xmlNamespace && (strictNamespace.isEmpty() || xml.namespaceUri() != strictNamespace))) return false;
    bool body = ooxml;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!ooxml && xml.isStartElement() && xml.namespaceUri() == xmlNamespace && xml.name() == family(format)) body = true;
    }
    if (xml.hasError() || !body) return false;
    if (ooxml) return zip.directory()->entry(QStringLiteral("[Content_Types].xml")) && zip.directory()->entry(QStringLiteral("_rels/.rels"));
    return zip.directory()->entry(QStringLiteral("META-INF/manifest.xml"));
}

bool rewritePackage(const QString &file, const QMap<QString, QByteArray> &updates)
{
    KZip input(file);
    if (!input.open(QIODevice::ReadOnly)) return false;
    const QString replacement = file + QStringLiteral(".normalized");
    KZip output(replacement);
    if (!output.open(QIODevice::WriteOnly)) return false;
    const auto copy = [&](const auto &self, const KArchiveDirectory *directory, const QString &prefix) -> bool {
        for (const QString &name : directory->entries()) {
            const auto *entry = directory->entry(name);
            const QString path = prefix + name;
            if (entry->isDirectory()) {
                if (!self(self, static_cast<const KArchiveDirectory *>(entry), path + QLatin1Char('/'))) return false;
            } else if (entry->isFile()) {
                const QByteArray bytes = updates.contains(path) ? updates.value(path) : static_cast<const KArchiveFile *>(entry)->data();
                if (!output.writeFile(path, bytes)) return false;
            }
        }
        return true;
    };
    if (!copy(copy, input.directory(), {}) || !output.close()) return false;
    input.close();
    return QFile::remove(file) && QFile::rename(replacement, file);
}


#include "PptxDiagramCompatibility.h"

#include "OdpTextCompatibility.h"

bool prepareConvertedOdpText(const QString &file)
{
    KZip archive(file);
    if (!archive.open(QIODevice::ReadOnly)) return false;
    const auto read = [&](const QString &name, QDomDocument &doc) {
        const auto *part = dynamic_cast<const KArchiveFile *>(archive.directory()->entry(name));
        return part && doc.setContent(part->data(), QDomDocument::ParseOptions(QDomDocument::ParseOption::UseNamespaceProcessing)
                                                 | QDomDocument::ParseOption::PreserveSpacingOnlyNodes);
    };
    QDomDocument content, styles;
    if (!read(QStringLiteral("content.xml"), content) || !read(QStringLiteral("styles.xml"), styles)) return false;
    QMap<QString, QByteArray> updates;
    if (normalizeRectangularTextFrames(content, styles)) updates.insert(QStringLiteral("content.xml"), content.toByteArray());
    if (normalizeRectangularTextFrames(styles, content)) updates.insert(QStringLiteral("styles.xml"), styles.toByteArray());
    archive.close();
    return updates.isEmpty() || rewritePackage(file, updates);
}

bool prepareOdp(const QString &file)
{
    KZip input(file);
    if (!input.open(QIODevice::ReadOnly)) return false;
    const auto *content = dynamic_cast<const KArchiveFile *>(input.directory()->entry(QStringLiteral("content.xml")));
    if (!content) return false;
    QDomDocument document;
    if (!document.setContent(content->data(), QDomDocument::ParseOption::UseNamespaceProcessing)) return false;
    const QString textNamespace = QStringLiteral("urn:oasis:names:tc:opendocument:xmlns:text:1.0");
    // Impress loses custom-shape text in <text:a><text:span>...</text:span>
    // although this is valid ODF. Its own writer uses span outside the link.
    // Normalize that equivalent nesting only in the disposable export input.
    const auto links = document.elementsByTagNameNS(textNamespace, QStringLiteral("a"));
    bool changed = false;
    for (int index = links.size() - 1; index >= 0; --index) {
        auto link = links.at(index).toElement();
        bool spansOnly = link.hasChildNodes();
        for (auto child = link.firstChild(); !child.isNull(); child = child.nextSibling()) {
            if (!child.isElement() || child.namespaceURI() != textNamespace || child.localName() != QLatin1String("span")) spansOnly = false;
        }
        if (!spansOnly) continue;
        auto parent = link.parentNode();
        while (!link.firstChild().isNull()) {
            auto span = link.firstChild();
            auto innerLink = link.cloneNode(false);
            while (!span.firstChild().isNull()) innerLink.appendChild(span.firstChild());
            span.appendChild(innerLink);
            parent.insertBefore(span, link);
        }
        parent.removeChild(link);
        changed = true;
    }
    if (!changed) return true;
    input.close();
    return rewritePackage(file, {{QStringLiteral("content.xml"), document.toByteArray()}});
}

bool restoreMediaTiming(const QString &file)
{
    KZip input(file);
    if (!input.open(QIODevice::ReadOnly)) return false;
    const auto *slides = dynamic_cast<const KArchiveDirectory *>(input.directory()->entry(QStringLiteral("ppt/slides")));
    if (!slides) return false;
    const QString p = QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main");
    const QString a = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
    QMap<QString, QByteArray> updates;
    for (const QString &name : slides->entries()) {
        if (!name.endsWith(QLatin1String(".xml"))) continue;
        const auto *entry = dynamic_cast<const KArchiveFile *>(slides->entry(name));
        if (!entry) continue;
        QDomDocument doc;
        if (!doc.setContent(entry->data(), QDomDocument::ParseOption::UseNamespaceProcessing)) return false;
        // LibreOffice omits the default media timing tree when exporting via
        // ODF. Recreate its indefinite, click-to-play nodes for embedded media.
        // Existing animation/timing trees remain authoritative.
        if (!doc.elementsByTagNameNS(p, QStringLiteral("timing")).isEmpty()) continue;
        const auto pictures = doc.elementsByTagNameNS(p, QStringLiteral("pic"));
        QDomElement children;
        int nextId = 1;
        const auto node = [&](QDomNode parent, const QString &local) {
            auto element = doc.createElementNS(p, QStringLiteral("p:") + local);
            parent.appendChild(element);
            return element;
        };
        for (int index = 0; index < pictures.size(); ++index) {
            auto pic = pictures.at(index).toElement();
            const bool video = !pic.elementsByTagNameNS(a, QStringLiteral("videoFile")).isEmpty();
            const bool audio = !pic.elementsByTagNameNS(a, QStringLiteral("audioFile")).isEmpty();
            if (!video && !audio) continue;
            const auto ids = pic.elementsByTagNameNS(p, QStringLiteral("cNvPr"));
            if (ids.isEmpty()) continue;
            if (children.isNull()) {
                auto timing = doc.createElementNS(p, QStringLiteral("p:timing"));
                auto root = doc.documentElement();
                root.insertBefore(timing, root.firstChildElement(QStringLiteral("p:extLst")));
                auto container = node(node(node(timing, QStringLiteral("tnLst")), QStringLiteral("par")), QStringLiteral("cTn"));
                container.setAttribute(QStringLiteral("id"), nextId++);
                container.setAttribute(QStringLiteral("dur"), QStringLiteral("indefinite"));
                container.setAttribute(QStringLiteral("restart"), QStringLiteral("never"));
                container.setAttribute(QStringLiteral("nodeType"), QStringLiteral("tmRoot"));
                children = node(container, QStringLiteral("childTnLst"));
            }
            auto media = node(node(children, video ? QStringLiteral("video") : QStringLiteral("audio")), QStringLiteral("cMediaNode"));
            auto time = node(media, QStringLiteral("cTn"));
            time.setAttribute(QStringLiteral("id"), nextId++);
            node(node(time, QStringLiteral("stCondLst")), QStringLiteral("cond")).setAttribute(QStringLiteral("delay"), QStringLiteral("indefinite"));
            node(node(media, QStringLiteral("tgtEl")), QStringLiteral("spTgt")).setAttribute(QStringLiteral("spid"), ids.at(0).toElement().attribute(QStringLiteral("id")));
        }
        if (!children.isNull()) updates.insert(QStringLiteral("ppt/slides/") + name, doc.toByteArray());
    }
    input.close();
    return updates.isEmpty() || rewritePackage(file, updates);
}

QStringList programDirectories(const QString &overrideDir)
{
    if (!overrideDir.isEmpty()) {
        return {overrideDir};
    }
    QStringList paths;
    const QString configured = qEnvironmentVariable("CALLIGRA_LIBREOFFICE_PROGRAM");
    if (!configured.isEmpty()) {
        return {configured};
    }
    for (const QString &name : {QStringLiteral("libreoffice"), QStringLiteral("soffice")}) {
        const QString executable = QStandardPaths::findExecutable(name);
        if (!executable.isEmpty()) {
            paths.append(QFileInfo(executable).canonicalPath());
        }
    }
    paths << QStringLiteral("/usr/lib/libreoffice/program") << QStringLiteral("/usr/lib64/libreoffice/program")
          << QStringLiteral("/opt/libreoffice/program") << QStringLiteral("/Applications/LibreOffice.app/Contents/MacOS");
#ifdef Q_OS_WIN
    paths << qEnvironmentVariable("ProgramFiles") + QStringLiteral("/LibreOffice/program");
#endif
    paths.removeDuplicates();
    return paths;
}

#include "OnlyOfficeConversion.h"

int convertKit(const QStringList &directories, const QString &profile, const QString &input, const QString &output, const QString &format)
{
    for (const QString &directory : directories) {
        for (const QString &library : {QStringLiteral("libmergedlo"), QStringLiteral("libsofficeapp"), QStringLiteral("sofficeapp")}) {
            QLibrary engine(QDir(directory).filePath(library));
            engine.setLoadHints(QLibrary::PreventUnloadHint);
            using Hook = Office *(*)(const char *, const char *);
            const auto hook = reinterpret_cast<Hook>(engine.resolve("libreofficekit_hook_2"));
            if (!hook) {
                continue;
            }
            Office *office = hook(QFile::encodeName(directory).constData(), QUrl::fromLocalFile(profile).toEncoded().constData());
            if (!office) {
                continue;
            }
            if (!office->api || office->api->size < sizeof(OfficeClass)) {
                return fail(4, QStringLiteral("The installed LibreOfficeKit ABI is not supported."));
            }
            Document *document = office->api->load(office, QUrl::fromLocalFile(input).toEncoded().constData());
            if (!document || !document->api || document->api->size < sizeof(DocumentClass)) {
                char *error = office->api->error(office);
                const QString message = error ? QString::fromUtf8(error) : QStringLiteral("LibreOffice could not open the document.");
                if (error) office->api->freeError(error);
                office->api->destroy(office);
                return fail(4, message);
            }
            const bool saved = document->api->save(document, QUrl::fromLocalFile(output).toEncoded().constData(), format.toLatin1().constData(), nullptr);
            QString message;
            if (!saved) {
                char *error = office->api->error(office);
                message = error ? QString::fromUtf8(error) : QStringLiteral("LibreOffice could not save the document.");
                if (error) office->api->freeError(error);
            }
            // This is a disposable process. Some LibreOffice media/image
            // backends deadlock while disposing documents, even after a
            // successful save. Release the process after committing output
            // instead of running engine teardown in the caller's lifetime.
            return saved ? 0 : fail(4, message);
        }
    }
    return 78;
}

int convertCli(const QStringList &directories, const QString &profile, const QString &input, const QString &outputDir, const QString &format)
{
    for (const QString &directory : directories) {
        const QString executable = QDir(directory).filePath(QStringLiteral("soffice"));
        if (!QFileInfo(executable).isExecutable()) {
            continue;
        }
        QProcess process;
        const QMap<QString, QString> filters = {
            {QStringLiteral("pptx"), QStringLiteral("pptx:Impress MS PowerPoint 2007 XML")},
            {QStringLiteral("odp"), QStringLiteral("odp:impress8")},
            {QStringLiteral("docx"), QStringLiteral("docx:Office Open XML Text")},
            {QStringLiteral("odt"), QStringLiteral("odt:writer8")},
            {QStringLiteral("xlsx"), QStringLiteral("xlsx:Calc MS Excel 2007 XML")},
            {QStringLiteral("ods"), QStringLiteral("ods:calc8")}};
        const QString filter = filters.value(format);
        process.start(executable, {QStringLiteral("-env:UserInstallation=") + QUrl::fromLocalFile(profile).toString(),
                                  QStringLiteral("--headless"), QStringLiteral("--norestore"), QStringLiteral("--nodefault"),
                                  QStringLiteral("--nofirststartwizard"), QStringLiteral("--convert-to"), filter,
                                  QStringLiteral("--outdir"), outputDir, input});
        if (!process.waitForStarted(5000)) continue;
        if (!process.waitForFinished(120000)) {
            process.kill();
            process.waitForFinished(5000);
            return fail(4, QStringLiteral("LibreOffice conversion timed out."));
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            return fail(4, QString::fromUtf8(process.readAllStandardError()).trimmed());
        }
        return 0;
    }
    return 78;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("calligraofficeconverter"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Isolated Office/OpenDocument converter. Supports LibreOffice and ONLYOFFICE x2t."));
    parser.addHelpOption();
    for (const QString &name : {QStringLiteral("input"), QStringLiteral("output"), QStringLiteral("from"), QStringLiteral("to"),
                                QStringLiteral("engine"), QStringLiteral("program-dir"), QStringLiteral("onlyoffice-x2t")}) {
        parser.addOption(QCommandLineOption(name, name, QStringLiteral("value")));
    }
    parser.process(app);
    const QString from = parser.value(QStringLiteral("from"));
    const QString to = parser.value(QStringLiteral("to"));
    const QString input = QFileInfo(parser.value(QStringLiteral("input"))).absoluteFilePath();
    const QString output = QFileInfo(parser.value(QStringLiteral("output"))).absoluteFilePath();
    QString selectedEngine = parser.value(QStringLiteral("engine"));
    if (selectedEngine.isEmpty()) {
        selectedEngine = family(from) == QLatin1String("presentation") ? qEnvironmentVariable("CALLIGRA_PPTX_BACKEND") : QString();
        if (selectedEngine.isEmpty()) selectedEngine = qEnvironmentVariable("CALLIGRA_OFFICE_BACKEND", "auto");
    }
    if (!parser.isSet(QStringLiteral("input")) || !parser.isSet(QStringLiteral("output"))
        || family(from).isEmpty() || family(from) != family(to)
        || (!selectedEngine.isEmpty() && selectedEngine != QLatin1String("auto") && selectedEngine != QLatin1String("kit") && selectedEngine != QLatin1String("cli") && selectedEngine != QLatin1String("libreoffice") && selectedEngine != QLatin1String("onlyoffice"))) {
        return fail(2, QStringLiteral("Specify --input, --output, --from and --to using matching odt/docx, ods/xlsx or odp/pptx formats, and --engine auto, libreoffice, kit, cli or onlyoffice."));
    }
    if (!validPackage(input, from)) {
        return fail(3, QStringLiteral("Invalid or damaged document package: ") + input);
    }
    // Check the destination before starting an expensive conversion. QSaveFile
    // keeps an existing document intact until the final commit.
    QSaveFile target(output);
    if (!target.open(QIODevice::WriteOnly))
        return fail(5, QStringLiteral("Could not write the converted document: ") + output);
    QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/calligra-office-XXXXXX"));
    if (!temporary.isValid()) return fail(5, QStringLiteral("Could not create the conversion workspace."));
    const QString profile = temporary.filePath(QStringLiteral("profile"));
    const QString source = temporary.filePath(QStringLiteral("source/document.") + from);
    const QString destinationDir = temporary.filePath(QStringLiteral("output"));
    QDir().mkpath(QFileInfo(source).absolutePath());
    QDir().mkpath(destinationDir);
    if (!QFile::copy(input, source)) return fail(5, QStringLiteral("Could not read the source document."));
    if (selectedEngine != QLatin1String("onlyoffice") && from == QLatin1String("pptx") && !preparePptxDiagrams(source)) return fail(3, QStringLiteral("Could not prepare the PowerPoint diagrams."));
    if (selectedEngine != QLatin1String("onlyoffice") && from == QLatin1String("odp") && !prepareOdp(source)) return fail(3, QStringLiteral("Could not prepare the OpenDocument presentation."));
    const QString converted = QDir(destinationDir).filePath(QStringLiteral("document.") + to);
    qputenv("SAL_USE_VCLPLUGIN", "svp");
    qputenv("GSETTINGS_BACKEND", "memory");
    qputenv("SAL_DISABLE_OPENCL", "1");
    const QStringList directories = programDirectories(parser.value(QStringLiteral("program-dir")));
    int result = 78;
    if (selectedEngine == QLatin1String("onlyoffice")) {
        result = convertOnlyOffice(parser.value(QStringLiteral("onlyoffice-x2t")), temporary.path(), source, converted, to);
    } else {
        if (selectedEngine != QLatin1String("cli")) result = convertKit(directories, profile, source, converted, to);
        if (result == 78 && selectedEngine != QLatin1String("kit")) result = convertCli(directories, profile, source, destinationDir, to);
        if (result == 78) return fail(78, QStringLiteral("LibreOffice is required for this conversion backend. Install LibreOffice or select --engine onlyoffice."));
    }
    if (result != 0) return result;
    if (selectedEngine != QLatin1String("onlyoffice") && to == QLatin1String("pptx") && !restoreMediaTiming(converted)) return fail(6, QStringLiteral("Could not preserve embedded media timing."));
    if (selectedEngine != QLatin1String("onlyoffice") && to == QLatin1String("odp") && !prepareConvertedOdpText(converted))
        return fail(6, QStringLiteral("Could not preserve fitted text boxes in the converted presentation."));
    if (!validPackage(converted, to)) return fail(6, QStringLiteral("The converter did not produce a valid document. The destination was not replaced."));
    QFile sourceFile(converted);
    if (!sourceFile.open(QIODevice::ReadOnly)) return fail(5, QStringLiteral("Could not write the converted document: ") + output);
    while (!sourceFile.atEnd()) {
        const QByteArray block = sourceFile.read(1024 * 1024);
        if ((block.isEmpty() && sourceFile.error() != QFileDevice::NoError) || target.write(block) != block.size()) {
            target.cancelWriting();
            return fail(5, QStringLiteral("Could not finish writing the converted document."));
        }
    }
    if (!target.commit()) return fail(5, QStringLiteral("Could not commit the converted document."));
    sourceFile.close();
    temporary.remove();
    std::_Exit(0);
}
