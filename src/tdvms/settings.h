// Where tdvms keeps its own files, and the mailbox credentials and defaults.
// Port of tdvms/compat.py (directories), credentials.py and settings.py.
//
// The files are the same ones the Python tool reads and writes
// (mailbox.env, settings.env in the config dir), in the same KEY=value shell
// syntax, so a mailbox set up in either tool works in both.
#pragma once

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <optional>

namespace skg::tdvms {

// $XDG_CONFIG_HOME/tdvms, %APPDATA%\tdvms, else ~/.config/tdvms
QString configDir();
// $XDG_STATE_HOME/tdvms, %LOCALAPPDATA%\tdvms, else ~/.local/state/tdvms
QString stateDir();

extern const QStringList kCredentialVars; // TDVMS_IMAP_HOST/USER/PASS
extern const QString kSlotsVar;           // TDVMS_SLOTS
extern const QString kNotifyVar;          // TDVMS_NOTIFY
constexpr int kDefaultSlots = 8;

QString credentialsPath(); // <config>/mailbox.env
QString settingsPath();    // <config>/settings.env

// KEY=value lines (shell quoting understood) into a map, for `keys` only.
QMap<QString, QString> readEnvFile(const QString &path,
                                   const QStringList &keys);
// Writes `keys` from `values`, owner-readable only. Returns false on error.
bool writeEnvFile(const QString &path, const QMap<QString, QString> &values,
                  const QStringList &keys, const QString &what);

struct Resolved {
  QString value;
  QString source; // "environment (VAR)", the file path, or ""
};

// Each credential: TDVMS_IMAP_* in the environment, then AFAD_IMAP_*, then
// the file.
QMap<QString, Resolved> resolveCredentials(
    const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment(),
    const QString &path = QString());

struct Credentials {
  QString host, user, password;
  bool complete() const {
    return !host.isEmpty() && !user.isEmpty() && !password.isEmpty();
  }
};
Credentials credentials(
    const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment(),
    const QString &path = QString());
bool saveCredentials(const Credentials &c, const QString &path = QString());

// A saved default: the environment, then settings.env; nullopt when unset.
std::optional<Resolved> settingValue(
    const QString &key,
    const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment(),
    const QString &path = QString());
bool saveSettings(const QMap<QString, QString> &values,
                  const QString &path = QString());

// The pool size: `cli` when > 0, else TDVMS_SLOTS, else saved, else 8.
// Throws std::invalid_argument on a saved value that is not a count.
int slotCount(
    int cli = 0,
    const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment(),
    const QString &path = QString());
// The base address: `cli`, else the mailbox user.
QString baseAddress(
    const QString &cli = QString(),
    const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment(),
    const QString &path = QString());
// An address fit for a screen that may be photographed: the first letter of
// the local part and the domain, "o•••@example.org". Text that is not an
// address comes back with only its first letter. A plus-tag, which names a
// request slot and nothing about the person, is kept: "o•••+a3@example.org".
QString maskAddress(const QString &address);
// The same for every address inside a longer text (a status report).
QString maskAddresses(const QString &text);

QStringList notifyTargets(
    const QStringList &cli = {},
    const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment(),
    const QString &path = QString());

} // namespace skg::tdvms
