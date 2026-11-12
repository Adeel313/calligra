/*
 *  SPDX-FileCopyrightText: 2011 Jignesh Kakadiya <jigneshhk1992@gmail.com>
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef KWPAGETOOL_H
#define KWPAGETOOL_H

#include <KoPageFormat.h>
#include <KoToolBase.h>

#include <QMarginsF>
#include <QPointer>

#include <functional>

class KWCanvas;
class KWDocument;
class KUndo2MagicString;
class KWPageSetupGallery;
struct PageSetupState;
struct KoPageLayout;
struct KoColumns;

class KWPageTool : public KoToolBase
{
    Q_OBJECT
public:
    explicit KWPageTool(KoCanvasBase *canvas);
    ~KWPageTool() override;

public:
    void paint(QPainter &painter, const KoViewConverter &converter) override;

public Q_SLOTS:
    void activate(KoToolBase::ToolActivation toolActivation, const QSet<KoShape *> &shapes) override;

public: // Events
    void mousePressEvent(KoPointerEvent *event) override;
    void mouseMoveEvent(KoPointerEvent *event) override;
    void mouseReleaseEvent(KoPointerEvent *event) override;
    //  virtual void mouseDoubleClickEvent(KoPointerEvent *event);

    //  virtual void keyPressEvent(QKeyEvent *event);

private Q_SLOTS:
    /// Force the remaining content on the page to next page.
    void insertPageBreak();

private:
    KWCanvas *getCanvas() const;
    KWDocument *getDocument() const;

    /// The page setup of the page the cursor is on, as the gallery shows it.
    PageSetupState pageSetupState() const;
    /// Let the gallery follow the page setup of the current page.
    void refreshPageSetupGallery();
    /// Change the page style of the current page as @p change describes.
    void applyPageSetup(const std::function<void(KoPageLayout &layout, KoColumns &columns)> &change,
                        const KUndo2MagicString &text);
    void applyMargins(const QMarginsF &margins);
    void applyOrientation(KoPageFormat::Orientation orientation);
    void applyPageFormat(KoPageFormat::Format format);
    void applyColumns(int count);

protected:
    QList<QPointer<QWidget>> createOptionWidgets() override;

private:
    KWCanvas *m_canvas;
    KWDocument *m_document;
    QPointer<KWPageSetupGallery> m_gallery;
};

#endif
