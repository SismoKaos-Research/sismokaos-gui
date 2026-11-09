#include "tools/toolregistry.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace skg {

namespace {

const QList<ToolDesc> kTools = {
    {QStringLiteral("sismokaos"),
     QStringLiteral("Sismokaos-featureExtract"),
     QStringLiteral("https://github.com/hogib/Sismokaos-featureExtract"),
     {QStringLiteral("--help")},
     QStringLiteral("counter"),
     {},
     {}},
    {QStringLiteral("sfc"),
     QStringLiteral("seismic-features-classification"),
     QStringLiteral("https://github.com/hogib/seismic-features-classification"),
     {QStringLiteral("--version")},
     QStringLiteral("sfc-json"),
     {QStringLiteral("SEISMIC_PROGRESS=json")},
     {}},
    {QStringLiteral("tdvms"),
     QStringLiteral("tdvms"),
     QStringLiteral("https://github.com/hogib/tdvms-pull"),
     {QStringLiteral("--help")},
     QStringLiteral("counter"),
     {},
     {}},
    {QStringLiteral("ayzek"),
     QStringLiteral("ayzek"),
     QStringLiteral("https://github.com/hogib/ayzek"),
     {QStringLiteral("--help")},
     QString(),
     {},
     QStringLiteral("ayzek")},
};

QString venvExecutable(const QString &projectDir, const QString &id) {
  if (projectDir.isEmpty())
    return {};
#ifdef Q_OS_WIN
  const QString candidate = projectDir + "/.venv/Scripts/" + id + ".exe";
#else
  const QString candidate = projectDir + "/.venv/bin/" + id;
#endif
  return QFileInfo(candidate).isExecutable() ? candidate : QString();
}

} // namespace

QString sourceName(ToolStatus::Source s) {
  switch (s) {
  case ToolStatus::Source::None:
    return ToolRegistry::tr("not found");
  case ToolStatus::Source::Explicit:
    return ToolRegistry::tr("configured path");
  case ToolStatus::Source::Bundled:
    return ToolRegistry::tr("shipped with the application");
  case ToolStatus::Source::Path:
    return ToolRegistry::tr("on PATH");
  case ToolStatus::Source::Venv:
    return ToolRegistry::tr("project .venv");
  case ToolStatus::Source::Uv:
    return ToolRegistry::tr("uv run --project");
  }
  return {};
}

QStringList bundledToolDirs(const QString &subdir) {
  QStringList dirs;
  const QString env = qEnvironmentVariable("SKG_BUNDLED_TOOLS_DIR");
  if (!env.isEmpty())
    dirs << env + '/' + subdir;
  const QString app = QCoreApplication::applicationDirPath();
  if (!app.isEmpty())
    dirs << app + '/' + subdir << app + "/../share/sismokaos-gui/" + subdir
         << app + "/../Resources/" + subdir;
#ifdef SKG_BUILD_DIR
  dirs << QStringLiteral(SKG_BUILD_DIR) + '/' + subdir;
#endif
  for (QString &d : dirs)
    d = QDir::cleanPath(d);
  return dirs;
}

ToolRegistry::ToolRegistry(QSettings *settings, QObject *parent)
    : QObject(parent), settings_(settings) {}

const QList<ToolDesc> &ToolRegistry::tools() const { return kTools; }

const ToolDesc *ToolRegistry::desc(const QString &id) const {
  for (const ToolDesc &t : kTools)
    if (t.id == id)
      return &t;
  return nullptr;
}

QStringList ToolRegistry::ids() const {
  QStringList out;
  for (const ToolDesc &t : kTools)
    out << t.id;
  return out;
}

QString ToolRegistry::executable(const QString &id) const {
  return settings_->value("tools/" + id + "/executable").toString();
}

void ToolRegistry::setExecutable(const QString &id, const QString &path) {
  settings_->setValue("tools/" + id + "/executable", path);
  emit changed(id);
}

