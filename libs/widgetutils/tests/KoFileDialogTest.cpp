/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "KoFileDialog.h"

#include <QApplication>
#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class KoFileDialogTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void powerPointSaveFiltersUseCanonicalExtensions()
    {
        const QStringList formats = {
            QStringLiteral("application/vnd.ms-powerpoint"),
            QStringLiteral("application/vnd.openxmlformats-officedocument.presentationml.presentation"),
        };
        KoFileDialog dialog(nullptr, KoFileDialog::SaveFile, QString());
        dialog.setMimeTypeFilters(formats, formats.first());
        const QStringList filters = dialog.nameFilters();
        QCOMPARE(filters.size(), 3);
        QVERIFY(filters.at(0).endsWith(QStringLiteral(" (*.ppt)")));
        QVERIFY(filters.at(1).endsWith(QStringLiteral(" (*.pptx)")));
        QVERIFY(filters.at(0).section(QLatin1Char('('), 0, 0) != filters.at(1).section(QLatin1Char('('), 0, 0));
    }

    void saveKeepsSuggestedFilename()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString name = QStringLiteral("first-open.xlsx");
        const QString path = directory.filePath(name);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const QString xlsx = QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
        // Include Sheets' complete export list: a short list does not trigger
        // the native KDE dialog's loss of the initial filename.
        const QStringList formats = {
            QStringLiteral("application/vnd.oasis.opendocument.spreadsheet"),
            QStringLiteral("application/vnd.oasis.opendocument.spreadsheet-template"),
            QStringLiteral("application/x-kspread"), QStringLiteral("application/vnd.ms-excel"),
            QStringLiteral("text/csv"), QStringLiteral("text/plain"), xlsx,
            QStringLiteral("application/x-gnumeric"), QStringLiteral("application/vnd.sun.xml.calc"),
            QStringLiteral("text/html"), QStringLiteral("text/x-tex")};

        KoFileDialog dialog(nullptr, KoFileDialog::SaveFile, QString());
        dialog.setDefaultDir(path, true);
        dialog.setMimeTypeFilters(formats, xlsx);

        bool filenamePresent = false;
        QTimer::singleShot(500, [&] {
            // Inspect the actual filename input, including KDE's native helper
            // (whose selectedFiles() is empty until the dialog is accepted).
            for (QWidget *widget : QApplication::allWidgets()) {
                if (auto *edit = qobject_cast<QLineEdit *>(widget)) {
                    filenamePresent |= edit->text() == name || edit->text() == path;
                }
            }
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (auto *fileDialog = qobject_cast<QFileDialog *>(widget)) {
                    fileDialog->reject();
                }
            }
        });
        QVERIFY(dialog.filename().isEmpty());
        QVERIFY2(filenamePresent, "Save must retain the opened spreadsheet's filename");

        KoFileDialog openDialog(nullptr, KoFileDialog::OpenFile, QString());
        openDialog.setMimeTypeFilters(formats);
        QCOMPARE(openDialog.nameFilters().size(), dialog.nameFilters().size() + 1);
    }
};

QTEST_MAIN(KoFileDialogTest)
#include "KoFileDialogTest.moc"
