// Keeps the computer from going to sleep on its own while jobs run. The
// screen may still switch off; closing a laptop's lid still suspends it.
//
// How, by system (mechanism() names the one in use, empty when none):
//   - Windows: SetThreadExecutionState(ES_SYSTEM_REQUIRED) for as long as it
//     is on.
//   - macOS: a `caffeinate -i` child process tied to this process.
//   - Linux with systemd-logind: a `systemd-inhibit --what=idle:sleep` child
//     process that holds the lock while its input stays open, so the lock
//     goes when this process ends however it ends. Without systemd-inhibit
//     on the PATH nothing is done.
#pragma once

#include <QObject>
#include <QString>

class QProcess;

namespace skg {

class KeepAwake : public QObject {
  Q_OBJECT
public:
  explicit KeepAwake(QObject *parent = nullptr);
  ~KeepAwake() override;

  void setActive(bool on);
  bool active() const { return on_; }
  // "SetThreadExecutionState", "caffeinate", "systemd-inhibit", or empty when
  // this system offers none of them.
  static QString mechanism();

private:
  bool on_ = false;
  QProcess *helper_ = nullptr;
};

} // namespace skg
