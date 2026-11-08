/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2001 David Faure <faure@kde.org>
 * SPDX-FileCopyrightText: 2005-2007, 2009, 2010 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2010-2011 Boudewijn Rempt <boud@kogmbh.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

// words includes
#define SHOW_ANNOTATIONS 1
#include "KWView.h"

#include "../../calligra-version.h"
#include "KWCanvas.h"
#include "KWDocument.h"
#include "KWFactory.h"
#include "KWGui.h"
#include "KWPageManager.h"
#include "KWStatusBar.h"
#include "WordsDebug.h"
#include "commands/KWShapeCreateCommand.h"
#include "dialogs/KWConfigureDialog.h"
#include "dialogs/KWCreateBookmarkDialog.h"
#include "dialogs/KWFrameDialog.h"
#include "dialogs/KWPageSettingsDialog.h"
#include "dialogs/KWPrintDialog.h"
#include "dialogs/KWPrintingDialog.h"
#include "dialogs/KWSelectBookmarkDialog.h"
#include "frames/KWCopyShape.h"
#include "frames/KWFrame.h"
#include "frames/KWTextFrameSet.h"
#include "gemini/ViewModeSwitchEvent.h"
#include "Words.h"
#include "mailmerge/KWMailMergeDialog.h"
#include "mailmerge/KWMailMergeField.h"
#include "mailmerge/KWMailMergeManager.h"
#include "ui_KWInsertImage.h"
#include "widgets/KoFindToolbar.h"

// calligra libs includes
#include <KoAnchorInlineObject.h>
#include <KoAnnotation.h>
#include <KoAnnotationLayoutManager.h>
#include <KoAnnotationManager.h>
#include <KoBookmark.h>
#include <KoCanvasController.h>
#include <KoCanvasControllerWidget.h>
#include <KoCanvasResourceManager.h>
#include <KoCopyController.h>
#include <KoCutController.h>
#include <KoDocumentInfo.h>
#include <KoDocumentRdfBase.h>
#include <KoFind.h>
#include <KoGridData.h>
#include <KoGuidesData.h>
#include <KoInlineTextObjectManager.h>
#include <KoMainWindow.h>
#include <KoPart.h>
#include <KoPathShape.h> // for KoPathShapeId
#include <KoPointedAt.h>
#include <KoProperties.h>
#include <KoSelection.h>
#include <KoShape.h>
#include <KoShapeAnchor.h>
#include <KoShapeContainer.h>
#include <KoShapeCreateCommand.h>
#include <KoShapeController.h>
#include <KoShapeFactoryBase.h>
#include <KoShapeGroupCommand.h>
#include <KoShapeManager.h>
#include <KoShapeRegistry.h>
#include <KoStandardAction.h>
#include <KoStore.h>
#include <KoTemplateCreateDia.h>
#include <KoText.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
#include <KoTextRangeManager.h>
#include <KoTextShapeData.h>
#include <KoToolManager.h>
#include <KoToolProxy.h>
#include <tools/KoInteractionTool.h>
#include <KoZoomController.h>

#ifdef SHOULD_BUILD_RDF
#include <KoDocumentRdf.h>
#include <KoSemanticStylesheetsEditor.h>
#endif

#include <KoComponentData.h>
#include <KoFindText.h>
#include <KoIcon.h>
#include <KoTextLayoutRootArea.h>

// KF5
#include <KActionCollection>
#include <KActionMenu>
#include <KLocalizedString>
#include <KToolBar>
#include <ktoggleaction.h>
#include <kxmlguifactory.h>

// Qt
#include <QClipboard>
#include <QBuffer>
#include <QFileDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QImageReader>
#include <QMimeData>
#include <QMenuBar>
#include <QPushButton>
#include <QScrollBar>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>

#include <QTextDocument>

#include <limits>

KWView::KWView(KoPart *part, KWDocument *document, QWidget *parent)
    : KoView(part, document, parent)
    , m_canvas(nullptr)
    , m_textMinX(1)
    , m_textMaxX(600)
    , m_minPageNum(1)
    , m_maxPageNum(1)
    , m_isFullscreenMode(false)
{
    setAcceptDrops(true);

    m_document = document;
    m_snapToGrid = m_document->gridData().snapToGrid();
    m_gui = new KWGui(QString(), this);
    m_canvas = m_gui->canvas();

    setFocusProxy(m_canvas);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins({});
    layout->setSpacing(0);
    layout->addWidget(m_gui);
    layout->addWidget(statusBar());

    setComponentName(KWFactory::componentData().componentName(), KWFactory::componentData().componentDisplayName());
    setXMLFile("calligrawords.rc");

    m_currentPage = m_document->pageManager()->begin();

    m_document->annotationLayoutManager()->setShapeManager(m_canvas->shapeManager());
    m_document->annotationLayoutManager()->setCanvasBase(m_canvas);
    m_document->annotationLayoutManager()->setViewContentWidth(m_canvas->viewMode()->contentsSize().width());
    connect(m_document->annotationLayoutManager(), &KoAnnotationLayoutManager::hasAnnotationsChanged, this, &KWView::hasNotes);
    // We need to create associate widget before connect them in actions
    // Perhaps there is a better place for the WordCount widget creates here
    // If you know where to move it in a better place, just do it
    buildAssociatedWidget();

    setupActions();

    connect(m_canvas->shapeManager()->selection(), &KoSelection::selectionChanged, this, &KWView::selectionChanged);

    m_find = new KoFindText(this);
    KoFindToolbar *toolbar = new KoFindToolbar(m_find, actionCollection(), this);
    toolbar->setVisible(false);
    connect(m_find, &KoFindBase::matchFound, this, &KWView::findMatchFound);
    connect(m_find, &KoFindText::updateCanvas, m_canvas, QOverload<>::of(&KWCanvas::update));
    // The text documents to search in will potentially change when we add/remove shapes and after load
    connect(m_document, &KWDocument::shapeAdded, this, &KWView::refreshFindTexts);
    connect(m_document, &KWDocument::shapeRemoved, this, &KWView::refreshFindTexts);

    refreshFindTexts();

    layout->addWidget(toolbar);

    m_zoomController = new KoZoomController(m_gui->canvasController(), &m_zoomHandler, actionCollection(), KoZoomAction::SpecialButtons(), this);
    if (statusBar())
        KWStatusBar::addViewControls(statusBar(), this);

    // the zoom controller needs to be initialized after the status bar gets initialized as
    // that resulted in bug 180759
    QSizeF pageSize = m_currentPage.rect().size();
    if (m_canvas->showAnnotations()) {
        pageSize += QSize(KWCanvasBase::AnnotationAreaWidth, 0.0);
    }
    m_zoomController->setPageSize(pageSize);
    m_zoomController->setTextMinMax(m_currentPage.contentRect().left(), m_currentPage.contentRect().right());
    KoZoomMode::Modes modes = KoZoomMode::ZOOM_WIDTH | KoZoomMode::ZOOM_TEXT;
    if (m_canvas->viewMode()->hasPages())
        modes |= KoZoomMode::ZOOM_PAGE;
    m_zoomController->zoomAction()->setZoomModes(modes);
    connect(m_canvas, &KWCanvas::documentSize, m_zoomController, [this](const QSizeF &sz) {
        m_zoomController->setDocumentSize(sz);
    });
    m_canvas->updateSize(); // to emit the doc size at least once
    m_zoomController->setZoom(m_document->config().zoomMode(), m_document->config().zoom() / 100.);
    connect(m_zoomController, &KoZoomController::zoomChanged, this, &KWView::zoomChanged);

    // Timer start in Fullscreen mode view.
    m_hideCursorTimer = new QTimer(this);
    connect(m_hideCursorTimer, &QTimer::timeout, this, &KWView::hideCursor);

    m_dfmExitButton = new QPushButton(i18n("Exit Fullscreen Mode"));
    addStatusBarItem(m_dfmExitButton, 0);
    m_dfmExitButton->setVisible(false);
    connect(m_dfmExitButton, &QAbstractButton::clicked, this, &KWView::exitFullscreenMode);

#ifdef SHOULD_BUILD_RDF
    if (KoDocumentRdf *rdf = dynamic_cast<KoDocumentRdf *>(m_document->documentRdf())) {
        connect(rdf,
                SIGNAL(semanticObjectViewSiteUpdated(hKoRdfBasicSemanticItem, QString)),
                this,
                SLOT(semanticObjectViewSiteUpdated(hKoRdfBasicSemanticItem, QString)));
    }
#endif

    // A newly opened document starts in text editing rather than in the object
    // interaction tool.  Without this the text-only Insert commands (table,
    // page break) stay disabled until the user clicks into the document.
    QTimer::singleShot(0, m_canvas, [this] {
        if (!m_canvas || !m_canvas->shapeManager()->selection()->selectedShapes().isEmpty()) {
            return;
        }
        m_canvas->setFocus(Qt::OtherFocusReason);
        KoToolManager::instance()->switchToolRequested(QStringLiteral("TextToolFactory_ID"));
    });
}

KWView::~KWView()
{
    KoToolManager::instance()->removeCanvasController(m_gui->canvasController());

    m_canvas = nullptr;
}

KoCanvasBase *KWView::canvasBase() const
{
    return m_canvas;
}

const KWPage KWView::currentPage() const
{
    return m_currentPage;
}

