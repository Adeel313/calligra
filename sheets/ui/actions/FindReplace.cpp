/* This file is part of the KDE project
   SPDX-FileCopyrightText: 1998-2023 The Calligra Team <calligra-devel@kde.org>
   SPDX-FileCopyrightText: 2023 Tomas Mecir <mecirt@gmail.com>

   SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "FindReplace.h"
#include "Actions.h"
#include "Comment.h" // for the command

#include "core/CellStorage.h"
#include "core/Sheet.h"
#include "ui/CellToolBase.h"
#include "ui/commands/DataManipulators.h"

#include "KoCanvasBase.h"
#include "KoDialog.h"

#include <KLocalizedString>
#include <KStandardAction>
#include <kfind.h>
#include <kreplace.h>

#include <algorithm>

using namespace Calligra::Sheets;

FindReplaceAction::FindReplaceAction(Actions *actions)
    : CellAction(actions, "edit_find", QString(), QIcon(), QString())
{
    m_findOptions = 0;
    m_findLeftColumn = 0;
    m_findRightColumn = 0;
    m_findTopRow = 0;
    m_findBottomRow = 0;
    m_typeValue = FindOption::Value;
    m_directionValue = FindOption::Row;
    m_find = nullptr;
    m_replace = nullptr;
    m_replaceCommand = nullptr;
    m_selection = nullptr;

    m_currentSheet = nullptr;
    m_firstSheet = nullptr;
}

FindReplaceAction::~FindReplaceAction()
{
    delete m_find;
    delete m_replace;
}

QAction *FindReplaceAction::createAction()
{
    QAction *action = KStandardAction::find(nullptr, nullptr, m_actions->tool());
    connect(action, &QAction::triggered, this, &FindReplaceAction::triggered);
    return action;
}

void FindReplaceAction::execute(Selection *selection, Sheet *sheet, QWidget *canvasWidget)
{
    m_selection = selection;
    FindDlg *dialog = new FindDlg(canvasWidget, "Find", m_findOptions, m_findStrings);
    dialog->setHasSelection(!selection->isSingular());
    dialog->setHasCursor(true);
    if (KFindDialog::Accepted != dialog->exec()) {
        delete dialog;
        return;
    }

    // Save for next time
    m_findOptions = dialog->options();
    m_findStrings = dialog->findHistory();
    m_typeValue = dialog->searchType();
    m_directionValue = dialog->searchDirection();

    // Create the KFind object
    delete m_find;
    delete m_replace;
    m_find = new KFind(dialog->pattern(), dialog->options(), canvasWidget);
    m_replace = nullptr;
    m_replaceCommand = nullptr;

    m_currentSheet = sheet;
    m_firstSheet = sheet;

    initFindReplace();
    findNext();

    delete dialog;
}

void FindReplaceAction::executeReplace(Selection *selection, Sheet *sheet, QWidget *canvasWidget)
{
    m_selection = selection;
    SearchDlg *dialog = new SearchDlg(canvasWidget, "Replace", m_findOptions, m_findStrings, m_replaceStrings);
    dialog->setHasSelection(!selection->isSingular());
    dialog->setHasCursor(true);
    if (KReplaceDialog::Accepted != dialog->exec()) {
        delete dialog;
        return;
    }

    // KReplace cannot make progress with an empty needle. Reject it before
    // replacing the current search state, which otherwise leaves a live
    // replace dialog with no valid operation behind it.
    if (dialog->pattern().isEmpty()) {
        delete dialog;
        return;
    }

    m_findOptions = dialog->options();
    m_findStrings = dialog->findHistory();
    m_replaceStrings = dialog->replacementHistory();
    m_typeValue = dialog->searchType();

    delete m_find;
    delete m_replace;
    m_find = nullptr;
    m_replace = nullptr;
    // NOTE Stefan: Avoid beginning of line replacements with nothing which
    //              will lead to an infinite loop (Bug #125535). The reason
    //              for this is unclear to me, but who cares and who would
    //              want to do something like this, häh?!
    if (dialog->pattern() == "^" && dialog->replacement().isEmpty())
        return;
    m_replace = new KReplace(dialog->pattern(), dialog->replacement(), dialog->options());

    m_currentSheet = sheet;
    m_firstSheet = sheet;
    initFindReplace();
    connect(m_replace, &KReplace::textReplaced, this, &FindReplaceAction::slotReplace);

    m_replaceCommand = new KUndo2Command(kundo2_i18n("Replace"));

    findNext();
    delete dialog;
}

// Initialize a find or replace operation, using m_find or m_replace,
// and m_findOptions.
void FindReplaceAction::initFindReplace()
{
    KFind *findObj = m_find ? m_find : m_replace;
    Q_ASSERT(findObj);
    connect(findObj, &KFind::textFound, this, &FindReplaceAction::slotHighlight);
    connect(findObj, &KFind::findNext, this, &FindReplaceAction::findNext);

    bool bck = m_findOptions & KFind::FindBackwards;
    Sheet *currentSheet = m_currentSheet;

    QRect region = (m_findOptions & KFind::SelectedText)
        ? m_selection->lastRange()
        : QRect(1, 1, currentSheet->fullCellStorage()->columns(), currentSheet->fullCellStorage()->rows()); // All cells

    // Storage bounds can be wider than the coordinates supported by Sheets (for
    // example, when formulas refer to entire columns). Keep the scan rectangle
    // inside the valid cell range before deriving inclusive QRect edges.
    region = region.normalized().intersected(QRect(QPoint(1, 1), QPoint(KS_colMax, KS_rowMax)));
    if (region.isEmpty())
        region = QRect(1, 1, 1, 1);

    int colStart = !bck ? region.left() : region.right();
    int colEnd = !bck ? region.right() : region.left();
    int rowStart = !bck ? region.top() : region.bottom();
    int rowEnd = !bck ? region.bottom() : region.top();

    m_findLeftColumn = region.left();
    m_findRightColumn = region.right();
    m_findTopRow = region.top();
    m_findBottomRow = region.bottom();

    m_findStart = QPoint(colStart, rowStart);
    m_findPos = (m_findOptions & KFind::FromCursor) ? m_selection->cursor() : m_findStart;
    m_findEnd = QPoint(colEnd, rowEnd);

    // Search only cells that actually have input (or comments for note search).
    // Walking every coordinate in a large used rectangle makes replace appear
    // hung on sparse spreadsheets.
    m_findCells.clear();
    m_findCellIndex = 0;
    const auto inRegion = [&region](int col, int row) {
        return col >= region.left() && col <= region.right() && row >= region.top() && row <= region.bottom();
    };
    if (m_typeValue == FindOption::Note) {
        CommentStorage *storage = currentSheet->fullCellStorage()->commentStorage();
        for (int i = 0; i < storage->count(); ++i) {
            const int col = storage->col(i);
            const int row = storage->row(i);
            if (inRegion(col, row))
                m_findCells.append(QPoint(col, row));
        }
    } else {
        UserInputStorage *storage = currentSheet->fullCellStorage()->userInputStorage();
        for (int i = 0; i < storage->count(); ++i) {
            const int col = storage->col(i);
            const int row = storage->row(i);
            if (inRegion(col, row))
                m_findCells.append(QPoint(col, row));
        }
    }
    const bool rowSearch = m_directionValue == FindOption::Row;
    std::sort(m_findCells.begin(), m_findCells.end(), [rowSearch](const QPoint &a, const QPoint &b) {
        if (rowSearch)
            return a.y() == b.y() ? a.x() < b.x() : a.y() < b.y();
        return a.x() == b.x() ? a.y() < b.y() : a.x() < b.x();
    });
    const bool backwards = m_findOptions & KFind::FindBackwards;
    if (backwards)
        std::reverse(m_findCells.begin(), m_findCells.end());
    const auto beforeCursor = [rowSearch, backwards](const QPoint &point, const QPoint &cursor) {
        const bool less = rowSearch ? (point.y() < cursor.y() || (point.y() == cursor.y() && point.x() < cursor.x()))
                                    : (point.x() < cursor.x() || (point.x() == cursor.x() && point.y() < cursor.y()));
        const bool greater = rowSearch ? (point.y() > cursor.y() || (point.y() == cursor.y() && point.x() > cursor.x()))
                                       : (point.x() > cursor.x() || (point.x() == cursor.x() && point.y() > cursor.y()));
        return backwards ? greater : less;
    };
    while (m_findCellIndex < m_findCells.size() && beforeCursor(m_findCells.at(m_findCellIndex), m_findPos))
        ++m_findCellIndex;
    // debugSheets << m_findPos <<" to" << m_findEnd;
    // debugSheets <<"leftcol=" << m_findLeftColumn <<" rightcol=" << m_findRightColumn;
}

void FindReplaceAction::slotHighlight(const QString & /*text*/, int /*matchingIndex*/, int /*matchedLength*/)
{
    if (!m_selection || !m_selection->canvas() || !m_currentSheet) {
        return;
    }
    m_selection->initialize(m_findPos);
    QDialog *dialog = nullptr;
    if (m_find)
        dialog = m_find->findNextDialog();
    else if (m_replace)
        dialog = m_replace->replaceNextDialog();
    if (!dialog) {
        return;
    }
    debugSheets << " baseDialog :" << dialog;
    QWidget *canvasWidget = m_selection->canvas()->canvasWidget();
    if (!canvasWidget) {
        return;
    }
    QRect cellRect(m_findPos, m_findPos);
    cellRect.moveTopLeft(canvasWidget->mapToGlobal(cellRect.topLeft()));
    KoDialog::avoidArea(dialog, cellRect);
}

