/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <KAboutData>
#include <KLocalizedString>
#include <KoDocument.h>
#include <KoDocumentEntry.h>
#include <KoFilterManager.h>
#include <KoGlobal.h>
#include <KoPart.h>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (argc != 4) return 2;
    const QString format = QString::fromLocal8Bit(argv[1]);
    const bool words = format == QLatin1String("docx");
    if (!words && format != QLatin1String("xlsx")) return 2;
    const QString name = words ? QStringLiteral("calligrawords") : QStringLiteral("calligrasheets");
    app.setApplicationName(name);
    KAboutData::setApplicationData(KAboutData(name, name, QStringLiteral("1")));
    KLocalizedString::setApplicationDomain(name.toUtf8().constData());
    KoGlobal::initialize();
    const QByteArray native = words ? QByteArray("application/vnd.oasis.opendocument.text") : QByteArray("application/vnd.oasis.opendocument.spreadsheet");
    const QByteArray office = words ? QByteArray("application/vnd.openxmlformats-officedocument.wordprocessingml.document") : QByteArray("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    const QString output = QFileInfo(QString::fromLocal8Bit(argv[3])).absoluteFilePath();
    QDir().mkpath(output);
    QJsonObject result;
    result.insert(QStringLiteral("import_registered"), KoFilterManager::mimeFilter(native, KoFilterManager::Import).contains(QString::fromLatin1(office)));
    result.insert(QStringLiteral("export_registered"), KoFilterManager::mimeFilter(native, KoFilterManager::Export).contains(QString::fromLatin1(office)));
    QString error;
    KoPart *part = KoDocumentEntry::queryByMimeType(QString::fromLatin1(native)).createKoPart(&error);
    int status = 3;
    if (part) {
        auto *document = part->document();
        document->setAutoSave(0);
        document->setAutoErrorHandlingEnabled(false);
        document->setSaveInBatchMode(true);
        const bool opened = document->openUrl(QUrl::fromLocalFile(QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath()));
        result.insert(QStringLiteral("opened"), opened);
        status = 4;
        if (opened) {
            QEventLoop pending;
            QTimer::singleShot(500, &pending, &QEventLoop::quit);
            pending.exec();
            document->setOutputMimeType(native);
            const bool nativeSaved = document->exportDocument(QUrl::fromLocalFile(output + (words ? QStringLiteral("/calligra.odt") : QStringLiteral("/calligra.ods"))));
            document->setOutputMimeType(office);
            const bool officeSaved = document->exportDocument(QUrl::fromLocalFile(output + QStringLiteral("/calligra.") + format));
            result.insert(QStringLiteral("native_saved"), nativeSaved);
            result.insert(QStringLiteral("office_saved"), officeSaved);
            status = nativeSaved && officeSaved ? 0 : 5;
        }
        error = document->errorMessage();
        delete part;
    }
    result.insert(QStringLiteral("error"), error);
    QFile report(output + QStringLiteral("/result.json"));
    if (report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(result).toJson());
    return status;
}