KWTextFrameSet *KWView::currentTextFrameSet() const
{
    if (!m_canvas) {
        return nullptr;
    }
    const QVariant documentVariant = m_canvas->resourceManager()->resource(KoText::CurrentTextDocument);
    QTextDocument *document = static_cast<QTextDocument *>(documentVariant.value<void *>());
    if (!document) {
        // The text tool clears its document when it is deactivated, which also
        // happens when a ribbon button or a dialog takes the focus.  Fall back
        // to the frame that had the cursor last so that e.g. Insert Image still
        // lands in the header the caret was in.
        return m_lastTextFrameSet;
    }
    for (KWFrameSet *frameSet : m_document->frameSets()) {
        KWTextFrameSet *textFrameSet = dynamic_cast<KWTextFrameSet *>(frameSet);
        if (textFrameSet && textFrameSet->document() == document) {
            m_lastTextFrameSet = textFrameSet;
            return textFrameSet;
        }
    }
    return m_lastTextFrameSet;
}

KWTextFrameSet *KWView::headerFooterFrameSetAtPointer()
{
    if (!m_canvas) {
        return nullptr;
    }
    const QPoint viewPoint = m_canvas->mapFromGlobal(QCursor::pos());
    const QPointF documentPoint = viewConverter()->viewToDocument(m_canvas->documentOffset() + viewPoint - m_canvas->pos());
    return headerFooterFrameSetAt(documentPoint);
}

KWTextFrameSet *KWView::headerFooterFrameSetAt(const QPointF &documentPoint) const
{
    if (!m_document) {
        return nullptr;
    }
    const KWPage page = m_document->pageManager()->page(documentPoint);
    if (!page.isValid()) {
        return nullptr;
    }
    // The page content rect is the main text area; the area above and below it
    // belongs to the header and the footer of that page style.  A footer with
    // content can reach up into the text area, so the page margins are taken
    // into account as well and the whole band around the text area counts.
    const QRectF textArea = page.contentRect();
    const QRectF pageRect = page.rect();
    const qreal headerEnd = pageRect.isEmpty() ? textArea.top() : qMax(textArea.top(), pageRect.top() + page.topMargin());
    const qreal footerStart = pageRect.isEmpty() ? textArea.bottom() : qMin(textArea.bottom(), pageRect.bottom() - page.bottomMargin());
    const bool inHeader = documentPoint.y() < headerEnd;
    const bool inFooter = documentPoint.y() > footerStart;
    if (!inHeader && !inFooter) {
        return nullptr;
    }

    const KWPageStyle pageStyle = page.pageStyle();
    const Words::HeaderFooterType policy = inHeader ? pageStyle.headerPolicy() : pageStyle.footerPolicy();
    if (policy == Words::HFTypeNone) {
        return nullptr;
    }
    const bool evenPage = policy == Words::HFTypeEvenOdd && page.pageNumber() % 2 == 0;
    Words::TextFrameSetType type;
    if (inHeader) {
        type = evenPage ? Words::EvenPagesHeaderTextFrameSet : Words::OddPagesHeaderTextFrameSet;
    } else {
        type = evenPage ? Words::EvenPagesFooterTextFrameSet : Words::OddPagesFooterTextFrameSet;
    }

    // Prefer the frameset that matches the page side, but fall back to the only
    // header/footer frameset of that page style: documents that use a single
    // header or footer for every page do not always end up with the odd/even
    // frameset the page number suggests.
    KWTextFrameSet *fallback = nullptr;
    const QList<KWFrameSet *> frameSets = m_document->frameSets();
    for (KWFrameSet *frameSet : frameSets) {
        KWTextFrameSet *textFrameSet = dynamic_cast<KWTextFrameSet *>(frameSet);
        if (!textFrameSet || textFrameSet->pageStyle() != pageStyle) {
            continue;
        }
        const Words::TextFrameSetType candidate = textFrameSet->textFrameSetType();
        const bool isHeaderType = candidate == Words::OddPagesHeaderTextFrameSet || candidate == Words::EvenPagesHeaderTextFrameSet;
        if (isHeaderType != inHeader) {
            continue;
        }
        if (candidate == type) {
            return textFrameSet;
        }
        if (!fallback) {
            fallback = textFrameSet;
        }
    }
    return fallback;
}

bool KWView::startEditingHeaderFooterAt(const QPointF &documentPoint)
{
    if (!m_canvas) {
        return false;
    }
    KWTextFrameSet *frameSet = headerFooterFrameSetAt(documentPoint);
    if (!frameSet) {
        return false;
    }

    // Text that is already part of the header/footer keeps the normal
    // double-click behaviour (selecting the word under the cursor).
    if (KoShape *hitShape = m_canvas->shapeManager()->shapeAt(documentPoint)) {
        KoTextShapeData *hitData = qobject_cast<KoTextShapeData *>(hitShape->userData());
        if (hitData && hitData->document() == frameSet->document() && hitData->document()->characterCount() > 1) {
            return false;
        }
    }

    // The frame that is shown on a page can be a copy of the shape that owns the
    // text, and only the owning shape can be edited.
    KoShape *editableShape = nullptr;
    const QList<KoShape *> shapes = frameSet->shapes();
    for (KoShape *shape : shapes) {
        if (!dynamic_cast<KWCopyShape *>(shape)) {
            editableShape = shape;
            break;
        }
    }
    if (!editableShape) {
        return false;
    }

    KoToolManager *toolManager = KoToolManager::instance();
    // The text tool keeps editing the shape it already edits, and switching to
    // the tool it already is does nothing, so leave the tool first.  Selecting
    // the header/footer has to happen while it is not the text tool, as the
    // text tool reverts selection changes.
    const bool textToolActive = toolManager->activeToolId() == QLatin1String("TextToolFactory_ID");
    if (textToolActive) {
        toolManager->switchToolRequested(KoInteractionTool_ID);
    }

    KoSelection *selection = m_canvas->shapeManager()->selection();
    selection->deselectAll();
    selection->select(editableShape);
    toolManager->switchToolRequested(QStringLiteral("TextToolFactory_ID"));

    if (KoTextEditor *editor = KoTextDocument(frameSet->document()).textEditor()) {
        // Put the cursor at the end of the (possibly still empty) header/footer.
        editor->setPosition(editor->document()->characterCount() - 1);
    }
    m_canvas->setFocus(Qt::OtherFocusReason);
    return true;
}

QWidget *KWView::canvas() const
{
    return m_canvas;
}

void KWView::updateReadWrite(bool readWrite)
{
    m_actionFormatFrameSet->setEnabled(readWrite);
    m_actionViewHeader->setEnabled(readWrite);
    m_actionViewFooter->setEnabled(readWrite);
    m_actionViewSnapToGrid->setEnabled(readWrite);
    QAction *action = actionCollection()->action("insert_framebreak");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("insert_variable");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("format_page");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("anchor");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("edit_cut");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("edit_copy");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("edit_paste");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("edit_paste_text");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("delete_page");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("edit_delete");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("create_linked_frame");
    if (action)
        action->setEnabled(readWrite);
    action = actionCollection()->action("create_custom_outline");
    if (action)
        action->setEnabled(readWrite);
}

void KWView::buildAssociatedWidget()
{
    wordCount = new KWStatisticsWidget(this, true);
    wordCount->setLayoutDirection(KWStatisticsWidget::LayoutHorizontal);
    wordCount->setCanvas(dynamic_cast<KWCanvas *>(this->canvas()));
    if (statusBar())
        statusBar()->insertWidget(0, wordCount);
}

