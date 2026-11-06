/*
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWMailMergeDialog.h"
#include "KWMailMergeManager.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

KWMailMergeDialog::KWMailMergeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Edit Recipient List"));
    resize(640, 400);

    auto *layout = new QVBoxLayout(this);

    m_info = new QLabel(this);
    layout->addWidget(m_info);

    m_view = new QTableView(this);
    m_view->setModel(KWMailMergeManager::instance()->model());
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_view, 1);

    auto *buttons = new QHBoxLayout;
    auto *addButton = new QPushButton(i18n("Add Record"), this);
    m_removeButton = new QPushButton(i18n("Remove Record"), this);
    buttons->addWidget(addButton);
    buttons->addWidget(m_removeButton);
    buttons->addStretch(1);
    auto *closeButton = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(addButton, &QPushButton::clicked, this, &KWMailMergeDialog::addRecord);
    connect(m_removeButton, &QPushButton::clicked, this, &KWMailMergeDialog::removeRecord);
    connect(closeButton, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &KWMailMergeDialog::updateInfo);
    connect(KWMailMergeManager::instance()->model(), &QAbstractItemModel::rowsRemoved, this, &KWMailMergeDialog::updateInfo);
    connect(KWMailMergeManager::instance()->model(), &QAbstractItemModel::rowsInserted, this, &KWMailMergeDialog::updateInfo);
    connect(KWMailMergeManager::instance()->model(), &QAbstractItemModel::modelReset, this, &KWMailMergeDialog::updateInfo);

    updateInfo();
}

void KWMailMergeDialog::addRecord()
{
    KWMailMergeManager::instance()->model()->addRecord();
}

void KWMailMergeDialog::removeRecord()
{
    const QModelIndexList rows = m_view->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    for (const QModelIndex &index : rows) {
        KWMailMergeManager::instance()->model()->removeRow(index.row());
    }
}

void KWMailMergeDialog::updateInfo()
{
    KWMailMergeModel *model = KWMailMergeManager::instance()->model();
    const int included = model->includedRecords().size();
    if (model->sourceUrl().isValid()) {
        m_info->setText(i18n("Recipients from %1: %2 records, %3 selected for the merge.",
                             model->sourceUrl().fileName(),
                             model->rowCount(),
                             included));
    } else {
        m_info->setText(i18n("No recipient file loaded. Use \"Select Recipients\" or add records manually: %1 records, %2 selected.",
                             model->rowCount(),
                             included));
    }
    m_removeButton->setEnabled(!m_view->selectionModel()->selectedRows().isEmpty());
}