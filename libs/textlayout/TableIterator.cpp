/* This file is part of the KDE project
 * SPDX-FileCopyrightText: 2011 C. Boemann KO GmbH <cbo@kogmbh.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include "TableIterator.h"

#include "FrameIterator.h"

#include <KoTableStyle.h>

#include <QTextTable>

TableIterator::TableIterator(QTextTable *t)
{
    table = t;
    frameIterators.resize(table->columns());
    for (int col = 0; col < table->columns(); ++col) {
        frameIterators[col] = 0;
    }
    row = 0;
    headerRows = table->format().property(KoTableStyle::NumberHeadingRows).toInt();
    headerRowPositions.resize(headerRows + 1);
    headerCellAreas.resize(headerRows);
    for (int row = 0; row < headerRows; ++row) {
        headerCellAreas[row].resize(table->columns());
        for (int col = 0; col < table->columns(); ++col) {
            headerCellAreas[row][col] = 0;
        }
    }
}

TableIterator::TableIterator(TableIterator *other)
{
    table = other->table;
    // A copy must be a snapshot of the source. The table itself may have gained
    // or lost columns since the source iterator was created (for example while
    // relayouting after inserting a column), so the source's own dimensions
    // have to be used instead of the current table geometry.
    frameIterators.resize(other->frameIterators.size());
    for (int col = 0; col < other->frameIterators.size(); ++col) {
        if (other->frameIterators[col]) {
            frameIterators[col] = new FrameIterator(other->frameIterators[col]);
        } else {
            frameIterators[col] = 0;
        }
    }
    row = other->row;
    headerRows = other->headerRows;
    headerPositionX = other->headerPositionX;
    headerRowPositions = other->headerRowPositions;
    headerCellAreas.resize(other->headerCellAreas.size());
    for (int row = 0; row < other->headerCellAreas.size(); ++row) {
        headerCellAreas[row] = other->headerCellAreas[row];
    }
}

TableIterator::~TableIterator()
{
    for (int col = 0; col < frameIterators.size(); ++col) {
        delete frameIterators[col];
    }
}

bool TableIterator::operator==(const TableIterator &other) const
{
    if (table != other.table)
        return false;

    if (row != other.row)
        return false;

    if (headerRows != other.headerRows)
        return false;

    if (frameIterators.size() != other.frameIterators.size())
        return false;

    if (headerCellAreas.size() != other.headerCellAreas.size())
        return false;

    for (int row = 0; row < headerRows; ++row) {
        if (headerCellAreas[row].size() != other.headerCellAreas[row].size())
            return false;
        for (int col = 0; col < headerCellAreas[row].size(); ++col) {
            if (headerCellAreas[row][col] != other.headerCellAreas[row][col])
                return false;
        }
    }

    for (int col = 0; col < frameIterators.size(); ++col) {
        if (frameIterators[col] && other.frameIterators[col]) {
            if (!(*frameIterators[col] == *(other.frameIterators[col])))
                return false;
        }
    }

    return true;
}

FrameIterator *TableIterator::frameIterator(int column)
{
    FrameIterator *it = nullptr;
    if (row == table->rows()) {
        delete frameIterators[column];
        frameIterators[column] = it;
    } else if (frameIterators[column] == 0) {
        it = new FrameIterator(table->cellAt(row, column));
        it->masterPageName = masterPageName;
        frameIterators[column] = it;
    } else {
        it = frameIterators[column];
    }
    return it;
}
