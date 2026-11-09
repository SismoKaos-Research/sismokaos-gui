#include <QtTest>

#include "tools/toolregistry.h"

using namespace skg;

namespace {
#ifdef Q_OS_WIN
const QString kExe = QStringLiteral(".exe");
const QString kVenvBin = QStringLiteral("/.venv/Scripts/");
#else
const QString kExe;
const QString kVenvBin = QStringLiteral("/.venv/bin/");
#endif
// tests/helper.cpp copied under a tool's name answers "<name> 9.9 fake".
bool fakeTool(const QString &path) {
  return QFile::copy(QStringLiteral(SKG_TEST_HELPER), path);
}
} // namespace

class TstToolRegistry : public QObject {
  Q_OBJECT
private slots:
  void init() {
    tmp_ = std::make_unique<QTemporaryDir>();
    QVERIFY(tmp_->isValid());
    settings_ = std::make_unique<QSettings>(tmp_->filePath("s.ini"),
                                            QSettings::IniFormat);
    reg_ = std::make_unique<ToolRegistry>(settings_.get());
  }

  void descriptors() {
    QCOMPARE(reg_->ids(), (QStringList{"sismokaos", "sfc", "tdvms", "ayzek"}));
    QVERIFY(reg_->desc("sfc") != nullptr);
    QCOMPARE(reg_->desc("sfc")->progressParser, QString("sfc-json"));
    QVERIFY(reg_->desc("nope") == nullptr);
  }

  void bundledExecutable() {
    // A copy shipped beside the application wins over the PATH; the
    // environment variable names the directory in the tests.
    QVERIFY(QDir(tmp_->path()).mkdir("ayzek"));
    const QString exe = tmp_->filePath("ayzek/ayzek" + kExe);
    QVERIFY(fakeTool(exe));
    qputenv("SKG_BUNDLED_TOOLS_DIR", tmp_->path().toUtf8());
    const ToolStatus st = reg_->status("ayzek");
    qunsetenv("SKG_BUNDLED_TOOLS_DIR");
    QVERIFY(st.found());
    QCOMPARE(st.source, ToolStatus::Source::Bundled);
    QCOMPARE(QFileInfo(st.program).canonicalFilePath(),
             QFileInfo(exe).canonicalFilePath());
    QVERIFY(bundledToolDirs("ayzek").size() >= 4);
    // An explicit path still comes first.
    const QString other = tmp_->filePath("own-ayzek" + kExe);
    QVERIFY(fakeTool(other));
    reg_->setExecutable("ayzek", other);
    QCOMPARE(reg_->status("ayzek").source, ToolStatus::Source::Explicit);
  }

  void explicitExecutable() {
    // A fake tool that prints its version.
    const QString exe = tmp_->filePath("sfc" + kExe);
    QVERIFY(fakeTool(exe));

    reg_->setExecutable("sfc", exe);
    ToolStatus st = reg_->status("sfc");
    QVERIFY(st.found());
    QCOMPARE(st.source, ToolStatus::Source::Explicit);
    QCOMPARE(st.program, exe);
    QCOMPARE(reg_->probeVersion("sfc"), QString("sfc 9.9 fake"));
    QCOMPARE(reg_->status("sfc").version, QString("sfc 9.9 fake"));

    JobSpec spec = reg_->makeSpec("sfc", {"extract"}, tmp_->path(), "");
    QCOMPARE(spec.program, exe);
    QCOMPARE(spec.args, (QStringList{"extract"}));
    QCOMPARE(spec.tool, QString("sfc"));
    QCOMPARE(spec.progressParser, QString("sfc-json"));
    QCOMPARE(spec.env.value("SEISMIC_PROGRESS"), QString("json"));
    QCOMPARE(spec.name, QString("sfc extract"));
  }

  void brokenExplicitPathIsReported() {
    reg_->setExecutable("tdvms", tmp_->filePath("missing"));
    const ToolStatus st = reg_->status("tdvms");
    QVERIFY(!st.found());
    QCOMPARE(st.source, ToolStatus::Source::None);
  }

  void venvFallback() {
    const QString proj = tmp_->filePath("proj");
    QVERIFY(QDir().mkpath(proj + kVenvBin));
    QVERIFY(fakeTool(proj + kVenvBin + "sismokaos" + kExe));
    reg_->setProjectDir("sismokaos", proj);
    // Only meaningful when no `sismokaos` is on PATH, as in CI; otherwise PATH
    // wins by design.
    const ToolStatus st = reg_->status("sismokaos");
    QVERIFY(st.found());
    if (QStandardPaths::findExecutable("sismokaos").isEmpty())
      QCOMPARE(st.source, ToolStatus::Source::Venv);
  }

  void uvFallback() {
    const QString proj = tmp_->filePath("uvproj");
    QVERIFY(QDir().mkpath(proj));
    QFile f(proj + "/pyproject.toml");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("[project]\nname='x'\n");
    f.close();
    reg_->setProjectDir("tdvms", proj);
    const ToolStatus st = reg_->status("tdvms");
    if (QStandardPaths::findExecutable("tdvms").isEmpty() &&
        !QStandardPaths::findExecutable("uv").isEmpty()) {
      QVERIFY(st.found());
      QCOMPARE(st.source, ToolStatus::Source::Uv);
      QCOMPARE(st.prefixArgs.first(), QString("run"));
      QCOMPARE(st.prefixArgs.last(), QString("tdvms"));
    }
  }

private:
  std::unique_ptr<QTemporaryDir> tmp_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<ToolRegistry> reg_;
};

QTEST_GUILESS_MAIN(TstToolRegistry)
#include "tst_toolregistry.moc"
