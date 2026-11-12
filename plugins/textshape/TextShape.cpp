/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2006-2010 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2008-2010 Thorsten Zachmann <zachmann@kde.org>
 * SPDX-FileCopyrightText: 2008 Pierre Stirnweiss \pierre.stirnweiss_calligra@gadz.org>
 * SPDX-FileCopyrightText: 2010 KO GmbH <cbo@kogmbh.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "TextShape.h"
#include "SimpleRootAreaProvider.h"
#include <KoTextSharedLoadingData.h>

#include <FrameIterator.h>
#include <KoTextEditor.h>
#include <KoTextLayoutRootArea.h>

#include <KoCanvasBase.h>
#include <KoCanvasResourceManager.h>
#include <KoChangeTracker.h>
#include <KoInlineTextObjectManager.h>
#include <KoOdfLoadingContext.h>
#include <KoOdfWorkaround.h>
#include <KoPageProvider.h>
#include <KoParagraphStyle.h>
#include <KoPostscriptPaintDevice.h>
#include <KoSelection.h>
#include <KoShapeBackground.h>
#include <KoShapeLoadingContext.h>
#include <KoShapePaintingContext.h>
#include <KoShapeSavingContext.h>
#include <KoStyleStack.h>
#include <KoText.h>
#include <KoTextDocument.h>
#include <KoTextDocumentLayout.h>
#include <KoTextPage.h>
#include <KoTextRangeManager.h>
#include <KoTextShapeContainerModel.h>
#include <KoViewConverter.h>
#include <KoXmlNS.h>
#include <KoXmlReader.h>
#include <KoXmlWriter.h>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QScopedValueRollback>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextTable>

#include <QDebug>

TextShape::TextShape(KoInlineTextObjectManager *inlineTextObjectManager, KoTextRangeManager *textRangeManager)
    : KoShapeContainer(new KoTextShapeContainerModel())
    , KoFrameShape(KoXmlNS::draw, "text-box")
    , m_pageProvider(nullptr)
    , m_imageCollection(nullptr)
    , m_clip(true)
{
    setShapeId(TextShape_SHAPEID);
    m_textShapeData = new KoTextShapeData();
    setUserData(m_textShapeData);
    SimpleRootAreaProvider *provider = new SimpleRootAreaProvider(m_textShapeData, this);

    KoTextDocument(m_textShapeData->document()).setInlineTextObjectManager(inlineTextObjectManager);
    KoTextDocument(m_textShapeData->document()).setTextRangeManager(textRangeManager);

    m_layout = new KoTextDocumentLayout(m_textShapeData->document(), provider);
    m_textShapeData->document()->setDocumentLayout(m_layout);

    setCollisionDetection(true);

    QObject::connect(m_layout, &KoTextDocumentLayout::layoutIsDirty, m_layout, &KoTextDocumentLayout::scheduleLayout);
    // Shrink-to-fit frames and text on shapes are fitted here whenever the
    // layout changed (edited text, resized shape). The document is the
    // connection's context, so it dies with the shape's text data.
    QObject::connect(m_layout, &KoTextDocumentLayout::finishedLayout, m_textShapeData->document(), [this]() {
        applyAutofit();
    });
}

TextShape::~TextShape() = default;