void FindReplaceAction::slotReplace(const QString &newText, int, int, int)
{
    if (!m_replaceCommand || !m_currentSheet) {
        return;
    }
    if (m_typeValue == FindOption::Value) {
        DataManipulator *command = new DataManipulator(m_replaceCommand);
        command->setParsing(true);
        command->setSheet(m_currentSheet);
        command->setValue(Value(newText));
        command->add(Region(m_findPos, m_currentSheet));
    } else if (m_typeValue == FindOption::Note) {
        CommentCommand *command = new CommentCommand(m_replaceCommand);
        command->setComment(newText);
        command->setSheet(m_currentSheet);
        command->add(Region(m_findPos, m_currentSheet));
    }
}

Cell FindReplaceAction::nextFindValidCell(int col, int row)
{
    if (!m_currentSheet || col < 1 || col > KS_colMax || row < 1 || row > KS_rowMax)
        return Cell();
    Cell cell = Cell(m_currentSheet, col, row);
    if (cell.isDefault() || cell.isPartOfMerged() || cell.isFormula())
        cell = Cell();
    if (m_typeValue == FindOption::Note && !cell.isNull() && cell.comment().isEmpty())
        cell = Cell();
    return cell;
}

Cell FindReplaceAction::findNextCell()
{
    while (m_findCellIndex < m_findCells.size()) {
        const QPoint position = m_findCells.at(m_findCellIndex++);
        Cell cell = nextFindValidCell(position.x(), position.y());
        if (!cell.isNull())
            return cell;
    }
    return Cell();
}

