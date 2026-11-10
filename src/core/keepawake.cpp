#include "core/keepawake.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace skg {

namespace {

#if defined(Q_OS_MACOS)
QString helperProgram() {
  return QStandardPaths::findExecutable(QStringLiteral("caffeinate"));
}
#elif defined(Q_OS_UNIX)
QString helperProgram() {
  // The helper runs `cat`, which ends when its input closes.
  if (QStandardPaths::findExecutable(QStringLiteral("cat")).isEmpty())
    return {};
  return QStandardPaths::findExecutable(QStringLiteral("systemd-inhibit"));
}
#endif

} // namespace

KeepAwake::KeepAwake(QObject *parent) : QObject(parent) {}

KeepAwake::~KeepAwake() { setActive(false); }

QString KeepAwake::mechanism() {
#if defined(Q_OS_WIN)
  return QStringLiteral("SetThreadExecutionState");
#elif defined(Q_OS_MACOS)
  return helperProgram().isEmpty() ? QString() : QStringLiteral("caffeinate");
#elif defined(Q_OS_UNIX)
  return helperProgram().isEmpty() ? QString()
                                   : QStringLiteral("systemd-inhibit");
#else
  return {};
#endif
}

void KeepAwake::setActive(bool on) {
  if (on == on_)
    return;
  on_ = on;
#if defined(Q_OS_WIN)
  // Must be the same thread each time; this object lives on the GUI thread.
  SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED)
                             : ES_CONTINUOUS);
#elif defined(Q_OS_UNIX)
  if (!on) {
    if (helper_) {
      helper_->closeWriteChannel(); // systemd-inhibit's `cat` ends on this
      helper_->terminate();         // caffeinate ends on this
      if (!helper_->waitForFinished(1000))
        helper_->kill();
      helper_->deleteLater();
      helper_ = nullptr;
    }
    return;
  }
  const QString program = helperProgram();
  if (program.isEmpty())
    return;
  helper_ = new QProcess(this);
  helper_->setStandardOutputFile(QProcess::nullDevice());
  helper_->setStandardErrorFile(QProcess::nullDevice());
#if defined(Q_OS_MACOS)
  // -i: no idle sleep; -w: only for as long as this process exists.
  helper_->start(program,
                 {QStringLiteral("-i"), QStringLiteral("-w"),
                  QString::number(QCoreApplication::applicationPid())});
#else
  helper_->start(program, {QStringLiteral("--what=idle:sleep"),
                           QStringLiteral("--who=sismokaos-gui"),
                           QStringLiteral("--why=Jobs are running"),
                           QStringLiteral("--mode=block"),
                           QStringLiteral("cat")});
#endif
#endif
}

} // namespace skg
