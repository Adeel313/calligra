/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2005-2007, 2009, 2010 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2010 Boudewijn Rempt <boud@kogmbh.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KWVIEW_H
#define KWVIEW_H

#include "./dockers/KWStatisticsDocker.h"
#include "./dockers/KWStatisticsWidget.h"
#include "KWPage.h"
#include "words_export.h"

#include <KoFindMatch.h>
#include <KoView.h>
#include <KoViewConverter.h>
#include <KoZoomHandler.h>

#include <QPointer>
#include <QWidget>

class KWDocument;
class KWCanvas;
class KWFrame;
class KWGui;
class KWTextFrameSet;

class KoPart;
class KoCanvasBase;
class KoZoomController;
class KoFindText;

class QMimeData;
class QPushButton;
class QUrl;
#ifdef SHOULD_BUILD_RDF
class KoRdfBasicSemanticItem;
typedef QExplicitlySharedDataPointer<KoRdfBasicSemanticItem> hKoRdfBasicSemanticItem;
#endif

class KToggleAction;
class KActionMenu;
/**
 * Words' view class. Following the broad model-view-controller idea this class
 * shows you one view on the document. There can be multiple views of the same document each
 * in with independent settings for viewMode and zoom etc.
 */
class WORDS_EXPORT KWView : public KoView
{
    Q_OBJECT

public:
    static const qreal AnnotationAreaWidth;

    /**
     * Construct a new view on the words document.
     * The view will have a canvas as a member which does all the actual painting, the view will
     * be responsible for handling the actions.  The View is technically speaking the controller
     * class in the MVC design.
     * @param part a KoPart
     * @param document the document we show.
     * @param parent a parent widget we show ourselves in.
     */
    KWView(KoPart *part, KWDocument *document, QWidget *parent);
    ~KWView() override;

    /**
     * return the KWDocument that owns this view.
     * @see KoView::document()
     */
    KWDocument *kwdocument() const
    {
        return m_document;
    }

    /// reimplemented from superclass
    void addImages(const QVector<QImage> &imageList, const QPoint &insertAt) override;

    /// Insert images inline at the current cursor of \a textFrameSet.
    void insertImagesIntoTextFrame(KWTextFrameSet *textFrameSet, const QVector<QImage> &imageList);

    /// Insert an empty formula inline at the current cursor of \a textFrameSet
    /// and start editing it, the way an equation is inserted in Office.
    void insertFormulaIntoTextFrame(KWTextFrameSet *textFrameSet);

    // interface KoView
    /// reimplemented method from superclass
    void updateReadWrite(bool readWrite) override;
    /// reimplemented method from superclass
    virtual QWidget *canvas() const;

    /// returns true if this view has the snap-to-grid enabled.
    bool snapToGrid() const
    {
        return m_snapToGrid;
    }

    /**
     * Return the current canvas; much like canvas(), but this one does not downcast.
     */
    KoCanvasBase *canvasBase() const;

    /// Return the view converter for this view.
    KoViewConverter *viewConverter()
    {
        return &m_zoomHandler;
    }

    /// show a popup on the view, adding to it a list of actions
    void popupContextMenu(const QPoint &globalPosition, const QList<QAction *> &actions);

    const KWPage currentPage() const;
    void setCurrentPage(const KWPage &page);

    /// The text frame set that currently owns the text cursor, if any.
    KWTextFrameSet *currentTextFrameSet() const;

    /// The header or footer text frame set of the page under the mouse, if any.
    KWTextFrameSet *headerFooterFrameSetAtPointer();
    /// The header or footer text frame set the given document point belongs to.
    /// Returns nullptr for points inside the page's text area.
    KWTextFrameSet *headerFooterFrameSetAt(const QPointF &documentPoint) const;

    /*!
     * Start editing the header or footer the given document point is in.
     *
     * This makes double clicking a header or a footer of a page work even when
     * it is still empty and therefore has no text to click on.
     * @return true when the point belongs to a header/footer that is now edited.
     */
    bool startEditingHeaderFooterAt(const QPointF &documentPoint);

