/*
 *  SPDX-FileCopyrightText: 2011 Jignesh Kakadiya <jigneshhk1992@gmail.com>
 *
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 */

// pagetool includes
#include "KWPageTool.h"
#include "KWView.h"
#include "frames/KWTextFrameSet.h"

// words includes
#include "KWCanvas.h"
#include "KWDocument.h"

// Qt includes
#include <QList>
#include <QGridLayout>
#include <QIcon>
#include <QToolButton>
#include <QVBoxLayout>

// calligra includes
#include <KoCanvasBase.h>
#include <KoPointerEvent.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
#include <KoViewConverter.h>

KWPageTool::KWPageTool(KoCanvasBase *canvas)
    : KoToolBase(canvas)
{
    m_canvas = dynamic_cast<KWCanvas *>(canvas);
    if (m_canvas) {
        m_document = m_canvas->document();
    }
}

KWPageTool::~KWPageTool() = default;

void KWPageTool::paint(QPainter &painter, const KoViewConverter &converter)
{
    Q_UNUSED(painter)
    Q_UNUSED(converter)
}

void KWPageTool::activate(ToolActivation toolActivation, const QSet<KoShape *> &shapes)
{
    useCursor(Qt::ArrowCursor);
    repaintDecorations();
    Q_UNUSED(toolActivation);
    Q_UNUSED(shapes);
}

void KWPageTool::mousePressEvent(KoPointerEvent *event)
{
    event->ignore();
}

void KWPageTool::mouseMoveEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KWPageTool::mouseReleaseEvent(KoPointerEvent *event)
{
    Q_UNUSED(event);
}

void KWPageTool::insertPageBreak()
{
    if (m_document->mainFrameSet()) {
        // lets just add one to the main text frameset
        KoTextEditor *editor = KoTextDocument(m_document->mainFrameSet()->document()).textEditor();
        if (editor == KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
            editor->insertFrameBreak();
        }
    }
}

QList<QPointer<QWidget>> KWPageTool::createOptionWidgets()
{
    QList<QPointer<QWidget>> widgets;

    const auto addGroup = [this, &widgets](const QString &title, const QList<QPair<QString, QString>> &actions) {
        auto *group = new QWidget;
        group->setWindowTitle(title);
        auto *layout = new QGridLayout(group);
        layout->setContentsMargins(2, 1, 2, 1);
        layout->setHorizontalSpacing(4);
        layout->setVerticalSpacing(1);
        layout->setSizeConstraint(QLayout::SetFixedSize);

        QList<QPair<QAction *, QString>> availableActions;
        for (const auto &[name, label] : actions) {
            if (QAction *action = m_canvas->view()->action(name)) {
                availableActions.append({action, label});
            }
        }
        const int columns = qMax(1, (availableActions.size() + 1) / 2);
        for (int i = 0; i < availableActions.size(); ++i) {
            QAction *action = availableActions.at(i).first;
            auto *button = new QToolButton(group);
            button->setDefaultAction(action);
            if (!availableActions.at(i).second.isEmpty()) {
                button->setText(availableActions.at(i).second);
            }
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(28, 28));
            button->setAutoRaise(true);
            if (action->menu()) {
                button->setPopupMode(QToolButton::MenuButtonPopup);
            }
            layout->addWidget(button, i / columns, i % columns, Qt::AlignLeft | Qt::AlignTop);
        }
        widgets.append(group);
    };

    addGroup(i18n("Page"), {{QStringLiteral("format_page"), i18n("Page Style")}});
    addGroup(i18n("Breaks"), {{QStringLiteral("insert_framebreak"), i18n("Page Break")}});
    addGroup(i18n("Paragraph"), {{QStringLiteral("format_paragraph"), i18n("Paragraph")}});
    addGroup(i18n("Tools"), {{QStringLiteral("insert_header"), i18n("Header")}, {QStringLiteral("insert_footer"), i18n("Footer")}});

    return widgets;
}
