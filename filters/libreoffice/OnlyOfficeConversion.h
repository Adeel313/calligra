/* SPDX-License-Identifier: LGPL-2.0-or-later */
// The x2t process is an optional, independently installed conversion backend.
QString onlyOfficeExecutable(const QString &configured)
{
    QString override = configured;
    if (override.isEmpty()) override = qEnvironmentVariable("CALLIGRA_ONLYOFFICE_X2T");
    if (!override.isEmpty()) {
        const QFileInfo info(override);
        return info.isFile() && info.isExecutable() ? info.canonicalFilePath() : QString();
    }
    QStringList candidates;
    candidates << QStandardPaths::findExecutable(QStringLiteral("x2t"))
               << QStringLiteral("/opt/onlyoffice/desktopeditors/converter/x2t")
               << QStringLiteral("/usr/share/onlyoffice/desktopeditors/converter/x2t")
               << QStringLiteral("/var/www/onlyoffice/documentserver/server/FileConverter/bin/x2t");
    for (const auto &path : candidates) {
        const QFileInfo info(path);
        if (!path.isEmpty() && info.isFile() && info.isExecutable()) return info.canonicalFilePath();
    }
    return {};
}

int convertOnlyOffice(const QString &configured, const QString &workspace, const QString &input, const QString &output, const QString &format)
{
    const QString executable = onlyOfficeExecutable(configured);
    if (executable.isEmpty())
        return fail(78, QStringLiteral("ONLYOFFICE x2t is not available. Install ONLYOFFICE Desktop Editors or Document Server, or set CALLIGRA_ONLYOFFICE_X2T to the x2t executable."));
    // Codes from ONLYOFFICE/core Common/OfficeFileFormats.h. Keep the XML
    // protocol out of the GUI process and let x2t do its native PPTX/ODP work.
    const QMap<QString, int> formats = {{QStringLiteral("pptx"), 129}, {QStringLiteral("odp"), 131},
        {QStringLiteral("docx"), 65}, {QStringLiteral("odt"), 67}, {QStringLiteral("xlsx"), 257}, {QStringLiteral("ods"), 259}};
    QDomDocument task;
    auto root = task.createElement(QStringLiteral("TaskQueueDataConvert"));
    task.appendChild(root);
    const auto parameter = [&](const QString &name, const QString &value) {
        auto node = task.createElement(name);
        node.appendChild(task.createTextNode(value)); root.appendChild(node);
    };
    parameter(QStringLiteral("m_sFileFrom"), input);
    parameter(QStringLiteral("m_sFileTo"), output);
    parameter(QStringLiteral("m_nFormatTo"), QString::number(formats.value(format)));
    parameter(QStringLiteral("m_bIsNoBase64"), QStringLiteral("true"));
    const QString scratch = QDir(workspace).filePath(QStringLiteral("x2t-work"));
    if (!QDir().mkpath(scratch)) return fail(5, QStringLiteral("Could not create the ONLYOFFICE workspace."));
    parameter(QStringLiteral("m_sTempDir"), scratch);
    const QString fontDir = qEnvironmentVariable("CALLIGRA_ONLYOFFICE_FONT_DIR");
    if (!fontDir.isEmpty()) parameter(QStringLiteral("m_sFontDir"), fontDir);
    const QDir engineDir(QFileInfo(executable).absolutePath());
    const QString fonts = engineDir.filePath(QStringLiteral("AllFonts.js"));
    if (QFileInfo::exists(fonts)) parameter(QStringLiteral("m_sAllFontsPath"), fonts);
    for (const QString &relative : {QStringLiteral("../editors/sdkjs/slide/themes"), QStringLiteral("../../../sdkjs/slide/themes")}) {
        const QString themes = engineDir.absoluteFilePath(relative);
        if (QFileInfo(themes).isDir()) { parameter(QStringLiteral("m_sThemeDir"), QDir::cleanPath(themes)); break; }
    }
    const QString taskPath = QDir(workspace).filePath(QStringLiteral("onlyoffice-task.xml"));
    QFile taskFile(taskPath);
    const QByteArray taskBytes = task.toByteArray();
    if (!taskFile.open(QIODevice::WriteOnly) || taskFile.write(taskBytes) != taskBytes.size())
        return fail(5, QStringLiteral("Could not write the ONLYOFFICE conversion task."));
    taskFile.close();
    QProcess process;
    process.setWorkingDirectory(engineDir.absolutePath());
    auto environment = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_LINUX
    const QString libraryPath = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
    environment.insert(QStringLiteral("LD_LIBRARY_PATH"), engineDir.absolutePath() + (libraryPath.isEmpty() ? QString() : QLatin1Char(':') + libraryPath));
#endif
    process.setProcessEnvironment(environment);
    process.start(executable, {taskPath});
    if (!process.waitForStarted(5000)) return fail(4, QStringLiteral("Could not start ONLYOFFICE x2t: ") + process.errorString());
    QByteArray diagnostics;
    QElapsedTimer timer; timer.start();
    while (!process.waitForFinished(100)) {
        diagnostics += process.readAllStandardError();
        diagnostics += process.readAllStandardOutput();
        diagnostics = diagnostics.right(16384);
        if (timer.elapsed() > 120000) {
            process.kill(); process.waitForFinished(5000);
            return fail(4, QStringLiteral("ONLYOFFICE conversion timed out. The destination was not replaced."));
        }
    }
    diagnostics += process.readAllStandardError(); diagnostics += process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return fail(4, QStringLiteral("ONLYOFFICE conversion failed (exit %1): %2").arg(process.exitCode()).arg(QString::fromUtf8(diagnostics.right(16384)).trimmed()));
    return 0; // The shared caller validates the package before atomic commit.
}
