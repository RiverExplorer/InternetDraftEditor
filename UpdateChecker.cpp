// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file UpdateChecker.cpp
 * @brief Implementation of the GitHub update check.
 */

#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

/**
 * @brief The GitHub releases list for this editor.
 *
 * The list endpoint, not /releases/latest: the current release
 * is a pre-release, which /releases/latest skips. The first
 * entry in the list is the newest release.
 */
static const char* ReleasesUrl =
    "https://api.github.com/repos/RiverExplorer/InternetDraftEditor/releases";

/**
 * @brief The QSettings key for the last successful check time.
 */
static const char* LastCheckTimeKey = "update/lastCheckTime";

/**
 * @brief The QSettings key for the app version at the last
 *        successful check.
 */
static const char* LastCheckVersionKey = "update/lastCheckVersion";

/**
 * @brief How many days pass between automatic checks.
 */
static const int CheckIntervalDays = 7;

UpdateChecker::UpdateChecker(QWidget* Parent)
    : QObject(Parent)
    , _ParentWidget(Parent)
    , _NetworkManager(this)
    , _ReleasesReply(nullptr)
    , _DownloadReply(nullptr)
    , _Busy(false)
    , _Manual(false)
    , _DownloadPath()
    , _DownloadFile()
    , _ExpectedDownloadSize(0)
    , _ProgressDialog(nullptr)
{
}

void UpdateChecker::CheckForUpdates(bool Manual)
{
    // A check already running is ignored: two overlapping
    // requests would show two dialogs for one release.
    if (_Busy) {
        return;
    }
    if (!Manual && !ShouldCheckOnStartup()) {
        return;
    }
    _Manual = Manual;
    _Busy = true;
    RequestReleases();
}

int UpdateChecker::CompareVersions(const QString& Left, const QString& Right)
{
    const QStringList LeftParts =
        StrippedVersion(Left).split(QChar('.'), Qt::SkipEmptyParts);
    const QStringList RightParts =
        StrippedVersion(Right).split(QChar('.'), Qt::SkipEmptyParts);
    const int PartCount = qMax(LeftParts.size(), RightParts.size());
    for (int Index = 0; Index < PartCount; ++Index) {
        // A missing part counts as 0, so 1.1 beats 1.1.0 only
        // when the extra parts are non-zero; 1.1 and 1.1.0
        // compare equal.
        const int LeftPart =
            (Index < LeftParts.size()) ? NumericPrefix(LeftParts.at(Index)) : 0;
        const int RightPart =
            (Index < RightParts.size()) ? NumericPrefix(RightParts.at(Index)) : 0;
        if (LeftPart < RightPart) {
            return -1;
        }
        if (LeftPart > RightPart) {
            return 1;
        }
    }
    return 0;
}

bool UpdateChecker::ShouldCheckOnStartup() const
{
    QSettings Settings;
    const QDateTime LastCheck =
        Settings.value(QLatin1String(LastCheckTimeKey)).toDateTime();
    // (a) First run ever: nothing was ever remembered.
    if (!LastCheck.isValid()) {
        return true;
    }
    // (b) First run after an update: the running version differs
    // from the version remembered at the last check.
    const QString LastVersion =
        Settings.value(QLatin1String(LastCheckVersionKey)).toString();
    if (LastVersion != QCoreApplication::applicationVersion()) {
        return true;
    }
    // (c) 7 or more days have passed since the last check.
    return LastCheck.daysTo(QDateTime::currentDateTime()) >= CheckIntervalDays;
}

void UpdateChecker::RequestReleases()
{
    QNetworkRequest Request(QUrl(QString::fromLatin1(ReleasesUrl)));
    // The GitHub API requires a User-Agent header.
    Request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("InternetDraftsEditor/")
                          + QCoreApplication::applicationVersion());
    Request.setRawHeader("Accept", "application/vnd.github+json");
    // QNetworkAccessManager::get returns QNetworkReply*.
    _ReleasesReply = _NetworkManager.get(Request);
    connect(_ReleasesReply, &QNetworkReply::finished,
            this, &UpdateChecker::OnReleasesFinished);
}

void UpdateChecker::RecordSuccessfulCheck()
{
    QSettings Settings;
    Settings.setValue(QLatin1String(LastCheckTimeKey),
                      QDateTime::currentDateTime());
    Settings.setValue(QLatin1String(LastCheckVersionKey),
                      QCoreApplication::applicationVersion());
}

void UpdateChecker::FailCheck(bool Manual, const QString& Message)
{
    // An automatic check fails quietly: no dialog, no error, and
    // nothing recorded, so the check is tried again on the next
    // startup. A manual check gets a brief message.
    if (Manual) {
        QMessageBox::information(_ParentWidget, tr("Check for Updates"),
                                 Message);
    }
}