void TextShape::applyAutofit()
{
    if (m_fittingAutofit || m_textShapeData->resizeMethod() != KoTextShapeData::ShrinkToFitResize)
        return;
    KoTextLayoutRootArea *rootArea = m_textShapeData->rootArea();
    if (!rootArea)
        return;

    QTextDocument *document = m_textShapeData->document();
    const qreal padding = m_textShapeData->topPadding() + m_textShapeData->bottomPadding();
    const qreal available = size().height() - padding;
    if (available <= 0.0)
        return;

    // Empty paragraphs at the end are an editing artefact, not content; they
    // must not make the text smaller.
    QTextBlock lastVisible = document->lastBlock();
    while (lastVisible.isValid() && lastVisible.text().trimmed().isEmpty() && !lastVisible.textList())
        lastVisible = lastVisible.previous();
    if (!lastVisible.isValid())
        return;

    // Text that did not fit is not laid out at all, so it cannot be measured.
    // The layout cursor tells whether everything reached the frame.
    bool overflow = false;
    if (FrameIterator *next = rootArea->nextStartOfArea()) {
        if (!next->it.atEnd()) {
            const QTextBlock stoppedAt = next->it.currentBlock();
            overflow = stoppedAt.isValid()
                && stoppedAt.position() < lastVisible.position() + lastVisible.length();
        }
    }

    // Measure the text itself: the root area is as tall as the shape, so its
    // bounds say nothing about how much room the text really needs.
    qreal required = 0.0;
    for (QTextBlock block = document->begin(); block.isValid() && block.position() <= lastVisible.position(); block = block.next()) {
        const QTextLayout *blockLayout = block.layout();
        if (!blockLayout)
            continue;
        for (int i = 0; i < blockLayout->lineCount(); ++i) {
            required = qMax(required, blockLayout->position().y() + blockLayout->lineAt(i).rect().bottom() - rootArea->top());
        }
    }
    if (required <= 0.0)
        return;

    qreal target;
    if (overflow) {
        // Shrink in bounded steps: the measured height is only a lower bound
        // when part of the text is missing from the layout.
        target = m_appliedAutofit * 0.9;
    } else {
        const qreal factor = available / required;
        // Ignore rounding noise, or growing and shrinking would alternate.
        if (factor >= 0.98 && factor <= 1.06)
            return;
        // Grow only in small steps so that a wrapping change cannot make the
        // text alternate between too large and too small.
        target = m_appliedAutofit * qBound(qreal(0.9), factor, qreal(1.05));
    }
    // The sizes the document was loaded with are an upper bound, mirroring the
    // scale PowerPoint recorded for the shape.
    target = qBound(qreal(0.2), target, qreal(1.0));
    if (qAbs(target - m_appliedAutofit) < 0.005)
        return;

    QScopedValueRollback<bool> guard(m_fittingAutofit, true);
    const bool undoRedoEnabled = document->isUndoRedoEnabled();
    const bool modified = document->isModified();
    document->setUndoRedoEnabled(false);
    KoTextShapeDataBase::scaleFontSizes(document, target / m_appliedAutofit);
    document->setUndoRedoEnabled(undoRedoEnabled);
    // Fitting is not an edit: keep the document's modified state as it was, so
    // opening a document does not look like an unsaved change.
    document->setModified(modified);
    m_appliedAutofit = target;
}

void TextShape::paintComponent(QPainter &painter, const KoViewConverter &converter, KoShapePaintingContext &paintContext)
{
    painter.save();
    applyConversion(painter, converter);
    KoBorder *border = this->border();

    if (border) {
        paintBorder(painter, converter);
    } else if (paintContext.showTextShapeOutlines) {
        // No need to paint the outlines if there is a real border.
        if (qAbs(rotation()) > 1)
            painter.setRenderHint(QPainter::Antialiasing);

        QPen pen(QColor(210, 210, 210), 0); // use cosmetic pen
        QPointF onePixel = converter.viewToDocument(QPointF(1.0, 1.0));
        QRectF rect(QPointF(0.0, 0.0), size() - QSizeF(onePixel.x(), onePixel.y()));
        painter.setPen(pen);
        painter.drawRect(rect);
    }
    painter.restore();

    if (m_textShapeData->isDirty()) { // not layouted yet.
        return;
    }

    QTextDocument *doc = m_textShapeData->document();
    Q_ASSERT(doc);
    KoTextDocumentLayout *lay = qobject_cast<KoTextDocumentLayout *>(doc->documentLayout());
    Q_ASSERT(lay);
    lay->showInlineObjectVisualization(paintContext.showInlineObjectVisualization);

    applyConversion(painter, converter);

    if (background()) {
        QPainterPath p;
        p.addRect(QRectF(QPointF(), size()));
        background()->paint(painter, converter, paintContext, p);
    }

    // this enables to use the same shapes on different pages showing different page numbers
    if (m_pageProvider) {
        KoTextPage *page = m_pageProvider->page(this);
        if (page) {
            // this is used to not trigger repaints if layout during the painting is done
            m_paintRegion = painter.clipRegion();
            if (!m_textShapeData->rootArea()->page() || page->pageNumber() != m_textShapeData->rootArea()->page()->pageNumber()) {
                m_textShapeData->rootArea()->setPage(page); // takes over ownership of the page
            } else {
                delete page;
            }
        }
    }

    KoTextDocumentLayout::PaintContext pc;

    // The cursor selection is an on-screen decoration: adding it when printing makes Qt paint
    // every paragraph once more in the highlight colour, so only do it when selections are shown.
    if (paintContext.showSelections) {
        QAbstractTextDocumentLayout::Selection selection;
        KoTextEditor *textEditor = KoTextDocument(m_textShapeData->document()).textEditor();
        selection.cursor = *(textEditor->cursor());
        QPalette palette = pc.textContext.palette;
        selection.format.setBackground(palette.brush(QPalette::Highlight));
        selection.format.setForeground(palette.brush(QPalette::HighlightedText));
        pc.textContext.selections.append(selection);

        pc.textContext.selections += KoTextDocument(doc).selections();
    }
    pc.viewConverter = &converter;
    pc.imageCollection = m_imageCollection;
    pc.showFormattingCharacters = paintContext.showFormattingCharacters;
    pc.showTableBorders = paintContext.showTableBorders;
    pc.showSectionBounds = paintContext.showSectionBounds;
    pc.showSpellChecking = paintContext.showSpellChecking;
    pc.showSelections = paintContext.showSelections;

    // When clipping the painter we need to make sure not to cutoff cosmetic pens which
    // may used to draw e.g. table-borders for user convenience when on screen (but not
    // on e.g. printing). Such cosmetic pens are special cause they will always have the
    // same pen-width (1 pixel) independent of zoom-factor or painter transformations and
    // are not taken into account in any border-calculations.
    QRectF clipRect = outlineRect();
    qreal cosmeticPenX = 1 * 72. / painter.device()->logicalDpiX();
    qreal cosmeticPenY = 1 * 72. / painter.device()->logicalDpiY();
    painter.setClipRect(clipRect.adjusted(-cosmeticPenX, -cosmeticPenY, cosmeticPenX, cosmeticPenY), Qt::IntersectClip);

    painter.save();
    painter.translate(0, -m_textShapeData->documentOffset());
    m_textShapeData->rootArea()->paint(&painter, pc); // only need to draw ourselves
    painter.restore();

    m_paintRegion = QRegion();
}

