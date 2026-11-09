// A Job that runs C++ code in its own thread. The code gets a TaskContext to
// log lines, report progress and notice a cancel request; it may run a nested
// QEventLoop (network code does), since the thread has an event loop.
#pragma once

#include <QThread>

#include <atomic>
#include <functional>

#include "jobs/job.h"

namespace skg {

class TaskJob;

class TaskContext : public QObject {
  Q_OBJECT
public:
  explicit TaskContext(TaskJob *job);

  // Thread-safe: these cross to the job's thread through queued signals.
  void log(const QString &line);
  void progress(double fraction, const QString &text);
  void progress(const QString &text); // fraction unknown/unchanged
  bool cancelled() const { return cancelled_.load(); }
  QString taskName() const;

signals:
  void lineLogged(const QString &line);
  void progressReported(double fraction, const QString &text,
                        bool withFraction);
  // Emitted (in the job's thread) when cancel() is called; long waits can
  // connect to it to wake up.
  void cancelRequested();

private:
  friend class TaskJob;

  TaskJob *job_;
  std::atomic<bool> cancelled_{false};
};

class TaskJob : public Job {
  Q_OBJECT
public:
  // `work` returns the exit code (0 = success). It runs in a worker thread.
  using Work = std::function<int(TaskContext &)>;

  TaskJob(QString name, QString tool, QString description, Work work,
          QObject *parent = nullptr);
  ~TaskJob() override;

  QString description() const override { return description_; }

  void start() override;
  // Sets the cancel flag and emits TaskContext::cancelRequested. The task
  // must return on its own; a thread cannot be killed safely.
  void cancel() override;
  // Blocks until the worker has finished; for tests and shutdown.
  bool wait(int ms = -1);

private:
  QString description_;
  Work work_;
  QThread thread_;
  TaskContext *context_ = nullptr;
  QObject *worker_ = nullptr;
};

} // namespace skg
