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
#include <QDoubleSpinBox>
#include <QIcon>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QMenu>
#include <QTimer>
#include <QPainter>

// calligra includes
#include <KoCanvasBase.h>
#include <KoPointerEvent.h>
#include <KoRibbonIcons.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
#include <KoViewConverter.h>
#include <KoCharacterStyle.h>
#include <KoOdfLineNumberingConfiguration.h>
#include <KoUnit.h>

namespace
{
QIcon pageSetupIcon(int type)
{
    QPixmap pixmap(64, 64);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#596575"), 1.2));
    painter.setBrush(Qt::white);
    const QRectF page = type == 1 ? QRectF(2, 7, 28, 19) : QRectF(6, 2, 20, 28);
    painter.drawRoundedRect(page, 1, 1);
    painter.setPen(QPen(QColor("#185abd"), 1.3));
    if (type == 0) {
        painter.setPen(QPen(QColor("#185abd"), 1, Qt::DashLine));
        painter.drawRect(page.adjusted(4, 5, -4, -5));
    } else if (type == 2) {
        painter.drawLine(QPointF(10, 7), QPointF(22, 7));
        painter.drawLine(QPointF(10, 7), QPointF(13, 5));
        painter.drawLine(QPointF(22, 7), QPointF(19, 5));
        painter.drawLine(QPointF(16, 12), QPointF(16, 25));
        painter.drawLine(QPointF(16, 12), QPointF(14, 15));
        painter.drawLine(QPointF(16, 25), QPointF(14, 22));
    } else {
        for (int y = type == 1 ? 12 : 7; y < (type == 1 ? 23 : 27); y += 4) {
            if (type == 3) {
                painter.drawLine(9, y, 14, y);
                painter.drawLine(18, y, 23, y);
            } else {
                painter.drawLine(7, y, 25, y);
            }
        }
    }
    painter.end();
    return QIcon(pixmap);
}
}

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

    // Microsoft Office style "Page Setup" group: margins, orientation, size,
    // columns, breaks, line numbering and hyphenation.
    auto *pageSetupGroup = new QWidget;
    pageSetupGroup->setWindowTitle(i18n("Page Setup"));
    pageSetupGroup->setProperty("ribbonGroupOrder", 10);
    auto *pageSetupLayout = new QGridLayout(pageSetupGroup);
    pageSetupLayout->setContentsMargins(2, 1, 2, 1);
    pageSetupLayout->setHorizontalSpacing(4);
    pageSetupLayout->setVerticalSpacing(1);
    pageSetupLayout->setSizeConstraint(QLayout::SetFixedSize);

    int buttonIndex = 0;
    const auto placeButton = [pageSetupLayout, &buttonIndex](QToolButton *button) {
        const bool large = buttonIndex < 4;
        button->setToolButtonStyle(large ? Qt::ToolButtonTextUnderIcon : Qt::ToolButtonTextBesideIcon);
        button->setIconSize(large ? QSize(32, 32) : QSize(16, 16));
        if (large) {
            button->setMinimumSize(64, 72);
            pageSetupLayout->addWidget(button, 0, buttonIndex, 3, 1, Qt::AlignTop);
        } else {
            pageSetupLayout->addWidget(button, (buttonIndex - 4) % 3, 4 + (buttonIndex - 4) / 3, Qt::AlignLeft);
        }
        ++buttonIndex;
    };
    const auto addPageSetupAction = [this, pageSetupGroup, &placeButton, &buttonIndex](const QString &actionName, const QString &label) {
        auto *button = new QToolButton(pageSetupGroup);
        button->setObjectName(QStringLiteral("layout%1Button").arg(label));
        button->setText(label);
        const QIcon icon = pageSetupIcon(buttonIndex);
        button->setIcon(icon);
        // KWGui builds the ribbon before KWView registers its actions.
        QTimer::singleShot(0, button, [this, button, actionName, label, icon] {
            QAction *action = m_canvas->view()->action(actionName);
            if (!action) {
                button->setEnabled(false);
                return;
            }
            button->setDefaultAction(action);
            button->setText(label);
            button->setIcon(icon);
            connect(action, &QAction::changed, button, [button, label, icon] {
                button->setText(label);
                button->setIcon(icon);
            });
            if (action->menu()) {
                button->setPopupMode(QToolButton::MenuButtonPopup);
            }
        });
        button->setAutoRaise(true);
        placeButton(button);
    };

    addPageSetupAction(QStringLiteral("format_page"), i18n("Margins"));
    addPageSetupAction(QStringLiteral("format_page"), i18n("Orientation"));
    addPageSetupAction(QStringLiteral("format_page"), i18n("Size"));
    addPageSetupAction(QStringLiteral("format_page"), i18n("Columns"));
    auto *manualBreak = new QToolButton(pageSetupGroup);
    manualBreak->setObjectName(QStringLiteral("layoutBreaksButton"));
    manualBreak->setText(i18n("Breaks"));
    manualBreak->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("insert-page-break")));
    manualBreak->setAutoRaise(true);
    auto *breaksMenu = new QMenu(manualBreak);
    const auto populateBreaksMenu = [this, breaksMenu] {
        breaksMenu->clear();
        // These actions are replaced when the active text tool changes.
        for (const QString &name : {QStringLiteral("insert_framebreak"), QStringLiteral("insert_section"), QStringLiteral("line_break")}) {
            if (QAction *action = m_canvas->view()->action(name)) {
                breaksMenu->addAction(action);
            }
        }
    };
    connect(breaksMenu, &QMenu::aboutToShow, this, populateBreaksMenu);
    QTimer::singleShot(0, breaksMenu, populateBreaksMenu);
    manualBreak->setMenu(breaksMenu);
    manualBreak->setPopupMode(QToolButton::InstantPopup);
    placeButton(manualBreak);

    auto *lineNumbers = new QToolButton(pageSetupGroup);
    lineNumbers->setText(i18n("Line Numbering"));
    lineNumbers->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("format-list-ordered")));
    lineNumbers->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    lineNumbers->setIconSize(QSize(28, 28));
    lineNumbers->setCheckable(true);
    lineNumbers->setAutoRaise(true);
    if (m_document->mainFrameSet()) {
        KoTextDocument textDocument(m_document->mainFrameSet()->document());
        if (KoOdfLineNumberingConfiguration *configuration = textDocument.lineNumberingConfiguration()) {
            lineNumbers->setChecked(configuration->enabled());
        }
    }
    connect(lineNumbers, &QToolButton::toggled, this, [this](bool enabled) {
        if (!m_document->mainFrameSet()) {
            return;
        }
        QTextDocument *text = m_document->mainFrameSet()->document();
        KoTextDocument textDocument(text);
        auto *configuration = textDocument.lineNumberingConfiguration();
        if (!configuration) {
            configuration = new KoOdfLineNumberingConfiguration;
            textDocument.setLineNumberingConfiguration(configuration);
        }
        configuration->setEnabled(enabled);
        text->markContentsDirty(0, text->characterCount());
        m_document->setModified(true);
        m_document->relayout();
        m_canvas->update();
    });
    placeButton(lineNumbers);

    auto *hyphenation = new QToolButton(pageSetupGroup);
    hyphenation->setText(i18n("Hyphenation"));
    hyphenation->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("format-text-hyphenation")));
    hyphenation->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    hyphenation->setIconSize(QSize(28, 28));
    hyphenation->setCheckable(true);
    hyphenation->setAutoRaise(true);
    hyphenation->setToolTip(i18n("Allow hyphenation for the current text selection"));
    if (KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
        hyphenation->setChecked(editor->charFormat().property(KoCharacterStyle::HasHyphenation).toBool());
    }
    connect(hyphenation, &QToolButton::toggled, this, [this](bool enabled) {
        if (KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
            QTextCharFormat format = editor->charFormat();
            format.setProperty(KoCharacterStyle::HasHyphenation, enabled);
            editor->setCharFormat(format);
        }
    });
    placeButton(hyphenation);

    widgets.append(pageSetupGroup);

    auto *paragraphGroup = new QWidget;
    paragraphGroup->setWindowTitle(i18n("Paragraph"));
    paragraphGroup->setProperty("ribbonGroupOrder", 20);
    auto *paragraphLayout = new QGridLayout(paragraphGroup);
    paragraphLayout->setContentsMargins(2, 1, 2, 1);
    paragraphLayout->setHorizontalSpacing(4);
    paragraphLayout->setVerticalSpacing(1);
    paragraphLayout->setSizeConstraint(QLayout::SetFixedSize);
    const KoUnit unit = m_document->unit();
    const QStringList paragraphLabels = {i18n("Before"), i18n("After"), i18n("Left"), i18n("Right")};
    auto *indentTitle = new QLabel(i18n("Indent"), paragraphGroup);
    auto *spacingTitle = new QLabel(i18n("Spacing"), paragraphGroup);
    paragraphLayout->addWidget(indentTitle, 0, 0, 1, 2);
    paragraphLayout->addWidget(spacingTitle, 0, 2, 1, 2);
    for (int i = 0; i < paragraphLabels.size(); ++i) {
        auto *field = new QDoubleSpinBox(paragraphGroup);
        field->setObjectName(QStringLiteral("layoutParagraph%1").arg(i));
        field->setDecimals(2);
        field->setRange(-1000.0, 1000.0);
        field->setSingleStep(0.05);
        field->setSuffix(QStringLiteral(" %1").arg(unit.symbol()));
        field->setToolTip(paragraphLabels.at(i));
        field->setAccessibleName(paragraphLabels.at(i));
        if (KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
            const QTextBlockFormat currentFormat = editor->blockFormat();
            qreal currentPoints = 0.0;
            switch (i) {
            case 0:
                currentPoints = currentFormat.topMargin();
                break;
            case 1:
                currentPoints = currentFormat.bottomMargin();
                break;
            case 2:
                currentPoints = currentFormat.leftMargin();
                break;
            case 3:
                currentPoints = currentFormat.rightMargin();
                break;
            }
            field->setValue(unit.toUserValue(currentPoints));
        }
        auto *label = new QLabel(paragraphLabels.at(i), paragraphGroup);
        label->setBuddy(field);
        // Indentation on the left, spacing on the right; each is a
        // labelled pair of vertically stacked fields, as in Word.
        const int column = i < 2 ? 2 : 0;
        const int row = 1 + i % 2;
        paragraphLayout->addWidget(label, row, column);
        paragraphLayout->addWidget(field, row, column + 1);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, unit, i](double value) {
            KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas);
            if (!editor) {
                return;
            }
            QTextBlockFormat format;
            const qreal points = unit.fromUserValue(value);
            switch (i) {
            case 0:
                format.setTopMargin(points);
                break;
            case 1:
                format.setBottomMargin(points);
                break;
            case 2:
                format.setLeftMargin(points);
                break;
            case 3:
                format.setRightMargin(points);
                break;
            default:
                return;
            }
            editor->mergeBlockFormat(format);
        });
    }
    widgets.append(paragraphGroup);

    return widgets;
}