QPointF TextShape::convertScreenPos(const QPointF &point) const
{
    QPointF p = absoluteTransformation(nullptr).inverted().map(point);
    return p + QPointF(0.0, m_textShapeData->documentOffset());
}

QPainterPath TextShape::outline() const
{
    QPainterPath path;
    path.addRect(QRectF(QPointF(0, 0), size()));
    return path;
}

QRectF TextShape::outlineRect() const
{
    if (m_textShapeData->rootArea()) {
        QRectF rect = m_textShapeData->rootArea()->boundingRect();
        rect.moveTop(rect.top() - m_textShapeData->rootArea()->top());
        if (m_clip) {
            rect.setHeight(size().height());
        }
        return rect | QRectF(QPointF(0, 0), size());
    }
    return QRectF(QPointF(0, 0), size());
}

void TextShape::shapeChanged(ChangeType type, KoShape *shape)
{
    Q_UNUSED(shape);
    KoShapeContainer::shapeChanged(type, shape);
    if (type == PositionChanged || type == SizeChanged || type == CollisionDetected) {
        m_textShapeData->setDirty();
    }
}

void TextShape::saveOdf(KoShapeSavingContext &context) const
{
    KoXmlWriter &writer = context.xmlWriter();

    QString textHeight = additionalAttribute("fo:min-height");
    const_cast<TextShape *>(this)->removeAdditionalAttribute("fo:min-height");
    writer.startElement("draw:frame");
    saveOdfAttributes(context, OdfAllAttributes);

    // ODF presentation tables are direct children of draw:frame. Wrapping
    // them in draw:text-box makes Impress discard them during PPTX export.
    QTextTable *standaloneTable = nullptr;
    if (QCoreApplication::applicationName() == QLatin1String("calligrastage")) {
        const auto frames = m_textShapeData->document()->rootFrame()->childFrames();
        if (frames.size() == 1) {
            standaloneTable = qobject_cast<QTextTable *>(frames.first());
            if (standaloneTable) {
                for (auto it = m_textShapeData->document()->rootFrame()->begin(); !it.atEnd(); ++it) {
                    if (it.currentBlock().isValid() && !it.currentBlock().text().isEmpty()) {
                        standaloneTable = nullptr;
                        break;
                    }
                }
            }
        }
    }
    if (!standaloneTable) writer.startElement("draw:text-box");
    if (!textHeight.isEmpty())
        writer.addAttribute("fo:min-height", textHeight);
    KoTextDocumentLayout *lay = qobject_cast<KoTextDocumentLayout *>(m_textShapeData->document()->documentLayout());
    int index = -1;
    if (lay) {
        int i = 0;
        foreach (KoShape *shape, lay->shapes()) {
            if (shape == this) {
                index = i;
            } else if (index >= 0) {
                writer.addAttribute("draw:chain-next-name", shape->name());
                break;
            }
            ++i;
        }
    }
    const bool saveMyText = index == 0; // only save the text once.

    if (standaloneTable && saveMyText) {
        // Include the hidden paragraph before the table so even a 1x1 table
        // is serialized as a whole table rather than a cell text selection.
        m_textShapeData->saveOdf(context, nullptr, standaloneTable->firstPosition() - 1, standaloneTable->lastPosition());
    } else {
        m_textShapeData->saveOdf(context, nullptr, 0, saveMyText ? -1 : 0);
    }
    if (!standaloneTable) writer.endElement(); // draw:text-box
    saveOdfCommonChildElements(context);
    writer.endElement(); // draw:frame
}

