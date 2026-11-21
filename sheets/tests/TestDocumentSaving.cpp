// SPDX-License-Identifier: LGPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <KoStore.h>
#include <memory>
#include "core/CellStorage.h"
#include "core/Map.h"
#include "core/Sheet.h"
#include "part/Doc.h"
#include "part/Part.h"

using namespace Calligra::Sheets;

class TestDocumentSaving : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void mislabeledOds_data();
    void mislabeledOds();
};

void TestDocumentSaving::mislabeledOds_data()
{
    QTest::addColumn<QString>("extension");
    QTest::addColumn<QByteArray>("expectedMime");
    QTest::newRow("ordinary-ods") << QStringLiteral("ods") << QByteArray("application/vnd.oasis.opendocument.spreadsheet");
    QTest::newRow("ods-named-xlsx") << QStringLiteral("xlsx") << QByteArray("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
}

void TestDocumentSaving::mislabeledOds()
{
    QFETCH(QString, extension);
    QFETCH(QByteArray, expectedMime);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const QString path = temporary.filePath(QStringLiteral("workbook.") + extension);
    {
        std::unique_ptr<KoStore> package(KoStore::createStore(path, KoStore::Write,
            QByteArray("application/vnd.oasis.opendocument.spreadsheet"), KoStore::Zip));
        QVERIFY(package && !package->bad());
        const QByteArray content(R"(<office:document-content
xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"
xmlns:table="urn:oasis:names:tc:opendocument:xmlns:table:1.0" office:version="1.2">
<office:body><office:spreadsheet><table:table table:name="Data">
<table:table-row><table:table-cell office:value-type="float" office:value="1"/>
</table:table-row></table:table></office:spreadsheet></office:body></office:document-content>)");
        QVERIFY(package->open(QStringLiteral("content.xml")));
        QCOMPARE(package->write(content), qint64(content.size()));
        QVERIFY(package->close());
        const QByteArray manifest(R"(<manifest:manifest xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0">
<manifest:file-entry manifest:full-path="/" manifest:media-type="application/vnd.oasis.opendocument.spreadsheet"/>
<manifest:file-entry manifest:full-path="content.xml" manifest:media-type="text/xml"/>
</manifest:manifest>)");
        QVERIFY(package->open(QStringLiteral("META-INF/manifest.xml")));
        QCOMPARE(package->write(manifest), qint64(manifest.size()));
        QVERIFY(package->close());
    }
    Part part;
    Doc document(&part);
    part.setDocument(&document);
    document.setSaveInBatchMode(true);
    document.setCheckAutoSaveFile(false);
    document.setBackupFile(false);
    QVERIFY(document.openUrl(QUrl::fromLocalFile(path)));
    QCOMPARE(document.outputMimeType(), expectedMime);
    auto *sheet = static_cast<Sheet *>(document.map()->sheet(0));
    sheet->fullCellStorage()->setValue(1, 1, Value(42));
    document.setModified(true);
    QVERIFY(document.save());
    std::unique_ptr<KoStore> package(KoStore::createStore(path, KoStore::Read));
    QVERIFY(package);
    QVERIFY(!package->bad());
    QCOMPARE(package->hasFile(QStringLiteral("[Content_Types].xml")), extension == QLatin1String("xlsx"));
    QCOMPARE(package->hasFile(QStringLiteral("content.xml")), extension == QLatin1String("ods"));
}

QTEST_MAIN(TestDocumentSaving)
#include "TestDocumentSaving.moc"
