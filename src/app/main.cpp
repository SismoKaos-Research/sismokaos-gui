// Entry point: registers the modules and shows the main window.
#include <QApplication>
#include <QIcon>
#include <QSettings>

#include "app/mainwindow.h"
#include "core/i18n.h"
#include "core/moduleregistry.h"
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

  QSettings settings;
  I18n i18n(&settings);
  i18n.applySaved();

  JobModel jobs;
  ToolRegistry tools(&settings);
  AppContext ctx{&jobs, &tools, &settings};

  // The tabs, in order. A new module is one more line here.
  ModuleRegistry modules;
  modules.emplace<TdvmsModule>();
  modules.emplace<FeaturesModule>();
  modules.emplace<ClassificationModule>();
  modules.emplace<AnalysisModule>();
  modules.emplace<IndicatorsModule>();
  modules.emplace<EarlyWarningModule>();
  modules.emplace<SettingsModule>(&i18n);

  MainWindow w(ctx, modules, &i18n);
  w.show();
  return app.exec();
}
