/**
 * @file BibCache.cpp
 * @brief A local cache of fetched bibliography files (bibxml).
 */

#include "BibCache.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

// How long one download may run before it is given up on (15 seconds).
// A bibliography file is about a kilobyte; anything slower is a dead
// connection, and the interface must not wait on it forever.
static const int FetchTimeoutMs = 15000;

// The QSettings key holding the cache folder the author picked.
static const char* CacheLocationKey = "bibliography/cacheLocation";

// The index file name inside the cache folder.
static const char* IndexFileName = "index.json";

BibCache::BibCache(QObject* Parent)
    : QObject(Parent)
    , _Network(new QNetworkAccessManager(this))
    , _Location(Location())
    , _Pending(0)
{
    EnsureLocation();
    LoadIndex();
}

QString BibCache::DefaultLocation()
{
    QString Config = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);
    return QDir(Config).filePath(
        QStringLiteral("RiverExplorer/CoOp/Bibliography"));
}

QString BibCache::Location() const
{
    QSettings Settings;
    return Settings.value(QString::fromLatin1(CacheLocationKey),
                          DefaultLocation()).toString();
}

QVector<BibCacheEntry> BibCache::Entries() const
{
    return _Entries;
}

bool BibCache::IsCached(const QString& Url) const
{
    for (const BibCacheEntry& Entry : _Entries) {
        if (Entry.Url != Url) {
            continue;
        }
        QString Path = QDir(_Location).filePath(Entry.File);
        if (!QFile::exists(Path)) {
            return false;
        }
        QString Anchor;
        QString Title;
        return ParseReferenceFile(Path, Anchor, Title);
    }
    return false;
}

QString BibCache::CachedTitle(const QString& Url) const
{
    for (const BibCacheEntry& Entry : _Entries) {
        if (Entry.Url == Url) {
            return Entry.Title;
        }
    }
    return QString();
}

void BibCache::EnsureCached(const QStringList& Urls)
{
    QStringList Wanted = Urls;
    Wanted.removeDuplicates();
    for (const QString& Url : Wanted) {
        if (IsCached(Url)) {
            continue;
        }
        QUrl Address(Url);
        if (Address.scheme() != QStringLiteral("http") &&
            Address.scheme() != QStringLiteral("https")) {
            _Failed[Url] = tr("The address is not a web address "
                              "the cache can fetch.");
            continue;
        }
        FetchOne(Url);
    }
    if (_Pending == 0) {
        // Nothing to fetch: answer at once, queued, so the caller
        // always hears back the same way.
        QMetaObject::invokeMethod(this, [this]() { emit FetchFinished(); },
                                  Qt::QueuedConnection);
    }
}

void BibCache::RefreshEntry(const QString& Url)
{
    _Failed.remove(Url);
    QUrl Address(Url);
    if (Address.scheme() != QStringLiteral("http") &&
        Address.scheme() != QStringLiteral("https")) {
        _Failed[Url] = tr("The address is not a web address "
                          "the cache can fetch.");
        if (_Pending == 0) {
            // Nothing running: answer at once, queued, so the
            // caller always hears back the same way. When a batch
            // is already running its finish covers this too.
            QMetaObject::invokeMethod(this, [this]() { emit FetchFinished(); },
                                      Qt::QueuedConnection);
        }
        return;
    }
    FetchOne(Url);
}

bool BibCache::DeleteEntry(const QString& Url)
{
    for (int i = 0; i < _Entries.size(); ++i) {
        if (_Entries[i].Url != Url) {
            continue;
        }
        QFile::remove(QDir(_Location).filePath(_Entries[i].File));
        _Entries.removeAt(i);
        _Failed.remove(Url);
        SaveIndex();
        emit EntriesChanged();
        return true;
    }
    return false;
}

bool BibCache::ClearAll()
{
    for (const BibCacheEntry& Entry : _Entries) {
        QFile::remove(QDir(_Location).filePath(Entry.File));
    }
    _Entries.clear();
    _Failed.clear();
    SaveIndex();
    emit EntriesChanged();
    return _Entries.isEmpty();
}

