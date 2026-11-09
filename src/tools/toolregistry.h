// Where the command-line tools are and how to call them.
//
// A ToolDesc describes one tool (id, display name, how to ask its version,
// which progress parser reads its output). The registry resolves each tool
// to an executable: an explicit path from the settings, then the PATH, then
// the .venv of a configured project directory, then `uv run --project DIR`.
// Adding a tool is one more entry in kTools (toolregistry.cpp).
#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "jobs/processjob.h"

namespace skg {

struct ToolDesc {
  QString id;              // "sfc"; also the executable name
  QString displayName;     // "seismic-features-classification"
  QString homepage;        // repository URL, for the settings page
  QStringList versionArgs; // e.g. {"--version"}
  QString progressParser;  // name for makeProgressParser()
  QStringList extraEnv;    // "KEY=value" entries set for every run
};

struct ToolStatus {
  enum class Source { None, Explicit, Path, Venv, Uv };
  Source source = Source::None;
  QString program;        // what will be executed ("" when not found)
  QStringList prefixArgs; // arguments placed before the tool's own ("run",
                          // "--project", DIR for the uv fallback)
  QString version;        // last probed version text, "" when unknown
  bool found() const { return !program.isEmpty(); }
};

class ToolRegistry : public QObject {
  Q_OBJECT
public:
  explicit ToolRegistry(QSettings *settings, QObject *parent = nullptr);

  const QList<ToolDesc> &tools() const;
  const ToolDesc *desc(const QString &id) const;
  QStringList ids() const;

  // User configuration, persisted in QSettings under tools/<id>/.
  QString executable(const QString &id) const;
  void setExecutable(const QString &id, const QString &path);
  QString projectDir(const QString &id) const;
  void setProjectDir(const QString &id, const QString &dir);

  // Resolution (cheap, no process started) and version probe (runs the
  // tool once, synchronously, with a timeout).
  ToolStatus status(const QString &id) const;
  QString probeVersion(const QString &id, int timeoutMs = 8000);

  // A JobSpec for `<tool> args...` in workDir; name is for the jobs table.
  JobSpec makeSpec(const QString &id, const QStringList &args,
                   const QString &workDir, const QString &name) const;

signals:
  void changed(const QString &id);

private:
  QSettings *settings_;
};

QString sourceName(ToolStatus::Source s);

} // namespace skg
