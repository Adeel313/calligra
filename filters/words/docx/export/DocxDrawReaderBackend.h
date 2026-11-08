/* This file is part of the KDE project
   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#ifndef DOCXDRAWREADERBACKEND_H
#define DOCXDRAWREADERBACKEND_H

#include <OdfDrawReaderBackend.h>

#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QScopedPointer>
#include <QSizeF>
#include <QString>

class KoXmlWriter;
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
    void elementDrawTextBox(KoXmlStreamReader &reader, OdfReaderContext *context) override;

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
        //! Content already converted to WordprocessingML paragraphs and tables.
        //! Used for text boxes, whose contents can hold tables and formatting
        //! that the plain text member cannot express.
        QByteArray richText;
        QString fill;
        QString stroke;
        bool flipH = false;
        bool flipV = false;
    };

    //! Writes the text box of the enclosing draw:frame as a Word text box.
    void writeTextBoxDrawing(const QString &name);
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

    //! Converted content of the <draw:text-box> of the frame being read.
    QByteArray m_frameTextBoxContent;
    QString m_frameName;
    QString m_frameFill;
    QString m_frameStroke;
    //! True once the current frame produced a drawing, so a text box is only
    //! added for frames that have nothing else to show.
    bool m_frameDrewDrawing = false;
    //! Buffers the converted text box content while the text backend runs.
    QBuffer m_textBoxIO;
    QScopedPointer<KoXmlWriter> m_textBoxWriter;
    KoXmlWriter *m_suspendedWriter = nullptr;
    bool m_suspendedInsideParagraph = false;
    bool m_suspendedInsideRun = false;
};

#endif
