#include "app/notifier.h"

#include <QApplication>
#include <QGuiApplication>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWidget>

#include "core/appsettings.h"
#include "jobs/job.h"
#include "jobs/jobmodel.h"

namespace skg {

JobNotifier::JobNotifier(JobModel *jobs, QSettings *settings, QWidget *window,
                         QObject *parent)
    : QObject(parent), jobs_(jobs), settings_(settings), window_(window) {
  qRegisterMetaType<skg::Job *>("skg::Job*");
  userIsLooking = [] { return QApplication::activeWindow() != nullptr; };
  show = [this](const QString &title, const QString &text, bool failure) {
    showOnDesktop(title, text, failure);
  };
  connect(jobs_, &JobModel::jobAdded, this, &JobNotifier::jobAdded);
  connect(jobs_, &JobModel::jobFinished, this, &JobNotifier::jobFinished);
  // Back at the application: the tray icon has done its work.
  connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
          [this](Qt::ApplicationState state) {
            if (state == Qt::ApplicationActive && tray_)
              tray_->hide();
          });
}

bool JobNotifier::enabled() const {
  return !settings_ || settings_
                           ->value(QLatin1String(appsettings::kNotifyKey),
                                   appsettings::kNotifyDefault)
                           .toBool();
}

qint64 JobNotifier::thresholdMs() const {
  const int s = settings_
                    ? settings_
                          ->value(QLatin1String(appsettings::kNotifyAfterKey),
                                  appsettings::kNotifyAfterDefault)
                          .toInt()
                    : appsettings::kNotifyAfterDefault;
  return qint64(qMax(0, s)) * 1000;
}

void JobNotifier::jobAdded(Job *) {
  if (!busy_.isValid()) {
    busy_.start();
    done_ = failed_ = cancelled_ = 0;
    onlyName_.clear();
    firstFailed_ = nullptr;
  }
}

void JobNotifier::jobFinished(Job *job) {
  if (!busy_.isValid())
    return;
  switch (job->state()) {
  case Job::State::Done:
    ++done_;
    break;
  case Job::State::Failed:
    ++failed_;
    if (!firstFailed_)
      firstFailed_ = job;
    break;
  case Job::State::Cancelled:
    ++cancelled_;
    break;
  default:
    return; // not an end
  }
  onlyName_ = job->name();
  const bool last = jobs_->runningCount() == 0 && jobs_->queuedCount() == 0;
  if (!last) {
    // A failure among jobs that go on: said now, not hours later.
    if (job->state() == Job::State::Failed &&
        job->elapsedMs() >= thresholdMs())
      announce(tr("A job failed"),
               tr("%1 failed after %2. The other jobs go on.")
                   .arg(job->name(), JobModel::elapsedText(job->elapsedMs())),
               job, true);
    return;
  }
  const qint64 lasted = busy_.elapsed();
  busy_.invalidate();
  const int ended = done_ + failed_ + cancelled_;
  // Stopped by the user, all of it: they know.
  if (done_ + failed_ == 0)
    return;
  if (lasted < thresholdMs())
    return;
  const QString took = JobModel::elapsedText(lasted);
  QString title, text;
  if (ended == 1) {
    title = failed_ ? tr("A job failed") : tr("A job has finished");
    text = failed_ ? tr("%1 failed after %2.").arg(onlyName_, took)
                   : tr("%1 finished after %2.").arg(onlyName_, took);
  } else {
    title = failed_ ? tr("The jobs have ended, some failed")
                    : tr("All jobs have finished");
    text = tr("%1 finished").arg(done_);
    if (failed_)
      text += QStringLiteral(", ") + tr("%1 failed").arg(failed_);
    if (cancelled_)
      text += QStringLiteral(", ") + tr("%1 stopped").arg(cancelled_);
    text += QStringLiteral(" · ") + took;
  }
  announce(title, text, firstFailed_ ? firstFailed_.data() : job, failed_ > 0);
}

void JobNotifier::announce(const QString &title, const QString &text, Job *job,
                           bool failure) {
  if (!enabled() || (userIsLooking && userIsLooking()))
    return;
  lastJob_ = job;
  if (show)
    show(title, text, failure);
  emit announced(title, text, job, failure);
}

void JobNotifier::showOnDesktop(const QString &title, const QString &text,
                                bool failure) {
  if (window_)
    QApplication::alert(window_); // until the window is activated
  if (!QSystemTrayIcon::isSystemTrayAvailable() ||
      !QSystemTrayIcon::supportsMessages())
    return;
  if (!tray_) {
    tray_ = new QSystemTrayIcon(QApplication::windowIcon(), this);
    tray_->setToolTip(QStringLiteral("sismokaos-gui"));
    const auto react = [this] {
      tray_->hide();
      emit activated(lastJob_);
    };
    connect(tray_, &QSystemTrayIcon::messageClicked, this, react);
    connect(tray_, &QSystemTrayIcon::activated, this,
            [react](QSystemTrayIcon::ActivationReason) { react(); });
  }
  tray_->show();
  tray_->showMessage(title, text,
                     failure ? QSystemTrayIcon::Warning
                             : QSystemTrayIcon::Information,
                     15000);
}

} // namespace skg
