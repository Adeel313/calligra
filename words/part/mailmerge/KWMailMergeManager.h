/*
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KWMAILMERGEMANAGER_H
#define KWMAILMERGEMANAGER_H

#include <QAbstractTableModel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>

class KWMailMergeField;
class KWMailMergeManager;
class KoDocument;
class KoMainWindow;

/**
 * The recipient table of a mail merge: one column of include-checkboxes
 * followed by the data columns of the loaded CSV/TSV file.
 */
class KWMailMergeModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit KWMailMergeModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    /// Replace the table with the given header line and data rows.
    void setRecords(const QStringList &headers, const QList<QStringList> &rows, const QUrl &sourceUrl);
    /// Append one empty, included record.
    void addRecord();

    QStringList columnNames() const;
    QString value(int row, const QString &columnName) const;
    QList<int> includedRecords() const;
    QUrl sourceUrl() const;

private:
    QStringList m_headers;
    QList<QStringList> m_rows;
    QList<bool> m_included;
    QUrl m_sourceUrl;
};

/**
 * Application-wide mail merge state: the recipient list, the current record,
 * the preview mode and the merge fields attached to open documents.
 *
 * Merge fields (KWMailMergeField) display either their placeholder
 * ("«ColumnName»") or the current recipient's value; Finish & Merge writes
 * one letter per recipient into a new document.
 */
class KWMailMergeManager : public QObject
{
    Q_OBJECT
public:
    static KWMailMergeManager *instance();

    KWMailMergeModel *model() const;

    bool hasRecipients() const;
    QStringList columnNames() const;
    int recordCount() const;
    int currentRecord() const;

    /// The current recipient's value for @p columnName.
    QString value(const QString &columnName) const;
    /// The «ColumnName» placeholder text for @p columnName.
    static QString placeholderText(const QString &columnName);
    /// What a merge field should currently display.
    QString displayValue(const QString &columnName) const;

    bool previewEnabled() const;
    bool highlightEnabled() const;
    /// While baking, merge fields save as plain values instead of field markup.
    bool isBaking() const;

    void registerField(KWMailMergeField *field);
    void unregisterField(KWMailMergeField *field);
    /// Update the display of all merge fields in all open documents.
    void refreshFields();

public Q_SLOTS:
    /// Load recipients from a CSV or (tab separated) TSV/CSV file.
    bool loadFromCsv(const QUrl &url);
    void setCurrentRecord(int record);
    void firstRecord();
    void previousRecord();
    void nextRecord();
    void lastRecord();
    void setPreviewEnabled(bool enabled);
    void setHighlightEnabled(bool enabled);

    /**
     * Merge the included recipients into a new document: one letter per
     * recipient, separated by page breaks. @p source is the template
     * document (its current state is used) and @p mainWindow the window to
     * open the merged document from.
     */
    void mergeToNewDocument(KoDocument *source, KoMainWindow *mainWindow);

Q_SIGNALS:
    void recipientsChanged();
    void recordChanged(int record);
    void displayChanged();

private:
    KWMailMergeManager();

    KWMailMergeModel *m_model;
    QList<KWMailMergeField *> m_fields;
    int m_currentRecord;
    bool m_previewEnabled;
    bool m_highlightEnabled;
    bool m_baking;
};

#endif // KWMAILMERGEMANAGER_H