void FindReplaceAction::executeFindNext(Selection *selection, Sheet *sheet, QWidget *canvasWidget)
{
    m_selection = selection;
    KFind *findObj = m_find ? m_find : m_replace;
    if (!findObj) {
        execute(selection, sheet, canvasWidget);
        return;
    }

    findNext();
}

void FindReplaceAction::executeFindPrevious(Selection *selection, Sheet *sheet, QWidget *canvasWidget)
{
    m_selection = selection;
    KFind *findObj = m_find ? m_find : m_replace;
    if (!findObj) {
        execute(selection, sheet, canvasWidget);
        return;
    }

    findPrevious();
}

void FindReplaceAction::findNext()
{
    KFind *findObj = m_find ? m_find : m_replace;
    if (!findObj || !m_selection || !m_currentSheet)
        return;
    KFind::Result res = KFind::NoMatch;
    Cell cell = findNextCell();
    bool forw = !(m_findOptions & KFind::FindBackwards);
    while (res == KFind::NoMatch && !cell.isNull()) {
        if (findObj->needData()) {
            if (m_typeValue == FindOption::Note)
                findObj->setData(cell.comment());
            else
                findObj->setData(cell.userInput());
            m_findPos = QPoint(cell.column(), cell.row());
            // debugSheets <<"setData(cell" << m_findPos << ')';
        }

        // Let KFind inspect the text fragment, and display a dialog if a match is found
        if (m_find)
            res = m_find->find();
        else
            res = m_replace->replace();

        if (res == KFind::NoMatch) {
            // Go to next cell, skipping unwanted cells
            if (m_directionValue == FindOption::Row) {
                if (forw)
                    ++m_findPos.rx();
                else
                    --m_findPos.rx();
            } else {
                if (forw)
                    ++m_findPos.ry();
                else
                    --m_findPos.ry();
            }
            cell = findNextCell();
        }
    }

    if (res == KFind::NoMatch) {
        // emitUndoRedo();
        // removeHighlight();
        if (findObj->shouldRestart()) {
            m_findOptions &= ~KFind::FromCursor;
            m_findPos = m_findStart;
            m_findCellIndex = 0;
            findObj->resetCounts();
            findNext();
        } else { // done, close the 'find next' dialog
            if (m_find)
                m_find->closeFindNextDialog();
            else {
                if (m_replaceCommand && m_selection->canvas()) {
                    m_selection->canvas()->addCommand(m_replaceCommand);
                }
                m_replaceCommand = nullptr;
                m_replace->closeReplaceNextDialog();
            }
        }
    } else if (!cell.isNull()) {
        // move to the cell
        Sheet *sheet = dynamic_cast<Sheet *>(cell.sheet());
        if (sheet != m_selection->activeSheet())
            m_selection->emitVisibleSheetRequested(sheet);
        m_selection->initialize(Region(cell.column(), cell.row(), cell.sheet()), sheet);
        m_actions->tool()->scrollToCell(m_selection->cursor());
    }
}