bool BibCache::MoveLocation(const QString& NewPath, bool CopyOld)
{
    QString New = QDir::cleanPath(NewPath);
    QString Old = QDir::cleanPath(_Location);
    if (New.isEmpty()) {
        _LastError = tr("Pick a folder first.");
        return false;
    }
    if (New == Old) {
        _LastError = tr("That is already the cache folder.");
        return false;
    }
    if (New.startsWith(Old + QStringLiteral("/")) ||
        Old.startsWith(New + QStringLiteral("/"))) {
        _LastError = tr("The new folder cannot sit inside the old one, "
                        "or the other way round.");
        return false;
    }
    QDir NewDir(New);
    if (!NewDir.mkpath(QStringLiteral("."))) {
        _LastError = tr("Could not create the folder.");
        return false;
    }
    QDir OldDir(Old);
    if (OldDir.exists()) {
        if (CopyOld) {
            for (const QFileInfo& Info :
                 OldDir.entryInfoList(QDir::Files)) {
                QString Target = NewDir.filePath(Info.fileName());
                if (QFile::exists(Target)) {
                    QFile::remove(Target);
                }
                if (!QFile::copy(Info.absoluteFilePath(), Target)) {
                    _LastError = tr("Could not copy %1.")
                        .arg(Info.fileName());
                    return false;
                }
            }
        }
        // Either way the old folder is left empty: there is no
        // reason to keep data the program can no longer use and
        // would not remember.
        if (!OldDir.removeRecursively()) {
            _LastError = tr("Could not remove the old cache folder.");
            return false;
        }
    }
    QSettings Settings;
    Settings.setValue(QString::fromLatin1(CacheLocationKey), New);
    _Location = New;
    _Failed.clear();
    LoadIndex();
    emit EntriesChanged();
    return true;
}

QString BibCache::FailedError(const QString& Url) const
{
    return _Failed.value(Url);
}

QMap<QString, QString> BibCache::FailedFetches() const
{
    return _Failed;
}

QString BibCache::LastError() const
{
    return _LastError;
}

void BibCache::LoadIndex()
{
    _Entries.clear();
    QFile File(IndexPath());
    if (!File.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    QJsonDocument Document = QJsonDocument::fromJson(File.readAll());
    File.close();
    if (Document.isNull() || !Document.isObject()) {
        return;
    }
    QJsonArray Rows = Document.object()
        .value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& Value : Rows) {
        if (!Value.isObject()) {
            continue;
        }
        QJsonObject Row = Value.toObject();
        BibCacheEntry Entry;
        Entry.Url = Row.value(QStringLiteral("url")).toString();
        Entry.File = Row.value(QStringLiteral("file")).toString();
        Entry.Anchor = Row.value(QStringLiteral("anchor")).toString();
        Entry.Title = Row.value(QStringLiteral("title")).toString();
        Entry.Fetched = Row.value(QStringLiteral("fetched")).toString();
        if (Entry.Url.isEmpty() || Entry.File.isEmpty()) {
            continue;
        }
        Entry.Size = QFileInfo(QDir(_Location).filePath(Entry.File)).size();
        _Entries.append(Entry);
    }
    // An entry whose file is gone is not an entry any more.
    bool Pruned = false;
    for (int i = _Entries.size() - 1; i >= 0; --i) {
        if (!QFile::exists(QDir(_Location).filePath(_Entries[i].File))) {
            _Entries.removeAt(i);
            Pruned = true;
        }
    }
    if (Pruned) {
        SaveIndex();
    }
}

bool BibCache::SaveIndex()
{
    QJsonArray Rows;
    for (const BibCacheEntry& Entry : _Entries) {
        QJsonObject Row;
        Row[QStringLiteral("url")] = Entry.Url;
        Row[QStringLiteral("file")] = Entry.File;
        Row[QStringLiteral("anchor")] = Entry.Anchor;
        Row[QStringLiteral("title")] = Entry.Title;
        Row[QStringLiteral("fetched")] = Entry.Fetched;
        Rows.append(Row);
    }
    QJsonObject Root;
    Root[QStringLiteral("entries")] = Rows;
    QJsonDocument Document(Root);
    if (!EnsureLocation()) {
        return false;
    }
    QFile File(IndexPath());
    if (!File.open(QIODevice::WriteOnly | QIODevice::Truncate |
                   QIODevice::Text)) {
        _LastError = tr("Could not write the cache index.");
        return false;
    }
    File.write(Document.toJson(QJsonDocument::Indented));
    File.close();
    return true;
}

bool BibCache::EnsureLocation()
{
    if (QDir().mkpath(_Location)) {
        return true;
    }
    _LastError = tr("Could not create the cache folder.");
    return false;
}

QString BibCache::IndexPath() const
{
    return QDir(_Location).filePath(QString::fromLatin1(IndexFileName));
}

QString BibCache::CacheFileName(const QString& Url) const
{
    QString Base = QFileInfo(QUrl(Url).path()).fileName();
    if (Base.isEmpty()) {
        Base = QStringLiteral("reference.xml");
    }
    for (const BibCacheEntry& Entry : _Entries) {
        if (Entry.File == Base && Entry.Url != Url) {
            QString Stem = Base;
            QString Suffix;
            int Dot = Base.lastIndexOf(QLatin1Char('.'));
            if (Dot > 0) {
                Stem = Base.left(Dot);
                Suffix = Base.mid(Dot);
            }
            Base = Stem + QStringLiteral("-") +
                QString::fromLatin1(
                    QCryptographicHash::hash(Url.toUtf8(),
                                             QCryptographicHash::Sha1)
                        .toHex().left(8)) +
                Suffix;
            break;
        }
    }
    if (_FetchingFiles.contains(Base)) {
        // A download already in flight claimed this name; give
        // this one a name of its own rather than overwriting it.
        QString Stem = Base;
        QString Suffix;
        int Dot = Base.lastIndexOf(QLatin1Char('.'));
        if (Dot > 0) {
            Stem = Base.left(Dot);
            Suffix = Base.mid(Dot);
        }
        Base = Stem + QStringLiteral("-") +
            QString::fromLatin1(
                QCryptographicHash::hash(Url.toUtf8(),
                                         QCryptographicHash::Sha1)
                    .toHex().left(8)) +
            Suffix;
    }
    return Base;
}

