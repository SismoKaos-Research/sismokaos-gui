// Entry point: registers the modules and shows the main window.
#include <QApplication>
#include <QSettings>

#include "app/mainwindow.h"
#include "core/i18n.h"
#include "core/moduleregistry.h"
#include "jobs/jobmodel.h"
#include "modules/placeholder/placeholdermodule.h"
#include "modules/settings/settingsmodule.h"
#include "tools/toolregistry.h"

using namespace skg;

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("sismokaos"));
  QCoreApplication::setApplicationName(QStringLiteral("sismokaos-gui"));
  QCoreApplication::setApplicationVersion(QStringLiteral(SKG_VERSION));

  QSettings settings;
  I18n i18n(&settings);
  i18n.applySaved();

  JobModel jobs;
  ToolRegistry tools(&settings);
  AppContext ctx{&jobs, &tools, &settings};

  // The tabs, in order. A new module is one more line here.
  ModuleRegistry modules;
  modules.emplace<PlaceholderModule>(
      QStringLiteral("data"), [] { return QObject::tr("Data"); },
      [] {
        return QObject::tr("AFAD/TDVMS campaigns and FDSN downloads: "
                           "ledgers, fetch progress, coverage.");
      },
      QStringList{QStringLiteral("tdvms"), QStringLiteral("sismokaos")});
  modules.emplace<PlaceholderModule>(
      QStringLiteral("features"), [] { return QObject::tr("Features"); },
      [] {
        return QObject::tr("Preprocessing and feature extraction: "
                           "config.json, preprocess, extract, results.");
      },
      QStringList{QStringLiteral("sismokaos"), QStringLiteral("sfc")});
  modules.emplace<PlaceholderModule>(
      QStringLiteral("classification"),
      [] { return QObject::tr("Classification"); },
      [] {
        return QObject::tr("sfc projects: settings, runs, experiments, "
                           "reports.");
      },
      QStringList{QStringLiteral("sfc")});
  modules.emplace<PlaceholderModule>(
      QStringLiteral("results"), [] { return QObject::tr("Results"); },
      [] {
        return QObject::tr("Feature matrices, run metadata and HTML "
                           "reports, browsed and plotted.");
      });
  modules.emplace<SettingsModule>(&i18n);

  MainWindow w(ctx, modules, &i18n);
  w.show();
  return app.exec();
}
