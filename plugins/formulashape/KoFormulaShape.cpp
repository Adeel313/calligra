/* This file is part of the KDE project
   SPDX-FileCopyrightText: 2006 Martin Pfeiffer <hubipete@gmx.net>
                 2009 Jeremias Epperlein <jeeree@web.de>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "KoFormulaShape.h"

#include <QFontMetricsF>
#include <QSizeF>
#include <QUrl>

#include <KLocalizedString>
#include <KMessageBox>
#include <kguiitem.h>

#include <KoDocumentResourceManager.h>
#include <KoOdfLoadingContext.h>
#include <KoShapeLoadingContext.h>
#include <KoShapeSavingContext.h>
#include <KoStore.h>
#include <KoXmlNS.h>
#include <KoXmlReader.h>
#include <KoXmlWriter.h>

#include "FormulaData.h"
#include "FormulaDebug.h"
#include "FormulaDocument.h"
#include "FormulaElement.h"
#include "FormulaRenderer.h"

static const qreal MinimumWidth = 60.0;
static const qreal MinimumHeight = 30.0;

/// How much a formula is enlarged while it is edited; the equation keeps the
/// size it has on the page, as it does in an office suite.
static const qreal EditScale = 1.0;

KoFormulaShape::KoFormulaShape(KoDocumentResourceManager *documentResourceManager)
    : KoFrameShape(KoXmlNS::draw, "object")
    , m_formulaData(std::make_unique<FormulaData>(std::make_unique<FormulaElement>()))
    , m_formulaRenderer(std::make_unique<FormulaRenderer>())
    , m_isInline(false)
{
    m_document = std::make_unique<FormulaDocument>(this);
    m_resourceManager = documentResourceManager;
}

KoFormulaShape::~KoFormulaShape()
{
}

void KoFormulaShape::paint(QPainter &painter, const KoViewConverter &converter, KoShapePaintingContext &)
{
    painter.save();
    applyConversion(painter, converter); // apply zooming and coordinate translation
    m_formulaRenderer->layoutElement(m_formulaData->formulaElement());
    const qreal scale = editingScale();
    if (scale != 1.0) {
        // Editing magnification: the element layout stays in its own
        // coordinates, everything drawn is scaled up instead.
        painter.scale(scale, scale);
    }
    if (m_formulaData->formulaElement()->childElements().isEmpty()) {
        // An empty formula draws nothing at all, which makes a freshly inserted
        // one almost impossible to find on the page. Show where it is until it
        // gets some content.
        QPen pen(QColor(140, 140, 140));
        pen.setStyle(Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(QPointF(), size() / scale));
        if (m_isEditing) {
            // Tell the user that this box is waiting for an equation, the way
            // the Office equation editor does while the placeholder is empty.
            QFont hintFont;
            hintFont.setPointSizeF(12.0);
            hintFont.setItalic(true);
            painter.setFont(hintFont);
            painter.setPen(QColor(150, 150, 150));
            const QRectF hintRect = QRectF(QPointF(), size() / scale).adjusted(6, 0, -6, 0);
            const QFontMetricsF metrics(hintFont);
            painter.drawText(hintRect,
                             Qt::AlignLeft | Qt::AlignVCenter,
                             metrics.elidedText(i18n("Type equation here."), Qt::ElideRight, hintRect.width()));
        }
    } else {
        m_formulaRenderer->paintElement(painter, m_formulaData->formulaElement()); // paint the formula
    }
    painter.restore();
}

void KoFormulaShape::updateLayout()
{
    m_formulaRenderer->layoutElement(m_formulaData->formulaElement());

    QSizeF size = m_formulaData->formulaElement()->boundingRect().size();
    // An empty formula has no extent of its own. Keep a frame that is large
    // enough to show the "Type equation here." placeholder the way Office does.
    qreal minimumWidth = MinimumWidth;
    if (m_formulaData->formulaElement()->childElements().isEmpty()) {
        QFont hintFont;
        hintFont.setPointSizeF(12.0);
        hintFont.setItalic(true);
        minimumWidth = QFontMetricsF(hintFont).horizontalAdvance(i18n("Type equation here.")) + 16.0;
    }
    size.setWidth(qMax(size.width(), minimumWidth));
    size.setHeight(qMax(size.height(), MinimumHeight));
    // While the formula is edited the frame covers the enlarged drawing, so the
    // surrounding text makes room for it; it shrinks back when editing ends.
    size *= editingScale();
    KoShape::setSize(size);
}

void KoFormulaShape::setEditing(bool editing)
{
    if (m_isEditing == editing) {
        return;
    }
    m_isEditing = editing;
    // The frame follows the magnification, which is what the text layout and
    // the cursor positions are based on.
    updateLayout();
    update();
}

bool KoFormulaShape::isEditing() const
{
    return m_isEditing;
}

qreal KoFormulaShape::editingScale() const
{
    return m_isEditing ? EditScale : 1.0;
}

BasicElement *KoFormulaShape::elementAt(const QPointF &p)
{
    return m_formulaData->formulaElement()->childElementAt(p);
}

void KoFormulaShape::resize(const QSizeF &)
{ /* do nothing as FormulaShape is fixed size */
}

