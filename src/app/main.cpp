// Entry point: registers the modules and shows the main window.
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>

#include <QIcon>
#include <QSettings>
#include <memory>

#include "app/mainwindow.h"
#include "core/i18n.h"
#include "core/moduleregistry.h"
#include "core/theme.h"
#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "modules/analysis/analysismodule.h"
#include "modules/classification/classificationmodule.h"
#include "modules/earlywarning/earlywarningmodule.h"
#include "modules/features/featuresmodule.h"
#include "modules/indicators/indicatorsmodule.h"
#include "modules/settings/settingsmodule.h"
#include "study/scancache.h"
#include "modules/study/studymodule.h"
#include "modules/study/studyswitcher.h"
#include "modules/tdvms/tdvmsmodule.h"
#include "study/store.h"
#include "tools/toolregistry.h"

using namespace skg;

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  // A decimal point everywhere, whatever the system is set to (core/ui.h).
  ui::pinNumberFormat();
  // The station list and catalogs embedded in the tdvms library (a static
  // library's resources are only linked in when something names them).
  Q_INIT_RESOURCE(data);
  QCoreApplication::setOrganizationName(QStringLiteral("sismokaos"));
  QCoreApplication::setApplicationName(QStringLiteral("sismokaos-gui"));
  QCoreApplication::setApplicationVersion(QStringLiteral(SKG_VERSION));
  app.setWindowIcon(QIcon(QStringLiteral(":/app/sismokaos-gui.svg")));

  // Developer options: a settings file of one's own (so a trial run does not
  // touch the real settings) and a picture of every page, then exit.
  QCommandLineParser cli;
  cli.addHelpOption();
  cli.addVersionOption();
  cli.addOption({QStringLiteral("settings"),
                 QStringLiteral("Use this settings file (INI)."),
                 QStringLiteral("file")});
  cli.addOption({QStringLiteral("screenshots"),
                 QStringLiteral("Save a PNG of every page here and exit."),
                 QStringLiteral("dir")});
  cli.process(app);
  const std::unique_ptr<QSettings> settingsPtr =
      cli.isSet(QStringLiteral("settings"))
          ? std::make_unique<QSettings>(cli.value(QStringLiteral("settings")),
                                        QSettings::IniFormat)
          : std::make_unique<QSettings>();
  QSettings &settings = *settingsPtr;
  // Exact waveform scans are kept for all studies of this user. A trial run
  // with its own settings file keeps its own, beside that file.
  study::ScanCache::setSharedDir(
      cli.isSet(QStringLiteral("settings"))
          ? cli.value(QStringLiteral("settings")) +
                QStringLiteral(".scan-cache")
          : study::ScanCache::defaultDir());
  I18n i18n(&settings);
  i18n.applySaved();

  JobModel jobs;
  ToolRegistry tools(&settings);
  Theme theme(&settings);
  theme.apply();
  study::StudyStore studies(&settings);
  QString reopenError;
  studies.reopenLast(&reopenError);
  Navigator nav;
  WorkQueue queue;
  AppContext ctx{&jobs, &tools, &settings, &theme, &studies, &nav, &queue};

  // The pages, in order within their sidebar section. A new module is one
  // more line here.
  ModuleRegistry modules;
  // The study workflow reads top to bottom: choose the earthquakes, get
  // their waveforms, run, compare.
  modules.emplace<CasesModule>();
  modules.emplace<TdvmsModule>();
  modules.emplace<RunsModule>();
  modules.emplace<CompareModule>();
  modules.emplace<FeaturesModule>();
  modules.emplace<ClassificationModule>();
  modules.emplace<AnalysisModule>();
  modules.emplace<IndicatorsModule>();
  modules.emplace<EarlyWarningModule>();
  modules.emplace<SettingsModule>(&i18n, &theme);

  MainWindow w(ctx, modules, &i18n);
  w.addHeaderWidget(new StudySwitcher(&studies, &w));
  // A new study ends on the Cases page, where its first step is.
  StudySwitcher::showPage = [&w](const QString &id) { w.showModule(id); };
  QObject::connect(&w, &MainWindow::newStudyRequested, &w,
                   [&] { StudySwitcher::newStudy(&w, &studies); });
  QObject::connect(&w, &MainWindow::openStudyRequested, &w,
                   [&] { StudySwitcher::openStudy(&w, &studies); });
  // A recent study from the File menu, or a study folder dropped on the
  // window.
  QObject::connect(&w, &MainWindow::openStudyPathRequested, &w,
                   [&](const QString &dir) {
                     if (StudySwitcher::openPath(&w, &studies, dir))
                       w.showModule(QStringLiteral("cases"));
                   });
  QObject::connect(&w, &MainWindow::studyInputsRequested, &w,
                   [&] { StudySwitcher::studyInputs(&w, &studies); });
  w.show();
  if (!reopenError.isEmpty() && !cli.isSet(QStringLiteral("screenshots")))
    QTimer::singleShot(0, &w, [&] { w.showNotice(reopenError); });
  if (cli.isSet(QStringLiteral("screenshots"))) {
    const QString dir = cli.value(QStringLiteral("screenshots"));
    QTimer::singleShot(0, &w, [&] {
      QDir().mkpath(dir);
      for (const auto &m : modules.modules()) {
        w.showModule(m->id());
        for (int i = 0; i < 8; ++i)
          QCoreApplication::processEvents();
        w.grab().save(QDir(dir).filePath(m->id() + QStringLiteral(".png")));
      }
      QCoreApplication::exit(0);
    });
  }
  return app.exec();
}
