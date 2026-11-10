// Asks GitHub for the newest release and compares it with this build. On a
// newer one, offers to run the installed updater (install.sh / install.ps1
// kept beside the application as "update" / "update.ps1"), which waits for
// this process to exit, replaces the installation and starts it again; or
// opens the release page when no updater is found.
//
// The updater is started only once the application is really closing: with
// jobs running the user is asked first ("Stop them and update"), and a close
// that is refused starts nothing. The script itself changes nothing either
// while this process is still alive.
//
// A released build is offered released versions only. A pre-release build
// (0.4.0-beta.1) is also offered a later pre-release of the same version and
// its release; see isNewer().
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QString>

#include <functional>

class QWidget;

namespace skg {

class UpdateCheck : public QObject {
  Q_OBJECT
public:
  static const QString kRepo; // "SismoKaos-Research/sismokaos-gui"
  static const QString kSettingOnStart;

  UpdateCheck(QSettings *settings, QWidget *dialogParent,
              QObject *parent = nullptr);

  // `quiet`: say nothing unless a newer release exists (the start-up check).
  void check(bool quiet);
  // The updater script beside the installation, or empty.
  static QString updaterPath();
  // True when `latest` is a higher version than `current` ("1.2.0", "v1.2.0").
  // A pre-release is older than its release and than a later pre-release of
  // it: 0.4.0-beta.1 < 0.4.0-beta.2 < 0.4.0-rc.1 < 0.4.0.
  static bool isNewer(const QString &latest, const QString &current);
  static bool isPreRelease(const QString &version);
  // The newest of `tags` that this build should be offered, or empty: for a
  // released build pre-releases are left out.
  static QString bestOffer(const QStringList &tags, const QString &current);
  // Starts the updater detached (it relaunches the application) for
  // `version` (empty: the latest release) and returns true; the caller then
  // quits.
  static bool runUpdater(const QString &version = {});

  // The steps between "Update now" and the updater, set by the main window
  // (tests set their own). Unset ones take the defaults in the comments.
  std::function<int()> runningJobs;                // 0
  std::function<bool(int jobs)> confirmStopJobs;   // asks the user
  // Stops the jobs and closes the application's window without asking
  // again; false when the close was refused. Default: true.
  std::function<bool()> closeForUpdate;
  std::function<bool(const QString &version)> launch; // runUpdater
  std::function<void()> quit;                      // QCoreApplication::quit
  // What "Update now" does. False when nothing was started; `why` then says
  // so when it is worth telling the user (empty when they declined).
  bool startUpdate(const QString &version, QString *why = nullptr);

signals:
  void updateAvailable(const QString &version, const QString &url);

private:
  void offer(const QString &version, const QString &url, const QString &notes);

  QSettings *settings_;
  QWidget *parent_;
  QNetworkAccessManager net_;
  bool busy_ = false;
};

} // namespace skg
