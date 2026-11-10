// The Module interface: one page of the application.
//
// To add a module: create src/modules/<name>/ with its own CMakeLists.txt
// (see modules/placeholder for the smallest one), subclass Module, and
// register it in app/main.cpp. The module gets an AppContext with the jobs
// table (to start tools), the tool registry (to build commands) and the
// settings. All user-visible strings go through tr(); widgets re-read them on
// QEvent::LanguageChange.
#pragma once

#include <QVariantMap>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QWidget>

namespace skg {

class JobModel;
class ToolRegistry;
class Theme;
namespace study {
class StudyStore;
}

// Lets a page send the user to another page ("Next: get waveforms"). `args`
// carries what the target should show or prefill; a page that cares connects
// to `requested` and looks for its own id. May be null in tests.
class Navigator : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  void show(const QString &moduleId, const QVariantMap &args = {}) {
    emit requested(moduleId, args);
  }
signals:
  void requested(const QString &moduleId, const QVariantMap &args);
};

// Navigator::show() with this id opens the jobs panel instead of a page;
// args "job" may hold the job's row in the JobModel to select it.
inline const QString kNavJobsPanel = QStringLiteral("@jobs");

// Work that waits its turn outside the jobs table: the Runs page queues
// cells before they become jobs. A page that keeps such a queue reports how
// many wait (set(), under a name of its own); the shell adds them up for the
// status strip, the question at quitting, and for not announcing "all jobs
// have finished" between two queued runs. May be null in tests.
class WorkQueue : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;
  void set(const QString &owner, int waiting) {
    waiting = waiting < 0 ? 0 : waiting;
    if (counts_.value(owner).toInt() == waiting)
      return;
    counts_.insert(owner, waiting);
    emit changed();
  }
  int waiting() const {
    int n = 0;
    for (auto it = counts_.cbegin(); it != counts_.cend(); ++it)
      n += it.value().toInt();
    return n;
  }
signals:
  void changed();

private:
  QVariantMap counts_;
};

struct AppContext {
  JobModel *jobs = nullptr;
  ToolRegistry *tools = nullptr;
  QSettings *settings = nullptr;
  Theme *theme = nullptr; // colours and icons of the current look
  study::StudyStore *study = nullptr; // the open study, if any
  Navigator *nav = nullptr;           // page-to-page navigation
  WorkQueue *queue = nullptr;         // what waits outside the jobs table
};

class Module : public QObject {
  Q_OBJECT
public:
  using QObject::QObject;

  // Where the module appears in the sidebar: the study workflow at the top,
  // the tools below it, or pinned to the bottom (settings).
  enum class Section { Study, Tools, Footer };

  // Stable identifier used in settings keys; never translated.
  virtual QString id() const = 0;
  // Sidebar and page title, and the one line under the page title; both
  // translated at call time.
  virtual QString title() const = 0;
  virtual QString description() const { return {}; }
  virtual Section section() const { return Section::Tools; }
  // Name of the sidebar icon in core/resources/icons (without ".svg");
  // defaults to the id, and to a plain square when there is no such file.
  virtual QString iconId() const { return id(); }
  // Tool ids the module drives; the settings page reports them per module.
  virtual QStringList requiredTools() const { return {}; }
  // The page's widget; created once by the main window.
  virtual QWidget *createWidget(AppContext &ctx, QWidget *parent) = 0;
};

} // namespace skg
