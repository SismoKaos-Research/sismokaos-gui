// Asks GitHub for the latest release and compares it with this build. On a
// newer one, offers to run the installed updater (install.sh / install.ps1
// kept beside the application as "update" / "update.ps1"), which waits for
// this process to exit, replaces the installation and starts it again; or
// opens the release page when no updater is found.
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
#include <QString>

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
  static bool isNewer(const QString &latest, const QString &current);
  // Starts the updater detached (it relaunches the application) and returns
  // true; the caller then quits.
  static bool runUpdater();

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
