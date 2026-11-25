/* SPDX-License-Identifier: LGPL-2.1-only */
#include "MsooXmlImport.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class ImportProbe : public MSOOXML::MsooXmlImport
{
public:
    ImportProbe() : MsooXmlImport(QStringLiteral("text"), nullptr) {}
    using MsooXmlImport::createDocumentFromFile;

protected:
    bool acceptsSourceMimeType(const QByteArray &) const override { return true; }
    bool acceptsDestinationMimeType(const QByteArray &) const override { return true; }
    KoFilter::ConversionStatus parseParts(KoOdfWriters *, MSOOXML::MsooXmlRelationships *, QString &) override
    {
        return KoFilter::OK;
    }
};

class MsooXmlImportTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void invalidPackage_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("text-disguised-as-docx") << QByteArray("This is plain text.\r\n");
        QTest::newRow("empty-docx") << QByteArray();
        QTest::newRow("truncated-zip") << QByteArray::fromHex("504b0304140000000800");
    }

    void invalidPackage()
    {
        QFETCH(QByteArray, contents);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("invalid.docx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(contents), qint64(contents.size()));
        file.close();

        ImportProbe importer;
        QCOMPARE(importer.createDocumentFromFile(nullptr, nullptr, path), KoFilter::InvalidFormat);
    }

    void missingFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ImportProbe importer;
        QCOMPARE(importer.createDocumentFromFile(nullptr, nullptr, directory.filePath(QStringLiteral("missing.docx"))), KoFilter::FileNotFound);
    }
};

QTEST_GUILESS_MAIN(MsooXmlImportTest)
#include "MsooXmlImportTest.moc"
