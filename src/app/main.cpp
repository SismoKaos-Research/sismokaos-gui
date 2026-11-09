// Entry point: registers the modules and shows the main window.
#include <QApplication>
#include <QIcon>
#include <QSettings>

#include "app/mainwindow.h"
#include "core/i18n.h"
#include "core/moduleregistry.h"
#include "jobs/jobmodel.h"
#include "modules/analysis/analysismodule.h"
#include "modules/features/featuresmodule.h"
#include "modules/placeholder/placeholdermodule.h"
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
  modules.emplace<PlaceholderModule>(
      QStringLiteral("classification"),
      [] { return QObject::tr("Classification"); },
      [] {
        return QObject::tr("sfc projects: settings, runs, experiments, "
                           "reports.");
      },
      QStringList{QStringLiteral("sfc")});
  modules.emplace<AnalysisModule>();
  modules.emplace<SettingsModule>(&i18n);

  MainWindow w(ctx, modules, &i18n);
  w.show();
  return app.exec();
}
