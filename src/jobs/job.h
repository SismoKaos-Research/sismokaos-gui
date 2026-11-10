// A Job is one unit of work the user can watch in the jobs panel: a log, a
// state, progress, elapsed time, and a way to cancel it. Two kinds exist:
// ProcessJob runs an external program (jobs/processjob.h); TaskJob runs C++
// code in a worker thread (jobs/taskjob.h), which is how the ported tools run.
#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include "jobs/progress.h"

namespace skg {

class Job : public QObject {
  Q_OBJECT
public:
  enum class State { Queued, Running, Done, Failed, Cancelled };
  Q_ENUM(State)

  Job(QString name, QString tool, QObject *parent = nullptr);

  const QString &name() const { return name_; }
  const QString &tool() const { return tool_; }
  State state() const { return state_; }
  // Exit code of the work: 0 success, >0 failure, -1 abnormal end.
  int exitCode() const { return exitCode_; }
  qint64 elapsedMs() const;
  // When the work began (local time); invalid before it has.
  const QDateTime &startedAt() const { return startedAt_; }
  const QStringList &log() const { return log_; }
  const ProgressState &progress() const { return progress_; }
  // One line saying what runs: a command line, or a task summary.
  virtual QString description() const = 0;

  virtual void start() = 0;
  virtual void cancel() = 0;

signals:
  void lineReceived(const QString &line);
  void progressChanged();
  void stateChanged();

protected:
  // For subclasses: mark the start, append output, report progress, end.
  void begin();
  void appendLine(const QString &line);
  void setProgress(const ProgressState &p);
  void setState(State s);
  // Writes the "= exit N after T s" trailer and moves to Done/Failed, unless
  // the job was cancelled, in which case the state stays Cancelled.
  void finish(int exitCode);

private:
  QString name_;
  QString tool_;
  QStringList log_;
  QElapsedTimer timer_;
  QDateTime startedAt_;
  qint64 finalElapsedMs_ = -1;
  State state_ = State::Queued;
  int exitCode_ = -1;
  ProgressState progress_;
};

QString stateName(Job::State s);

} // namespace skg