void KWView::setupActions()
{
    m_actionFormatFrameSet = new QAction(i18n("Shape Properties..."), this);
    actionCollection()->addAction("format_frameset", m_actionFormatFrameSet);
    m_actionFormatFrameSet->setToolTip(i18n("Change how the shape behave"));
    m_actionFormatFrameSet->setEnabled(false);
    connect(m_actionFormatFrameSet, &QAction::triggered, this, &KWView::editFrameProperties);

    QAction *action = actionCollection()->addAction(KStandardAction::Prior, "page_previous", this, SLOT(goToPreviousPage()));

    action = actionCollection()->addAction(KStandardAction::Next, "page_next", this, SLOT(goToNextPage()));

    // -------------- File menu
    m_actionCreateTemplate = new QAction(i18n("Create Template From Document..."), this);
    m_actionCreateTemplate->setToolTip(i18n("Save this document and use it later as a template"));
    m_actionCreateTemplate->setWhatsThis(
        i18n("You can save this document as a template.<br><br>You can use this new template as a starting point for another document."));
    actionCollection()->addAction("extra_template", m_actionCreateTemplate);
    connect(m_actionCreateTemplate, &QAction::triggered, this, &KWView::createTemplate);

    // -------------- Edit actions
    action = actionCollection()->addAction(KStandardAction::Cut, "edit_cut", nullptr, nullptr);
    new KoCutController(canvasBase(), action);
    action = actionCollection()->addAction(KStandardAction::Copy, "edit_copy", nullptr, nullptr);
    new KoCopyController(canvasBase(), action);
    action = actionCollection()->addAction(KStandardAction::Paste, "edit_paste", nullptr, nullptr);
    // Paste is handled here instead of by KoPasteController so that an image
    // on the clipboard can be inserted at the text cursor (including inside
    // headers and footers) rather than being dropped at the mouse position.
    connect(action, &QAction::triggered, this, &KWView::pasteRequested);

    action = new QAction(koIcon("edit-delete"), i18n("Delete"), this);
    action->setShortcut(QKeySequence("Del"));
    connect(action, &QAction::triggered, this, &KWView::editDeleteSelection);
    // Keep Delete enabled based on the shape selection instead of
    // KoToolProxy::selectionChanged: the latter reports the active tool's
    // internal selection (a text selection for the text tool, nothing at all for
    // shape tools like the chart tool), so Delete got disabled exactly when an
    // object was selected and Del had to work.
    auto updateDeleteAction = [this, action]() {
        action->setEnabled(kwdocument()->isReadWrite() && !canvasBase()->shapeManager()->selection()->selectedShapes().isEmpty());
    };
    connect(canvasBase()->shapeManager()->selection(), &KoSelection::selectionChanged, this, updateDeleteAction);
    updateDeleteAction();
    actionCollection()->addAction("edit_delete", action);

    // -------------- View menu
    action = new QAction(i18n("Show Formatting Characters"), this);
    action->setCheckable(true);
    actionCollection()->addAction("view_formattingchars", action);
    connect(action, &QAction::toggled, this, &KWView::setShowFormattingChars);
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowFormattingCharacters, QVariant(false));
    action->setChecked(m_document->config().showFormattingChars()); // will change resource if true
    action->setToolTip(i18n("Toggle the display of non-printing characters"));
    action->setWhatsThis(
        i18n("Toggle the display of non-printing characters.<br/><br/>When this is enabled, Words shows you tabs, spaces, carriage returns and other "
             "non-printing characters."));

    action = new QAction(i18n("Show Field Shadings"), this);
    action->setCheckable(true);
    actionCollection()->addAction("view_fieldshadings", action);
    connect(action, &QAction::toggled, this, &KWView::setShowInlineObjectVisualization);
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowInlineObjectVisualization, QVariant(false));
    action->setChecked(m_document->config().showInlineObjectVisualization()); // will change resource if true
    action->setToolTip(i18n("Toggle the shaded background of fields"));
    action->setWhatsThis(i18n("Toggle the visualization of fields (variables etc.) by drawing their background in a contrasting color."));

    action = new QAction(i18n("Show Text Shape Borders"), this);
    action->setToolTip(i18n("Turns the border display on and off"));
    action->setCheckable(true);
    actionCollection()->addAction("view_frameborders", action);
    connect(action, &QAction::toggled, this, &KWView::toggleViewFrameBorders);
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowTextShapeOutlines, QVariant(false));
    action->setChecked(m_document->config().viewFrameBorders()); // will change resource if true
    action->setWhatsThis(
        i18n("Turns the border display on and off.<br/><br/>The borders are never printed. This option is useful to see how the document will appear on the "
             "printed page."));

    action = new QAction(i18n("Show Table Borders"), this);
    action->setCheckable(true);
    actionCollection()->addAction("view_tableborders", action);
    connect(action, &QAction::toggled, this, &KWView::setShowTableBorders);
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowTableBorders, QVariant(false));
    action->setChecked(m_document->config().showTableBorders()); // will change resource if true
    action->setToolTip(i18n("Toggle the display of table borders"));
    action->setWhatsThis(
        i18n("Toggle the display of table borders.<br/><br/>When this is enabled, Words shows you any invisible table borders with a thin gray line."));

    action = new QAction(i18n("Show Section Bounds"), this);
    action->setCheckable(true);
    actionCollection()->addAction("view_sectionbounds", action);
    connect(action, &QAction::toggled, this, &KWView::setShowSectionBounds);
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowSectionBounds, QVariant(false));
    action->setChecked(m_document->config().showSectionBounds()); // will change resource if true
    action->setToolTip(i18n("Toggle the display of section bounds"));
    action->setWhatsThis(i18n(
        "Toggle the display of section bounds.<br/><br/>When this is enabled, any section bounds will be indicated with a thin gray horizontal brackets."));

    action = new QAction(i18n("Show Rulers"), this);
    action->setCheckable(true);
    action->setToolTip(i18n("Shows or hides rulers"));
    action->setWhatsThis(
        i18n("The rulers are the white measuring spaces top and left of the "
             "document. The rulers show the position and width of pages and of frames and can "
             "be used to position tabulators among others.<p>Uncheck this to disable "
             "the rulers from being displayed.</p>"));
    action->setChecked(m_document->config().viewRulers());
    actionCollection()->addAction("show_ruler", action);
    connect(action, &QAction::toggled, this, &KWView::showRulers);

    action = m_document->gridData().gridToggleAction(m_canvas);
    actionCollection()->addAction("view_grid", action);

    m_actionViewSnapToGrid = new KToggleAction(i18n("Snap to Grid"), this);
    actionCollection()->addAction("view_snaptogrid", m_actionViewSnapToGrid);
    m_actionViewSnapToGrid->setChecked(m_snapToGrid);
    connect(m_actionViewSnapToGrid, &QAction::triggered, this, &KWView::toggleSnapToGrid);

    KToggleAction *guides = KoStandardAction::showGuides(this, SLOT(setGuideVisibility(bool)), actionCollection());
    guides->setChecked(m_document->guidesData().showGuideLines());

    KToggleAction *tAction = new KToggleAction(i18n("Show Status Bar"), this);
    tAction->setToolTip(i18n("Shows or hides the status bar"));
    actionCollection()->addAction("showStatusBar", tAction);
    connect(tAction, &QAction::toggled, this, &KWView::showStatusBar);

    if (mainWindow()) {
        mainWindow()->actionCollection()->action("view_fullscreen")->setEnabled(false);
        tAction = new KToggleAction(i18n("Fullscreen Mode"), this);
        tAction->setToolTip(i18n("Set view in fullscreen mode"));
        tAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
        actionCollection()->addAction("view_fullscreen", tAction);
        connect(tAction, &QAction::toggled, this, &KWView::setFullscreenMode);
    }

#ifdef SHOULD_BUILD_RDF
    action = new QAction(i18n("Semantic Stylesheets..."), this);
    actionCollection()->addAction("edit_semantic_stylesheets", action);
    action->setToolTip(i18n("Modify and add semantic stylesheets"));
    action->setWhatsThis(i18n("Stylesheets are used to format the display of information which is stored in RDF."));
    connect(action, SIGNAL(triggered()), this, SLOT(editSemanticStylesheets()));

    if (KoDocumentRdf *rdf = dynamic_cast<KoDocumentRdf *>(m_document->documentRdf())) {
        QAction *createRef = rdf->createInsertSemanticObjectReferenceAction(canvasBase());
        actionCollection()->addAction("insert_semanticobject_ref", createRef);
        KActionMenu *subMenu = new KActionMenu(i18n("Create"), this);
        actionCollection()->addAction("insert_semanticobject_new", subMenu);
        foreach (QAction *action, rdf->createInsertSemanticObjectNewActions(canvasBase())) {
            subMenu->addAction(action);
        }
    }
