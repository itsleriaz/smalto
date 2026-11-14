#include "LospecClient.h"

#include <QCryptographicHash>
#include <QColor>
#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QVariantMap>

#include <algorithm>

namespace {

const QString kSite = QStringLiteral("https://lospec.com");

QVariantList colorsOf(const QJsonArray& hex)
{
    QVariantList colors;
    for (const QJsonValue& v : hex) {
        const QColor c(QLatin1Char('#') + v.toString());
        if (c.isValid())
            colors.append(c);
    }
    return colors;
}

} // namespace

LospecClient::LospecClient(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

QUrl LospecClient::listUrl(const QString& tag, int colors, int countMode, int sort, int page)
{
    static const char* modes[] = {"any", "exact", "max", "min"};
    static const char* sorts[] = {"default", "downloads", "alphabetical", "newest"};
    QUrlQuery query;
    query.addQueryItem("colorNumberFilterType", modes[std::clamp(countMode, 0, 3)]);
    query.addQueryItem("colorNumber", QString::number(std::max(1, colors)));
    query.addQueryItem("page", QString::number(std::max(0, page)));
    query.addQueryItem("tag", tag.trimmed().toLower());
    query.addQueryItem("sortingType", sorts[std::clamp(sort, 0, 3)]);
    QUrl url(kSite + "/palette-list/load");
    url.setQuery(query);
    return url;
}

QUrl LospecClient::pageUrl(const QString& slug) const
{
    return QUrl(kSite + "/palette-list/" + slug);
}

QString LospecClient::slugFor(const QString& nameOrUrl)
{
    QString s = nameOrUrl.trimmed();
    const int at = s.indexOf(QLatin1String("palette-list/"));
    if (at >= 0) {
        s = s.mid(at + int(qstrlen("palette-list/")));
        s = s.section('?', 0, 0).section('#', 0, 0).section('/', 0, 0);
        s.remove(QRegularExpression(QStringLiteral("\\.[a-z]+$")));
    }
    s = s.toLower();
    s.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    s.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    return s;
}

bool LospecClient::parseList(const QByteArray& json, QVariantList& results, int& totalCount, QString* error)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonValue list = doc.object().value("palettes");
    if (!doc.isObject() || !list.isArray()) {
        if (error)
            *error = QObject::tr("Lospec sent something unexpected.");
        return false;
    }
    results.clear();
    for (const QJsonValue& v : list.toArray()) {
        const QJsonObject p = v.toObject();
        const QVariantList colors = colorsOf(p.value("colors").toArray());
        if (colors.isEmpty() || p.value("slug").toString().isEmpty())
            continue;
        results.append(QVariantMap{
            {"name", p.value("title").toString()},
            {"slug", p.value("slug").toString()},
            {"author", p.value("user").toObject().value("name").toString()},
            {"colors", colors},
            {"count", int(colors.size())},
            {"downloads", p.value("downloads").toVariant().toString()},
        });
    }
    totalCount = doc.object().value("totalCount").toInt(int(results.size()));
    return true;
}

bool LospecClient::parsePalette(const QByteArray& json, const QString& slug, QVariantMap& result, QString* error)
{
    const QJsonObject p = QJsonDocument::fromJson(json).object();
    const QVariantList colors = colorsOf(p.value("colors").toArray());
    if (colors.isEmpty()) {
        if (error)
            *error = QObject::tr("Lospec sent no colors for \"%1\".").arg(slug);
        return false;
    }
    result = QVariantMap{
        {"name", p.value("name").toString(slug)},
        {"slug", slug},
        {"author", p.value("author").toString()},
        {"colors", colors},
        {"count", int(colors.size())},
        {"downloads", QString()},
    };
    return true;
}

void LospecClient::browse(const QString& tag, int colors, int countMode, int sort, int page)
{
    m_requestedPage = std::max(0, page);
    request(listUrl(tag, colors, countMode, sort, page), false, {});
}

void LospecClient::fetch(const QString& nameOrUrl)
{
    const QString slug = slugFor(nameOrUrl);
    if (slug.isEmpty())
        return;
    request(QUrl(kSite + "/palette-list/" + slug + ".json"), true, slug);
}

void LospecClient::retry()
{
    if (m_lastUrl.isValid())
        request(m_lastUrl, m_lastSingle, m_lastSlug);
}

void LospecClient::request(const QUrl& url, bool single, const QString& slug)
{
    if (m_reply) { // only the newest request counts
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    m_lastUrl = url;
    m_lastSingle = single;
    m_lastSlug = slug;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("Smalto/%1").arg(QCoreApplication::applicationVersion()));
    req.setTransferTimeout(15000);
    m_reply = m_network->get(req);
    QNetworkReply* reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, single, slug] { finished(reply, single, slug); });
    setError({});
    emit loadingChanged();
}

QString LospecClient::cacheFolder() const
{
    return m_cacheFolder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/lospec"
                                   : m_cacheFolder;
}

QString LospecClient::cachePath(const QString& folder, const QUrl& url)
{
    const QByteArray hash = QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex();
    return QDir(folder).filePath(QString::fromLatin1(hash) + ".json");
}

void LospecClient::saveCached(const QString& folder, const QUrl& url, const QByteArray& body)
{
    QDir().mkpath(folder);
    QSaveFile file(cachePath(folder, url));
    if (file.open(QIODevice::WriteOnly) && file.write(body) == body.size())
        file.commit();
}

QByteArray LospecClient::loadCached(const QString& folder, const QUrl& url)
{
    QFile file(cachePath(folder, url));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool LospecClient::show(const QByteArray& body, bool single, const QString& slug)
{
    QString error;
    if (single) {
        QVariantMap palette;
        if (!parsePalette(body, slug, palette, &error)) {
            setError(error);
            return false;
        }
        m_results = {palette};
        m_totalCount = 1;
        m_page = 0;
    } else {
        QVariantList results;
        int total = 0;
        if (!parseList(body, results, total, &error)) {
            setError(error);
            return false;
        }
        m_results = results;
        m_totalCount = total;
        m_page = m_requestedPage;
    }
    return true;
}

void LospecClient::finished(QNetworkReply* reply, bool single, const QString& slug)
{
    reply->deleteLater();
    m_reply = nullptr;
    emit loadingChanged();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (single && status == 404) {
        setError(tr("Lospec has no palette called \"%1\".").arg(slug));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        // Unreachable: the same request's last good reply, if there is one.
        const QByteArray saved = loadCached(cacheFolder(), reply->request().url());
        if (!saved.isEmpty() && show(saved, single, slug)) {
            m_offline = true;
            emit resultsChanged();
            return;
        }
        setError(tr("Couldn't reach Lospec: %1").arg(reply->errorString()));
        return;
    }
    const QByteArray body = reply->readAll();
    if (!show(body, single, slug))
        return;
    saveCached(cacheFolder(), reply->request().url(), body);
    m_offline = false;
    emit resultsChanged();
}

void LospecClient::setError(const QString& error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}