void UpdateChecker::OnReleasesFinished()
{
    QNetworkReply* Reply = _ReleasesReply;
    _ReleasesReply = nullptr;
    const bool Manual = _Manual;
    _Manual = false;
    _Busy = false;

    // QNetworkReply::error returns QNetworkReply::NetworkError;
    // attribute returns QVariant, so toInt reads the HTTP status.
    const QNetworkReply::NetworkError Error = Reply->error();
    const int Status =
        Reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray Data = Reply->readAll();
    Reply->deleteLater();
    if (Error != QNetworkReply::NoError || Status != 200) {
        FailCheck(Manual,
                  tr("Could not check for updates (network error)."));
        return;
    }

    // QJsonDocument::fromJson returns QJsonDocument; a document
    // that is not an array is not the releases list.
    QJsonParseError ParseError;
    const QJsonDocument Document = QJsonDocument::fromJson(Data, &ParseError);
    if (ParseError.error != QJsonParseError::NoError || !Document.isArray()) {
        FailCheck(Manual,
                  tr("Could not check for updates (network error)."));
        return;
    }
    // QJsonDocument::array returns QJsonArray; first returns
    // QJsonValue, whose toObject returns QJsonObject.
    const QJsonArray Releases = Document.array();
    if (Releases.isEmpty()) {
        FailCheck(Manual,
                  tr("Could not check for updates (network error)."));
        return;
    }
    const QJsonObject Latest = Releases.first().toObject();
    // QJsonObject::value returns QJsonValue; toString returns QString.
    const QString Tag = Latest.value(QStringLiteral("tag_name")).toString();
    if (Tag.isEmpty()) {
        FailCheck(Manual,
                  tr("Could not check for updates (network error)."));
        return;
    }

    // The API answered with a usable releases list: remember the
    // check now, whether or not a newer version turns up.
    RecordSuccessfulCheck();

    const QString CurrentVersion = QCoreApplication::applicationVersion();
    if (CompareVersions(Tag, CurrentVersion) <= 0) {
        // No newer version. An automatic check says nothing; a
        // manual check says so briefly.
        if (Manual) {
            QMessageBox::information(
                _ParentWidget, tr("Check for Updates"),
                tr("You're up to date (version %1).").arg(CurrentVersion));
        }
        return;
    }

    // The newest release: find the installer, the asset whose
    // name ends with ".exe". QJsonValue::toArray returns
    // QJsonArray; toDouble reads the asset size (a JSON number).
    const QJsonArray Assets =
        Latest.value(QStringLiteral("assets")).toArray();
    QString DownloadUrl;
    qint64 ExpectedSize = 0;
    for (const QJsonValue& AssetValue : Assets) {
        const QJsonObject Asset = AssetValue.toObject();
        const QString AssetName =
            Asset.value(QStringLiteral("name")).toString();
        if (AssetName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
            DownloadUrl = Asset.value(QStringLiteral("browser_download_url"))
                              .toString();
            ExpectedSize = static_cast<qint64>(
                Asset.value(QStringLiteral("size")).toDouble());
            break;
        }
    }
    if (DownloadUrl.isEmpty()) {
        FailCheck(Manual,
                  tr("The latest release has no installer to download."));
        return;
    }

    OfferUpdate(StrippedVersion(Tag),
                Latest.value(QStringLiteral("name")).toString(),
                DownloadUrl, ExpectedSize);
}

void UpdateChecker::OfferUpdate(const QString& NewVersion,
                                const QString& ReleaseTitle,
                                const QString& DownloadUrl,
                                qint64 ExpectedSize)
{
    const QString CurrentVersion = QCoreApplication::applicationVersion();
    QString Text;
    if (ReleaseTitle.isEmpty()) {
        Text = tr("InternetDraftsEditor %1 is available "
                  "(you have %2).\n\n"
                  "Do you want to download and install it now?")
                   .arg(NewVersion, CurrentVersion);
    } else {
        Text = tr("InternetDraftsEditor %1 is available "
                  "(you have %2).\n\n"
                  "%3\n\n"
                  "Do you want to download and install it now?")
                   .arg(NewVersion, CurrentVersion, ReleaseTitle);
    }
    QMessageBox Box(_ParentWidget);
    Box.setWindowTitle(tr("Check for Updates"));
    Box.setText(Text);
    // addButton returns QPushButton*; clickedButton returns
    // QAbstractButton*, so the choice is the button pointer.
    QPushButton* DownloadButton = Box.addButton(
        tr("Download and Install"), QMessageBox::AcceptRole);
    Box.addButton(tr("Later"), QMessageBox::RejectRole);
    Box.setDefaultButton(DownloadButton);
    Box.exec();
    if (Box.clickedButton() == DownloadButton) {
        StartDownload(DownloadUrl, NewVersion, ExpectedSize);
    }
}

