/* This file is part of the KDE project
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "KWResearchWidget.h"

#include <KLocalizedString>

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>

KWResearchWidget::KWResearchWidget(QWidget *parent)
    : QWidget(parent)
    , m_query(new QLineEdit(this))
    , m_back(new QToolButton(this))
    , m_forward(new QToolButton(this))
    , m_external(new QToolButton(this))
    , m_browser(new QTextBrowser(this))
    , m_nam(new QNetworkAccessManager(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(2);

    m_back->setIcon(QIcon::fromTheme(QStringLiteral("go-previous")));
    m_back->setToolTip(i18n("Back"));
    m_back->setAutoRaise(true);
    m_back->setEnabled(false);
    connect(m_back, &QToolButton::clicked, m_browser, &QTextBrowser::backward);

    m_forward->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
    m_forward->setToolTip(i18n("Forward"));
    m_forward->setAutoRaise(true);
    m_forward->setEnabled(false);
    connect(m_forward, &QToolButton::clicked, m_browser, &QTextBrowser::forward);

    m_query->setPlaceholderText(i18n("Search or define a term"));
    m_query->setClearButtonEnabled(true);
    connect(m_query, &QLineEdit::returnPressed, this, &KWResearchWidget::doLookup);

    auto *search = new QToolButton(this);
    search->setIcon(QIcon::fromTheme(QStringLiteral("edit-find")));
    search->setToolTip(i18n("Search"));
    search->setAutoRaise(true);
    connect(search, &QToolButton::clicked, this, &KWResearchWidget::doLookup);

    m_external->setIcon(QIcon::fromTheme(QStringLiteral("internet-web-browser")));
    m_external->setToolTip(i18n("Open in Browser"));
    m_external->setAutoRaise(true);
    connect(m_external, &QToolButton::clicked, this, &KWResearchWidget::openExternal);

    top->addWidget(m_back);
    top->addWidget(m_forward);
    top->addWidget(m_query, 1);
    top->addWidget(search);
    top->addWidget(m_external);

    m_browser->setOpenExternalLinks(false);
    m_browser->setOpenLinks(false);
    connect(m_browser, &QTextBrowser::backwardAvailable, m_back, &QToolButton::setEnabled);
    connect(m_browser, &QTextBrowser::forwardAvailable, m_forward, &QToolButton::setEnabled);
    connect(m_browser, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        QUrl resolved = url.isRelative() ? m_currentUrl.resolved(url) : url;
        if (resolved.scheme().startsWith(QLatin1String("http")))
            QDesktopServices::openUrl(resolved);
        else
            QDesktopServices::openUrl(resolved);
    });

    connect(m_nam, &QNetworkAccessManager::finished, this, &KWResearchWidget::handleReply);

    layout->addLayout(top);
    layout->addWidget(m_browser, 1);

    m_browser->setHtml(QStringLiteral("<p style='color:gray;'>%1</p>")
                           .arg(i18n("Select text in the document and click Smart Lookup, or type a term above.")));
}

void KWResearchWidget::lookup(const QString &query)
{
    const QString trimmed = query.simplified();
    if (trimmed.isEmpty())
        return;
    m_query->setText(trimmed);
    startLookup(trimmed);
}

void KWResearchWidget::doLookup()
{
    lookup(m_query->text());
}

void KWResearchWidget::startLookup(const QString &query)
{
    m_currentQuery = query;
    m_results.clear();
    m_replySource.clear();
    m_pending = 0;

    m_browser->setHtml(QStringLiteral("<p style='color:gray;'>%1</p>").arg(i18n("Looking up \"%1\"...", query.toHtmlEscaped())));

    // Dictionary (concise definitions)
    QUrl dictUrl(QStringLiteral("https://api.dictionaryapi.dev/api/v2/entries/en/") + QUrl::toPercentEncoding(query.toLower()));
    fetchSource(DictionarySource, dictUrl);

    // Wiktionary (authoritative dictionary project)
    QUrl wtUrl(QStringLiteral("https://en.wiktionary.org/w/api.php"));
    QUrlQuery wt;
    wt.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    wt.addQueryItem(QStringLiteral("prop"), QStringLiteral("extracts"));
    wt.addQueryItem(QStringLiteral("exintro"), QStringLiteral("1"));
    wt.addQueryItem(QStringLiteral("explaintext"), QStringLiteral("1"));
    wt.addQueryItem(QStringLiteral("redirects"), QStringLiteral("1"));
    wt.addQueryItem(QStringLiteral("titles"), query);
    wt.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    wt.addQueryItem(QStringLiteral("origin"), QStringLiteral("*"));
    wtUrl.setQuery(wt);
    fetchSource(WiktionarySource, wtUrl);

    // Wikipedia (broad context, shown last)
    QUrl wikiUrl(QStringLiteral("https://en.wikipedia.org/w/api.php"));
    QUrlQuery wq;
    wq.addQueryItem(QStringLiteral("action"), QStringLiteral("query"));
    wq.addQueryItem(QStringLiteral("prop"), QStringLiteral("extracts"));
    wq.addQueryItem(QStringLiteral("exintro"), QStringLiteral("1"));
    wq.addQueryItem(QStringLiteral("explaintext"), QStringLiteral("1"));
    wq.addQueryItem(QStringLiteral("redirects"), QStringLiteral("1"));
    wq.addQueryItem(QStringLiteral("titles"), query);
    wq.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    wq.addQueryItem(QStringLiteral("origin"), QStringLiteral("*"));
    wikiUrl.setQuery(wq);
    fetchSource(WikipediaSource, wikiUrl);

    m_currentUrl = QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Special:Search?search=") + QUrl::toPercentEncoding(query));
}

void KWResearchWidget::fetchSource(Source source, const QUrl &url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("CalligraWords/Research"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_nam->get(req);
    m_replySource.insert(intptr_t(reply), source);
    ++m_pending;
}

QString KWResearchWidget::decodeJsonString(QString s) const
{
    s.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    s.replace(QStringLiteral("\\r"), QString());
    s.replace(QStringLiteral("\\t"), QStringLiteral(" "));
    s.replace(QStringLiteral("\\\""), QStringLiteral("\""));
    s.replace(QStringLiteral("\\/"), QStringLiteral("/"));
    s.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    return s;
}

QString KWResearchWidget::extractJsonString(const QString &json, const QString &key) const
{
    QRegularExpression re(QStringLiteral("\"%1\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"").arg(QRegularExpression::escape(key)));
    auto m = re.match(json);
    return m.hasMatch() ? decodeJsonString(m.captured(1)) : QString();
}

void KWResearchWidget::handleReply(QNetworkReply *reply)
{
    reply->deleteLater();
    const auto it = m_replySource.find(intptr_t(reply));
    if (it == m_replySource.end())
        return;
    const Source source = it.value();
    m_replySource.erase(it);
    --m_pending;

    Result result;
    result.done = true;
    result.ok = reply->error() == QNetworkReply::NoError;
    const QByteArray data = reply->readAll();

    if (!result.ok) {
        result.html = QStringLiteral("<p style='color:gray;'>%1</p>").arg(reply->errorString().toHtmlEscaped());
        m_results.insert(source, result);
        if (m_pending <= 0)
            renderResults();
        return;
    }

    if (source == DictionarySource) {
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        result.sourceUrl = QUrl(QStringLiteral("https://dictionary.cambridge.org/dictionary/english/") + QUrl::toPercentEncoding(m_currentQuery.toLower()));
        if (err.error == QJsonParseError::NoError && doc.isArray() && !doc.array().isEmpty()) {
            const QJsonObject entry = doc.array().first().toObject();
            result.title = entry.value(QStringLiteral("word")).toString(m_currentQuery);
            QString html;
            const QString phonetic = entry.value(QStringLiteral("phonetic")).toString();
            if (!phonetic.isEmpty())
                html += QStringLiteral("<p style='color:gray;'>%1</p>").arg(phonetic.toHtmlEscaped());
            const QJsonArray meanings = entry.value(QStringLiteral("meanings")).toArray();
            int shown = 0;
            for (const QJsonValue &mv : meanings) {
                const QJsonObject meaning = mv.toObject();
                const QString pos = meaning.value(QStringLiteral("partOfSpeech")).toString();
                if (!pos.isEmpty())
                    html += QStringLiteral("<p><b>%1</b></p>").arg(pos.toHtmlEscaped());
                html += QStringLiteral("<ul>");
                const QJsonArray defs = meaning.value(QStringLiteral("definitions")).toArray();
                int defCount = 0;
                for (const QJsonValue &dv : defs) {
                    if (defCount >= 3)
                        break;
                    const QJsonObject defObj = dv.toObject();
                    const QString def = defObj.value(QStringLiteral("definition")).toString();
                    if (def.isEmpty())
                        continue;
                    html += QStringLiteral("<li>%1</li>").arg(def.toHtmlEscaped());
                    ++defCount;
                    ++shown;
                }
                html += QStringLiteral("</ul>");
                if (shown >= 6)
                    break;
            }
            result.html = html.isEmpty() ? QStringLiteral("<p style='color:gray;'>%1</p>").arg(i18n("No definition found.")) : html;
            result.ok = !html.isEmpty();
        } else {
            result.ok = false;
            result.html = QStringLiteral("<p style='color:gray;'>%1</p>").arg(i18n("No definition found."));
        }
    } else if (source == WiktionarySource || source == WikipediaSource) {
        const QString json = QString::fromUtf8(data);
        const QString title = extractJsonString(json, QStringLiteral("title"));
        QString extract = extractJsonString(json, QStringLiteral("extract"));
        extract = extract.trimmed();
        const bool missing = json.contains(QStringLiteral("\"missing\"")) && extract.isEmpty();
        result.title = title.isEmpty() ? m_currentQuery : title;
        if (source == WikipediaSource)
            result.sourceUrl = QUrl(QStringLiteral("https://en.wikipedia.org/wiki/") + QUrl::toPercentEncoding(result.title));
        else
            result.sourceUrl = QUrl(QStringLiteral("https://en.wiktionary.org/wiki/") + QUrl::toPercentEncoding(result.title));
        if (missing || extract.isEmpty()) {
            result.ok = false;
            result.html = QStringLiteral("<p style='color:gray;'>%1</p>").arg(i18n("No result found."));
        } else {
            // Keep Wikipedia concise; it is shown last.
            if (source == WikipediaSource && extract.size() > 900) {
                int cut = extract.lastIndexOf(QLatin1Char('.'), 900);
                if (cut < 250)
                    cut = 900;
                extract = extract.left(cut + 1) + QStringLiteral(" …");
            }
            result.html = QStringLiteral("<p>%1</p>").arg(extract.toHtmlEscaped().replace(QStringLiteral("\n"), QStringLiteral("<br>")));
            result.ok = true;
        }
    }

    m_results.insert(source, result);
    if (m_pending <= 0)
        renderResults();
}

QString KWResearchWidget::renderSection(const QString &heading, const Result &result, bool collapsed) const
{
    QString body;
    if (!result.done)
        body = QStringLiteral("<p style='color:gray;'>%1</p>").arg(i18n("Loading..."));
    else
        body = result.html;
    if (result.sourceUrl.isValid()) {
        body += QStringLiteral("<p><a href='%1'>%2</a></p>")
                    .arg(result.sourceUrl.toString(QUrl::FullyEncoded), i18n("Open source"));
    }
    if (collapsed) {
        return QStringLiteral("<details><summary><b>%1</b></summary>%2</details>").arg(heading.toHtmlEscaped(), body);
    }
    return QStringLiteral("<h3>%1</h3>%2").arg(heading.toHtmlEscaped(), body);
}

void KWResearchWidget::renderResults()
{
    QString html;
    html += QStringLiteral("<style>body{font-family:sans-serif;} h2{margin-bottom:0.2em;} h3{margin-top:1em;margin-bottom:0.2em;} ul{margin-top:0.2em;} details{margin-top:0.8em;} summary{cursor:pointer;font-size:1.05em;}</style>");
    html += QStringLiteral("<h2>%1</h2>").arg(m_currentQuery.toHtmlEscaped());

    const Result dict = m_results.value(DictionarySource);
    const Result wt = m_results.value(WiktionarySource);
    const Result wiki = m_results.value(WikipediaSource);

    // Authoritative concise sources first.
    html += renderSection(i18n("Dictionary (DictionaryAPI.dev)"), dict, false);
    html += renderSection(i18n("Wiktionary"), wt, false);

    // Wikipedia last and collapsed by default, since it can be large.
    html += renderSection(i18n("Wikipedia"), wiki, true);

    html += QStringLiteral("<hr><p style='color:gray;'>%1</p>").arg(i18n("Sources: DictionaryAPI.dev, Wiktionary, Wikipedia. Verify important facts with the original source."));
    m_browser->setHtml(html);
}

void KWResearchWidget::openExternal()
{
    if (m_currentUrl.isValid())
        QDesktopServices::openUrl(m_currentUrl);
    else if (!m_query->text().trimmed().isEmpty())
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://en.wikipedia.org/wiki/Special:Search?search=") + QUrl::toPercentEncoding(m_query->text().trimmed())));
}
