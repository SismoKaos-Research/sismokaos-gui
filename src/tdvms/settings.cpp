#include "tdvms/settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <stdexcept>

namespace skg::tdvms {

const QStringList kCredentialVars = {QStringLiteral("TDVMS_IMAP_HOST"),
                                     QStringLiteral("TDVMS_IMAP_USER"),
                                     QStringLiteral("TDVMS_IMAP_PASS")};
const QString kSlotsVar = QStringLiteral("TDVMS_SLOTS");
const QString kNotifyVar = QStringLiteral("TDVMS_NOTIFY");

namespace {
QString base(const char *xdg, const char *windows, const QString &fallback) {
  const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (!env.value(QLatin1String(xdg)).isEmpty())
    return env.value(QLatin1String(xdg));
#ifdef Q_OS_WIN
  if (!env.value(QLatin1String(windows)).isEmpty())
    return env.value(QLatin1String(windows));
#else
  Q_UNUSED(windows);
#endif
  return QDir::home().filePath(fallback);
}

QString legacy(const QString &var) {
  return QStringLiteral("AFAD_") + var.mid(QStringLiteral("TDVMS_").size());
}

// One shell word from `raw`: handles 'single' (with '"'"' escapes as shlex
// writes them), "double", and bare values; stops at unquoted whitespace.
QString shellWord(const QString &raw) {
  QString out;
  int i = 0;
  const int n = raw.size();
  while (i < n) {
    const QChar c = raw[i];
    if (c == '\'') {
      const int end = raw.indexOf('\'', i + 1);
      if (end < 0) {
        out += raw.mid(i + 1);
        return out;
      }
      out += raw.mid(i + 1, end - i - 1);
      i = end + 1;
    } else if (c == '"') {
      ++i;
      while (i < n && raw[i] != '"') {
        if (raw[i] == '\\' && i + 1 < n)
          ++i;
        out += raw[i++];
      }
      ++i;
    } else if (c.isSpace()) {
      break;
    } else if (c == '\\' && i + 1 < n) {
      out += raw[i + 1];
      i += 2;
    } else {
      out += c;
      ++i;
    }
  }
  return out;
}

QString shellQuote(const QString &value) {
  if (value.isEmpty())
    return QStringLiteral("''");
  QString v = value;
  v.replace('\'', QStringLiteral("'\"'\"'"));
  return '\'' + v + '\'';
}
} // namespace

QString configDir() {
  return base("XDG_CONFIG_HOME", "APPDATA", QStringLiteral(".config")) +
         QStringLiteral("/tdvms");
}

QString stateDir() {
  return base("XDG_STATE_HOME", "LOCALAPPDATA",
              QStringLiteral(".local/state")) +
         QStringLiteral("/tdvms");
}

QString credentialsPath() {
  return configDir() + QStringLiteral("/mailbox.env");
}
QString settingsPath() { return configDir() + QStringLiteral("/settings.env"); }

QMap<QString, QString> readEnvFile(const QString &path,
                                   const QStringList &keys) {
  QMap<QString, QString> out;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return out;
  for (QByteArray rawLine : f.readAll().split('\n')) {
    QString line = QString::fromUtf8(rawLine).trimmed();
    if (line.isEmpty() || line.startsWith('#') || !line.contains('='))
      continue;
    if (line.startsWith(QLatin1String("export ")))
      line = line.mid(7).trimmed();
    const int eq = line.indexOf('=');
    const QString key = line.left(eq);
    if (keys.contains(key))
      out.insert(key, shellWord(line.mid(eq + 1)));
  }
  return out;
}

bool writeEnvFile(const QString &path, const QMap<QString, QString> &values,
                  const QStringList &keys, const QString &what) {
  QDir().mkpath(QFileInfo(path).absolutePath());
#ifndef Q_OS_WIN
  QFile::setPermissions(QFileInfo(path).absolutePath(),
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                            QFileDevice::ExeOwner);
#endif
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  QString body = QStringLiteral("# tdvms %1 -- written by sismokaos-gui.\n"
                                "# Environment variables of the same name take "
                                "precedence.\n")
                     .arg(what);
  for (const QString &k : keys)
    body += k + '=' + shellQuote(values.value(k)) + '\n';
  f.write(body.toUtf8());
  if (!f.commit())
    return false;
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return true;
}

QMap<QString, Resolved> resolveCredentials(const QProcessEnvironment &env,
                                           const QString &path) {
  const QString p = path.isEmpty() ? credentialsPath() : path;
  const auto saved = readEnvFile(p, kCredentialVars);
  QMap<QString, Resolved> out;
  for (const QString &var : kCredentialVars) {
    if (!env.value(var).isEmpty())
      out.insert(var,
                 {env.value(var), QStringLiteral("environment (%1)").arg(var)});
    else if (!env.value(legacy(var)).isEmpty())
      out.insert(var, {env.value(legacy(var)),
                       QStringLiteral("environment (%1)").arg(legacy(var))});
    else if (!saved.value(var).isEmpty())
      out.insert(var, {saved.value(var), p});
    else
      out.insert(var, {});
  }
  return out;
}

Credentials credentials(const QProcessEnvironment &env, const QString &path) {
  const auto got = resolveCredentials(env, path);
  return {got.value(kCredentialVars[0]).value,
          got.value(kCredentialVars[1]).value,
          got.value(kCredentialVars[2]).value};
}

bool saveCredentials(const Credentials &c, const QString &path) {
  QMap<QString, QString> values{{kCredentialVars[0], c.host},
                                {kCredentialVars[1], c.user},
                                {kCredentialVars[2], c.password}};
  return writeEnvFile(path.isEmpty() ? credentialsPath() : path, values,
                      kCredentialVars, QStringLiteral("mailbox credentials"));
}

std::optional<Resolved> settingValue(const QString &key,
                                     const QProcessEnvironment &env,
                                     const QString &path) {
  if (!env.value(key).isEmpty())
    return Resolved{env.value(key),
                    QStringLiteral("environment (%1)").arg(key)};
  const QString p = path.isEmpty() ? settingsPath() : path;
  const QString saved = readEnvFile(p, {kSlotsVar, kNotifyVar}).value(key);
  if (!saved.isEmpty())
    return Resolved{saved, p};
  return std::nullopt;
}

bool saveSettings(const QMap<QString, QString> &values, const QString &path) {
  const QString p = path.isEmpty() ? settingsPath() : path;
  const QStringList keys{kSlotsVar, kNotifyVar};
  QMap<QString, QString> current = readEnvFile(p, keys);
  for (auto it = values.begin(); it != values.end(); ++it)
    if (keys.contains(it.key()))
      current.insert(it.key(), it.value());
  return writeEnvFile(p, current, keys, QStringLiteral("settings"));
}

int slotCount(int cli, const QProcessEnvironment &env, const QString &path) {
  if (cli > 0)
    return cli;
  const auto got = settingValue(kSlotsVar, env, path);
  if (!got)
    return kDefaultSlots;
  bool ok = false;
  const int n = got->value.toInt(&ok);
  if (!ok || n < 1)
    throw std::invalid_argument(
        QStringLiteral("%1='%2' (from %3) is not a whole number of slots, 1 "
                       "or more")
            .arg(kSlotsVar, got->value, got->source)
            .toStdString());
  return n;
}

QString baseAddress(const QString &cli, const QProcessEnvironment &env,
                    const QString &path) {
  if (!cli.isEmpty())
    return cli;
  return resolveCredentials(env, path).value(kCredentialVars[1]).value;
}

QStringList notifyTargets(const QStringList &cli,
                          const QProcessEnvironment &env, const QString &path) {
  if (!cli.isEmpty())
    return cli;
  const auto got = settingValue(kNotifyVar, env, path);
  if (!got)
    return {};
  QStringList out;
  for (const QString &t :
       got->value.split(QRegularExpression("[ ,]+"), Qt::SkipEmptyParts))
    out << t.trimmed();
  return out;
}

QString maskAddress(const QString &address) {
  const QString a = address.trimmed();
  if (a.isEmpty())
    return a;
  const int at = a.lastIndexOf(QLatin1Char('@'));
  const QString dots = QStringLiteral("\u2022\u2022\u2022");
  if (at <= 0)
    return a.left(1) + dots;
  const int plus = a.indexOf(QLatin1Char('+'));
  const QString tag = plus > 0 && plus < at ? a.mid(plus, at - plus) : QString();
  return a.left(1) + dots + tag + a.mid(at);
}

QString maskAddresses(const QString &text) {
  static const QRegularExpression address(
      QStringLiteral("[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+"));
  QString out;
  int from = 0;
  auto it = address.globalMatch(text);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    out += text.mid(from, m.capturedStart() - from) + maskAddress(m.captured());
    from = int(m.capturedEnd());
  }
  return out + text.mid(from);
}

} // namespace skg::tdvms