QString TextShape::saveStyle(KoGenStyle &style, KoShapeSavingContext &context) const
{
    Qt::Alignment vAlign(m_textShapeData->verticalAlignment());
    QString verticalAlign = "top";
    if (vAlign == Qt::AlignBottom) {
        verticalAlign = "bottom";
    } else if (vAlign == Qt::AlignVCenter) {
        verticalAlign = "middle";
    }
    style.addProperty("draw:textarea-vertical-align", verticalAlign);

    KoTextShapeData::ResizeMethod resize = m_textShapeData->resizeMethod();
    if (resize == KoTextShapeData::AutoGrowWidth || resize == KoTextShapeData::AutoGrowWidthAndHeight)
        style.addProperty("draw:auto-grow-width", "true");
    if (resize != KoTextShapeData::AutoGrowHeight && resize != KoTextShapeData::AutoGrowWidthAndHeight)
        style.addProperty("draw:auto-grow-height", "false");
    if (resize == KoTextShapeData::ShrinkToFitResize) {
        style.addProperty("draw:fit-to-size", "false");
        style.addProperty("style:shrink-to-fit", "true");
        if (m_textShapeData->autofitFontScale() > 0)
            style.addProperty("calligra:autofit-font-scale", m_textShapeData->autofitFontScale());
        if (m_textShapeData->autofitLineSpacingReduction() > 0)
            style.addProperty("calligra:autofit-line-spacing-reduction", m_textShapeData->autofitLineSpacingReduction());
    }

    m_textShapeData->saveStyle(style, context);

    return KoShape::saveStyle(style, context);
}

void TextShape::loadStyle(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    KoShape::loadStyle(element, context);
    KoStyleStack &styleStack = context.odfLoadingContext().styleStack();
    styleStack.setTypeProperties("graphic");

    QString verticalAlign(styleStack.property(KoXmlNS::draw, "textarea-vertical-align"));
    Qt::Alignment alignment(Qt::AlignTop);
    if (verticalAlign == "bottom") {
        alignment = Qt::AlignBottom;
    } else if (verticalAlign == "justify") {
        // not yet supported
        alignment = Qt::AlignVCenter;
    } else if (verticalAlign == "middle") {
        alignment = Qt::AlignVCenter;
    }

    m_textShapeData->setVerticalAlignment(alignment);

    const QString fitToSize = styleStack.property(KoXmlNS::draw, "fit-to-size");
    KoTextShapeData::ResizeMethod resize = KoTextShapeData::NoResize;
    if (fitToSize == "true" || fitToSize == "shrink-to-fit" || styleStack.property(KoXmlNS::style, "shrink-to-fit") == "true") {
        resize = KoTextShapeData::ShrinkToFitResize;
    } else {
        // An explicit svg:width or svg:height defined do change the default value (means those value
        // used if not explicit defined otherwise) for auto-grow-height and auto-grow-height. So
        // they are mutable exclusive.
        // It is not clear (means we did not test and took care of it) what happens if both are
        // defined and are in conflict with each other or how the fit-to-size is related to this.

        QString autoGrowWidth = styleStack.property(KoXmlNS::draw, "auto-grow-width");
        if (autoGrowWidth.isEmpty()) {
            autoGrowWidth = element.hasAttributeNS(KoXmlNS::svg, "width") ? "false" : "true";
        }

        QString autoGrowHeight = styleStack.property(KoXmlNS::draw, "auto-grow-height");
        if (autoGrowHeight.isEmpty()) {
            autoGrowHeight = element.hasAttributeNS(KoXmlNS::svg, "height") ? "false" : "true";
        }

        if (autoGrowWidth == "true") {
            resize = autoGrowHeight == "true" ? KoTextShapeData::AutoGrowWidthAndHeight : KoTextShapeData::AutoGrowWidth;
        } else if (autoGrowHeight == "true") {
            resize = KoTextShapeData::AutoGrowHeight;
        }
    }

    m_textShapeData->setResizeMethod(resize);

    if (resize == KoTextShapeData::ShrinkToFitResize) {
        const QString autofitFontScale = styleStack.property(KoXmlNS::calligra, "autofit-font-scale");
        if (!autofitFontScale.isEmpty())
            m_textShapeData->setAutofitFontScale(autofitFontScale.toInt());
        const QString autofitLineSpacingReduction = styleStack.property(KoXmlNS::calligra, "autofit-line-spacing-reduction");
        if (!autofitLineSpacingReduction.isEmpty())
            m_textShapeData->setAutofitLineSpacingReduction(autofitLineSpacingReduction.toInt());
    }
}

