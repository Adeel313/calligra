/*
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KWMAILMERGEDIALOG_H
#define KWMAILMERGEDIALOG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QTableView;

/**
 * Editor for the mail merge recipient list: shows the records of the
 * loaded CSV/TSV file with an include-checkbox per record.
 */
class KWMailMergeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit KWMailMergeDialog(QWidget *parent = nullptr);

private Q_SLOTS:
    void addRecord();
    void removeRecord();
    void updateInfo();

private:
    QTableView *m_view;
    QLabel *m_info;
    QPushButton *m_removeButton;
};

#endif // KWMAILMERGEDIALOG_H