#endif

    // -------------- Settings menu
    action = new QAction(koIcon("configure"), i18n("Configure..."), this);
    actionCollection()->addAction("configure", action);
    connect(action, &QAction::triggered, this, &KWView::configure);
    // not sure why this isn't done through KStandardAction, but since it isn't
    // we ought to set the MenuRole manually so the item ends up in the appropriate
    // menu on OS X:
    action->setMenuRole(QAction::PreferencesRole);

    // -------------- Page tool
    action = new QAction(i18n("Page Layout..."), this);
    actionCollection()->addAction("format_page", action);
    action->setToolTip(i18n("Change properties of entire page"));
    action->setWhatsThis(i18n(
        "Change properties of the entire page.<p>Currently you can change paper size, paper orientation, header and footer sizes, and column settings.</p>"));
    connect(action, &QAction::triggered, this, &KWView::formatPage);

    m_actionViewHeader = new QAction(i18n("Create Header"), this);
    actionCollection()->addAction("insert_header", m_actionViewHeader);
    if (m_currentPage.isValid())
        m_actionViewHeader->setEnabled(m_currentPage.pageStyle().headerPolicy() == Words::HFTypeNone);
    connect(m_actionViewHeader, &QAction::triggered, this, &KWView::enableHeader);

    m_actionViewFooter = new QAction(i18n("Create Footer"), this);
    actionCollection()->addAction("insert_footer", m_actionViewFooter);
    if (m_currentPage.isValid())
        m_actionViewFooter->setEnabled(m_currentPage.pageStyle().footerPolicy() == Words::HFTypeNone);
    connect(m_actionViewFooter, &QAction::triggered, this, &KWView::enableFooter);

    // -------- Show annotations (called Comments in the UI)
    tAction = new KToggleAction(i18n("Show Comments"), this);
    tAction->setToolTip(i18n("Shows comments in the document"));
    tAction->setChecked(m_canvas && m_canvas->showAnnotations());
    actionCollection()->addAction("view_notes", tAction);
    connect(tAction, &QAction::toggled, this, &KWView::showNotes);

    // -------- Statistics in the status bar
    KToggleAction *tActionBis = new KToggleAction(i18n("Word Count"), this);
    tActionBis->setToolTip(i18n("Shows or hides word counting in status bar"));
    tActionBis->setChecked(kwdocument()->config().statusBarShowWordCount());
    actionCollection()->addAction("view_wordCount", tActionBis);
    connect(tActionBis, &QAction::toggled, this, &KWView::showWordCountInStatusBar);
    wordCount->setVisible(kwdocument()->config().statusBarShowWordCount());

    /* ********** From old kwview ****
    We probably want to have each of these again, so just move them when you want to implement it
    This saves problems with finding out which we missed near the end.

    m_actionEditCustomVarsEdit = new QAction(i18n("Custom Variables..."), 0,
            this, SLOT(editCustomVars()), // TODO: new dialog w add etc.
            actionCollection(), "custom_vars");

    m_actionEditCustomVars = new QAction(i18n("Edit Variable..."), 0,
            this, SLOT(editCustomVariable()),
            actionCollection(), "edit_customvars");

    m_actionImportStyle= new QAction(i18n("Import Styles..."), 0,
            this, SLOT(importStyle()),
            actionCollection(), "import_style");

    m_actionConfigureCompletion = new QAction(i18n("Configure Completion..."), 0,
            this, SLOT(configureCompletion()),
            actionCollection(), "configure_completion");
    m_actionConfigureCompletion->setToolTip(i18n("Change the words and options for autocompletion"));
    m_actionConfigureCompletion->setWhatsThis(i18n("Add words or change the options for autocompletion."));

    new QAction(i18n("Completion"), KStdAccel::shortcut(KStdAccel::TextCompletion), this, SLOT(slotCompletion()), actionCollection(), "completion");
    */

    // -------------- Mail merge (Mailings ribbon tab)
    QAction *mailMergeSelect = new QAction(koIcon("document-open"), i18n("Select Recipients..."), this);
    actionCollection()->addAction("mailmerge_select_recipients", mailMergeSelect);
    mailMergeSelect->setToolTip(i18n("Load the recipient list for a mail merge from a CSV or tab separated file"));
    connect(mailMergeSelect, &QAction::triggered, this, &KWView::mailMergeSelectRecipients);

    QAction *mailMergeEdit = new QAction(koIcon("document-edit"), i18n("Edit Recipient List..."), this);
    actionCollection()->addAction("mailmerge_edit_recipients", mailMergeEdit);
    mailMergeEdit->setToolTip(i18n("View and edit the mail merge recipients"));
    connect(mailMergeEdit, &QAction::triggered, this, &KWView::mailMergeEditRecipients);

    m_mailMergeFieldMenu = new KActionMenu(koIcon("insert-text"), i18n("Insert Merge Field"), this);
    actionCollection()->addAction("mailmerge_insert_field", m_mailMergeFieldMenu);
    m_mailMergeFieldMenu->setToolTip(i18n("Insert a merge field that shows a recipient value"));
    connect(m_mailMergeFieldMenu->menu(), &QMenu::triggered, this, &KWView::mailMergeInsertField);

    m_actionMailMergeGreeting = new QAction(i18n("Greeting Line"), this);
    actionCollection()->addAction("mailmerge_greeting_line", m_actionMailMergeGreeting);
    m_actionMailMergeGreeting->setToolTip(i18n("Insert a greeting line built from merge fields"));
    connect(m_actionMailMergeGreeting, &QAction::triggered, this, &KWView::mailMergeGreetingLine);

    m_actionMailMergeHighlight = new KToggleAction(i18n("Highlight Merge Fields"), this);
    actionCollection()->addAction("mailmerge_highlight", m_actionMailMergeHighlight);
    m_actionMailMergeHighlight->setChecked(KWMailMergeManager::instance()->highlightEnabled());
    connect(m_actionMailMergeHighlight, &KToggleAction::toggled, this, &KWView::mailMergeHighlightFields);

    m_actionMailMergePreview = new KToggleAction(koIcon("document-preview"), i18n("Preview Results"), this);
    actionCollection()->addAction("mailmerge_preview", m_actionMailMergePreview);
    connect(m_actionMailMergePreview, &KToggleAction::toggled, this, &KWView::mailMergePreview);

    m_actionMailMergeFirst = new QAction(QIcon::fromTheme("go-first"), i18n("First Record"), this);
    actionCollection()->addAction("mailmerge_first", m_actionMailMergeFirst);
    connect(m_actionMailMergeFirst, &QAction::triggered, this, &KWView::mailMergeFirstRecord);

    m_actionMailMergePrev = new QAction(QIcon::fromTheme("go-previous"), i18n("Previous Record"), this);
    actionCollection()->addAction("mailmerge_prev", m_actionMailMergePrev);
    connect(m_actionMailMergePrev, &QAction::triggered, this, &KWView::mailMergePreviousRecord);

    m_actionMailMergeNext = new QAction(QIcon::fromTheme("go-next"), i18n("Next Record"), this);
    actionCollection()->addAction("mailmerge_next", m_actionMailMergeNext);
    connect(m_actionMailMergeNext, &QAction::triggered, this, &KWView::mailMergeNextRecord);

    m_actionMailMergeLast = new QAction(QIcon::fromTheme("go-last"), i18n("Last Record"), this);
    actionCollection()->addAction("mailmerge_last", m_actionMailMergeLast);
    connect(m_actionMailMergeLast, &QAction::triggered, this, &KWView::mailMergeLastRecord);

    m_actionMailMergeFinish = new QAction(koIcon("document-export"), i18n("Edit Individual Documents"), this);
    actionCollection()->addAction("mailmerge_finish", m_actionMailMergeFinish);
    m_actionMailMergeFinish->setToolTip(i18n("Merge all included recipients into one document, one letter per recipient"));
    connect(m_actionMailMergeFinish, &QAction::triggered, this, &KWView::mailMergeFinish);

    connect(KWMailMergeManager::instance(), &KWMailMergeManager::recipientsChanged, this, &KWView::updateMailMergeActions);
    connect(KWMailMergeManager::instance(), &KWMailMergeManager::recordChanged, this, &KWView::updateMailMergeActions);
    connect(KWMailMergeManager::instance(), &KWMailMergeManager::displayChanged, this, &KWView::updateMailMergeActions);
    updateMailMergeActions();
}

QList<KoShape *> KWView::selectedShapes() const
{
    return canvasBase()->shapeManager()->selection()->selectedShapes(KoFlake::TopLevelSelection);
}

KoShape *KWView::selectedShape() const
{
    KoSelection *selection = canvasBase()->shapeManager()->selection();

    foreach (KoShape *s, selection->selectedShapes(KoFlake::TopLevelSelection)) {
        if (s->isGeometryProtected())
            continue;
        return s;
    }

    return nullptr;
}

// -------------------- Actions -----------------------

/**
 * Find the image an HTML snippet points at, which is how browsers and chat
 * applications usually expose an image that was copied from them.
 */
static QUrl imageUrlFromHtml(const QString &html)
{
    if (html.isEmpty()) {
        return QUrl();
    }
    static const QRegularExpression imageTag(QStringLiteral("<img[^>]*\\bsrc\\s*=\\s*[\"']([^\"']+)[\"']"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = imageTag.match(html);
    if (!match.hasMatch()) {
        return QUrl();
    }
    const QString source = match.captured(1);
    if (source.startsWith(QLatin1String("data:"))) {
        return QUrl(source);
    }
    const QUrl url = QUrl::fromUserInput(source);
    return url.isValid() ? url : QUrl();
}

static QImage imageFromDataUri(const QUrl &url)
{
    const QString data = url.toString();
    const int comma = data.indexOf(QLatin1Char(','));
    if (comma < 0 || !data.contains(QLatin1String(";base64"))) {
        return QImage();
    }
    const QByteArray encoded = data.mid(comma + 1).toLatin1();
    return QImage::fromData(QByteArray::fromBase64(encoded));
}

QVector<QImage> KWView::imagesFromClipboard(const QMimeData *mimeData, QUrl *remoteImage) const
{
    QVector<QImage> images;

    // A plain bitmap, which is what most applications offer for a copied image.
    const QImage image = QApplication::clipboard()->image();
    if (!image.isNull()) {
        images.append(image);
        return images;
    }

    // Any other bitmap flavour the application offers.
    const QStringList formats = mimeData->formats();
    for (const QString &format : formats) {
        if (!format.startsWith(QLatin1String("image/")) || format == QLatin1String("image/svg+xml")) {
            continue;
        }
        const QImage flavourImage = QImage::fromData(mimeData->data(format));
        if (!flavourImage.isNull()) {
            images.append(flavourImage);
        }
    }
    if (!images.isEmpty()) {
        return images;
    }

    // Copied files, or a copied link to an image on the web.
    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            if (remoteImage && !remoteImage->isValid() && url.isValid() && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))) {
                *remoteImage = url;
            }
            continue;
        }
        QImageReader reader(url.toLocalFile());
        reader.setAutoTransform(true);
        const QImage loaded = reader.read();
        if (!loaded.isNull()) {
            images.append(loaded);
        }
    }
    if (!images.isEmpty() || (remoteImage && remoteImage->isValid())) {
        return images;
    }

    // An image copied out of a browser or a chat window is usually only
    // available as an HTML snippet pointing at the image.  Copied text that
    // happens to contain an image keeps pasting as text, though.
    const QString plainText = mimeData->text().trimmed();
    const bool textIsJustALink = !plainText.isEmpty() && !plainText.contains(QLatin1Char(' '))
        && (plainText.startsWith(QLatin1String("http")) || plainText.startsWith(QLatin1String("file:")));
    if (!plainText.isEmpty() && !textIsJustALink) {
        return images;
    }
    const QUrl imageUrl = imageUrlFromHtml(mimeData->html());
    if (imageUrl.isValid()) {
        if (imageUrl.isLocalFile()) {
            QImageReader reader(imageUrl.toLocalFile());
            reader.setAutoTransform(true);
            const QImage loaded = reader.read();
            if (!loaded.isNull()) {
                images.append(loaded);
            }
        } else if (imageUrl.scheme() == QLatin1String("data")) {
            const QImage loaded = imageFromDataUri(imageUrl);
            if (!loaded.isNull()) {
                images.append(loaded);
            }
        } else if (remoteImage && (imageUrl.scheme() == QLatin1String("http") || imageUrl.scheme() == QLatin1String("https"))) {
            *remoteImage = imageUrl;
        }
    }
    return images;
}

