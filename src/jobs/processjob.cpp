#include "jobs/processjob.h"
#include <utility>

namespace skg {

ProcessJob::ProcessJob(JobSpec spec, QObject *parent)
    : Job(spec.name, spec.tool, parent), spec_(std::move(spec)),
      parser_(makeProgressParser(spec_.progressParser)) {
  process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(&process_, &QProcess::readyReadStandardOutput, this,
          &ProcessJob::onReadyRead);
  connect(&process_, &QProcess::finished, this, &ProcessJob::onFinished);
  connect(&process_, &QProcess::errorOccurred, this,
          &ProcessJob::onErrorOccurred);
}

QString ProcessJob::description() const {
  QStringList parts{spec_.program};
  for (const QString &a : spec_.args)
    parts << (a.contains(' ') ? '"' + a + '"' : a);
  return parts.join(' ');
}

void ProcessJob::start() {
  if (state() != State::Queued)
    return;
  QProcessEnvironment env = spec_.env;
  env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
  env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
  process_.setProcessEnvironment(env);
  if (!spec_.workDir.isEmpty())
    process_.setWorkingDirectory(spec_.workDir);
  process_.setProgram(spec_.program);
  process_.setArguments(spec_.args);
  begin();
  process_.start(QIODevice::ReadOnly);
}

void ProcessJob::cancel() {
  if (state() != State::Running)
    return;
  setState(State::Cancelled);
  process_.terminate();
  // A console program on Windows ignores terminate() (it is WM_CLOSE), and
  // kill() finishes asynchronously; wait so the exit trailer is written
  // before cancel() returns.
  if (!process_.waitForFinished(3000)) {
    process_.kill();
    process_.waitForFinished(3000);
  }
}

void ProcessJob::feedLine(const QString &line) {
  bool hide = false;
  ProgressState p = progress();
  if (parser_->parse(line, p, hide))
    setProgress(p);
  if (hide)
    return;
  appendLine(line);
}

void ProcessJob::onReadyRead() {
  partial_ += process_.readAllStandardOutput();
  int nl;
  while ((nl = partial_.indexOf('\n')) >= 0) {
    QByteArray raw = partial_.left(nl);
    partial_.remove(0, nl + 1);
    if (raw.endsWith('\r'))
      raw.chop(1);
    feedLine(QString::fromUtf8(raw));
  }
}

void ProcessJob::onFinished(int code, QProcess::ExitStatus status) {
  if (!partial_.isEmpty()) { // last line without a newline
    partial_ += '\n';
    onReadyRead();
  }
  finish(status == QProcess::NormalExit ? code : -1);
}

void ProcessJob::onErrorOccurred(QProcess::ProcessError error) {
  if (error != QProcess::FailedToStart)
    return;
  appendLine(
      tr("could not start %1: %2").arg(spec_.program, process_.errorString()));
  finish(-1);
}

} // namespace skg
