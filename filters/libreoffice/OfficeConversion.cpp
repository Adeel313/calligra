/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "OfficeConversion.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QLibrary>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <KLocalizedString>
#include <dlfcn.h>

namespace
{
// LibreOffice initialises NSS for document signatures while a document is
// loaded. When the user's NSS database registers a PKCS#11 module whose
// library constructor registers a GApplication - OpenSC does, and LibreOffice
// picks the module up from the Firefox profile - the GDBus worker thread it
// starts cannot finish while dlopen() holds the dynamic loader lock. Every
// conversion then waits for the 25 second D-Bus timeout before it even starts.
// Preloading GIO for the converter keeps that call out of the dlopen() window.
//
// A desktop session that preloads libgtk-nocsd.so.0 (GTK client-side
// decoration workaround) into every process defeats that: the converter is a
// headless helper and the preload drags GTK/GIO in before LibreOffice starts,
// which costs the full 25 second timeout again. Strip it from the child.
QString gioPreloadLibrary()
{
    static const QString path = [] {
        for (const QString &name : {QStringLiteral("gio-2.0"), QStringLiteral("libgio-2.0.so.0")}) {
            QLibrary gio(name);
            gio.setLoadHints(QLibrary::PreventUnloadHint);
            if (!gio.load()) {
                continue;
            }
            Dl_info info {};
            const auto symbol = gio.resolve("g_bus_get_sync");
            if (symbol && dladdr(reinterpret_cast<void *>(symbol), &info) && info.dli_fname) {
                return QFileInfo(QString::fromLocal8Bit(info.dli_fname)).canonicalFilePath();
            }
            return QString();
        }
        return QString();
    }();
    return path;
}
}

OfficeConversion::Result OfficeConversion::run(const QString &input, const QString &output, const QString &from, const QString &to)
{
    Result result;
    QString worker = QCoreApplication::applicationDirPath() + QStringLiteral("/calligraofficeconverter");
    if (!QFileInfo(worker).isExecutable()) {
        worker = QStandardPaths::findExecutable(QStringLiteral("calligraofficeconverter"));
    }
    if (worker.isEmpty()) {
        result.backendUnavailable = true;
        result.status = KoFilter::NotImplemented;
        result.error = i18n("The Office conversion helper is not installed. Install calligraofficeconverter and LibreOffice.");
        return result;
    }
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QStringList preload = environment.value(QStringLiteral("LD_PRELOAD")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    preload.removeIf([](const QString &entry) { return entry.contains(QLatin1String("nocsd")); });
    const QString gio = gioPreloadLibrary();
    if (!gio.isEmpty()) {
        preload.prepend(gio);
    }
    environment.insert(QStringLiteral("LD_PRELOAD"), preload.join(QLatin1Char(':')));
    process.setProcessEnvironment(environment);
    process.setProgram(worker);
    process.setArguments({QStringLiteral("--input"), input, QStringLiteral("--output"), output,
                          QStringLiteral("--from"), from, QStringLiteral("--to"), to});
    process.start();
    if (!process.waitForStarted(5000)) {
        result.backendUnavailable = true;
        result.status = KoFilter::CreationError;
        result.error = i18n("Could not start the Office converter: %1", process.errorString());
        return result;
    }
    QElapsedTimer timer;
    timer.start();
    QByteArray diagnostics;
    while (!process.waitForFinished(100)) {
        diagnostics += process.readAllStandardError();
        diagnostics = diagnostics.right(16384);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (timer.elapsed() > 150000) {
            process.kill();
            process.waitForFinished(5000);
            result.status = KoFilter::UserCancelled;
            result.error = i18n("Office conversion timed out. The destination file was not replaced.");
            return result;
        }
    }
    diagnostics += process.readAllStandardError();
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
        result.status = KoFilter::OK;
        return result;
    }
    result.backendUnavailable = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 78;
    result.status = result.backendUnavailable ? KoFilter::NotImplemented : KoFilter::ParsingError;
    result.error = QString::fromUtf8(diagnostics.right(16384)).trimmed();
    if (result.error.isEmpty()) {
        result.error = i18n("The Office converter could not convert this document.");
    }
    return result;
}
