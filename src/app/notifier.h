// Tells the user that long work has ended while they were looking elsewhere:
// a desktop notification and a taskbar or dock alert.
//
// What is announced:
//   - the end of a busy period (one job, or the last of several) that lasted
//     at least the threshold of the settings (appsettings::kNotifyAfterKey),
//     with how many finished and failed; a period in which every job was
//     stopped by the user is not announced;
//   - a job that failed after running at least that long while others are
//     still running, at once, so that a failure overnight is not hidden
//     behind the jobs after it.
// Nothing is announced while a window of the application is active.
//
// Where it shows: QApplication::alert() everywhere (the taskbar entry
// flashes on Windows, the dock icon bounces on macOS, the window asks for
// attention on Linux desktops that support it); a message from a tray icon
// where the system has a tray that shows messages (Windows, macOS, most
// Linux desktops; a desktop without one gets the alert only). The tray icon
// exists only between an announcement and the user's return.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QSettings;
class QSystemTrayIcon;
class QWidget;

namespace skg {

class Job;
class JobModel;

class JobNotifier : public QObject {
  Q_OBJECT
public:
  JobNotifier(JobModel *jobs, QSettings *settings, QWidget *window,
              QObject *parent = nullptr);

  // Whether the user is looking at the application. The default asks Qt for
  // an active window; tests replace it.
  std::function<bool()> userIsLooking;
  // Showing an announcement on the desktop (the default: alert and tray
  // message). Tests replace it with nothing.
  std::function<void(const QString &title, const QString &text, bool failure)>
      show;

signals:
  // Something was announced; `job` is the one to show when the user reacts
  // (the failed one when there is one).
  void announced(const QString &title, const QString &text, skg::Job *job,
                 bool failure);
  // The user clicked the notification.
  void activated(skg::Job *job);

private:
  void jobAdded(Job *job);
  void jobFinished(Job *job);
  void announce(const QString &title, const QString &text, Job *job,
                bool failure);
  void showOnDesktop(const QString &title, const QString &text, bool failure);
  qint64 thresholdMs() const;
  bool enabled() const;

  JobModel *jobs_;
  QSettings *settings_;
  QPointer<QWidget> window_;
  QSystemTrayIcon *tray_ = nullptr;
  QPointer<Job> lastJob_;

  // The busy period under way.
  QElapsedTimer busy_;
  int done_ = 0, failed_ = 0, cancelled_ = 0;
  QString onlyName_;
  QPointer<Job> firstFailed_;
};

} // namespace skg
