// Copyright (c) 2026 RiverExplorer LLC
// RiverExplorer is a registered trademark of Douglas Mark Royer.
//
// This software is released under the MIT License.
// See LICENSE.md for the full license text.

/**
 * @file UpdateChecker.h
 * @brief Checks GitHub for a newer release of the editor.
 *
 * On startup the editor asks GitHub for new releases, but only
 * once every 7 days (or on the first run, or the first run after
 * an update). Help > Check for Updates asks at once, whatever
 * the timing rule says. When a newer release is found the user
 * is offered the download; accepting it downloads the installer
 * to the temporary folder, starts it, and quits the editor so
 * the installer can replace the files.
 */

#ifndef DOCEDITOR_UPDATECHECKER_H
#define DOCEDITOR_UPDATECHECKER_H

#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;
class QProgressDialog;
class QWidget;

/**
 * @brief Asks GitHub for new releases and installs one when the
 *        user says so.
 *
 * The checker owns the network manager, the remembered check
 * (QSettings keys update/lastCheckTime and
 * update/lastCheckVersion), the version comparison, and the
 * dialogs. MainWindow only creates it, starts the startup
 * check, and forwards Help > Check for Updates.
 */
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Creates the checker.
     * @param Parent The parent widget, for the dialogs, or nullptr.
     */
    explicit UpdateChecker(QWidget* Parent = nullptr);

    /**
     * @brief Checks GitHub for a newer release.
     *
     * An automatic check (Manual false) first applies the timing
     * rule: it checks when there is no remembered check, when
     * the remembered version differs from the running version
     * (the first run after an update), or when 7 or more days
     * have passed since the remembered check; otherwise it does
     * nothing. A manual check (Manual true) always checks.
     * A check already running is ignored.
     * @param Manual True for Help > Check for Updates; false for
     *        the startup check.
     */
    void CheckForUpdates(bool Manual);

    /**
     * @brief Compares two version numbers, part by part.
     *
     * A leading "V" or "v" (the GitHub tag carries one: "V1.1.0")
     * is ignored, each part is compared as a number, and missing
     * parts count as 0, so 1.10.0 beats 1.9.0.
     * @param Left The first version.
     * @param Right The second version.
     * @return -1 when Left is older, 0 when they are the same,
     *         1 when Left is newer.
     */
    static int CompareVersions(const QString& Left, const QString& Right);

private slots:
    /**
     * @brief Handles the GitHub releases list coming back.
     */
    void OnReleasesFinished();

    /**
     * @brief Writes the installer bytes received so far to the
     *        temporary file.
     */
    void OnDownloadReadyRead();

    /**
     * @brief Moves the download progress dialog along.
     * @param BytesReceived The bytes received so far.
     * @param BytesTotal The total bytes, or -1 when unknown.
     */
    void OnDownloadProgress(qint64 BytesReceived, qint64 BytesTotal);

    /**
     * @brief Finishes the download: checks the file, starts the
     *        installer, and quits the editor.
     */
    void OnDownloadFinished();

private:
    /**
     * @brief True when the startup check should ask GitHub.
     *
     * True when there is no remembered check (first run ever),
     * when the remembered version differs from the running
     * version (first run after an update), or when 7 or more
     * days have passed since the remembered check.
     * @return True when the startup check should run.
     */
    bool ShouldCheckOnStartup() const;

    /**
     * @brief Asks GitHub for the releases list.
     *
     * The list endpoint is used, not /releases/latest: the
     * current release is a pre-release, which /releases/latest
     * skips. The first entry in the list is the newest release.
     * The GitHub API requires a User-Agent header.
     */
    void RequestReleases();

    /**
     * @brief Remembers a successful check: the time and the
     *        running version.
     *
     * Recorded only when the GitHub API answered with a usable
     * releases list. On a failure nothing is recorded, so the
     * check is tried again on the next startup.
     */
    void RecordSuccessfulCheck();

    /**
     * @brief Reports a failed check.
     *
     * An automatic check fails quietly, with no dialog at all.
     * A manual check gets a brief message.
     * @param Manual True when this was a manual check.
     * @param Message The brief message for a manual check.
     */
    void FailCheck(bool Manual, const QString& Message);

    /**
     * @brief Offers the user the newer release.
     *
     * "Download and Install" starts the download; "Later" drops
     * it.
     * @param NewVersion The newer version, without the leading "V".
     * @param ReleaseTitle The release title, possibly empty.
     * @param DownloadUrl The installer download address.
     * @param ExpectedSize The installer size from the release, or
     *        0 when the release gave none.
     */
    void OfferUpdate(const QString& NewVersion, const QString& ReleaseTitle,
                     const QString& DownloadUrl, qint64 ExpectedSize);

    /**
     * @brief Downloads the installer to the temporary folder.
     * @param DownloadUrl The installer download address.
     * @param Version The version being downloaded.
     * @param ExpectedSize The installer size from the release, or
     *        0 when the release gave none.
     */
    void StartDownload(const QString& DownloadUrl, const QString& Version,
                       qint64 ExpectedSize);

    /**
     * @brief The version without a leading "V" or "v".
     * @param Version The version, possibly tagged ("V1.1.0").
     * @return The version without the leading letter.
     */
    static QString StrippedVersion(const QString& Version);

    /**
     * @brief The number at the start of a version part.
     *
     * "0-beta" counts as 0; a part with no digits counts as 0.
     * @param Part One dot-separated part of a version.
     * @return The leading number, or 0.
     */
    static int NumericPrefix(const QString& Part);

    QWidget* _ParentWidget;
    QNetworkAccessManager _NetworkManager;
    QNetworkReply* _ReleasesReply;
    QNetworkReply* _DownloadReply;
    bool _Busy;
    bool _Manual;
    QString _DownloadPath;
    QFile _DownloadFile;
    qint64 _ExpectedDownloadSize;
    QProgressDialog* _ProgressDialog;
};

#endif // DOCEDITOR_UPDATECHECKER_H