void FindReplaceAction::findPrevious()
{
    KFind *findObj = m_find ? m_find : m_replace;
    if (!findObj)
        return;
    // debugSheets <<"findPrevious";
    int opt = m_findOptions;
    bool forw = !(opt & KFind::FindBackwards);
    if (forw)
        m_findOptions = (opt | KFind::FindBackwards);
    else
        m_findOptions = (opt & ~KFind::FindBackwards);

    findNext();

    m_findOptions = opt; // restore initial options
}

FindAction::FindAction(Actions *actions, const QString &actionName)
    : CellAction(actions, actionName, QString(), QIcon(), QString())
{
    m_findAction = nullptr;
}

FindAction::~FindAction() = default;

FindReplaceAction *FindAction::findAction()
{
    if (m_findAction)
        return m_findAction;
    CellAction *action = m_actions->cellAction("edit_find");
    if (!action)
        return nullptr; // this shouldn't happen
    m_findAction = dynamic_cast<FindReplaceAction *>(action);
    return m_findAction;
}

FindNext::FindNext(Actions *actions)
    : FindAction(actions, "edit_find_next")
{
}

FindNext::~FindNext() = default;

QAction *FindNext::createAction()
{
    QAction *action = KStandardAction::findNext(nullptr, nullptr, m_actions->tool());
    connect(action, &QAction::triggered, this, &FindNext::triggered);
    return action;
}

void FindNext::execute(Selection *selection, Sheet *sheet, QWidget *canvasWidget)
{
    FindReplaceAction *find = findAction();
    if (!find)
        return;
    find->executeFindNext(selection, sheet, canvasWidget);
}

FindPrevious::FindPrevious(Actions *actions)
    : FindAction(actions, "edit_find_prev")
{
}

FindPrevious::~FindPrevious() = default;

QAction *FindPrevious::createAction()
{
    QAction *action = KStandardAction::findPrev(nullptr, nullptr, m_actions->tool());
    connect(action, &QAction::triggered, this, &FindPrevious::triggered);
    return action;
}

void FindPrevious::execute(Selection *selection, Sheet *sheet, QWidget *canvasWidget)
{
    FindReplaceAction *find = findAction();
    if (!find)
        return;
    find->executeFindPrevious(selection, sheet, canvasWidget);
}

Replace::Replace(Actions *actions)
    : FindAction(actions, "edit_replace")
{
}

Replace::~Replace() = default;

QAction *Replace::createAction()
{
    QAction *action = KStandardAction::replace(nullptr, nullptr, m_actions->tool());
    connect(action, &QAction::triggered, this, &Replace::triggered);
    return action;
}

void Replace::execute(Selection *selection, Sheet *sheet, QWidget *canvasWidget)
{
    FindReplaceAction *find = findAction();
    if (!find)
        return;
    find->executeReplace(selection, sheet, canvasWidget);
}
