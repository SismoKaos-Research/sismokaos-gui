// The Module interface: one tab of the application.
//
// To add a module: create src/modules/<name>/ with its own CMakeLists.txt
// (see modules/placeholder for the smallest one), subclass Module, and
// register it in app/main.cpp. The module gets an AppContext with the jobs
// table (to start tools), the tool registry (to build commands) and the
// settings. All user-visible strings go through tr(); widgets re-read them on
// QEvent::LanguageChange.
#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace skg {

class JobModel;
class ToolRegistry;

struct AppContext {
  JobModel *jobs = nullptr;
  ToolRegistry *tools = nullptr;
  QSettings *settings = nullptr;
};

class Module : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;

  // Stable identifier used in settings keys; never translated.
  virtual QString id() const = 0;
  // Tab title and a one-line description, translated at call time.
  virtual QString title() const = 0;
  virtual QString description() const { return {}; }
  // Tool ids the module drives; the settings page reports them per module.
  virtual QStringList requiredTools() const { return {}; }
  // The tab's widget; created once by the main window.
  virtual QWidget *createWidget(AppContext &ctx, QWidget *parent) = 0;
};

} // namespace skg
