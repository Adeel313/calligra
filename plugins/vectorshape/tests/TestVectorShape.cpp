/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "../VectorShape.h"

#include <KoColorBackground.h>
#include <KoShapePaintingContext.h>
#include <KoViewConverter.h>
#include <QImage>
#include <QDataStream>
#include <QPainter>
#include <QPicture>
#include <QTest>

class TestVectorShape : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void readingMode()
    {
        // Minimal WMF containing a thin black rule in a 100 x 100 window.
        QByteArray wmf;
        QDataStream stream(&wmf, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);
        stream << quint16(1) << quint16(9) << quint16(0x300) << quint32(38) << quint16(1) << quint32(7) << quint16(0);
        stream << quint32(5) << quint16(0x20c) << qint16(100) << qint16(100);
        stream << quint32(7) << quint16(0x2fc) << quint16(0) << quint32(0) << quint16(0);
        stream << quint32(4) << quint16(0x12d) << quint16(0);
        stream << quint32(7) << quint16(0x41b) << qint16(80) << qint16(51) << qint16(20) << qint16(49);
        stream << quint32(3) << quint16(0);

        VectorShape shape;
        shape.setSize(QSizeF(100, 100));
        shape.setCompressedContents(qCompress(wmf), VectorShape::VectorTypeWmf);
        KoViewConverter converter;
        bool preservedState = true;
        auto render = [&](bool enhanced, bool vectorOutput) {
            KoShapePaintingContext context;
            context.enhanceMetafileReadability = enhanced;
            QImage image(160, 160, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.translate(20, 20);
            const auto transform = painter.transform();
            const auto hints = painter.renderHints();
            if (vectorOutput) {
                QPicture picture;
                QPainter recorder(&picture);
                shape.paint(recorder, converter, context);
                recorder.end();
                painter.drawPicture(0, 0, picture);
            } else {
                shape.paint(painter, converter, context);
            }
            preservedState &= painter.transform() == transform && painter.renderHints() == hints;
            painter.end();
            return image;
        };
        const QImage normal = render(false, false);
        const QImage enhanced = render(true, false);
        QVERIFY(normal != enhanced);
        auto ink = [](const QImage &image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    count += qGray(image.pixel(x, y)) < 128;
                }
            }
            return count;
        };
        QVERIFY(ink(normal) > 100);
        // A 13% enlargement alone multiplies area by 1.2769. Extra ink
        // beyond that verifies stroke strengthening as well as magnification.
        QVERIFY(ink(enhanced) > ink(normal) * 1.28);
        QCOMPARE(render(false, true), render(true, true));
        QVERIFY(preservedState);
    }

    void frameFill_data()
    {
        QTest::addColumn<bool>("vectorOutput");
        QTest::addColumn<bool>("filled");
        QTest::newRow("screen-filled") << false << true;
        QTest::newRow("screen-transparent") << false << false;
        QTest::newRow("print-filled") << true << true;
        QTest::newRow("print-transparent") << true << false;
    }

    void frameFill()
    {
        QFETCH(bool, vectorOutput);
        QFETCH(bool, filled);
        VectorShape shape;
        shape.setSize(QSizeF(40, 20));
        shape.setCompressedContents(qCompress(QByteArrayLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' width='40' height='20'>"
            "<rect x='15' y='5' width='10' height='10' fill='black'/></svg>")), VectorShape::VectorTypeSvg);
        if (filled) {
            shape.setBackground(QSharedPointer<KoColorBackground>::create(Qt::white));
        }
        KoViewConverter converter;
        converter.setZoom(2);
        KoShapePaintingContext context;
        shape.waitUntilReady(converter, false);

        QImage image(100, 60, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::darkGray);
        QPainter painter(&image);
        painter.setClipRect(image.rect());
        if (vectorOutput) {
            QPicture picture;
            QPainter recorder(&picture);
            shape.paint(recorder, converter, context);
            recorder.end();
            painter.drawPicture(0, 0, picture);
        } else {
            shape.paint(painter, converter, context);
        }
        painter.end();
        QCOMPARE(image.pixelColor(4, 4), QColor(filled ? Qt::white : Qt::darkGray));
        QCOMPARE(image.pixelColor(40, 20), QColor(Qt::black));
        QCOMPARE(image.pixelColor(90, 50), QColor(Qt::darkGray));
    }
};

QTEST_MAIN(TestVectorShape)
#include "TestVectorShape.moc"