void UpdateChecker::StartDownload(const QString& DownloadUrl,
                                  const QString& Version,
                                  qint64 ExpectedSize)
{
    _ExpectedDownloadSize = ExpectedSize;
    // QStandardPaths::writableLocation returns QString.
    const QString TempDir = QStandardPaths::writableLocation(
        QStandardPaths::TempLocation);
    _DownloadPath = QDir::cleanPath(
        TempDir + QStringLiteral("/InternetDraftsEditor-") + Version
        + QStringLiteral("-setup.exe"));
    if (QFile::exists(_DownloadPath)) {
        QFile::remove(_DownloadPath);
    }
    _DownloadFile.setFileName(_DownloadPath);
    if (!_DownloadFile.open(QIODevice::WriteOnly)) {
        QMessageBox::information(
            _ParentWidget, tr("Check for Updates"),
            tr("Could not save the installer. Nothing was installed."));
        return;
    }

    QNetworkRequest Request{QUrl(DownloadUrl)};
    Request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("InternetDraftsEditor/")
                          + QCoreApplication::applicationVersion());
    _DownloadReply = _NetworkManager.get(Request);
    connect(_DownloadReply, &QNetworkReply::readyRead,
            this, &UpdateChecker::OnDownloadReadyRead);
    connect(_DownloadReply, &QNetworkReply::downloadProgress,
            this, &UpdateChecker::OnDownloadProgress);
    connect(_DownloadReply, &QNetworkReply::finished,
            this, &UpdateChecker::OnDownloadFinished);
    _Busy = true;

    _ProgressDialog = new QProgressDialog(
        tr("Downloading InternetDraftsEditor %1...").arg(Version),
        tr("Cancel"), 0, 100, _ParentWidget);
    _ProgressDialog->setMinimumDuration(0);
    _ProgressDialog->setAutoClose(false);
    _ProgressDialog->setValue(0);
    // Cancel stops the download; the finished slot then cleans
    // up the partial file.
    connect(_ProgressDialog, &QProgressDialog::canceled,
            _DownloadReply, &QNetworkReply::abort);
}

void UpdateChecker::OnDownloadReadyRead()
{
    if (_DownloadReply != nullptr && _DownloadFile.isOpen()) {
        // readAll returns QByteArray; QFile::write returns qint64.
        _DownloadFile.write(_DownloadReply->readAll());
    }
}

void UpdateChecker::OnDownloadProgress(qint64 BytesReceived, qint64 BytesTotal)
{
    if (_ProgressDialog == nullptr) {
        return;
    }
    if (BytesTotal > 0) {
        // Percent, so the dialog never needs the byte count as
        // an int.
        _ProgressDialog->setValue(static_cast<int>(
            (BytesReceived * 100) / BytesTotal));
    }
}

void UpdateChecker::OnDownloadFinished()
{
    QNetworkReply* Reply = _DownloadReply;
    _DownloadReply = nullptr;
    const bool Cancelled =
        (_ProgressDialog != nullptr) && _ProgressDialog->wasCanceled();
    const QNetworkReply::NetworkError Error = Reply->error();
    Reply->deleteLater();
    _DownloadFile.close();
    if (_ProgressDialog != nullptr) {
        _ProgressDialog->close();
        _ProgressDialog->deleteLater();
        _ProgressDialog = nullptr;
    }
    _Busy = false;

    if (Cancelled || Error != QNetworkReply::NoError) {
        QFile::remove(_DownloadPath);
        QMessageBox::information(
            _ParentWidget, tr("Check for Updates"),
            Cancelled
                ? tr("The download was cancelled. Nothing was installed.")
                : tr("The download did not finish. Nothing was installed."));
        return;
    }

    // QFileInfo::size returns qint64. The file must exist, hold
    // bytes, and — when the release gave a size — match it.
    const qint64 Size = QFileInfo(_DownloadPath).size();
    if (Size <= 0
        || (_ExpectedDownloadSize > 0 && Size != _ExpectedDownloadSize)) {
        QFile::remove(_DownloadPath);
        QMessageBox::information(
            _ParentWidget, tr("Check for Updates"),
            tr("The downloaded installer looks incomplete. "
               "Nothing was installed."));
        return;
    }

    // QProcess::startDetached returns bool: true when the
    // installer started. The installer is detached, so it keeps
    // running after this editor quits.
    if (!QProcess::startDetached(_DownloadPath, QStringList())) {
        QMessageBox::information(
            _ParentWidget, tr("Check for Updates"),
            tr("The installer downloaded, but it could not be started."));
        return;
    }

    // Quit first, so the installer can replace the editor's files.
    QCoreApplication::quit();
}

QString UpdateChecker::StrippedVersion(const QString& Version)
{
    if (Version.startsWith(QChar('V')) || Version.startsWith(QChar('v'))) {
        return Version.mid(1);
    }
    return Version;
}

int UpdateChecker::NumericPrefix(const QString& Part)
{
    int Value = 0;
    for (const QChar& Character : Part) {
        if (!Character.isDigit()) {
            break;
        }
        Value = Value * 10 + Character.digitValue();
    }
    return Value;
}
