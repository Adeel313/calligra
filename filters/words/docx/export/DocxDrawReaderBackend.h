/* This file is part of the KDE project
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef DOCXDRAWREADERBACKEND_H
#define DOCXDRAWREADERBACKEND_H

#include <OdfDrawReaderBackend.h>

#include <QHash>
#include <QByteArray>
#include <QSizeF>

class OdfReaderContext;
class OdfReaderDocxContext;

class DocxDrawReaderBackend : public OdfDrawReaderBackend
{
public:
    void setContext(OdfReaderDocxContext *context);

    void elementDrawFrame(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementDrawImage(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementDrawObject(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    bool hasDrawingError() const { return m_chartError; }
    void elementDrawPath(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementDrawLine(KoXmlStreamReader &reader, OdfReaderContext *context) override;
    void elementDrawRect(KoXmlStreamReader &reader, OdfReaderContext *context) override;

private:
    struct EmbeddedImage {
        QString target;
        QString relationshipId;
        QString name;
        qint64 widthEmu = 0;
        qint64 heightEmu = 0;
        qint64 offsetX = 0; //!< From frame position (in EMUs)
        qint64 offsetY = 0; //!< From frame position (in EMUs)
    };

    struct PresetShape {
        QString preset;
        QString text;
        QString fill;
        QString stroke;
        bool flipH = false;
        bool flipV = false;
    };
    void writePrimitive(KoXmlStreamReader &reader, const QString &preset, bool line);
    QHash<QString, PresetShape> m_presetShapes;
    void writeDrawing(const EmbeddedImage &drawing, bool chart = false, const QString &sourcePath = QString(), const PresetShape *shape = nullptr);
    OdfReaderDocxContext *m_context = nullptr;
    QHash<QString, QByteArray> m_chartData;
    QHash<QString, EmbeddedImage> m_charts;
    bool m_chartError = false;
    bool m_frameHasChart = false;
    int m_nextChartId = 1;
    QSizeF m_frameSize;
    qreal m_framePosX = 0.0; //!< Parsed from svg:x in the draw:frame
    qreal m_framePosY = 0.0; //!< Parsed from svg:y in the draw:frame
    bool m_frameFloating = false; //!< Anchored frames stay floating even at (0, 0)
    QString m_frameWrap;
    QString m_frameRunThrough;
    bool m_frameWrapContour = false;
    QString m_frameWrapContourMode;
    QString m_frameClip;
    int m_frameRotation = 0; //!< Clockwise rotation in 1/60000 degree units
    QHash<QString, EmbeddedImage> m_images;
    QHash<QString, QByteArray> m_imageData;
    int m_nextImageId = 1;
    int m_nextDrawingId = 1;
};

#endif