    /**
     * Collect the images the clipboard provides.  An image that is only
     * available as a link on the web is returned through \a remoteImage.
     */
    QVector<QImage> imagesFromClipboard(const QMimeData *mimeData, QUrl *remoteImage) const;
    /// The text frame set a pasted image belongs to (the caret's or the one under the mouse).
    KWTextFrameSet *pasteTargetTextFrameSet();
    /// Insert images the clipboard provided at the paste target.
    void insertPastedImages(const QVector<QImage> &images);
    /// Download an image that was copied as a link and insert it at the paste target.
    void pasteImageFromUrl(const QUrl &url);

    /// go to page
    void goToPage(const KWPage &page);

    KoZoomController *zoomController() const override
    {
        return m_zoomController;
    }

    int minPageNumber() const
    {
        return m_minPageNum;
    }
    int maxPageNumber() const
    {
        return m_maxPageNum;
    }

    void viewMouseMoveEvent(QMouseEvent *e);

Q_SIGNALS:
    void shownPagesChanged();

public Q_SLOTS:
    void offsetInDocumentMoved(int yOffset);

    /// displays the KWPageSettingsDialog that allows to change properties of the entire page
    void formatPage();

    /// turns the border display on/off
    void toggleViewFrameBorders(bool on);
    /// toggle the display of non-printing characters
    void setShowFormattingChars(bool on);
    /// toggle the display of field shadings
    void setShowInlineObjectVisualization(bool on);
    /// toggle the display of table borders
    void setShowTableBorders(bool on);
    /// toggle the display of section bounds
    void setShowSectionBounds(bool on);
    /// go to previous page
    void goToPreviousPage(Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    /// go to next page
    void goToNextPage(Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    /// Call when "Exit Fullscreen Mode" in status bar clicked.
    void exitFullscreenMode();

protected:
    /// reimplemented method from superclass
    void showEvent(QShowEvent *event) override;
    bool event(QEvent *event) override;

private:
    void setupActions();
    KoPrintJob *createPrintJob() override;
    QPrintDialog *createPrintDialog(KoPrintJob *printJob, QWidget *parent) override;
    /// loops over the selected shapes and returns the top level shapes.
    QList<KoShape *> selectedShapes() const;
    KoShape *selectedShape() const;

private Q_SLOTS:
    /// create a template from document
    void createTemplate();
    /// displays the KWFrameDialog that allows to alter the frameset properties
    void editFrameProperties();
    /// called if another shape got selected
    void selectionChanged();
    /// enable document headers
    void enableHeader();
    /// enable document footers
    void enableFooter();
    /// snap to grid
    void toggleSnapToGrid();
    /// displays libs/main/rdf/SemanticStylesheetsEditor to edit Rdf stylesheets
    void editSemanticStylesheets();
    /// called if the zoom changed
    void zoomChanged(KoZoomMode::Mode mode, qreal zoom);
    /// shows or hides the rulers
    void showRulers(bool visible);
    /// shows or hides the status bar
    void showStatusBar(bool);
    /// calls delete on the active tool
    void editDeleteSelection();
    /** decide if we enable or disable the action "delete_page" upon m_document->page_count() */
    void updateStatusBarAction();
    /// show guides menu option uses this
    void setGuideVisibility(bool on);
    /// open the configure dialog.
    void configure();
#ifdef SHOULD_BUILD_RDF
    /// A semantic item was updated and should have it's text refreshed.
    void semanticObjectViewSiteUpdated(hKoRdfBasicSemanticItem item, const QString &xmlid);
#endif
    /// A match was found when searching.
    void findMatchFound(const KoFindMatch &match);
    /// This is used to update the text that can be searched.
    void refreshFindTexts();
    /// The KWPageSettingsDialog was closed.
    void pageSettingsDialogFinished();
    /// user wants to past data from the clipboard
    void pasteRequested();
    /// Call when the user want to show/hide the WordsCount in the statusbar
    void showWordCountInStatusBar(bool doShow);
    /// Show annotations ("notes" in the UI) on the canvas - this is the user view menu visibility change
    void showNotes(bool show);
    /// "hasAnnotations" has changed ("notes" in the UI) - will cause showNotes above to change too
    void hasNotes(bool has);
    /**
     * Set view into fullscreen mode, hide menu bar, status bar, tool bar, dockers
     * and set view into  full screen mode.
     */
    void setFullscreenMode(bool); /// Call after 4 seconds, user doesn't move cursor.
    void hideCursor();
    /// Hide status bar and scroll bars after seconds in fullscreen mode.
    void hideUI();

    // Mail merge (Mailings ribbon tab)
    /// Load recipients from a CSV/TSV file.
    void mailMergeSelectRecipients();
    /// Show the recipient list editor.
    void mailMergeEditRecipients();
    /// Insert the merge field named by the triggered action's data.
    void mailMergeInsertField(QAction *action);
    /// Insert a greeting line built from merge fields.
    void mailMergeGreetingLine();
    /// Toggle the Microsoft Office style merge field shading.
    void mailMergeHighlightFields(bool enabled);
    /// Toggle the display of recipient values instead of placeholders.
    void mailMergePreview(bool enabled);
    void mailMergeFirstRecord();
    void mailMergePreviousRecord();
    void mailMergeNextRecord();
    void mailMergeLastRecord();
    /// Merge all included recipients into a new document.
    void mailMergeFinish();
    /// Enable/disable the mail merge actions and rebuild the field menu.
    void updateMailMergeActions();

private:
    KWGui *m_gui;
    KWCanvas *m_canvas;
    KWDocument *m_document;
    KoZoomHandler m_zoomHandler;
    KoZoomController *m_zoomController;
    KWPage m_currentPage;
    KoFindText *m_find;

    QAction *m_actionCreateTemplate;
    QAction *m_actionFormatFrameSet;
    QAction *m_actionInsertFrameBreak;
    QAction *m_actionFormatFont;
    QAction *m_actionEditDelFrame;
    QAction *m_actionRaiseFrame;
    QAction *m_actionLowerFrame;
    QAction *m_actionBringToFront;
    QAction *m_actionSendBackward;

    // Mail merge actions
    KActionMenu *m_mailMergeFieldMenu = nullptr;
    QAction *m_actionMailMergeGreeting = nullptr;
    QAction *m_actionMailMergeFirst = nullptr;
    QAction *m_actionMailMergePrev = nullptr;
    QAction *m_actionMailMergeNext = nullptr;
    QAction *m_actionMailMergeLast = nullptr;
    QAction *m_actionMailMergeFinish = nullptr;
    KToggleAction *m_actionMailMergeHighlight = nullptr;
    KToggleAction *m_actionMailMergePreview = nullptr;
    KToggleAction *m_actionFormatBold;
    KToggleAction *m_actionFormatItalic;
    KToggleAction *m_actionFormatUnderline;
    KToggleAction *m_actionFormatStrikeOut;
    QAction *m_actionViewHeader;
    QAction *m_actionViewFooter;
    KToggleAction *m_actionViewSnapToGrid;

    bool m_snapToGrid;
    QString m_lastPageSettingsTab;

    // Remembered so that actions which steal the focus (the ribbon, a file
    // dialog) still know which text frame, e.g. a header, last had the cursor.
    mutable QPointer<KWTextFrameSet> m_lastTextFrameSet;

    QSizeF m_pageSize; // The max size of the pages we currently show. Prevents endless loop
    qreal m_textMinX; // The min x value where text can appear we currently show. Prevents endless loop
    qreal m_textMaxX; // The max x value where text can appear we currently show. Prevents endless loop
    int m_minPageNum;
    int m_maxPageNum;

    // Word count stuff for display in status bar
    void buildAssociatedWidget();
    KWStatisticsWidget *wordCount;

    bool m_isFullscreenMode;
    QTimer *m_hideCursorTimer;
    // The button will add to status bar in fullscreen mode to let user come
    // back to standard view.
    QPushButton *m_dfmExitButton;
};

#endif
