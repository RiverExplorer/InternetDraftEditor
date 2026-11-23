/**
 * @file BibCache.h
 * @brief A local cache of fetched bibliography files (bibxml).
 *
 * The editor cites included references with xi:include, pointing at
 * the IETF bibliography files (bib.ietf.org). Those files are fetched
 * over HTTPS with Qt's network code and kept here, one file per
 * address, with a small index (index.json) beside them. Validation
 * and the outline read from the cache; the draft file itself keeps
 * the xi:include. The cache folder is managed from the Bibliography
 * dialog (Settings > Bibliography...).
 */

#ifndef DOCEDITOR_BIBCACHE_H
#define DOCEDITOR_BIBCACHE_H

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief One cached bibliography entry.
 */
struct BibCacheEntry
{
    QString Url;     ///< The address the file was fetched from.
    QString File;    ///< The file name inside the cache folder.
    QString Anchor;  ///< The anchor the fetched reference carries.
    QString Title;   ///< The reference title, when the file carries one.
    QString Fetched; ///< When it was fetched, ISO date.
    qint64 Size = 0; ///< The cached file size in bytes.
};

/**
 * @brief Fetches bibliography files and keeps them in a local cache.
 *
 * Fetching is asynchronous: EnsureCached starts the downloads for
 * the addresses that are not cached yet and emits FetchFinished
 * when they are all done (or at once, queued, when there is nothing
 * to fetch). A failed download is remembered with its reason, so a
 * caller can say plainly that the reference was not checked.
 */
class BibCache : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Builds the cache, loading the index from the cache folder.
     * @param Parent The parent object.
     */
    explicit BibCache(QObject* Parent = nullptr);

    /**
     * @brief The default cache folder.
     *
     * The shared CoOp folder beside the schemas and dictionaries:
     * %APPDATA%\RiverExplorer\CoOp\Bibliography on Windows.
     * @return The default folder path.
     */
    static QString DefaultLocation();

    /**
     * @brief The cache folder in use: the remembered setting, or the default.
     * @return The folder path.
     */
    QString Location() const;

    /**
     * @brief The cached entries, in index order.
     * @return The entries.
     */
    QVector<BibCacheEntry> Entries() const;

    /**
     * @brief True when the address has a cached file that reads as a reference.
     * @param Url The address.
     * @return True when it is cached and usable.
     */
    bool IsCached(const QString& Url) const;

    /**
     * @brief The cached title for an address, or empty when it is not cached.
     * @param Url The address.
     * @return The title, or empty.
     */
    QString CachedTitle(const QString& Url) const;

    /**
     * @brief Fetches the addresses that are not cached yet.
     *
     * Only http(s) addresses are fetched; anything else is recorded
     * as a failure. FetchFinished is emitted when the downloads are
     * all done, or queued at once when there is nothing to fetch.
     * @param Urls The addresses to make sure are cached.
     */
    void EnsureCached(const QStringList& Urls);

    /**
     * @brief Re-fetches one entry, replacing it.
     * @param Url The address to refresh.
     */
    void RefreshEntry(const QString& Url);

    /**
     * @brief Deletes one cached entry, file and index row.
     * @param Url The address to delete.
     * @return True when it was deleted.
     */
    bool DeleteEntry(const QString& Url);

    /**
     * @brief Deletes every cached entry, file and index row.
     * @return True when the cache is empty afterwards.
     */
    bool ClearAll();

    /**
     * @brief Moves the cache to a new folder.
     *
     * When CopyOld is true the cached entries move to the new folder;
     * when false they are thrown away. Either way the old folder is
     * left empty: there is no reason to keep data the program can
     * no longer use and would not remember.
     * @param NewPath The new folder.
     * @param CopyOld True to move the entries, false to delete them.
     * @return True when the move went through.
     */
    bool MoveLocation(const QString& NewPath, bool CopyOld);

    /**
     * @brief The reason a download failed, or empty when it did not fail.
     * @param Url The address.
     * @return The failure reason, or empty.
     */
    QString FailedError(const QString& Url) const;

    /**
     * @brief Every failed download, address to reason.
     * @return The failures.
     */
    QMap<QString, QString> FailedFetches() const;

    /**
     * @brief Why the last cache operation failed, or empty.
     * @return The failure reason, or empty.
     */
    QString LastError() const;

signals:
    /**
     * @brief Emitted when a round of downloads finishes.
     */
    void FetchFinished();

    /**
     * @brief Emitted when the entries change (fetch, delete, clear, move).
     */
    void EntriesChanged();

private:
    void LoadIndex();
    bool SaveIndex();
    bool EnsureLocation();
    QString IndexPath() const;
    QString CacheFileName(const QString& Url) const;
    void FetchOne(const QString& Url);
    void OnReplyFinished(QNetworkReply* Reply, const QString& Url,
                         const QString& FileName);
    bool ParseReferenceFile(const QString& Path, QString& Anchor,
                            QString& Title) const;

    QNetworkAccessManager* _Network;
    QString _Location;
    QVector<BibCacheEntry> _Entries;
    QMap<QString, QString> _Failed;
    QSet<QString> _FetchingFiles;
    int _Pending;
    QString _LastError;
};

#endif // DOCEDITOR_BIBCACHE_H