KWTextFrameSet *KWView::pasteTargetTextFrameSet()
{
    KWTextFrameSet *textFrameSet = currentTextFrameSet();
    // An empty header or footer has no text to put a cursor into, so a click
    // there leaves the caret (if any) in the body.  When the mouse points at a
    // header/footer band, a pasted image still belongs to that header/footer.
    if (!textFrameSet || textFrameSet->textFrameSetType() == Words::MainTextFrameSet) {
        if (KWTextFrameSet *headerFooter = headerFooterFrameSetAtPointer()) {
            textFrameSet = headerFooter;
        }
    }
    return textFrameSet;
}

void KWView::insertPastedImages(const QVector<QImage> &images)
{
    if (images.isEmpty()) {
        return;
    }
    if (KWTextFrameSet *textFrameSet = pasteTargetTextFrameSet()) {
        insertImagesIntoTextFrame(textFrameSet, images);
    } else {
        // No text caret: keep the historical behaviour and drop the image where
        // the mouse points.
        addImages(images, canvas()->mapFromGlobal(QCursor::pos()));
    }
}

void KWView::pasteImageFromUrl(const QUrl &url)
{
    QPointer<KWTextFrameSet> targetFrameSet(pasteTargetTextFrameSet());

    auto *manager = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manager, targetFrameSet]() {
        const QByteArray data = reply->readAll();
        const bool failed = reply->error() != QNetworkReply::NoError;
        const QUrl imageUrl = reply->url();
        reply->deleteLater();
        manager->deleteLater();
        if (failed) {
            warnWords << "Could not download the pasted image" << imageUrl;
            return;
        }
        QImage image;
        if (!image.loadFromData(data) || image.isNull()) {
            return;
        }
        if (targetFrameSet) {
            insertImagesIntoTextFrame(targetFrameSet.data(), QVector<QImage>{image});
        }
    });
}

/**
 * Pictures of shapes that were copied inside the document only reach the
 * clipboard as ODF data, so they are pulled out of that for a header or a
 * footer, where a pasted picture has to become part of the text.
 */
static QVector<QImage> imagesFromCopiedShapes(const QMimeData *mimeData)
{
    QVector<QImage> images;
    QString format;
    const QStringList formats = mimeData->formats();
    for (const QString &candidate : formats) {
        if (candidate.startsWith(QLatin1String("application/vnd.oasis.opendocument."))) {
            format = candidate;
            break;
        }
    }
    if (format.isEmpty()) {
        return images;
    }

    QBuffer buffer;
    buffer.setData(mimeData->data(format));
    if (!buffer.open(QIODevice::ReadOnly)) {
        return images;
    }
    const QScopedPointer<KoStore> store(KoStore::createStore(&buffer, KoStore::Read, format.toLatin1()));
    if (!store || store->bad()) {
        return images;
    }

    QByteArray content;
    if (!store->extractFile(QStringLiteral("content.xml"), content)) {
        return images;
    }
    static const QRegularExpression hrefExpression(QStringLiteral("xlink:href\\s*=\\s*\"([^\"]+)\""));
    QStringList pictureNames;
    QRegularExpressionMatchIterator matches = hrefExpression.globalMatch(QString::fromUtf8(content));
    while (matches.hasNext()) {
        const QString name = matches.next().captured(1);
        if (name.startsWith(QLatin1String("Pictures/")) && !pictureNames.contains(name)) {
            pictureNames.append(name);
        }
    }
    for (const QString &name : pictureNames) {
        QByteArray pictureData;
        if (!store->extractFile(name, pictureData)) {
            continue;
        }
        const QImage image = QImage::fromData(pictureData);
        if (!image.isNull()) {
            images.append(image);
        }
    }
    return images;
}

void KWView::pasteRequested()
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();

    if (mimeData) {
        // A bitmap (or a copied image file) belongs at the text cursor: when the
        // caret sits in a header or footer the image has to become part of that
        // header/footer instead of a free floating shape at the mouse position.
        const bool hasRichText = mimeData->hasFormat(QStringLiteral("application/vnd.oasis.opendocument.text"));
        KWTextFrameSet *target = pasteTargetTextFrameSet();
        const bool inHeaderOrFooter = target && target->textFrameSetType() != Words::MainTextFrameSet;

        QUrl remoteImage;
        QVector<QImage> images;
        if (!hasRichText) {
            images = imagesFromClipboard(mimeData, &remoteImage);
        } else if (inHeaderOrFooter) {
            // A picture copied in the document body is only on the clipboard as
            // ODF shape data; in a header or footer it becomes part of the text
            // like any other pasted image instead of a floating shape.
            images = imagesFromCopiedShapes(mimeData);
        }

        if (!images.isEmpty()) {
            insertPastedImages(images);
            return;
        }
        if (remoteImage.isValid()) {
            pasteImageFromUrl(remoteImage);
            return;
        }
    }

    if (m_canvas) {
        m_canvas->toolProxy()->paste();
    }
}

void KWView::showNotes(bool show)
{
    m_canvas->setShowAnnotations(show);
    m_canvas->updateSize();
}

void KWView::hasNotes(bool has)
{
    m_canvas->setShowAnnotations(has);
    m_canvas->updateSize();

    KToggleAction *action = (KToggleAction *)actionCollection()->action("view_notes");
    action->setEnabled(has);
    action->setChecked(has);
}

void KWView::showWordCountInStatusBar(bool doShow)
{
    kwdocument()->config().setStatusBarShowWordCount(doShow);
    wordCount->setVisible(doShow);
}

void KWView::editFrameProperties()
{
    const QList<KoShape *> &shapes = selectedShapes();
    if (!shapes.isEmpty()) {
        QPointer<KWFrameDialog> frameDialog = new KWFrameDialog(shapes, m_document, m_canvas);
        frameDialog->exec();
        delete frameDialog;
    }
}

KoPrintJob *KWView::createPrintJob()
{
    auto dia = new KWPrintingDialog(m_document, m_canvas->shapeManager(), this);
    dia->printer().setResolution(600);
    dia->printer().setCreator(QString::fromLatin1("Calligra Words %1").arg(CALLIGRA_VERSION));
    dia->printer().setFullPage(true); // ignore printer margins
    return dia;
}

QPrintDialog *KWView::createPrintDialog(KoPrintJob *printJob, QWidget *parent)
{
    return new KWPrintDialog(printJob, parent);
}

void KWView::createTemplate()
{
    KoTemplateCreateDia::createTemplate(koDocument()->documentPart()->templatesResourcePath(), ".ott", m_document, this);
}

void KWView::enableHeader()
{
    if (!m_currentPage.isValid())
        return;
    Q_ASSERT(m_currentPage.pageStyle().isValid());
    m_currentPage.pageStyle().setHeaderPolicy(Words::HFTypeUniform);
    m_actionViewHeader->setEnabled(false);
    m_document->relayout();
}

void KWView::enableFooter()
{
    if (!m_currentPage.isValid())
        return;
    Q_ASSERT(m_currentPage.pageStyle().isValid());
    m_currentPage.pageStyle().setFooterPolicy(Words::HFTypeUniform);
    m_actionViewFooter->setEnabled(false);
    m_document->relayout();
}

void KWView::toggleSnapToGrid()
{
    m_snapToGrid = !m_snapToGrid;
    m_document->gridData().setSnapToGrid(m_snapToGrid); // for persistency
}

void KWView::toggleViewFrameBorders(bool on)
{
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowTextShapeOutlines, on);
    m_canvas->update();
    m_document->config().setViewFrameBorders(on);
}

void KWView::setShowInlineObjectVisualization(bool on)
{
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowInlineObjectVisualization, QVariant(on));
    m_canvas->update();
    m_document->config().setShowInlineObjectVisualization(on);
}

void KWView::setShowFormattingChars(bool on)
{
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowFormattingCharacters, QVariant(on));
    m_canvas->update();
    m_document->config().setShowFormattingChars(on);
}

void KWView::setShowTableBorders(bool on)
{
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowTableBorders, QVariant(on));
    m_canvas->update();
    m_document->config().setShowTableBorders(on);
}

void KWView::setShowSectionBounds(bool on)
{
    m_canvas->resourceManager()->setResource(KoCanvasResourceManager::ShowSectionBounds, QVariant(on));
    m_canvas->update();
    //     m_document->config().setShowSectionBounds(on);
}

