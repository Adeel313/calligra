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

// calligra includes
#include <KoCanvasBase.h>
#include <KoPointerEvent.h>
#include <KoTextDocument.h>
#include <KoTextEditor.h>
#include <KoViewConverter.h>
#include <KoCharacterStyle.h>
#include <KoOdfLineNumberingConfiguration.h>
#include <KoUnit.h>

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

    addGroup(i18n("Page"), {{QStringLiteral("format_page"), i18n("Page Size")},
                             {QStringLiteral("format_page"), i18n("Orientation")},
                             {QStringLiteral("format_page"), i18n("Page Margins")},
                             {QStringLiteral("format_page"), i18n("Page Columns")}});
    auto *breaksGroup = new QWidget;
    breaksGroup->setWindowTitle(i18n("Breaks"));
    auto *breaksLayout = new QGridLayout(breaksGroup);
    breaksLayout->setContentsMargins(2, 1, 2, 1);
    breaksLayout->setHorizontalSpacing(4);
    breaksLayout->setVerticalSpacing(1);
    breaksLayout->setSizeConstraint(QLayout::SetFixedSize);
    if (QAction *pageBreakAction = m_canvas->view()->action(QStringLiteral("insert_framebreak"))) {
        auto *pageBreak = new QToolButton(breaksGroup);
        pageBreak->setDefaultAction(pageBreakAction);
        pageBreak->setText(i18n("Page Break"));
        pageBreak->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        pageBreak->setIconSize(QSize(24, 24));
        pageBreak->setAutoRaise(true);
        breaksLayout->addWidget(pageBreak, 0, 0);
    }
    auto *manualBreak = new QToolButton(breaksGroup);
    manualBreak->setText(i18n("Manual Break"));
    manualBreak->setIcon(QIcon::fromTheme(QStringLiteral("insert-text")));
    manualBreak->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    manualBreak->setIconSize(QSize(24, 24));
    manualBreak->setAutoRaise(true);
    manualBreak->setToolTip(i18n("Insert a line break at the cursor"));
    connect(manualBreak, &QToolButton::clicked, this, [this] {
        if (KoTextEditor *editor = KoTextEditor::getTextEditorFromCanvas(m_canvas)) {
            editor->insertText(QString(QChar::LineSeparator));
        }
    });
    breaksLayout->addWidget(manualBreak, 0, 1);
    widgets.append(breaksGroup);

    auto *paragraphGroup = new QWidget;
    paragraphGroup->setWindowTitle(i18n("Paragraph"));
    auto *paragraphLayout = new QGridLayout(paragraphGroup);
    paragraphLayout->setContentsMargins(2, 1, 2, 1);
    paragraphLayout->setHorizontalSpacing(4);
    paragraphLayout->setVerticalSpacing(1);
    paragraphLayout->setSizeConstraint(QLayout::SetFixedSize);
    const KoUnit unit = m_document->unit();
    const QStringList paragraphLabels = {i18n("Before"), i18n("After"), i18n("Left"), i18n("Right")};
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
        paragraphLayout->addWidget(label, i / 2, (i % 2) * 2);
        paragraphLayout->addWidget(field, i / 2, (i % 2) * 2 + 1);
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

    auto *toolsGroup = new QWidget;
    toolsGroup->setWindowTitle(i18n("Tools"));
    auto *toolsLayout = new QGridLayout(toolsGroup);
    toolsLayout->setContentsMargins(2, 1, 2, 1);
    toolsLayout->setHorizontalSpacing(4);
    toolsLayout->setVerticalSpacing(1);
    toolsLayout->setSizeConstraint(QLayout::SetFixedSize);
    const auto addActionButton = [this, toolsGroup, toolsLayout](const QString &actionName, const QString &label, int column) {
        if (QAction *action = m_canvas->view()->action(actionName)) {
            auto *button = new QToolButton(toolsGroup);
            button->setDefaultAction(action);
            button->setText(label);
            button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            button->setIconSize(QSize(24, 24));
            button->setAutoRaise(true);
            if (action->menu()) {
                button->setPopupMode(QToolButton::MenuButtonPopup);
            }
            toolsLayout->addWidget(button, 0, column);
        }
    };
    addActionButton(QStringLiteral("insert_variable"), i18n("Page Number"), 0);
    addActionButton(QStringLiteral("insert_header"), i18n("Header"), 1);
    addActionButton(QStringLiteral("insert_footer"), i18n("Footer"), 2);

    auto *lineNumbers = new QToolButton(toolsGroup);
    lineNumbers->setText(i18n("Line Numbering"));
    lineNumbers->setIcon(QIcon::fromTheme(QStringLiteral("format-list-ordered")));
    lineNumbers->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
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
    toolsLayout->addWidget(lineNumbers, 0, 3);

    auto *hyphenation = new QToolButton(toolsGroup);
    hyphenation->setText(i18n("Hyphenation"));
    hyphenation->setIcon(QIcon::fromTheme(QStringLiteral("format-text-italic")));
    hyphenation->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
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
    toolsLayout->addWidget(hyphenation, 0, 4);
    widgets.append(toolsGroup);

    return widgets;
}
