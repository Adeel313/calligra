/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2009-2010 Pierre Stirnweiss <pstirnweiss@googlemail.com>
 * SPDX-FileCopyrightText: 2010 Thomas Zander <zander@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ReviewTool.h"
#include <KoRibbonIcons.h>
#include <KoRibbonLayout.h>
#include "AnnotationTextShape.h"

#include "KoShapeBasedDocumentBase.h"
#include <KoAnnotation.h>
#include <KoCharacterStyle.h>
#include <KoGlobal.h>
#include <KoTextEditor.h>
#include <KoAnnotationManager.h>
#include <KoCanvasBase.h>
#include <KoCanvasResourceManager.h>
#include <KoPointerEvent.h>
#include <KoShapeController.h>
#include <KoShapeManager.h>
#include <KoSelection.h>
#include <KoShapeUserData.h>
#include <KoTextRangeManager.h>
#include <KoTextShapeData.h>
#include <KoToolBase.h>

#include <dialogs/SimpleAnnotationWidget.h>
#include <dialogs/SimpleSpellCheckingWidget.h>

#include <KLocalizedString>
#include <KUser>
#include <KConfigGroup>
#include <KSharedConfig>
#include <QAction>
#include <QDebug>
#include <QMenu>
#include <QInputDialog>
#include <QLocale>
#include <QToolButton>
#include <QVBoxLayout>

#include <QDate>

// #include "TextShape.h"
#define AnnotationShape_SHAPEID "AnnotationTextShapeID"

ReviewTool::ReviewTool(KoCanvasBase *canvas)
    : TextTool(canvas)
    , m_textEditor(nullptr)
    , m_textShapeData(nullptr)
    , m_canvas(canvas)
    , m_textShape(nullptr)
    , m_currentAnnotationShape(nullptr)
{
    createActions();
}

ReviewTool::~ReviewTool() = default;

void ReviewTool::createActions()
{
    m_removeAnnotationAction = new QAction(i18n("Remove Comment"), this);
    m_removeAnnotationAction->setToolTip(i18n("Remove Comment"));
    addAction("remove_annotation", m_removeAnnotationAction);
    connect(m_removeAnnotationAction, &QAction::triggered, this, &ReviewTool::removeAnnotation);
}

void ReviewTool::mouseReleaseEvent(KoPointerEvent *event)
{
    TextTool::mouseReleaseEvent(event);
    event->accept();
}
void ReviewTool::activate(KoToolBase::ToolActivation toolActivation, const QSet<KoShape *> &shapes)
{
    TextTool::activate(toolActivation, shapes);
}
void ReviewTool::deactivate()
{
    TextTool::deactivate();
}
void ReviewTool::mouseMoveEvent(KoPointerEvent *event)
{
    TextTool::mouseMoveEvent(event);
}
void ReviewTool::mousePressEvent(KoPointerEvent *event)
{
    TextTool::mousePressEvent(event);
    m_currentAnnotationShape = dynamic_cast<AnnotationTextShape *>(canvas()->shapeManager()->selection()->firstSelectedShape());
}
void ReviewTool::keyPressEvent(QKeyEvent *event)
{
    TextTool::keyPressEvent(event);
}
void ReviewTool::paint(QPainter &painter, const KoViewConverter &converter)
{
    TextTool::paint(painter, converter);
}