void KWView::formatPage()
{
    if (!m_currentPage.isValid())
        return;
    KWPageSettingsDialog *dia = new KWPageSettingsDialog(this, m_document, m_currentPage);
    if (!m_lastPageSettingsTab.isEmpty()) {
        KPageWidgetItem *item = dia->pageItem(m_lastPageSettingsTab);
        if (item)
            dia->setCurrentPage(item);
    }
    connect(dia, &QDialog::finished, this, &KWView::pageSettingsDialogFinished);
    dia->show();
}

void KWView::pageSettingsDialogFinished()
{
    KWPageSettingsDialog *dia = qobject_cast<KWPageSettingsDialog *>(QObject::sender());
    m_lastPageSettingsTab = dia && dia->currentPage() ? dia->currentPage()->name() : QString();
}

void KWView::editSemanticStylesheets()
{
#ifdef SHOULD_BUILD_RDF
    if (KoDocumentRdf *rdf = dynamic_cast<KoDocumentRdf *>(m_document->documentRdf())) {
        KoSemanticStylesheetsEditor *dia = new KoSemanticStylesheetsEditor(this, rdf);
        dia->show();
        // TODO this leaks memory
    }
#endif
}

void KWView::showRulers(bool visible)
{
    m_document->config().setViewRulers(visible);
    m_gui->updateRulers();
}

void KWView::showStatusBar(bool toggled)
{
    if (statusBar())
        statusBar()->setVisible(toggled);
}

