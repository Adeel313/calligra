/*
 * SPDX-FileCopyrightText: 2026 Calligra contributors
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWMailMergeManager.h"
#include "KWMailMergeField.h"

#include "KWDocument.h"
#include "frames/KWTextFrameSet.h"
#include "KWView.h"

#include <KLocalizedString>
#include <KMessageBox>
#include <KoDocument.h>
#include <KoMainWindow.h>
#include <KoOdf.h>
#include <KoTextDrag.h>
#include <KoTextEditor.h>
#include <KoTextOdfSaveHelper.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QTextCursor>
#include <QTextStream>

#include <algorithm>

// ----------------------------------------------------------------
//                     KWMailMergeModel
// ----------------------------------------------------------------

KWMailMergeModel::KWMailMergeModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int KWMailMergeModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int KWMailMergeModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_headers.size() + 1;
}

QVariant KWMailMergeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return QVariant();
    }
    if (index.column() == 0) {
        if (role == Qt::CheckStateRole) {
            return m_included.at(index.row()) ? Qt::Checked : Qt::Unchecked;
        }
        return QVariant();
    }
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        const QStringList &row = m_rows.at(index.row());
        const int column = index.column() - 1;
        return column < row.size() ? row.at(column) : QString();
    }
    return QVariant();
}

bool KWMailMergeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return false;
    }
    if (index.column() == 0 && role == Qt::CheckStateRole) {
        m_included[index.row()] = (value.toInt() == Qt::Checked);
        Q_EMIT dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }
    if (role == Qt::EditRole || role == Qt::DisplayRole) {
        QStringList &row = m_rows[index.row()];
        while (row.size() < m_headers.size()) {
            row.append(QString());
        }
        row[index.column() - 1] = value.toString();
        Q_EMIT dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }
    return false;
}

QVariant KWMailMergeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    if (section == 0) {
        return i18n("Include");
    }
    const int column = section - 1;
    return column < m_headers.size() ? m_headers.at(column) : QVariant();
}

Qt::ItemFlags KWMailMergeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    if (index.column() == 0) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool KWMailMergeModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || row + count > m_rows.size()) {
        return false;
    }
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    for (int i = 0; i < count; ++i) {
        m_rows.removeAt(row);
        m_included.removeAt(row);
    }
    endRemoveRows();
    return true;
}

void KWMailMergeModel::setRecords(const QStringList &headers, const QList<QStringList> &rows, const QUrl &sourceUrl)
{
    beginResetModel();
    m_headers = headers;
    m_rows = rows;
    m_included = QList<bool>(rows.size(), true);
    m_sourceUrl = sourceUrl;
    endResetModel();
}

void KWMailMergeModel::addRecord()
{
    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    m_rows.append(QStringList(m_headers.size()));
    m_included.append(true);
    endInsertRows();
}

QStringList KWMailMergeModel::columnNames() const
{
    return m_headers;
}

QString KWMailMergeModel::value(int row, const QString &columnName) const
{
    if (row < 0 || row >= m_rows.size()) {
        return QString();
    }
    const int column = m_headers.indexOf(columnName);
    if (column < 0) {
        return QString();
    }
    const QStringList &record = m_rows.at(row);
    return column < record.size() ? record.at(column) : QString();
}

QList<int> KWMailMergeModel::includedRecords() const
{
    QList<int> records;
    for (int i = 0; i < m_included.size(); ++i) {
        if (m_included.at(i)) {
            records.append(i);
        }
    }
    return records;
}

QUrl KWMailMergeModel::sourceUrl() const
{
    return m_sourceUrl;
}

// ----------------------------------------------------------------
//                     KWMailMergeManager
// ----------------------------------------------------------------

KWMailMergeManager::KWMailMergeManager()
    : QObject(nullptr)
    , m_model(new KWMailMergeModel(this))
    , m_currentRecord(0)
    , m_previewEnabled(false)
    , m_highlightEnabled(true)
    , m_baking(false)
{
}

KWMailMergeManager *KWMailMergeManager::instance()
{
    static KWMailMergeManager manager;
    return &manager;
}

KWMailMergeModel *KWMailMergeManager::model() const
{
    return m_model;
}

bool KWMailMergeManager::hasRecipients() const
{
    return m_model->rowCount() > 0;
}

QStringList KWMailMergeManager::columnNames() const
{
    return m_model->columnNames();
}

int KWMailMergeManager::recordCount() const
{
    return m_model->rowCount();
}

int KWMailMergeManager::currentRecord() const
{
    return m_currentRecord;
}

QString KWMailMergeManager::value(const QString &columnName) const
{
    return m_model->value(m_currentRecord, columnName);
}

QString KWMailMergeManager::placeholderText(const QString &columnName)
{
    return QStringLiteral("\u00ab%1\u00bb").arg(columnName);
}

QString KWMailMergeManager::displayValue(const QString &columnName) const
{
    if (columnName.isEmpty() || !hasRecipients()) {
        return placeholderText(columnName);
    }
    if (m_previewEnabled || m_baking) {
        return value(columnName);
    }
    return placeholderText(columnName);
}

bool KWMailMergeManager::previewEnabled() const
{
    return m_previewEnabled;
}

bool KWMailMergeManager::highlightEnabled() const
{
    return m_highlightEnabled;
}

bool KWMailMergeManager::isBaking() const
{
    return m_baking;
}

void KWMailMergeManager::registerField(KWMailMergeField *field)
{
    if (field && !m_fields.contains(field)) {
        m_fields.append(field);
    }
}

void KWMailMergeManager::unregisterField(KWMailMergeField *field)
{
    m_fields.removeAll(field);
}

void KWMailMergeManager::refreshFields()
{
    // Copy the list: setValue() may trigger layout and re-registration.
    const QList<KWMailMergeField *> fields = m_fields;
    for (KWMailMergeField *field : fields) {
        if (!field) {
            continue;
        }
        field->refresh();
        field->forceRepaint();
    }
}

// ----------------------------------------------------------------
//                     CSV / TSV parsing
// ----------------------------------------------------------------

static QList<QStringList> parseDelimitedText(const QString &content, QChar delimiter)
{
    QList<QStringList> rows;
    QStringList fields;
    QString field;
    bool inQuotes = false;

    for (int i = 0; i < content.size(); ++i) {
        const QChar c = content.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < content.size() && content.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == delimiter) {
            fields.append(field);
            field.clear();
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < content.size() && content.at(i + 1) == QLatin1Char('\n')) {
                ++i;
            }
            fields.append(field);
            field.clear();
            rows.append(fields);
            fields.clear();
        } else {
            field += c;
        }
    }
    if (!field.isEmpty() || !fields.isEmpty()) {
        fields.append(field);
        rows.append(fields);
    }
    return rows;
}

bool KWMailMergeManager::loadFromCsv(const QUrl &url)
{
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        KMessageBox::error(nullptr, i18n("Could not open the recipient file %1.", url.toLocalFile()));
        return false;
    }
    QTextStream stream(&file);
    QString content = stream.readAll();
    if (content.startsWith(QChar(0xfeff))) {
        content.remove(0, 1); // byte order mark
    }

    const QChar delimiter = content.contains(QLatin1Char('\t')) ? QLatin1Char('\t') : QLatin1Char(',');
    QList<QStringList> rows = parseDelimitedText(content, delimiter);
    if (rows.isEmpty()) {
        KMessageBox::error(nullptr, i18n("The recipient file %1 contains no data.", url.toLocalFile()));
        return false;
    }

    const QStringList headers = rows.takeFirst();
    m_model->setRecords(headers, rows, url);
    m_currentRecord = 0;

    Q_EMIT recipientsChanged();
    Q_EMIT recordChanged(m_currentRecord);
    Q_EMIT displayChanged();
    refreshFields();
    return true;
}

// ----------------------------------------------------------------
//                     record navigation
// ----------------------------------------------------------------

void KWMailMergeManager::setCurrentRecord(int record)
{
    if (!hasRecipients()) {
        return;
    }
    record = qBound(0, record, recordCount() - 1);
    if (m_currentRecord == record) {
        return;
    }
    m_currentRecord = record;
    Q_EMIT recordChanged(m_currentRecord);
    Q_EMIT displayChanged();
    refreshFields();
}

void KWMailMergeManager::firstRecord()
{
    setCurrentRecord(0);
}

void KWMailMergeManager::previousRecord()
{
    setCurrentRecord(m_currentRecord - 1);
}

void KWMailMergeManager::nextRecord()
{
    setCurrentRecord(m_currentRecord + 1);
}

void KWMailMergeManager::lastRecord()
{
    setCurrentRecord(recordCount() - 1);
}

void KWMailMergeManager::setPreviewEnabled(bool enabled)
{
    if (m_previewEnabled == enabled) {
        return;
    }
    m_previewEnabled = enabled;
    Q_EMIT displayChanged();
    refreshFields();
}

void KWMailMergeManager::setHighlightEnabled(bool enabled)
{
    if (m_highlightEnabled == enabled) {
        return;
    }
    m_highlightEnabled = enabled;
    Q_EMIT displayChanged();
    refreshFields();
}

// ----------------------------------------------------------------
//                     Finish & Merge
// ----------------------------------------------------------------

void KWMailMergeManager::mergeToNewDocument(KoDocument *source, KoMainWindow *mainWindow)
{
    if (!source || !mainWindow) {
        return;
    }
    KWDocument *sourceDocument = qobject_cast<KWDocument *>(source);
    const QList<int> records = m_model->includedRecords();
    if (!sourceDocument || records.isEmpty()) {
        KMessageBox::information(
            nullptr,
            i18n("There are no recipients to merge. Select recipients first and mark at least one record for inclusion."),
            i18n("Mail Merge"));
        return;
    }

    // 1. Save a snapshot of the template; the merge fields are written as
    //    text:database-display so the snapshot stays a reusable template.
    const QString snapshot =
        QDir::tempPath() + QStringLiteral("/calligra-mailmerge-template-%1.odt").arg(QCoreApplication::applicationPid());
    const bool wasModified = source->isModified();
    if (!source->saveNativeFormat(snapshot)) {
        KMessageBox::error(nullptr, i18n("Could not create a temporary copy of the document."), i18n("Mail Merge"));
        return;
    }
    source->setModified(wasModified);

    // 2. Open the snapshot in a new window; it becomes the merged document.
    QPointer<KoMainWindow> outputWindow;
    const QMetaObject::Connection connection = connect(mainWindow, &KoMainWindow::loadCompleted, this, [&](KoMainWindow *window) {
        outputWindow = window;
    });
    mainWindow->openDocument(QUrl::fromLocalFile(snapshot));
    disconnect(connection);
    if (!outputWindow) {
        KMessageBox::error(nullptr, i18n("Could not create the merged document."), i18n("Mail Merge"));
        return;
    }

    KWView *outputView = qobject_cast<KWView *>(outputWindow->rootView());
    KoCanvasBase *outputCanvas = outputView ? outputView->canvasBase() : nullptr;
    KoTextEditor *outputEditor = outputCanvas ? KoTextEditor::getTextEditorFromCanvas(outputCanvas) : nullptr;
    QTextDocument *sourceText = sourceDocument->mainFrameSet() ? sourceDocument->mainFrameSet()->document() : nullptr;
    if (!outputEditor || !sourceText) {
        KMessageBox::error(nullptr, i18n("Could not create the merged document."), i18n("Mail Merge"));
        return;
    }

    // 3. Empty the template body and paste one baked letter per recipient.
    //    While baking, merge fields save as their plain values; all other
    //    inline objects (page numbers etc.) stay dynamic.
    outputEditor->movePosition(QTextCursor::End);
    outputEditor->setPosition(0, QTextCursor::KeepAnchor);
    outputEditor->deleteChar();

    const bool wasBaking = m_baking;
    const int oldRecord = m_currentRecord;
    m_baking = true;
    for (int i = 0; i < records.size(); ++i) {
        m_currentRecord = records.at(i);
        KoTextOdfSaveHelper saveHelper(sourceText, 0, sourceText->characterCount() - 1);
        KoTextDrag drag;
        drag.setOdf(KoOdf::mimeType(KoOdf::Text), saveHelper);
        QMimeData *mimeData = drag.takeMimeData();
        if (i > 0) {
            outputEditor->insertFrameBreak();
        }
        outputEditor->paste(outputCanvas, mimeData);
        delete mimeData;
    }
    m_baking = wasBaking;
    m_currentRecord = oldRecord;
    refreshFields();
}