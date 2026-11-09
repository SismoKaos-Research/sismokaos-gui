// A Job is one run of an external tool (sismokaos, sfc, tdvms, ...) under
// QProcess: merged stdout/stderr read line by line, progress parsed by the
// tool's ProgressParser, exit status and elapsed time kept.
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <memory>

#include "jobs/progress.h"

namespace skg {

struct JobSpec {
  QString name; // shown to the user, e.g. "extract FRT"
  QString tool; // tool id ("sismokaos", "sfc", "tdvms") or "" for anything else
  QString program; // executable
  QStringList args;
  QString workDir;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString progressParser; // name for makeProgressParser(); "" = default chain
};

class Job : public QObject {
  Q_OBJECT
public:
  enum class State { Queued, Running, Done, Failed, Cancelled };
  Q_ENUM(State)

  explicit Job(JobSpec spec, QObject *parent = nullptr);

  const JobSpec &spec() const { return spec_; }
  State state() const { return state_; }
  int exitCode() const { return exitCode_; }
  qint64 elapsedMs() const;
  const QStringList &log() const { return log_; }
  QString commandLine() const;
  const ProgressState &progress() const { return progress_; }

  // Feeds one output line as the process would; public for tests.
  void feedLine(const QString &line);

  void start();
  void cancel();

signals:
  void lineReceived(const QString &line);
  void progressChanged();
  void stateChanged();

private:
  void setState(State s);
  void onReadyRead();
  void onFinished(int code, QProcess::ExitStatus status);
  void onErrorOccurred(QProcess::ProcessError error);

  JobSpec spec_;
  std::unique_ptr<ProgressParser> parser_;
  QProcess process_;
  QByteArray partial_;
  QStringList log_;
  QElapsedTimer timer_;
  qint64 finalElapsedMs_ = -1;
  State state_ = State::Queued;
  int exitCode_ = -1;
  ProgressState progress_;
};

QString stateName(Job::State s);

} // namespace skg