FormulaData *KoFormulaShape::formulaData() const
{
    return m_formulaData.get();
}

FormulaRenderer *KoFormulaShape::formulaRenderer() const
{
    return m_formulaRenderer.get();
}

bool KoFormulaShape::loadOdf(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    debugFormula << "Loading ODF in Formula";
    loadOdfAttributes(element, context, OdfAllAttributes);
    if (!loadOdfFrame(element, context)) {
        return false;
    }
    // The stored frame size was computed with the metrics in effect when the
    // formula was saved; recompute it so the frame encloses the formula as it
    // is laid out now.
    updateLayout();
    return true;
}

bool KoFormulaShape::loadOdfFrameElement(const KoXmlElement &element, KoShapeLoadingContext &context)
{
    // If this formula is embedded and not inline, then load the embedded document.
    if (element.tagName() == "object" && element.hasAttributeNS(KoXmlNS::xlink, "href")) {
        m_isInline = false;

        // This calls loadOdfEmbedded().
        return loadEmbeddedDocument(context.odfLoadingContext().store(), element, context.odfLoadingContext());
    }

    // It's not a frame:object, so it must be inline.
    const KoXmlElement &topLevelElement = KoXml::namedItemNS(element, KoXmlNS::math, "math");
    if (topLevelElement.isNull()) {
        warnFormula << "no math element as first child";
        return false;
    }

    // Create a new root element, load the formula and replace the old one.
    auto formulaElement = std::make_unique<FormulaElement>();
    formulaElement->readMathML(topLevelElement);
    m_formulaData->swapFormulaElement(formulaElement);
    m_formulaData->notifyDataChange(nullptr, false);

    m_isInline = true;

    return true;
}

