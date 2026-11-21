/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2010 Thomas Zander <zander@kde.org>
 * SPDX-FileCopyrightText: 2010 Jean Nicolas Artaud <jean.nicolas.artaud@kogmbh.com>
 * SPDX-FileCopyrightText: 2011 Pavol Korinek <pavol.korinek@ixonos.com>
 * SPDX-FileCopyrightText: 2011 Lukáš Tvrdý <lukas.tvrdy@ixonos.com>
 * SPDX-FileCopyrightText: 2011 Ko GmbH <cbo@kogmbh.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "ToCGenerator.h"

#include <KLocalizedString>

#include "DummyDocumentLayout.h"
#include "KoTextDocumentLayout.h"
#include "KoTextLayoutRootArea.h"

#include <KoParagraphStyle.h>
#include <KoStyleManager.h>
#include <KoTableOfContentsGeneratorInfo.h>
#include <KoTextBlockData.h>
#include <KoTextDocument.h>
#include <KoTextPage.h>

#include <KoBookmark.h>
#include <KoTextRangeManager.h>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTextDocument>
#include <TextLayoutDebug.h>

#include <algorithm>

ToCGenerator::ToCGenerator(QTextDocument *tocDocument, KoTableOfContentsGeneratorInfo *tocInfo)
    : QObject(tocDocument)
    , m_ToCDocument(tocDocument)
    , m_ToCInfo(tocInfo)
    , m_document(nullptr)
    , m_documentLayout(nullptr)
{
    Q_ASSERT(tocDocument);
    Q_ASSERT(tocInfo);

    tocDocument->setUndoRedoEnabled(false);
    tocDocument->setDocumentLayout(new DummyDocumentLayout(tocDocument));
    KoTextDocument(tocDocument).setRelativeTabs(tocInfo->m_relativeTabStopPosition);
}

ToCGenerator::~ToCGenerator()
{
    delete m_ToCInfo;
}

void ToCGenerator::setBlock(const QTextBlock &block)
{
    m_block = block;
    m_documentLayout = static_cast<KoTextDocumentLayout *>(m_block.document()->documentLayout());
    m_document = m_documentLayout->document();
}

QString ToCGenerator::fetchBookmarkRef(const QTextBlock &block, KoTextRangeManager *textRangeManager)
{
    QMultiHash<int, KoTextRange *> ranges = textRangeManager->textRangesChangingWithin(block.document(),
                                                                                       {&KoBookmark::staticMetaObject},
                                                                                       block.position(),
                                                                                       block.position() + block.length(),
                                                                                       block.position(),
                                                                                       block.position() + block.length());
    foreach (KoTextRange *range, ranges) {
        KoBookmark *bookmark = dynamic_cast<KoBookmark *>(range);
        if (bookmark) {
            return bookmark->name();
        }
    }
    return QString();
}

static QString removeWhitespacePrefix(const QString &text)
{
    int firstNonWhitespaceCharIndex = 0;
    const int length = text.length();
    while (firstNonWhitespaceCharIndex < length && text.at(firstNonWhitespaceCharIndex).isSpace()) {
        firstNonWhitespaceCharIndex++;
    }
    return text.right(length - firstNonWhitespaceCharIndex);
}

static QString normalizedTocEntry(QString text)
{
    text.remove(QChar::ObjectReplacementCharacter);
    text = text.section(QLatin1Char('\t'), 0, 0).simplified();
    text.remove(QLatin1Char(' '));
    return text;
}

static int cachedTocLevel(const KoParagraphStyle *style)
{
    if (!style) return 1;
    const KoParagraphStyle *parent = style->parentStyle();
    const QString name = parent ? parent->name() : style->name();
    const auto match = QRegularExpression(QStringLiteral("^(?:TOC|Contents)[ _]?([1-9])$"),
                                          QRegularExpression::CaseInsensitiveOption)
                           .match(name);
    return match.hasMatch() ? match.captured(1).toInt() : 1;
}

