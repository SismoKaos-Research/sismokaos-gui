// The main window: a sidebar with one entry per registered module (grouped
// into the study workflow, the tools and the settings), the page of the
// selected module under a header with its title and description, and a
// status strip at the bottom that opens the jobs panel.
//
// It also carries what belongs to the application as a whole: the menus
// (File: the study; View: the pages, the jobs, language and theme; Help: the
// manual, a problem report, the diagnostics, updates), the window title with
// the open study's name, a study folder dropped on the window, the first-run
// card, the question at quitting with jobs running, and the notifications
// when long jobs end (app/notifier.h).
#pragma once

#include <QList>
#include <QMainWindow>
#include <QPointer>

#include <functional>

#include "core/module.h"
#include "core/moduleregistry.h"

class QAbstractButton;
class QActionGroup;
class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QMenu;
class QMimeData;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace skg {

class I18n;
class Job;
class JobNotifier;
class JobsPanel;
class KeepAwake;
class UpdateCheck;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(AppContext ctx, ModuleRegistry &modules, I18n *i18n,
             QWidget *parent = nullptr);

  // Shows the page of the module with this id.
  void showModule(const QString &id);
  // A widget placed in the page header, left of the language switch (the
  // study selector).
  void addHeaderWidget(QWidget *w);
  // Opens the jobs panel, with `job` selected when given.
  void showJobs(Job *job = nullptr);
  // A line under the header the user closes (a study that would not reopen).
  void showNotice(const QString &text);

  // The study folder a drop means: a folder holding study.toml, or that file
  // itself; empty for anything else.
  static QString studyFolderOf(const QMimeData *mime);
  // What a drop on the window does: true when it was a study, whose opening
  // is then asked for (openStudyPathRequested).
  bool openDropped(const QMimeData *mime);
  // What the title bar says for a study of this name (empty: none open).
  static QString titleFor(const QString &studyName);
  // The question asked at quitting while jobs run: which jobs, and for how
  // long each has been running.
  QString quitQuestion() const;
  // Work that waits its turn: queued jobs, and the Runs page's queue.
  int queuedWork() const;
  // Asks it; true means "stop them and quit". Tests replace it.
  std::function<bool(const QString &question)> confirmQuit;
  // Closes the window for an update: the jobs are stopped without a second
  // question. False when the window did not close.
  bool closeForUpdate();
  // The text of Help > Copy diagnostics.
  QString diagnostics() const;
  JobNotifier *notifier() const { return notifier_; }
  UpdateCheck *updates() const { return updates_; }

signals:
  // The "no study open" bar on the study pages, and the File menu.
  void newStudyRequested();
  void openStudyRequested();
  // A recent study, or a folder dropped on the window.
  void openStudyPathRequested(const QString &dir);
  // File > Study inputs…
  void studyInputsRequested();

protected:
  void changeEvent(QEvent *e) override;
  void closeEvent(QCloseEvent *e) override;
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dropEvent(QDropEvent *e) override;

private:
  void buildSidebar();
  void buildMenus();
  void fillRecentMenu();
  void retranslate();
  void restyle();
  void setCurrent(int index);
  void setJobsVisible(bool on);
  void updateStrip();
  void updateStudyBar();
  void updateStudyState();
  void updateKeepAwake();
  void restoreWindowGeometry();
  void saveWindowGeometry();
  QString geometryKey() const;

  AppContext ctx_;
  ModuleRegistry &modules_;
  I18n *i18n_;
  int current_ = -1;
  bool closingForUpdate_ = false;

  QVBoxLayout *sideLayout_;
  QLabel *logo_;
  QLabel *brandSub_;
  QLabel *studySection_ = nullptr;
  QLabel *toolsSection_ = nullptr;
  QLabel *sideFoot_;
  QButtonGroup *navGroup_;
  QList<QToolButton *> navButtons_; // one per module, in registry order
  QStackedWidget *pages_;
  QLabel *pageTitle_;
  QLabel *pageDesc_;
  QHBoxLayout *headerExtras_;
  QList<QPushButton *> langButtons_;
  QSplitter *split_;
  JobsPanel *jobsPanel_;
  QLabel *stripDot_;
  QLabel *stripText_;
  QPushButton *jobsToggle_;
  QPointer<Job> lastFailed_; // the strip links to it
  QWidget *studyBar_;        // "no study open" with New / Open
  QLabel *studyBarText_;
  QPushButton *studyBarNew_;
  QPushButton *studyBarOpen_;
  QWidget *firstRun_; // what to do first, until dismissed
  QLabel *firstRunText_;
  QPushButton *firstRunClose_;
  QWidget *notice_;
  QLabel *noticeText_;
  QPushButton *noticeClose_;

  QMenu *fileMenu_;
  QMenu *recentMenu_;
  QMenu *viewMenu_;
  QMenu *languageMenu_;
  QMenu *themeMenu_;
  QMenu *helpMenu_;
  QAction *newStudyAction_;
  QAction *openStudyAction_;
  QAction *closeStudyAction_;
  QAction *reloadStudyAction_;
  QAction *studyFolderAction_;
  QAction *studyInputsAction_;
  QAction *quitAction_;
  QAction *jobsAction_;
  QList<QAction *> pageActions_; // one per module, in registry order
  QAction *manualAction_;
  QAction *reportAction_;
  QAction *diagnosticsAction_;
  QAction *aboutAction_;
  QAction *checkUpdatesAction_;
  QAction *updatesOnStartAction_;
  QList<QAction *> themeActions_;
  UpdateCheck *updates_;
  JobNotifier *notifier_;
  KeepAwake *keepAwake_;
};

} // namespace skg
