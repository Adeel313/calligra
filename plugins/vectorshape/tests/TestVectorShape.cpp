/* SPDX-License-Identifier: LGPL-2.0-or-later */
#include "../VectorShape.h"

#include <KoColorBackground.h>
#include <KoShapePaintingContext.h>
#include <KoViewConverter.h>
#include <QImage>
#include <QPainter>
#include <QPicture>
#include <QTest>

class TestVectorShape : public QObject
{
    Q_OBJECT
private Q_SLOTS:
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