bool KoFormulaShape::loadEmbeddedDocument(KoStore *store, const KoXmlElement &objectElement, const KoOdfLoadingContext &odfLoadingContext)
{
    if (!objectElement.hasAttributeNS(KoXmlNS::xlink, "href")) {
        errorFormula << "Object element has no valid xlink:href attribute";
        return false;
    }

    QString url = objectElement.attributeNS(KoXmlNS::xlink, "href");

    // It can happen that the url is empty e.g. when it is a
    // presentation:placeholder.
    if (url.isEmpty()) {
        return true;
    }

    QString tmpURL;
    if (url[0] == '#')
        url.remove(0, 1);

#define INTERNAL_PROTOCOL "intern"
#define STORE_PROTOCOL "tar"

    if (QUrl::fromUserInput(url).isRelative()) {
        if (url.startsWith("./"))
            tmpURL = QString(INTERNAL_PROTOCOL) + ":/" + url.mid(2);
        else
            tmpURL = QString(INTERNAL_PROTOCOL) + ":/" + url;
    } else
        tmpURL = url;

    QString path = tmpURL;
    if (tmpURL.startsWith(INTERNAL_PROTOCOL)) {
        path = store->currentPath();
        if (!path.isEmpty() && !path.endsWith('/'))
            path += '/';
        QString relPath = QUrl::fromUserInput(tmpURL).path();
        path += QStringView{relPath}.mid(1); // remove leading '/'
    }
    if (!path.endsWith('/'))
        path += '/';

    const QString mimeType = odfLoadingContext.mimeTypeForPath(path);
    // debugFormula << "path for manifest file=" << path << "mimeType=" << mimeType;
    if (mimeType.isEmpty()) {
        // debugFormula << "Manifest doesn't have media-type for" << path;
        return false;
    }

    const bool isOdf = mimeType.startsWith("application/vnd.oasis.opendocument");
    if (!isOdf) {
        tmpURL += "/maindoc.xml";
        // debugFormula << "tmpURL adjusted to" << tmpURL;
    }

    // debugFormula << "tmpURL=" << tmpURL;

    bool res = true;
    if (tmpURL.startsWith(STORE_PROTOCOL) || tmpURL.startsWith(INTERNAL_PROTOCOL) || QUrl::fromUserInput(tmpURL).isRelative()) {
        if (isOdf) {
            store->pushDirectory();
            Q_ASSERT(tmpURL.startsWith(INTERNAL_PROTOCOL));
            QString relPath = QUrl::fromUserInput(tmpURL).path().mid(1);
            store->enterDirectory(relPath);
            res = m_document->loadOasisFromStore(store);
            store->popDirectory();
        } else {
            if (tmpURL.startsWith(INTERNAL_PROTOCOL))
                tmpURL = QUrl::fromUserInput(tmpURL).path().mid(1);
            res = m_document->loadFromStore(store, tmpURL);
        }
        m_document->setStoreInternal(true);
    } else {
        // Reference to an external document. Hmmm...
        m_document->setStoreInternal(false);
        QUrl url = QUrl::fromUserInput(tmpURL);
        if (!url.isLocalFile()) {
            // QApplication::restoreOverrideCursor();

            // For security reasons we need to ask confirmation if the
            // url is remote.
            int result = KMessageBox::warningTwoActionsCancel(nullptr,
                                                              i18n("This document contains an external link to a remote document\n%1", tmpURL),
                                                              i18n("Confirmation Required"),
                                                              KGuiItem(i18n("Download")),
                                                              KGuiItem(i18n("Skip")));

            if (result == KMessageBox::Cancel) {
                // d->m_parent->setErrorMessage("USER_CANCELED");
                return false;
            }
            if (result == KMessageBox::PrimaryAction)
                res = m_document->openUrl(url);
            // and if == No, res will still be false so we'll use a kounavail below
        } else
            res = m_document->openUrl(url);
    }

    if (!res) {
        // QString errorMessage = m_document->errorMessage();
        return false;
    }

    tmpURL.clear();

    return res;
}

bool KoFormulaShape::loadOdfEmbedded(const KoXmlElement &topLevelElement, KoShapeLoadingContext &context)
{
    Q_UNUSED(context);
    debugFormula << topLevelElement.nodeName();

#if 0
    const KoXmlElement &topLevelElement = KoXml::namedItemNS(element, "http://www.w3.org/1998/Math/MathML", "math");
    if (topLevelElement.isNull()) {
        warnFormula << "no math element as first child";
        return false;
    }
#endif
    // Create a new root element, load the formula and replace the old one.
    auto formulaElement = std::make_unique<FormulaElement>();
    formulaElement->readMathML(topLevelElement);
    m_formulaData->swapFormulaElement(formulaElement);
    m_formulaData->notifyDataChange(nullptr, false);

    return true;
}

void KoFormulaShape::saveOdf(KoShapeSavingContext &context) const
{
    // FIXME: Add saving of embedded document if m_isInline is false;

    debugFormula << "Saving ODF in Formula";
    KoXmlWriter &writer = context.xmlWriter();
    writer.startElement("draw:frame");
    // A formula that is being edited is drawn magnified; the document keeps the
    // size it has in the text.
    auto *self = const_cast<KoFormulaShape *>(this);
    const QSizeF editingSize = size();
    if (m_isEditing) {
        self->KoShape::setSize(editingSize / EditScale);
    }
    saveOdfAttributes(context, OdfAllAttributes);
    if (m_isEditing) {
        self->KoShape::setSize(editingSize);
    }
    writer.startElement("draw:object");
    // TODO add some namespace magic to avoid adding "math:" namespace everywhere
    formulaData()->formulaElement()->writeMathML(&context.xmlWriter());
    writer.endElement(); // draw:object
    writer.endElement(); // draw:frame
}

KoDocumentResourceManager *KoFormulaShape::resourceManager() const
{
    return m_resourceManager;
}