bool ToCGenerator::generate()
{
    if (!m_ToCInfo)
        return true;

    QMap<QString, int> cachedEntries;
    KoStyleManager *styleManager = KoTextDocument(m_document).styleManager();
    if (m_ToCInfo->m_preserveCachedResults) {
        for (QTextBlock cachedBlock = m_ToCDocument->begin(); cachedBlock.isValid(); cachedBlock = cachedBlock.next()) {
            const QString entry = normalizedTocEntry(cachedBlock.text());
            if (!entry.isEmpty()) {
                const int styleId = cachedBlock.blockFormat().intProperty(KoParagraphStyle::StyleId);
                cachedEntries.insert(entry, cachedTocLevel(styleManager->paragraphStyle(styleId)));
            }
        }
    }

    if (m_ToCInfo->m_preserveCachedResults && !cachedEntries.isEmpty()) {
        // The cached index body already holds the entry text and page numbers
        // from the source document. Rebuilding it would replace the cached
        // numbers with freshly resolved page fields, which render as "###"
        // when no layout pass has completed yet (always the case in headless
        // conversion, where the timer-driven second pass never runs).
        m_success = true;
        return true;
    }

    m_preservePagebreak = m_ToCDocument->begin().blockFormat().intProperty(KoParagraphStyle::BreakBefore) & KoText::PageBreak;

    m_success = true;

    QTextCursor cursor = m_ToCDocument->rootFrame()->lastCursorPosition();
    cursor.setPosition(m_ToCDocument->rootFrame()->firstPosition(), QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    cursor.setBlockFormat(QTextBlockFormat());
    cursor.setCharFormat(QTextCharFormat());

    if (!m_ToCInfo->m_indexTitleTemplate.text.isEmpty()) {
        KoParagraphStyle *titleStyle = styleManager->paragraphStyle(m_ToCInfo->m_indexTitleTemplate.styleId);

        // titleStyle == 0? then it might be in unused styles
        if (!titleStyle) {
            titleStyle = styleManager->unusedStyle(m_ToCInfo->m_indexTitleTemplate.styleId); // this should return true only for ToC template preview
        }

        if (!titleStyle) {
            titleStyle = styleManager->defaultTableOfcontentsTitleStyle();
        }

        QTextBlock titleTextBlock = cursor.block();
        titleStyle->applyStyle(titleTextBlock);

        cursor.insertText(m_ToCInfo->m_indexTitleTemplate.text);
        if (m_preservePagebreak) {
            QTextBlockFormat blockFormat;
            blockFormat.setProperty(KoParagraphStyle::BreakBefore, KoText::PageBreak);
            cursor.mergeBlockFormat(blockFormat);
            m_preservePagebreak = false;
        }
    }

    // Add TOC
    // Iterate through all blocks to generate TOC
    QTextBlock block = m_document->rootFrame()->firstCursorPosition().block();
    int blockId = 0;
    for (; block.isValid(); block = block.next()) {
        if (m_ToCInfo->m_preserveCachedResults && !cachedEntries.isEmpty()) {
            const QString text = normalizedTocEntry(block.text());
            int level = cachedEntries.value(text, 0);
            if (level == 0) {
                for (auto it = cachedEntries.cbegin(); it != cachedEntries.cend(); ++it) {
                    if (text.startsWith(it.key()) && text.size() - it.key().size() <= 8) {
                        level = it.value();
                        break;
                    }
                }
            }
            if (level > 0) generateEntry(level, cursor, block, blockId);
            continue;
        }

        // Choose only TOC blocks
        if (m_ToCInfo->m_useOutlineLevel) {
            if (block.blockFormat().hasProperty(KoParagraphStyle::OutlineLevel)) {
                int level = block.blockFormat().intProperty(KoParagraphStyle::OutlineLevel);
                generateEntry(level, cursor, block, blockId);
                continue;
            }
        }

        if (m_ToCInfo->m_useIndexSourceStyles) {
            bool inserted = false;
            foreach (const IndexSourceStyles &indexSourceStyles, m_ToCInfo->m_indexSourceStyles) {
                foreach (const IndexSourceStyle &indexStyle, indexSourceStyles.styles) {
                    if (indexStyle.styleId == block.blockFormat().intProperty(KoParagraphStyle::StyleId)) {
                        generateEntry(indexSourceStyles.outlineLevel, cursor, block, blockId);
                        inserted = true;
                        break;
                    }
                }
                if (inserted)
                    break;
            }
            if (inserted)
                continue;
        }

        if (m_ToCInfo->m_useIndexMarks) {
            if (false) {
                generateEntry(1, cursor, block, blockId);
                continue;
            }
        }
    }
    cursor.endEditBlock();

    m_documentLayout->documentChanged(m_block.position(), 1, 1);
    return m_success;
}

static bool compareTab(const QVariant &tab1, const QVariant &tab2)
{
    return tab1.value<KoText::Tab>().position < tab2.value<KoText::Tab>().position;
}

void ToCGenerator::generateEntry(int outlineLevel, QTextCursor &cursor, QTextBlock &block, int &blockId)
{
    KoStyleManager *styleManager = KoTextDocument(m_document).styleManager();

    QString tocEntryText = block.text();
    tocEntryText.remove(QChar::ObjectReplacementCharacter);
    // some headings contain tabs, replace all occurrences with spaces
    tocEntryText.replace('\t', ' ').remove(QChar(0x200B));
    tocEntryText = removeWhitespacePrefix(tocEntryText);

    // Add only blocks with text
    if (!tocEntryText.isEmpty()) {
        KoParagraphStyle *tocTemplateStyle = nullptr;

        if (outlineLevel >= 1 && outlineLevel <= m_ToCInfo->m_outlineLevel) {
            // ODF templates may be sparse or appear in a different order.
            const TocEntryTemplate *tocEntryTemplate = nullptr;
            for (const auto &candidate : std::as_const(m_ToCInfo->m_entryTemplate)) {
                if (candidate.outlineLevel == outlineLevel) {
                    tocEntryTemplate = &candidate;
                    break;
                }
            }
            if (!tocEntryTemplate) return;

            tocTemplateStyle = styleManager->paragraphStyle(tocEntryTemplate->styleId);
            if (tocTemplateStyle == nullptr) {
                tocTemplateStyle = styleManager->defaultTableOfContentsEntryStyle(outlineLevel);
            }

            QTextBlockFormat blockFormat;
            if (m_preservePagebreak) {
                blockFormat.setProperty(KoParagraphStyle::BreakBefore, KoText::PageBreak);
                m_preservePagebreak = false;
            }
            if (!cursor.atStart() || !cursor.block().text().isEmpty()) {
                cursor.insertBlock(blockFormat, QTextCharFormat());
            } else {
                cursor.setBlockFormat(blockFormat);
            }

            QTextBlock tocEntryTextBlock = cursor.block();
            tocTemplateStyle->applyStyle(tocEntryTextBlock);
            cursor.setCharFormat(tocEntryTextBlock.charFormat());

            KoTextBlockData bd(block);

            // save the current style due to hyperlinks
            QTextCharFormat savedCharFormat = cursor.charFormat();
            QTextCharFormat activeCharFormat = savedCharFormat;
            foreach (IndexEntry *entry, tocEntryTemplate->indexEntries) {
                cursor.setCharFormat(activeCharFormat);
                if (!entry->styleName.isEmpty()) {
                    if (KoCharacterStyle *style = styleManager->characterStyle(entry->styleName)) {
                        QTextCharFormat format = cursor.charFormat();
                        style->applyStyle(format);
                        cursor.setCharFormat(format);
                    }
                }
                switch (entry->name) {
                case IndexEntry::LINK_START: {
                    // IndexEntryLinkStart *linkStart = static_cast<IndexEntryLinkStart*>(entry);

                    QString target = fetchBookmarkRef(block, m_documentLayout->textRangeManager());

                    if (target.isNull()) {
                        // generate unique name for the bookmark
                        target = tocEntryText + "|outline" + QString::number(blockId);
                        blockId++;

                        // insert new KoBookmark
                        QTextCursor blockCursor(block);
                        KoBookmark *bookmark = new KoBookmark(blockCursor);
                        bookmark->setName(target);
                        m_documentLayout->textRangeManager()->insert(bookmark);
                    }

                    if (!target.isNull()) {
                        // copy it to alter subset of properties
                        QTextCharFormat linkCf(cursor.charFormat());
                        linkCf.setAnchor(true);
                        linkCf.setProperty(KoCharacterStyle::AnchorType, KoCharacterStyle::Anchor);
                        linkCf.setAnchorHref('#' + target);
                        if (!linkCf.hasProperty(KoCharacterStyle::UnderlineStyle)) {
                            linkCf.setProperty(KoCharacterStyle::UnderlineStyle, KoCharacterStyle::NoLineStyle);
                        }
                        if (!linkCf.hasProperty(QTextFormat::ForegroundBrush)) {
                            linkCf.setForeground(Qt::black);
                        }

                        // A link does not imply blue/underlined text. TOCs
                        // inherit the entry style unless a link style is set.
                        activeCharFormat = linkCf;
                        cursor.setCharFormat(linkCf);
                    }
                    break;
                }
                case IndexEntry::CHAPTER: {
                    // IndexEntryChapter *chapter = static_cast<IndexEntryChapter*>(entry);
                    cursor.insertText(bd.counterText());
                    break;
                }
                case IndexEntry::SPAN: {
                    IndexEntrySpan *span = static_cast<IndexEntrySpan *>(entry);
                    cursor.insertText(span->text);
                    break;
                }
                case IndexEntry::TEXT: {
                    // IndexEntryText *text = static_cast<IndexEntryText*>(entry);
                    cursor.insertText(tocEntryText);
                    break;
                }
                case IndexEntry::TAB_STOP: {
                    IndexEntryTabStop *tabEntry = static_cast<IndexEntryTabStop *>(entry);

                    cursor.insertText("\t");

                    QTextBlockFormat blockFormat = cursor.blockFormat();
                    QList<QVariant> tabList = (blockFormat.property(KoParagraphStyle::TabPositions)).value<QList<QVariant>>();

                    if (tabEntry->m_position.isEmpty()) {
                        tabEntry->tab.position = KoTextLayoutArea::MaximumTabPos;
                    } // else the position is already parsed into tab.position
                    // The template overrides a paragraph tab at the same position.
                    tabList.erase(std::remove_if(tabList.begin(), tabList.end(), [tabEntry](const QVariant &value) {
                        return qFuzzyCompare(value.value<KoText::Tab>().position, tabEntry->tab.position);
                    }), tabList.end());
                    tabList.append(QVariant::fromValue<KoText::Tab>(tabEntry->tab));
                    std::sort(tabList.begin(), tabList.end(), compareTab);
                    blockFormat.setProperty(KoParagraphStyle::TabPositions, QVariant::fromValue<QList<QVariant>>(tabList));
                    cursor.setBlockFormat(blockFormat);
                    break;
                }
                case IndexEntry::PAGE_NUMBER: {
                    // IndexEntryPageNumber *pageNumber = static_cast<IndexEntryPageNumber*>(entry);
                    cursor.insertText(resolvePageNumber(block));
                    break;
                }
                case IndexEntry::LINK_END: {
                    // IndexEntryLinkEnd *linkEnd = static_cast<IndexEntryLinkEnd*>(entry);
                    cursor.setCharFormat(savedCharFormat);
                    activeCharFormat = savedCharFormat;
                    break;
                }
                default: {
                    qDebug() << "New or unknown index entry";
                    break;
                }
                }
            } // foreach
            cursor.setCharFormat(savedCharFormat); // restore the cursor char format
        }
    }
}

QString ToCGenerator::resolvePageNumber(const QTextBlock &headingBlock)
{
    KoTextDocumentLayout *layout = qobject_cast<KoTextDocumentLayout *>(m_document->documentLayout());
    KoTextLayoutRootArea *rootArea = layout->rootAreaForPosition(headingBlock.position());
    if (rootArea) {
        if (rootArea->page()) {
            return QString::number(rootArea->page()->visiblePageNumber());
        } else
            qDebug() << "had root but no page";
    }
    m_success = false;
    return "###";
}
