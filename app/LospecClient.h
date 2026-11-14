#pragma once

#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;

// Browses palettes on lospec.com and fetches one by name. Requests go out
// only when asked; results live in memory. Browsing uses the list endpoint
// Lospec's own site uses (undocumented: if it changes, browse() reports an
// error); fetch() uses the documented "<slug>.json".
class LospecClient : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // {name, slug, author, colors: [color], count, downloads} maps.
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY resultsChanged)
    Q_PROPERTY(int page READ page NOTIFY resultsChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY resultsChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    // Why the last request failed; empty when it didn't.
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // True when the shown results are from the last time that request worked,
    // saved on disk, because Lospec couldn't be reached.
    Q_PROPERTY(bool offline READ offline NOTIFY resultsChanged)

public:
    enum CountMode { AnyCount, ExactCount, MaxCount, MinCount };
    Q_ENUM(CountMode)
    enum Sort { DefaultSort, Downloads, Alphabetical, Newest };
    Q_ENUM(Sort)
    static constexpr int kPageSize = 10;

    explicit LospecClient(QObject* parent = nullptr);

    QVariantList results() const { return m_results; }
    int totalCount() const { return m_totalCount; }
    int page() const { return m_page; }
    int pageCount() const { return (m_totalCount + kPageSize - 1) / kPageSize; }
    bool loading() const { return m_reply != nullptr; }
    QString error() const { return m_error; }
    bool offline() const { return m_offline; }

    // Keeps saved replies in `folder` instead of the app's cache folder (tests).
    void setCacheFolder(const QString& folder) { m_cacheFolder = folder; }
    // Saved replies: one file per request URL. Only successful replies are saved.
    static QString cachePath(const QString& folder, const QUrl& url);
    static void saveCached(const QString& folder, const QUrl& url, const QByteArray& body);
    static QByteArray loadCached(const QString& folder, const QUrl& url);

    // One page (from 0) of palettes with `tag` (empty: all) and a color count
    // (`colors`, ignored for AnyCount), in `sort` order.
    Q_INVOKABLE void browse(const QString& tag, int colors, int countMode, int sort, int page);
    // One palette by its name, slug or Lospec link; it becomes the only result.
    Q_INVOKABLE void fetch(const QString& nameOrUrl);
    // Repeats the last request.
    Q_INVOKABLE void retry();
    Q_INVOKABLE QUrl pageUrl(const QString& slug) const;

    // Parsing and naming, separate from the network (tests).
    static QUrl listUrl(const QString& tag, int colors, int countMode, int sort, int page);
    static bool parseList(const QByteArray& json, QVariantList& results, int& totalCount, QString* error);
    static bool parsePalette(const QByteArray& json, const QString& slug, QVariantMap& result, QString* error);
    // "Sweetie 16", "sweetie-16" or ".../palette-list/sweetie-16(.json)" -> "sweetie-16".
    static QString slugFor(const QString& nameOrUrl);

signals:
    void resultsChanged();
    void loadingChanged();
    void errorChanged();

private:
    void request(const QUrl& url, bool single, const QString& slug);
    void finished(QNetworkReply* reply, bool single, const QString& slug);
    // Shows a reply's palettes; false (and an error set) if they don't parse.
    bool show(const QByteArray& body, bool single, const QString& slug);
    QString cacheFolder() const;
    void setError(const QString& error);

    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    QUrl m_lastUrl;
    bool m_lastSingle = false;
    QString m_lastSlug;
    QVariantList m_results;
    int m_totalCount = 0;
    int m_page = 0;
    int m_requestedPage = 0;
    QString m_error;
    QString m_cacheFolder;
    bool m_offline = false;
};