bool TextShape::loadOdf(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    m_textShapeData->document()->setUndoRedoEnabled(false);
    loadOdfAttributes(element, context, OdfAllAttributes);

    // this cannot be done in loadStyle as that fill the style stack wrongly and therefor it results
    // in wrong data to be loaded.
    m_textShapeData->loadStyle(element, context);

#ifndef NWORKAROUND_ODF_BUGS
    KoTextShapeData::ResizeMethod method = m_textShapeData->resizeMethod();
    if (KoOdfWorkaround::fixAutoGrow(method, context)) {
        KoTextDocumentLayout *lay = qobject_cast<KoTextDocumentLayout *>(m_textShapeData->document()->documentLayout());
        Q_ASSERT(lay);
        if (lay) {
            SimpleRootAreaProvider *provider = dynamic_cast<SimpleRootAreaProvider *>(lay->provider());
            if (provider) {
                provider->m_fixAutogrow = true;
            }
        }
    }
#endif

    bool answer = loadOdfFrame(element, context);
    m_textShapeData->document()->setUndoRedoEnabled(true);
    return answer;
}

bool TextShape::loadOdfFrame(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    // If the loadOdfFrame from the base class for draw:text-box fails, check
    // for table:table, because that is a legal child of draw:frame in ODF 1.2.
    if (!KoFrameShape::loadOdfFrame(element, context)) {
        const KoXmlElement &possibleTableElement(KoXml::namedItemNS(element, KoXmlNS::table, "table"));
        if (possibleTableElement.isNull()) {
            return false;
        } else {
            // Table cells provide their own fill and borders. Graphic defaults
            // inherited from a presentation theme belong to ordinary shapes.
            setBackground({});
            setStroke(nullptr);
            return loadOdfFrameElement(possibleTableElement, context);
        }
    }

    return true;
}

bool TextShape::loadOdfFrameElement(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    bool ok = m_textShapeData->loadOdf(element, context, nullptr, this);
    if (ok) {
        // PowerPoint stores the authored text size and displays it scaled down
        // until it fits. Apply that scale once; the shape keeps the text fitted
        // afterwards by scaling the font sizes (see applyAutofit()).
        const int authoredScale = m_textShapeData->autofitFontScale();
        if (authoredScale > 0 && authoredScale < 100000) {
            QTextDocument *document = m_textShapeData->document();
            const bool undoRedoEnabled = document->isUndoRedoEnabled();
            const bool modified = document->isModified();
            document->setUndoRedoEnabled(false);
            KoTextShapeDataBase::scaleFontSizes(document, authoredScale / 100000.0);
            document->setUndoRedoEnabled(undoRedoEnabled);
            document->setModified(modified);
            // The sizes now carry the scale, so it must not be applied again.
            m_textShapeData->setAutofitFontScale(0);
        }
    }
    return ok;
}

void TextShape::update() const
{
    KoShapeContainer::update();
}

void TextShape::update(const QRectF &shape) const
{
    // this is done to avoid updates which are called during the paint event and not needed.
    if (!m_paintRegion.contains(shape.toRect())) {
        KoShape::update(shape);
    }
}

void TextShape::waitUntilReady(const KoViewConverter &, bool asynchronous) const
{
    Q_UNUSED(asynchronous);
    KoTextDocumentLayout *lay = qobject_cast<KoTextDocumentLayout *>(m_textShapeData->document()->documentLayout());
    Q_ASSERT(lay);
    if (m_textShapeData->isDirty()) {
        // Do a simple layout-call which will make sure to relayout till things are done. If more
        // layouts are scheduled then we don't need to wait for them here but can just continue.
        lay->layout();
    }
}

KoImageCollection *TextShape::imageCollection()
{
    return m_imageCollection;
}

void TextShape::updateDocumentData()
{
    if (m_layout) {
        KoTextDocument document(m_textShapeData->document());
        m_layout->setStyleManager(document.styleManager());
        m_layout->setInlineTextObjectManager(document.inlineTextObjectManager());
        m_layout->setTextRangeManager(document.textRangeManager());
        m_layout->setChangeTracker(document.changeTracker());
    }
}