void KWView::setFullscreenMode(bool status)
{
    m_isFullscreenMode = status;

    mainWindow()->toggleDockersVisibility(!status);
    mainWindow()->menuBar()->setVisible(!status);

    mainWindow()->viewFullscreen(status);
    foreach (KToolBar *toolbar, mainWindow()->toolBars()) {
        if (toolbar->isVisible() == status) {
            toolbar->setVisible(!status);
        }
    }

    if (status) {
        QTimer::singleShot(2000, this, &KWView::hideUI);
    } else {
        mainWindow()->statusBar()->setVisible(true);
        static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    // Exit Fullscreen mode button.
    m_dfmExitButton->setVisible(status);

    // Hide cursor.
    if (status) {
        m_hideCursorTimer->start(4000);
    } else {
        // FIXME: Return back cursor to canvas if cursor is blank cursor.
        m_hideCursorTimer->stop();
    }

    // From time to time you can end up in a situation where the shape manager suddenly
    // looses track of the current shape selection. So, we trick it here. Logically,
    // it also makes sense to just make sure the text tool is active anyway when
    // switching to/from fullscreen (since that's explicitly for typing things
    // out, not layouting)
    const QList<KoShape *> selection = m_canvas->shapeManager()->selection()->selectedShapes();
    m_canvas->shapeManager()->selection()->deselectAll();
    if (selection.size() > 0)
        m_canvas->shapeManager()->selection()->select(selection.at(0));
    KoToolManager::instance()->switchToolRequested("TextToolFactory_ID");
}

void KWView::hideUI()
{
    if (m_isFullscreenMode) {
        mainWindow()->statusBar()->setVisible(false);
        // Hide vertical  and horizontal scroll bar.
        static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
}

void KWView::hideCursor()
{
    m_canvas->setCursor(Qt::BlankCursor);
    m_gui->setCursor(Qt::BlankCursor);
}

void KWView::exitFullscreenMode()
{
    if (m_isFullscreenMode) {
        QAction *action = actionCollection()->action("view_fullscreen");
        action->setChecked(false);
        m_gui->setCursor(Qt::ArrowCursor);
        setFullscreenMode(false);
    }
}

void KWView::viewMouseMoveEvent(QMouseEvent *e)
{
    if (!m_isFullscreenMode)
        return;

    m_gui->setCursor(Qt::ArrowCursor);

    // Handle status bar and horizontal scroll bar.
    if (e->position().y() >= (m_gui->size().height() - statusBar()->size().height())) {
        mainWindow()->statusBar()->setVisible(true);
        static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    } else {
        mainWindow()->statusBar()->setVisible(false);
        static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }

    // Handle vertical scroll bar.
    QScrollBar *vsb = static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->verticalScrollBar();
    if (e->position().x() >= (m_gui->size().width() - vsb->size().width() - 10)) {
        static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    } else {
        static_cast<KoCanvasControllerWidget *>(m_gui->canvasController())->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
}

void KWView::editDeleteSelection()
{
    const QList<KoShape *> selectedShapes = canvasBase()->shapeManager()->selection()->selectedShapes(KoFlake::TopLevelSelection);
    QList<KoShape *> deletableShapes;
    for (KoShape *shape : selectedShapes) {
        if (shape->isDeletable() && !shape->isGeometryProtected()) {
            deletableShapes.append(shape);
        }
    }
    if (!deletableShapes.isEmpty()) {
        canvasBase()->addCommand(canvasBase()->shapeController()->removeShapes(deletableShapes));

        // Office-like behavior: deleting a selected object returns to text
        // editing mode.  Without this the shape tool stays active (its Draw tab
        // stays highlighted) and the page's top-level text frame can end up
        // selected as an object.  Select the body text frame and activate the
        // text tool instead so the caret is ready for typing again.
        KWTextFrameSet *mainFs = kwdocument()->mainFrameSet();
        if (mainFs && !mainFs->shapes().isEmpty()) {
            KoSelection *selection = canvasBase()->shapeManager()->selection();
            selection->deselectAll();
            selection->select(mainFs->shapes().first());
            KoToolManager::instance()->switchToolRequested(QStringLiteral("TextToolFactory_ID"));
        }
        return;
    }
    canvasBase()->toolProxy()->deleteSelection();
}

void KWView::setGuideVisibility(bool on)
{
    m_document->guidesData().setShowGuideLines(on);
    m_canvas->update();
}

void KWView::configure()
{
    QPointer<KWConfigureDialog> dialog(new KWConfigureDialog(this));
    dialog->exec();
    delete dialog;
    // TODO update canvas
}

// end of actions

void KWView::popupContextMenu(const QPoint &globalPosition, const QList<QAction *> &actions)
{
    unplugActionList("frameset_type_action");
    plugActionList("frameset_type_action", actions);
    if (factory() == nullptr) // we are a kpart, the factory is only set on the active component.
        return;
    QMenu *menu = dynamic_cast<QMenu *>(factory()->container("frame_popup", this));
    if (menu)
        menu->exec(globalPosition);
}

void KWView::zoomChanged(KoZoomMode::Mode mode, qreal zoom)
{
    m_document->config().setZoom(qRound(zoom * 100.0));
    m_document->config().setZoomMode(mode);
    m_canvas->update();
}

void KWView::selectionChanged()
{
    KoShape *shape = canvasBase()->shapeManager()->selection()->firstSelectedShape();

    if (shape) {
        // Disallow shape properties action for auto-generated frames.
        const KWFrame *frame = kwdocument()->frameOfShape(shape);
        const bool enableAction = !frame || !Words::isAutoGenerated(KWFrameSet::from(frame->shape()));
        m_actionFormatFrameSet->setEnabled(enableAction);
        m_actionFormatFrameSet->setVisible(enableAction);
    }

    // actions that need at least one shape selected
    QAction *action = actionCollection()->action("anchor");
    if (action)
        action->setEnabled(shape && kwdocument()->mainFrameSet());
}

void KWView::setCurrentPage(const KWPage &currentPage)
{
    Q_ASSERT(currentPage.isValid());
    if (currentPage != m_currentPage) {
        m_currentPage = currentPage;
        m_canvas->resourceManager()->setResource(KoCanvasResourceManager::CurrentPage, m_currentPage.pageNumber());

        m_actionViewHeader->setEnabled(m_currentPage.pageStyle().headerPolicy() == Words::HFTypeNone);
        m_actionViewFooter->setEnabled(m_currentPage.pageStyle().footerPolicy() == Words::HFTypeNone);
    }
}

void KWView::goToPreviousPage(Qt::KeyboardModifiers modifiers)
{
    // Scroll display
    qreal moveDistance = m_canvas->canvasController()->visibleHeight() * 0.8;
    m_canvas->canvasController()->pan(QPoint(0, -moveDistance));

    // Find current frameset, FIXME for now assume main
    KWTextFrameSet *currentFrameSet = kwdocument()->mainFrameSet();

    // Since we move _up_ calculate the position where a frame would _start_ if
    // we were scrolled to the _first_ page
    QPointF pos = currentFrameSet->shapes().first()->absoluteTransformation(nullptr).map(QPointF(0, 5));

    pos += m_canvas->viewMode()->viewToDocument(m_canvas->documentOffset(), viewConverter());

    // Find textshape under that position and from current frameset
    QList<KoShape *> possibleTextShapes = m_canvas->shapeManager()->shapesAt(QRectF(pos.x() - 20, pos.y() - 20, 40, 40));
    KoTextShapeData *textShapeData = nullptr;
    foreach (KoShape *shape, possibleTextShapes) {
        KoShapeUserData *userData = shape->userData();
        if ((textShapeData = dynamic_cast<KoTextShapeData *>(userData))) {
            foreach (KoShape *s, currentFrameSet->shapes()) {
                if (s == shape) {
                    pos = shape->absoluteTransformation(nullptr).inverted().map(pos);
                    pos += QPointF(0.0, textShapeData->documentOffset());

                    KoTextLayoutArea *area = textShapeData->rootArea();
                    if (area) {
                        int cursorPos = area->hitTest(pos, Qt::FuzzyHit).position;
                        KoTextDocument(textShapeData->document())
                            .textEditor()
                            ->setPosition(cursorPos, (modifiers & Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
                    }
                    return;
                }
            }
        }
    }
}

void KWView::goToNextPage(Qt::KeyboardModifiers modifiers)
{
    // Scroll display
    qreal moveDistance = m_canvas->canvasController()->visibleHeight() * 0.8;
    m_canvas->canvasController()->pan(QPoint(0, moveDistance));

    // Find current frameset, FIXME for now assume main
    KWTextFrameSet *currentFrameSet = kwdocument()->mainFrameSet();

    // Since we move _down_ calculate the position where a frame would _end_ if
    // we were scrolled to the _lasst_ page
    KoShape *shape = currentFrameSet->shapes().last();
    QPointF pos = shape->absoluteTransformation(nullptr).map(QPointF(0, shape->size().height() - 5));
    pos.setY(pos.y() - m_document->pageManager()->page(qreal(pos.y())).rect().bottom());

    pos += m_canvas->viewMode()->viewToDocument(m_canvas->documentOffset() + QPointF(0, m_canvas->canvasController()->visibleHeight()), viewConverter());

    // Find textshape under that position and from current frameset
    QList<KoShape *> possibleTextShapes = m_canvas->shapeManager()->shapesAt(QRectF(pos.x() - 20, pos.y() - 20, 40, 40));
    KoTextShapeData *textShapeData = nullptr;
    foreach (KoShape *shape, possibleTextShapes) {
        KoShapeUserData *userData = shape->userData();
        if ((textShapeData = dynamic_cast<KoTextShapeData *>(userData))) {
            foreach (KoShape *s, currentFrameSet->shapes()) {
                if (s == shape) {
                    pos = shape->absoluteTransformation(nullptr).inverted().map(pos);
                    pos += QPointF(0.0, textShapeData->documentOffset());

                    KoTextLayoutArea *area = textShapeData->rootArea();
                    if (area) {
                        int cursorPos = area->hitTest(pos, Qt::FuzzyHit).position;
                        KoTextDocument(textShapeData->document())
                            .textEditor()
                            ->setPosition(cursorPos, (modifiers & Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
                    }
                    return;
                }
            }
        }
    }
}

void KWView::goToPage(const KWPage &page)
{
    KoCanvasController *controller = m_gui->canvasController();
    QPoint origPos = controller->scrollBarValue();
    QPointF pos = m_canvas->viewMode()->documentToView(QPointF(0, page.offsetInDocument()), m_canvas->viewConverter());
    origPos.setY((int)pos.y());
    controller->setScrollBarValue(origPos);
}

void KWView::showEvent(QShowEvent *e)
{
    KoView::showEvent(e);
    QTimer::singleShot(0, this, &KWView::updateStatusBarAction);
}

bool KWView::event(QEvent *event)
{
    switch (static_cast<int>(event->type())) {
    case ViewModeSwitchEvent::AboutToSwitchViewModeEvent: {
        ViewModeSynchronisationObject *syncObject = static_cast<ViewModeSwitchEvent *>(event)->synchronisationObject();
        if (m_canvas) {
            syncObject->scrollBarValue = m_canvas->canvasController()->scrollBarValue();
            syncObject->zoomLevel = zoomController()->zoomAction()->effectiveZoom();
            syncObject->activeToolId = KoToolManager::instance()->activeToolId();
            syncObject->shapes = m_canvas->shapeManager()->shapes();
            syncObject->initialized = true;
        }

        return true;
    }
    case ViewModeSwitchEvent::SwitchedToDesktopModeEvent: {
        ViewModeSynchronisationObject *syncObject = static_cast<ViewModeSwitchEvent *>(event)->synchronisationObject();
        if (m_canvas && syncObject->initialized) {
            m_canvas->canvasWidget()->setFocus();
            qApp->processEvents();

            m_canvas->shapeManager()->setShapes(syncObject->shapes);

            zoomController()->setZoom(KoZoomMode::ZOOM_CONSTANT, syncObject->zoomLevel);

            qApp->processEvents();
            m_canvas->canvasController()->setScrollBarValue(syncObject->scrollBarValue);

            qApp->processEvents();
            foreach (KoShape *shape, m_canvas->shapeManager()->shapesAt(currentPage().rect())) {
                if (qobject_cast<KoTextShapeDataBase *>(shape->userData())) {
                    m_canvas->shapeManager()->selection()->select(shape);
                    break;
                }
            }
            KoToolManager::instance()->switchToolRequested("TextToolFactory_ID");
        }

        return true;
    }
    }
    return QWidget::event(event);
}

void KWView::updateStatusBarAction()
{
    KToggleAction *action = (KToggleAction *)actionCollection()->action("showStatusBar");
    if (action && statusBar())
        action->setChecked(!statusBar()->isHidden());
}

void KWView::offsetInDocumentMoved(int yOffset)
{
    const qreal offset = -m_zoomHandler.viewToDocumentY(yOffset);
    const qreal height = m_zoomHandler.viewToDocumentY(m_gui->viewportSize().height());
    /*    if (m_currentPage.isValid()) { // most of the time the current will not change.
            const qreal pageTop = m_currentPage.offsetInDocument();
            const qreal pageBottom = pageTop + m_currentPage.height();
            const qreal visibleArea = qMin(offset + height, pageBottom) - qMax(pageTop, offset);
            if (visibleArea / height >= 0.45) // current page is just fine if > 45% is shown
                return;

            // using 'next'/'prev' is much cheaper than using a documentOffset, so try that first.
            if (pageTop > offset && pageTop < offset + height) { // check if the page above is a candidate.
                KWPage page = m_currentPage.previous();
                if (page.isValid() && pageTop - offset > visibleArea) {
                    firstDrawnPage = page;
                    return;
                }
            }
            if (pageBottom > offset && pageBottom < offset + height) { // check if the page above is a candidate.
                KWPage page = m_currentPage.next();
                if (page.isValid() && m_currentPage.height() - height > visibleArea) {
                    firstDrawnPage = page;
                    return;
                }
            }
        }
    */
    KWPage page = m_document->pageManager()->page(qreal(offset));
    qreal pageTop = page.offsetInDocument();
    QSizeF maxPageSize;
    qreal minTextX = std::numeric_limits<qreal>::max();
    qreal maxTextX = std::numeric_limits<qreal>::min();
    int minPageNum = page.pageNumber();
    int maxPageNum = page.pageNumber();
    while (page.isValid() && pageTop < qreal(offset + height)) {
        pageTop += page.height();
        QSizeF pageSize = page.rect().size();
        maxPageSize = QSize(qMax(maxPageSize.width(), pageSize.width()), qMax(maxPageSize.height(), pageSize.height()));
        minTextX = qMin(minTextX, page.contentRect().left());
        maxTextX = qMax(maxTextX, page.contentRect().right());
        maxPageNum = page.pageNumber();
        page = page.next();
    }

    if (maxPageSize != m_pageSize) {
        m_pageSize = maxPageSize;
        QSizeF pageSize = m_pageSize;
        if (m_canvas->showAnnotations()) {
            pageSize += QSize(KWCanvasBase::AnnotationAreaWidth, 0.0);
        }
        m_zoomController->setPageSize(pageSize);
    }
    if (minTextX != m_textMinX || maxTextX != m_textMaxX) {
        m_textMinX = minTextX;
        m_textMaxX = maxTextX;
        m_zoomController->setTextMinMax(minTextX, maxTextX);
    }
    if (minPageNum != m_minPageNum || maxPageNum != m_maxPageNum) {
        m_minPageNum = minPageNum;
        m_maxPageNum = maxPageNum;
        Q_EMIT shownPagesChanged();
    }
}

#ifdef SHOULD_BUILD_RDF
void KWView::semanticObjectViewSiteUpdated(hKoRdfBasicSemanticItem basicitem, const QString &xmlid)
{
    hKoRdfSemanticItem item(static_cast<KoRdfSemanticItem *>(basicitem.data()));
    kDebug(30015) << "xmlid:" << xmlid << " reflow item:" << item->name();
    KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(canvasBase());
    if (!editor) {
        kDebug(30015) << "no editor, not reflowing rdf semantic item.";
        return;
    }
    kDebug(30015) << "reflowing rdf semantic item.";
    KoRdfSemanticItemViewSite vs(item, xmlid);
    vs.reflowUsingCurrentStylesheet(editor);
}
#endif

void KWView::findMatchFound(const KoFindMatch &match)
{
    if (!match.isValid() || !match.location().canConvert<QTextCursor>() || !match.container().canConvert<QTextDocument *>()) {
        return;
    }

    QTextCursor cursor = match.location().value<QTextCursor>();

    m_canvas->resourceManager()->setResource(KoText::CurrentTextAnchor, cursor.anchor());
    m_canvas->resourceManager()->setResource(KoText::CurrentTextPosition, cursor.position());
}

void KWView::refreshFindTexts()
{
    QList<QTextDocument *> texts;
    foreach (KWFrameSet *fSet, m_document->frameSets()) {
        KWTextFrameSet *tFSet = dynamic_cast<KWTextFrameSet *>(fSet);
        if (tFSet) {
            texts.append(tFSet->document());
        }
    }
    m_find->setDocuments(texts);
}

void KWView::addImages(const QVector<QImage> &imageList, const QPoint &insertAt)
{
    if (!m_canvas) {
        // no canvas because we're not on the desktop?
        return;
    }

    QPointF pos = viewConverter()->viewToDocument(m_canvas->documentOffset() + insertAt - canvas()->pos());
    pos.setX(qMax(qreal(0), pos.x()));
    pos.setY(qMax(qreal(0), pos.y()));

    // create a factory
    KoShapeFactoryBase *factory = KoShapeRegistry::instance()->value("PictureShape");

    if (!factory) {
        warnWords << "No picture shape found, cannot drop images.";
        return;
    }

    KWTextFrameSet *textFrameSet = currentTextFrameSet();
    if (textFrameSet && Words::isHeaderFooter(textFrameSet)) {
        insertImagesIntoTextFrame(textFrameSet, imageList);
        return;
    }

    foreach (const QImage &image, imageList) {
        KoProperties params;
        params.setProperty("qimage", image);

        KoShape *shape = factory->createShape(&params, kwdocument()->resourceManager());
        if (!shape) {
            warnWords << "Could not create a shape from the image";
            continue;
        }

        // resize the shape so it will fit in the document, with some nice
        // hard-coded constants.
        qreal pageWidth = currentPage().width();
        qreal pageHeight = currentPage().height();

        if (shape->size().width() > (pageWidth * 0.8) || shape->size().height() > pageHeight) {
            QSizeF sz = shape->size();
            sz.scale(QSizeF(pageWidth * 0.6, pageHeight * .6), Qt::KeepAspectRatio);
            shape->setSize(sz);
        }

        shape->setTextRunAroundSide(KoShape::BothRunAroundSide);

        KoShapeAnchor *anchor = new KoShapeAnchor(shape);
        anchor->setAnchorType(KoShapeAnchor::AnchorPage);
        anchor->setHorizontalPos(KoShapeAnchor::HFromLeft);
        anchor->setVerticalPos(KoShapeAnchor::VFromTop);
        anchor->setHorizontalRel(KoShapeAnchor::HPage);
        anchor->setVerticalRel(KoShapeAnchor::VPage);
        shape->setAnchor(anchor);
        shape->setPosition(pos);

        pos += QPointF(25, 25); // increase the position for each shape we insert so the
                                // user can see them all.

        // create the undo step.
        KWShapeCreateCommand *cmd = new KWShapeCreateCommand(kwdocument(), shape);
        KoSelection *selection = m_canvas->shapeManager()->selection();
        selection->deselectAll();
        selection->select(shape);
        m_canvas->addCommand(cmd);
    }
}

void KWView::insertImagesIntoTextFrame(KWTextFrameSet *textFrameSet, const QVector<QImage> &imageList)
{
    if (!m_canvas || !textFrameSet || imageList.isEmpty()) {
        return;
    }

    KoTextEditor *editor = KoTextDocument(textFrameSet->document()).textEditor();
    if (!editor || editor->isEditProtected()) {
        return;
    }

    KoShapeFactoryBase *factory = KoShapeRegistry::instance()->value(QStringLiteral("PictureShape"));
    if (!factory) {
        warnWords << "No picture shape found, cannot insert images.";
        return;
    }

    const qreal maxWidth = textFrameSet->shapes().isEmpty()
        ? currentPage().width() * 0.8
        : textFrameSet->shapes().first()->size().width();
    const qreal maxHeight = currentPage().height() * 0.5;

    for (const QImage &image : imageList) {
        KoProperties params;
        params.setProperty(QStringLiteral("qimage"), image);

        KoShape *shape = factory->createShape(&params, kwdocument()->resourceManager());
        if (!shape) {
            warnWords << "Could not create a shape from the image";
            continue;
        }

        if (shape->size().width() > maxWidth * 0.6 || shape->size().height() > maxHeight) {
            QSizeF size = shape->size();
            size.scale(QSizeF(maxWidth * 0.6, maxHeight), Qt::KeepAspectRatio);
            shape->setSize(size);
        }
        shape->setTextRunAroundSide(KoShape::NoRunAround);

        KoShapeAnchor *anchor = new KoShapeAnchor(shape);
        anchor->setAnchorType(KoShapeAnchor::AnchorAsCharacter);
        anchor->setHorizontalRel(KoShapeAnchor::HChar);
        anchor->setHorizontalPos(KoShapeAnchor::HLeft);
        anchor->setVerticalRel(KoShapeAnchor::VBaseline);
        anchor->setVerticalPos(KoShapeAnchor::VTop);
        shape->setAnchor(anchor);

        m_canvas->addCommand(new KWShapeCreateCommand(kwdocument(), shape));
        editor->insertInlineObject(new KoAnchorInlineObject(anchor));

        KoSelection *selection = m_canvas->shapeManager()->selection();
        selection->deselectAll();
        selection->select(shape);
    }
    m_canvas->setFocus(Qt::OtherFocusReason);
}

const qreal KWView::AnnotationAreaWidth = 200.0; // only static const integral data members can be initialized within a class

// -------------------- Mail merge -----------------------

void KWView::mailMergeSelectRecipients()
{
    const QString filter = i18n("Recipient files (*.csv *.tsv *.txt);;All files (*)");
    const QString file = QFileDialog::getOpenFileName(this,
                                                      i18n("Select Recipients"),
                                                      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                      filter);
    if (!file.isEmpty()) {
        KWMailMergeManager::instance()->loadFromCsv(QUrl::fromLocalFile(file));
    }
}

void KWView::mailMergeEditRecipients()
{
    KWMailMergeDialog dialog(this);
    dialog.exec();
}

void KWView::mailMergeInsertField(QAction *action)
{
    if (!action) {
        return;
    }
    const QString columnName = action->data().toString();
    KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(canvasBase());
    if (!editor || columnName.isEmpty()) {
        return;
    }
    editor->insertInlineObject(new KWMailMergeField(columnName));
}

void KWView::mailMergeGreetingLine()
{
    const QStringList columns = KWMailMergeManager::instance()->columnNames();
    if (columns.isEmpty()) {
        return;
    }

    // Pick the most likely first/last name columns, fall back to the first ones.
    QString firstColumn;
    QString lastColumn;
    for (const QString &column : columns) {
        if (firstColumn.isEmpty() && column.contains(QLatin1String("first"), Qt::CaseInsensitive)) {
            firstColumn = column;
        } else if (lastColumn.isEmpty() && column.contains(QLatin1String("last"), Qt::CaseInsensitive)) {
            lastColumn = column;
        }
    }
    if (firstColumn.isEmpty()) {
        firstColumn = columns.first();
    }
    if (lastColumn.isEmpty()) {
        for (const QString &column : columns) {
            if (column != firstColumn && column.contains(QLatin1String("name"), Qt::CaseInsensitive)) {
                lastColumn = column;
                break;
            }
        }
    }

    KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(canvasBase());
    if (!editor) {
        return;
    }
    editor->insertText(i18n("Dear "));
    editor->insertInlineObject(new KWMailMergeField(firstColumn));
    if (!lastColumn.isEmpty()) {
        editor->insertText(QLatin1String(" "));
        editor->insertInlineObject(new KWMailMergeField(lastColumn));
    }
    editor->insertText(i18n(","));
}

void KWView::mailMergeHighlightFields(bool enabled)
{
    KWMailMergeManager::instance()->setHighlightEnabled(enabled);
}

void KWView::mailMergePreview(bool enabled)
{
    KWMailMergeManager::instance()->setPreviewEnabled(enabled);
}

void KWView::mailMergeFirstRecord()
{
    KWMailMergeManager::instance()->firstRecord();
}

void KWView::mailMergePreviousRecord()
{
    KWMailMergeManager::instance()->previousRecord();
}

void KWView::mailMergeNextRecord()
{
    KWMailMergeManager::instance()->nextRecord();
}

void KWView::mailMergeLastRecord()
{
    KWMailMergeManager::instance()->lastRecord();
}

void KWView::mailMergeFinish()
{
    KWMailMergeManager::instance()->mergeToNewDocument(kwdocument(), mainWindow());
}

void KWView::updateMailMergeActions()
{
    KWMailMergeManager *manager = KWMailMergeManager::instance();
    const bool hasRecipients = manager->hasRecipients();
    const int record = manager->currentRecord();

    m_mailMergeFieldMenu->setEnabled(hasRecipients);
    m_actionMailMergeGreeting->setEnabled(hasRecipients);
    m_actionMailMergeHighlight->setEnabled(hasRecipients);
    m_actionMailMergePreview->setEnabled(hasRecipients);
    m_actionMailMergeFinish->setEnabled(hasRecipients);
    m_actionMailMergeFirst->setEnabled(hasRecipients && record > 0);
    m_actionMailMergePrev->setEnabled(hasRecipients && record > 0);
    m_actionMailMergeNext->setEnabled(hasRecipients && record < manager->recordCount() - 1);
    m_actionMailMergeLast->setEnabled(hasRecipients && record < manager->recordCount() - 1);

    // Rebuild the merge field menu with the recipient columns.
    QMenu *menu = m_mailMergeFieldMenu->menu();
    menu->clear();
    const QStringList columns = manager->columnNames();
    for (const QString &column : columns) {
        QAction *fieldAction = menu->addAction(column);
        fieldAction->setData(column);
    }
    menu->setEnabled(!columns.isEmpty());
}
