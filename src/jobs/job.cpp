#include "jobs/job.h"
#include <utility>

namespace skg {

QString stateName(Job::State s) {
  switch (s) {
  case Job::State::Queued:
    return Job::tr("queued");
  case Job::State::Running:
    return Job::tr("running");
  case Job::State::Done:
    return Job::tr("done");
  case Job::State::Failed:
    return Job::tr("failed");
  case Job::State::Cancelled:
    return Job::tr("cancelled");
  }
  return {};
}

Job::Job(QString name, QString tool, QObject *parent)
    : QObject(parent), name_(std::move(name)), tool_(std::move(tool)) {}

qint64 Job::elapsedMs() const {
  if (finalElapsedMs_ >= 0)
    return finalElapsedMs_;
  return timer_.isValid() ? timer_.elapsed() : 0;
}

void Job::begin() {
  timer_.start();
  setState(State::Running);
  appendLine(QStringLiteral("$ ") + description());
}

void Job::appendLine(const QString &line) {
  log_ << line;
  emit lineReceived(line);
}

void Job::setProgress(const ProgressState &p) {
  progress_ = p;
  emit progressChanged();
}

void Job::setState(State s) {
  if (state_ == s)
    return;
  state_ = s;
  emit stateChanged();
}

void Job::finish(int exitCode) {
  if (finalElapsedMs_ >= 0)
    return; // already finished
  finalElapsedMs_ = timer_.isValid() ? timer_.elapsed() : 0;
  exitCode_ = exitCode;
  appendLine(QStringLiteral("= exit %1 after %2 s")
                 .arg(exitCode_)
                 .arg(finalElapsedMs_ / 1000.0, 0, 'f', 1));
  if (state_ == State::Cancelled)
    return;
  if (exitCode == 0) {
    progress_.fraction = 1.0;
    emit progressChanged();
    setState(State::Done);
  } else {
    setState(State::Failed);
  }
}

} // namespace skg
