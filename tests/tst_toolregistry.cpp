#include <QtTest>

#include "tools/toolregistry.h"

using namespace skg;

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
    QCOMPARE(reg_->ids(), (QStringList{"sismokaos", "sfc", "tdvms"}));
    QVERIFY(reg_->desc("sfc") != nullptr);
    QCOMPARE(reg_->desc("sfc")->progressParser, QString("sfc-json"));
    QVERIFY(reg_->desc("nope") == nullptr);
  }

  void explicitExecutable() {
    // A fake tool: a shell script that prints its version.
    const QString exe = tmp_->filePath("sfc");
    QFile f(exe);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("#!/bin/sh\necho sfc 9.9 fake\n");
    f.close();
    QVERIFY(f.setPermissions(f.permissions() | QFileDevice::ExeOwner));

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
    QVERIFY(QDir().mkpath(proj + "/.venv/bin"));
    QFile f(proj + "/.venv/bin/sismokaos");
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("#!/bin/sh\necho ok\n");
    f.close();
    QVERIFY(f.setPermissions(f.permissions() | QFileDevice::ExeOwner));
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
