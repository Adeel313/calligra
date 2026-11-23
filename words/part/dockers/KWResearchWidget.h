/* This file is part of the KDE project
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KWRESEARCHWIDGET_H
#define KWRESEARCHWIDGET_H

#include <QMap>
#include <QUrl>
#include <QWidget>

class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QToolButton;
class QTextBrowser;

class KWResearchWidget : public QWidget
{
    Q_OBJECT
public:
    explicit KWResearchWidget(QWidget *parent = nullptr);

public Q_SLOTS:
    void lookup(const QString &query);

private Q_SLOTS:
    void doLookup();
    void openExternal();
    void handleReply(QNetworkReply *reply);

private:
    enum Source {
        DictionarySource,
        WiktionarySource,
        WikipediaSource
    };

    struct Result {
        bool done = false;
        bool ok = false;
        QString title;
        QString html;
        QUrl sourceUrl;
    };

    void startLookup(const QString &query);
    void fetchSource(Source source, const QUrl &url);
    void renderResults();
    QString renderSection(const QString &heading, const Result &result, bool collapsed) const;
    QString decodeJsonString(QString s) const;
    QString extractJsonString(const QString &json, const QString &key) const;

    QLineEdit *m_query;
    QToolButton *m_back;
    QToolButton *m_forward;
    QToolButton *m_external;
    QTextBrowser *m_browser;
    QNetworkAccessManager *m_nam;
    QUrl m_currentUrl;
    QString m_currentQuery;
    QMap<int, Source> m_replySource;
    QMap<Source, Result> m_results;
    int m_pending = 0;
};

#endif