QList<QPointer<QWidget>> ReviewTool::createOptionWidgets()
{
    QList<QPointer<QWidget>> widgets;
    SimpleSpellCheckingWidget *sscw = new SimpleSpellCheckingWidget(this, nullptr);
    SimpleAnnotationWidget *saw = new SimpleAnnotationWidget(this, nullptr);

    connect(saw, &SimpleAnnotationWidget::doneWithFocus, this, &ReviewTool::returnFocusToCanvas);

    // Microsoft Office style "Proofing" group: spell checking and word count.
    auto *proofing = new QWidget;
    proofing->setWindowTitle(i18n("Proofing"));
    proofing->setProperty("ribbonGroupOrder", 10);
    auto *proofingLayout = new QVBoxLayout(proofing);
    proofingLayout->setContentsMargins(2, 1, 2, 1);
    proofingLayout->setSpacing(2);
    proofingLayout->setSizeConstraint(QLayout::SetFixedSize);
    proofingLayout->addWidget(sscw);
    QAction *wordCountAction = nullptr;
    const QList<QAction *> actionCandidates = canvas()->canvasWidget()->window()->findChildren<QAction *>();
    for (QAction *candidate : actionCandidates) {
        if (candidate->objectName() == QLatin1String("view_wordCount") || candidate->text() == i18n("Word Count")) {
            wordCountAction = candidate;
            break;
        }
    }
    QToolButton *wordCountButton = nullptr;
    if (wordCountAction) {
        wordCountButton = new QToolButton(proofing);
        wordCountButton->setDefaultAction(wordCountAction);
        wordCountButton->setText(i18n("Word Count"));
        wordCountButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        wordCountButton->setIconSize(QSize(24, 24));
        wordCountButton->setAutoRaise(true);
        proofingLayout->addWidget(wordCountButton, 0, Qt::AlignLeft);
    }
    if (qApp->applicationName().contains(QLatin1String("words"), Qt::CaseInsensitive)) {
        sscw->hide();
        proofing->setWindowTitle(i18n("Proofing"));
        auto *spell = sscw->findChild<QToolButton *>(QStringLiteral("toolAutoSpellCheck"));
        if (QAction *check = action("apply_spellcheck")) {
            spell->setDefaultAction(check);
        }
        const auto updateSpell = [spell] {
            spell->setText(i18n("Spell\ncheck"));
            spell->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("tools-check-spelling")));
        };
        updateSpell();
        if (spell->defaultAction()) {
            connect(spell->defaultAction(), &QAction::changed, spell, updateSpell);
        }
        auto *spellMenu = new QMenu(spell);
        if (QAction *automatic = action("tool_auto_spellcheck")) {
            spellMenu->addAction(automatic);
        }
        if (QAction *configure = action("tool_configure_spellcheck")) {
            spellMenu->addAction(configure);
        }
        spell->setMenu(spellMenu);
        spell->setPopupMode(QToolButton::MenuButtonPopup);

        auto *language = new QToolButton(proofing);
        language->setObjectName(QStringLiteral("reviewLanguage"));
        language->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("preferences-desktop-locale")));
        language->setAccessibleName(i18n("Proofing language"));
        language->setToolTip(i18n("Set the language of the selected text"));
        const auto updateLanguage = [language](const QTextCharFormat &format) {
            QString tag = format.stringProperty(KoCharacterStyle::Language);
            const QString country = format.stringProperty(KoCharacterStyle::Country);
            if (!country.isEmpty() && !tag.contains(QLatin1Char('_')) && !tag.contains(QLatin1Char('-'))) {
                tag += QLatin1Char('_') + country;
            }
            if (tag.isEmpty()) {
                tag = KSharedConfig::openConfig()->group(QStringLiteral("Spelling")).readEntry("defaultLanguage", QStringLiteral("en_US"));
            }
            const QLocale locale(tag);
            language->setText(QLocale::languageToString(locale.language()) + QLatin1Char('\n')
                              + QLocale::territoryToString(locale.territory()));
        };
        updateLanguage(TextTool::textEditor() ? TextTool::textEditor()->charFormat() : QTextCharFormat());
        connect(this, &TextTool::charFormatChanged, language,
                [updateLanguage](const QTextCharFormat &format, const QTextCharFormat &) { updateLanguage(format); });
        auto *languageMenu = new QMenu(language);
        auto *setLanguage = languageMenu->addAction(i18n("Set Proofing Language…"));
        connect(setLanguage, &QAction::triggered, this, [this, language, updateLanguage] {
            const QStringList names = KoGlobal::listOfLanguages();
            const QStringList tags = KoGlobal::listOfLanguageTags();
            bool accepted = false;
            const QString name = QInputDialog::getItem(language, i18n("Proofing Language"), i18n("Language:"), names, 0, false, &accepted);
            const int index = names.indexOf(name);
            KoTextEditor *editor = TextTool::textEditor();
            if (!accepted || index < 0 || index >= tags.size() || !editor) {
                return;
            }
            const QLocale locale(tags[index]);
            QTextCharFormat format;
            format.setProperty(KoCharacterStyle::Language, locale.name().section(QLatin1Char('_'), 0, 0));
            format.setProperty(KoCharacterStyle::Country, locale.name().section(QLatin1Char('_'), 1, 1));
            editor->beginEditBlock(kundo2_i18n("Set Proofing Language"));
            editor->mergeAutoStyle(format);
            editor->endEditBlock();
            updateLanguage(editor->charFormat());
            returnFocusToCanvas();
        });
        if (QAction *configure = action("tool_configure_spellcheck")) {
            languageMenu->addAction(configure);
        }
        language->setMenu(languageMenu);
        language->setPopupMode(QToolButton::InstantPopup);

        auto *hyphenation = new QToolButton(proofing);
        hyphenation->setObjectName(QStringLiteral("reviewHyphenation"));
        hyphenation->setText(i18n("Hyphenation"));
        hyphenation->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("format-text-hyphenation"),
                                                         QIcon::fromTheme(QStringLiteral("format-text-direction-ltr"))));
        hyphenation->setToolTip(i18n("Insert optional or nonbreaking hyphens"));
        auto *hyphenMenu = new QMenu(hyphenation);
        if (QAction *optional = action("soft_hyphen")) {
            hyphenMenu->addAction(optional);
        }
        if (QAction *nonbreaking = action("nonbreaking_hyphen")) {
            hyphenMenu->addAction(nonbreaking);
        }
        hyphenation->setMenu(hyphenMenu);
        hyphenation->setPopupMode(QToolButton::InstantPopup);

        auto *autocorrection = new QToolButton(proofing);
        autocorrection->setObjectName(QStringLiteral("reviewAutocorrection"));
        QAction *configureCorrection = action("configure_autocorrection");
        if (configureCorrection) {
            autocorrection->setDefaultAction(configureCorrection);
        } else {
            autocorrection->setEnabled(false);
        }
        const auto updateCorrection = [autocorrection] {
            autocorrection->setText(i18n("Auto\ncorrection"));
            autocorrection->setIcon(KoRibbonIcons::forThemeIcon(QStringLiteral("tools-wizard")));
        };
        updateCorrection();
        if (configureCorrection) {
            connect(configureCorrection, &QAction::changed, autocorrection, updateCorrection);
        }
        auto *correctionMenu = new QMenu(autocorrection);
        if (QAction *enable = action("enable_autocorrection")) {
            correctionMenu->addAction(enable);
        }
        if (configureCorrection) {
            correctionMenu->addAction(configureCorrection);
        }
        autocorrection->setMenu(correctionMenu);
        autocorrection->setPopupMode(QToolButton::MenuButtonPopup);
        QList<QToolButton *> proofingCommands{hyphenation, autocorrection};
        if (wordCountButton) {
            proofingCommands.prepend(wordCountButton);
        }
        KoRibbonLayout::arrange(proofing, {spell}, proofingCommands);
        auto *languageGroup = new QWidget;
        languageGroup->setWindowTitle(i18n("Language"));
        languageGroup->setProperty("ribbonGroupOrder", 30);
        KoRibbonLayout::arrange(languageGroup, {language}, {});
        widgets.append(languageGroup);

    }
    widgets.append(proofing);

    saw->setWindowTitle(i18n("Comments"));
    saw->setProperty("ribbonGroupOrder", 20);
    if (qApp->applicationName().contains(QLatin1String("words"), Qt::CaseInsensitive)) {
        KoRibbonLayout::arrange(saw,
                               {saw->findChild<QToolButton *>(QStringLiteral("insertAnnotation"))},
                               {saw->findChild<QToolButton *>(QStringLiteral("removeAnnotation"))});
    }
    widgets.append(saw);

    return widgets;
}

void ReviewTool::removeAnnotation()
{
    m_currentAnnotationShape = dynamic_cast<AnnotationTextShape *>(canvas()->shapeManager()->selection()->firstSelectedShape());
    if (m_currentAnnotationShape) {
        QList<KoShape *> shapes;
        shapes << m_currentAnnotationShape;
        canvas()->addCommand(canvas()->shapeController()->removeShapes(shapes));
        m_currentAnnotationShape = nullptr;
    }
}