void BibCache::FetchOne(const QString& Url)
{
    QUrl Address(Url);
    QNetworkRequest Request(Address);
    Request.setTransferTimeout(FetchTimeoutMs);
    Request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("ProtocolDocEditor/1.1 "
                                     "(bibliography cache)"));
    QNetworkReply* Reply = _Network->get(Request);
    ++_Pending;
    QString FileName = CacheFileName(Url);
    _FetchingFiles.insert(FileName);
    connect(Reply, &QNetworkReply::finished, this,
            [this, Reply, Url, FileName]() {
                OnReplyFinished(Reply, Url, FileName);
            });
}

void BibCache::OnReplyFinished(QNetworkReply* Reply, const QString& Url,
                               const QString& FileName)
{
    Reply->deleteLater();
    _FetchingFiles.remove(FileName);
    QString Path = QDir(_Location).filePath(FileName);
    if (Reply->error() != QNetworkReply::NoError) {
        _Failed[Url] = Reply->errorString();
    } else if (Reply->attribute(
                   QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
        _Failed[Url] = tr("The server answered %1.")
            .arg(Reply->attribute(
                     QNetworkRequest::HttpStatusCodeAttribute).toInt());
    } else if (!EnsureLocation()) {
        _Failed[Url] = _LastError;
    } else {
        QByteArray Data = Reply->readAll();
        QFile File(Path);
        bool Written = File.open(QIODevice::WriteOnly |
                                 QIODevice::Truncate);
        if (Written && File.write(Data) != Data.size()) {
            Written = false;
        }
        if (Written) {
            File.close();
        }
        if (!Written) {
            _Failed[Url] = tr("Could not write the cached file.");
            File.close();
            File.remove();
        } else {
            QString Anchor;
            QString Title;
            if (!ParseReferenceFile(Path, Anchor, Title)) {
                _Failed[Url] = tr("The downloaded file is not a reference.");
                QFile::remove(Path);
            } else {
                bool Found = false;
                for (BibCacheEntry& Entry : _Entries) {
                    if (Entry.Url != Url) {
                        continue;
                    }
                    if (Entry.File != FileName) {
                        QFile::remove(
                            QDir(_Location).filePath(Entry.File));
                    }
                    Entry.File = FileName;
                    Entry.Anchor = Anchor;
                    Entry.Title = Title;
                    Entry.Fetched =
                        QDate::currentDate().toString(Qt::ISODate);
                    Entry.Size = QFileInfo(Path).size();
                    Found = true;
                    break;
                }
                if (!Found) {
                    BibCacheEntry Entry;
                    Entry.Url = Url;
                    Entry.File = FileName;
                    Entry.Anchor = Anchor;
                    Entry.Title = Title;
                    Entry.Fetched =
                        QDate::currentDate().toString(Qt::ISODate);
                    Entry.Size = QFileInfo(Path).size();
                    _Entries.append(Entry);
                }
                _Failed.remove(Url);
                SaveIndex();
                emit EntriesChanged();
            }
        }
    }
    --_Pending;
    if (_Pending == 0) {
        emit FetchFinished();
    }
}

bool BibCache::ParseReferenceFile(const QString& Path, QString& Anchor,
                                  QString& Title) const
{
    Anchor.clear();
    Title.clear();
    QFile File(Path);
    if (!File.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QXmlStreamReader Reader(&File);
    bool SawRoot = false;
    while (!Reader.atEnd()) {
        Reader.readNext();
        if (!Reader.isStartElement()) {
            continue;
        }
        if (!SawRoot) {
            QString Root = Reader.name().toString();
            if (Root != QStringLiteral("reference") &&
                Root != QStringLiteral("referencegroup")) {
                return false;
            }
            Anchor = Reader.attributes()
                .value(QStringLiteral("anchor")).toString();
            if (Anchor.isEmpty()) {
                return false;
            }
            SawRoot = true;
            if (Root == QStringLiteral("referencegroup")) {
                return !Reader.hasError();
            }
        } else if (Reader.name() == QStringLiteral("title")) {
            Title = Reader.readElementText().trimmed();
            return !Reader.hasError();
        }
    }
    return SawRoot && !Reader.hasError();
}
