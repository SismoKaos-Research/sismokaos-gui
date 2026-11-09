// A Job that runs an external program under QProcess: merged stdout/stderr
// read line by line, progress parsed by the tool's ProgressParser.
#pragma once

#include <QProcess>
#include <QProcessEnvironment>

#include <memory>

#include "jobs/job.h"

namespace skg {

struct JobSpec {
  QString name;    // shown to the user, e.g. "extract FRT"
  QString tool;    // tool id ("sismokaos", "sfc", ...) or "" for anything else
  QString program; // executable
  QStringList args;
  QString workDir;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  QString progressParser; // name for makeProgressParser(); "" = default chain
};

class ProcessJob : public Job {
  Q_OBJECT
public:
  explicit ProcessJob(JobSpec spec, QObject *parent = nullptr);

  const JobSpec &spec() const { return spec_; }
  QString description() const override;

  // Feeds one output line as the process would; public for tests.
  void feedLine(const QString &line);

  void start() override;
  void cancel() override;

private:
  void onReadyRead();
  void onFinished(int code, QProcess::ExitStatus status);
  void onErrorOccurred(QProcess::ProcessError error);

  JobSpec spec_;
  std::unique_ptr<ProgressParser> parser_;
  QProcess process_;
  QByteArray partial_;
};

} // namespace skg
