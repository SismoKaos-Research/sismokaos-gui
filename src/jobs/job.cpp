#include "jobs/job.h"

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

Job::Job(JobSpec spec, QObject *parent)
    : QObject(parent), spec_(std::move(spec)),
      parser_(makeProgressParser(spec_.progressParser)) {
  process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(&process_, &QProcess::readyReadStandardOutput, this,
          &Job::onReadyRead);
  connect(&process_, &QProcess::finished, this, &Job::onFinished);
  connect(&process_, &QProcess::errorOccurred, this, &Job::onErrorOccurred);
}

qint64 Job::elapsedMs() const {
  if (finalElapsedMs_ >= 0)
    return finalElapsedMs_;
  return timer_.isValid() ? timer_.elapsed() : 0;
}

QString Job::commandLine() const {
  QStringList parts{spec_.program};
  for (const QString &a : spec_.args)
    parts << (a.contains(' ') ? '"' + a + '"' : a);
  return parts.join(' ');
}

void Job::setState(State s) {
  if (state_ == s)
    return;
  state_ = s;
  emit stateChanged();
}

void Job::start() {
  if (state_ != State::Queued)
    return;
  QProcessEnvironment env = spec_.env;
  env.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));
  env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
  process_.setProcessEnvironment(env);
  if (!spec_.workDir.isEmpty())
    process_.setWorkingDirectory(spec_.workDir);
  process_.setProgram(spec_.program);
  process_.setArguments(spec_.args);
  timer_.start();
  setState(State::Running);
  const QString head = QStringLiteral("$ ") + commandLine();
  log_ << head;
  emit lineReceived(head);
  process_.start(QIODevice::ReadOnly);
}

void Job::cancel() {
  if (state_ != State::Running)
    return;
  setState(State::Cancelled);
  process_.terminate();
  if (!process_.waitForFinished(3000))
    process_.kill();
}

void Job::feedLine(const QString &line) {
  bool hide = false;
  if (parser_->parse(line, progress_, hide))
    emit progressChanged();
  if (hide)
    return;
  log_ << line;
  emit lineReceived(line);
}

void Job::onReadyRead() {
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

void Job::onFinished(int code, QProcess::ExitStatus status) {
  if (!partial_.isEmpty()) { // last line without a newline
    partial_ += '\n';
    onReadyRead();
  }
  finalElapsedMs_ = timer_.elapsed();
  exitCode_ = status == QProcess::NormalExit ? code : -1;
  const QString tail = QStringLiteral("= exit %1 after %2 s")
                           .arg(exitCode_)
                           .arg(finalElapsedMs_ / 1000.0, 0, 'f', 1);
  log_ << tail;
  emit lineReceived(tail);
  if (state_ == State::Cancelled)
    return;
  if (status == QProcess::NormalExit && code == 0) {
    progress_.fraction = 1.0;
    emit progressChanged();
    setState(State::Done);
  } else {
    setState(State::Failed);
  }
}

void Job::onErrorOccurred(QProcess::ProcessError error) {
  if (error != QProcess::FailedToStart)
    return;
  finalElapsedMs_ = timer_.elapsed();
  const QString msg =
      tr("could not start %1: %2").arg(spec_.program, process_.errorString());
  log_ << msg;
  emit lineReceived(msg);
  setState(State::Failed);
}

} // namespace skg
