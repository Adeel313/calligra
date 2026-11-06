/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextLayout>
#include <QJsonObject>
#include <QPixmap>
#include <QPdfWriter>
#include <QPageLayout>
#include <QPainter>
#include <KoZoomHandler.h>
#include <QTimer>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>
#include <KAboutData>
#include <KLocalizedString>
#include <KoDocumentEntry.h>
#include <KoFilterManager.h>
#include <KoGlobal.h>
#include <KoPADocument.h>
#include <KoPageLayout.h>
#include <KoPAPageBase.h>
#include <KoShapeContainer.h>
#include <KoTextShapeDataBase.h>
#include <KoPart.h>

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("calligrastage"));
    if (argc != 3 && argc != 4) return 2;
    KAboutData::setApplicationData(KAboutData(QStringLiteral("calligrastage"), QStringLiteral("Stage PPTX integration test"), QStringLiteral("1")));
    KLocalizedString::setApplicationDomain("calligrastage");
    KoGlobal::initialize();
    const QString output = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    QDir().mkpath(output);
    QJsonObject result;
    const QByteArray odp("application/vnd.oasis.opendocument.presentation");
    const QByteArray pptx("application/vnd.openxmlformats-officedocument.presentationml.presentation");
    result.insert(QStringLiteral("import_registered"), KoFilterManager::mimeFilter(odp, KoFilterManager::Import).contains(QString::fromLatin1(pptx)));
    result.insert(QStringLiteral("export_registered"), KoFilterManager::mimeFilter(odp, KoFilterManager::Export).contains(QString::fromLatin1(pptx)));
    QString error;
    KoPart *part = KoDocumentEntry::queryByMimeType(QString::fromLatin1(odp)).createKoPart(&error);
    int status = 0;
    if (!part) {
        result.insert(QStringLiteral("error"), error);
        status = 3;
    } else {
        auto *document = dynamic_cast<KoPADocument *>(part->document());
        if (!document) return 3;
        document->setAutoSave(0);
        document->setAutoErrorHandlingEnabled(false);
        document->setSaveInBatchMode(true);
        const bool opened = document->openUrl(QUrl::fromLocalFile(QFileInfo(QString::fromLocal8Bit(argv[1])).absoluteFilePath()));
        result.insert(QStringLiteral("opened"), opened);
        if (!opened) {
            result.insert(QStringLiteral("error"), document->errorMessage());
            status = 4;
        } else {
            const auto pages = document->pages();
            if (argc == 4 && QString::fromLocal8Bit(argv[3]) == QLatin1String("--edit")) {
                const auto edit = [&](const auto &self, KoShape *shape) -> bool {
                    if (auto *data = dynamic_cast<KoTextShapeDataBase *>(shape->userData())) {
                        if (!data->document()->isEmpty()) {
                            QTextCursor cursor(data->document());
                            cursor.movePosition(QTextCursor::End);
                            cursor.insertText(QStringLiteral(" STAGE_EDIT_SENTINEL"));
                            return true;
                        }
                    }
                    if (auto *container = dynamic_cast<KoShapeContainer *>(shape)) {
                        for (KoShape *child : container->shapes()) if (self(self, child)) return true;
                    }
                    return false;
                };
                result.insert(QStringLiteral("edited"), !pages.isEmpty() && edit(edit, pages.first()));
            }
            result.insert(QStringLiteral("slides"), pages.size());
            for (auto *page : pages) document->pageThumbnail(page, QSize(960, 720));
            QEventLoop images;
            QTimer::singleShot(300, &images, &QEventLoop::quit);
            images.exec();
            for (int index = 0; index < pages.size(); ++index) {
                document->pageThumbnail(pages[index], QSize(960, 720)).save(output + QStringLiteral("/stage-%1.png").arg(index + 1));
            }
            if (qEnvironmentVariableIsSet("CALLIGRA_STAGE_TEST_PDF")) {
                QPdfWriter pdf(output + QStringLiteral("/native-stage.pdf"));
                pdf.setResolution(72);
                const auto layout = pages.first()->pageLayout();
                pdf.setPageLayout(QPageLayout(QPageSize(QSizeF(layout.width, layout.height), QPageSize::Point),
                    QPageLayout::Portrait, QMarginsF(0, 0, 0, 0), QPageLayout::Point));
                QPainter painter(&pdf);
                KoZoomHandler zoom;
                zoom.setDpi(72, 72);
                zoom.setZoom(1);
                for (int index = 0; index < pages.size(); ++index) {
                    if (index) pdf.newPage();
                    pages[index]->paintPage(painter, zoom);
                }
            }
            QJsonArray textLayout;
            for (int index = 0; index < pages.size(); ++index) {
                const auto inspect = [&](const auto &self, KoShape *shape) -> void {
                    if (auto *data = dynamic_cast<KoTextShapeDataBase *>(shape->userData())) {
                        const auto block = data->document()->firstBlock();
                        QJsonObject entry;
                        entry.insert(QStringLiteral("slide"), index + 1);
                        QString preview = data->document()->toPlainText().left(100);
                        if (!preview.isEmpty() && preview.back().isHighSurrogate()) preview.chop(1);
                        entry.insert(QStringLiteral("text"), preview);
                        entry.insert(QStringLiteral("scale"), shape->transformation().m11());
                        entry.insert(QStringLiteral("font_scale"), data->autofitFontScale());
                        entry.insert(QStringLiteral("spacing_reduction"), data->autofitLineSpacingReduction());
                        if (!block.begin().atEnd()) entry.insert(QStringLiteral("font_points"), block.begin().fragment().charFormat().fontPointSize());
                        if (block.layout()->lineCount() > 1) entry.insert(QStringLiteral("line_distance"), block.layout()->lineAt(1).y() - block.layout()->lineAt(0).y());
                        textLayout.append(entry);
                    }
                    if (auto *container = dynamic_cast<KoShapeContainer *>(shape))
                        for (KoShape *child : container->shapes()) self(self, child);
                };
                for (KoShape *shape : pages[index]->shapes()) inspect(inspect, shape);
            }
            result.insert(QStringLiteral("text_layout"), textLayout);
            document->setOutputMimeType(odp);
            const bool savedOdp = document->exportDocument(QUrl::fromLocalFile(output + QStringLiteral("/stage.odp")));
            document->setOutputMimeType(pptx);
            const bool savedPptx = document->exportDocument(QUrl::fromLocalFile(output + QStringLiteral("/stage.pptx")));
            result.insert(QStringLiteral("odp_saved"), savedOdp);
            result.insert(QStringLiteral("pptx_saved"), savedPptx);
            if (!savedOdp || !savedPptx) {
                result.insert(QStringLiteral("error"), document->errorMessage());
                status = 5;
            }
        }
        delete part;
    }
    QFile report(output + QStringLiteral("/stage-result.json"));
    if (report.open(QIODevice::WriteOnly)) report.write(QJsonDocument(result).toJson());
    return status;
}
