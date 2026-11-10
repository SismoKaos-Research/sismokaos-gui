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
#include "jobs/jobmodel.h"
#include "modules/analysis/analysismodule.h"
#include "modules/classification/classificationmodule.h"
#include "modules/earlywarning/earlywarningmodule.h"
#include "modules/features/featuresmodule.h"
#include "modules/indicators/indicatorsmodule.h"
#include "modules/settings/settingsmodule.h"
#include "modules/tdvms/tdvmsmodule.h"
#include "tools/toolregistry.h"

using namespace skg;

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
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
  I18n i18n(&settings);
  i18n.applySaved();

  JobModel jobs;
  ToolRegistry tools(&settings);
  Theme theme(&settings);
  theme.apply();
  AppContext ctx{&jobs, &tools, &settings, &theme};

  // The pages, in order within their sidebar section. A new module is one
  // more line here.
  ModuleRegistry modules;
  modules.emplace<TdvmsModule>();
  modules.emplace<FeaturesModule>();
  modules.emplace<ClassificationModule>();
  modules.emplace<AnalysisModule>();
  modules.emplace<IndicatorsModule>();
  modules.emplace<EarlyWarningModule>();
  modules.emplace<SettingsModule>(&i18n, &theme);

  MainWindow w(ctx, modules, &i18n);
  w.show();
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