QString ToolRegistry::projectDir(const QString &id) const {
  return settings_->value("tools/" + id + "/projectDir").toString();
}

void ToolRegistry::setProjectDir(const QString &id, const QString &dir) {
  settings_->setValue("tools/" + id + "/projectDir", dir);
  emit changed(id);
}

ToolStatus ToolRegistry::status(const QString &id) const {
  ToolStatus st;
  st.version = settings_->value("tools/" + id + "/version").toString();
  const QString explicitPath = executable(id);
  if (!explicitPath.isEmpty()) {
    if (QFileInfo(explicitPath).isExecutable()) {
      st.source = ToolStatus::Source::Explicit;
      st.program = explicitPath;
    }
    return st; // an explicit path that does not work is reported as such
  }
  if (const ToolDesc *d = desc(id); d && !d->bundledSubdir.isEmpty()) {
#ifdef Q_OS_WIN
    const QString exe = id + QStringLiteral(".exe");
#else
    const QString exe = id;
#endif
    for (const QString &dir : bundledToolDirs(d->bundledSubdir)) {
      const QString candidate = dir + '/' + exe;
      if (QFileInfo(candidate).isExecutable()) {
        st.source = ToolStatus::Source::Bundled;
        st.program = candidate;
        return st;
      }
    }
  }
  const QString onPath = QStandardPaths::findExecutable(id);
  if (!onPath.isEmpty()) {
    st.source = ToolStatus::Source::Path;
    st.program = onPath;
    return st;
  }
  const QString dir = projectDir(id);
  const QString inVenv = venvExecutable(dir, id);
  if (!inVenv.isEmpty()) {
    st.source = ToolStatus::Source::Venv;
    st.program = inVenv;
    return st;
  }
  if (!dir.isEmpty() && QFileInfo(dir + "/pyproject.toml").exists()) {
    const QString uv = QStandardPaths::findExecutable(QStringLiteral("uv"));
    if (!uv.isEmpty()) {
      st.source = ToolStatus::Source::Uv;
      st.program = uv;
      st.prefixArgs = {QStringLiteral("run"), QStringLiteral("--project"),
                       QDir(dir).absolutePath(), id};
    }
  }
  return st;
}

QString ToolRegistry::probeVersion(const QString &id, int timeoutMs) {
  const ToolDesc *d = desc(id);
  const ToolStatus st = status(id);
  if (!d || !st.found())
    return {};
  QProcess p;
  p.setProcessChannelMode(QProcess::MergedChannels);
  p.start(st.program, st.prefixArgs + d->versionArgs, QIODevice::ReadOnly);
  QString out;
  if (p.waitForFinished(timeoutMs)) {
    out = QString::fromUtf8(p.readAll()).trimmed();
    // Keep the first non-empty line: "sfc 0.4.1", or the usage line.
    for (const QString &line : out.split('\n'))
      if (!line.trimmed().isEmpty()) {
        out = line.trimmed();
        break;
      }
  } else {
    p.kill();
    out = tr("no answer within %1 s").arg(timeoutMs / 1000);
  }
  settings_->setValue("tools/" + id + "/version", out);
  emit changed(id);
  return out;
}

JobSpec ToolRegistry::makeSpec(const QString &id, const QStringList &args,
                               const QString &workDir,
                               const QString &name) const {
  const ToolDesc *d = desc(id);
  const ToolStatus st = status(id);
  JobSpec spec;
  spec.name = name.isEmpty() ? id + ' ' + args.join(' ') : name;
  spec.tool = id;
  spec.program = st.found() ? st.program : id; // unresolved: let QProcess fail
  spec.args = st.prefixArgs + args;
  spec.workDir = workDir;
  if (d) {
    spec.progressParser = d->progressParser;
    for (const QString &kv : d->extraEnv) {
      const int eq = kv.indexOf('=');
      if (eq > 0)
        spec.env.insert(kv.left(eq), kv.mid(eq + 1));
    }
  }
  return spec;
}

} // namespace skg
