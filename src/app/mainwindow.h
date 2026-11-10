// The main window: a sidebar with one entry per registered module (grouped
// into the study workflow, the tools and the settings), the page of the
// selected module under a header with its title and description, and a
// status strip at the bottom that opens the jobs panel.
#pragma once

#include <QList>
#include <QMainWindow>

#include "core/module.h"
#include "core/moduleregistry.h"

class QAbstractButton;
class QActionGroup;
class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QMenu;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace skg {

class I18n;
class Job;
class JobsPanel;
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

protected:
  void changeEvent(QEvent *e) override;
  void closeEvent(QCloseEvent *e) override;

private:
  void buildSidebar();
  void buildMenus();
  void retranslate();
  void restyle();
  void setCurrent(int index);
  void setJobsVisible(bool on);
  void updateStrip();

  AppContext ctx_;
  ModuleRegistry &modules_;
  I18n *i18n_;
  int current_ = -1;

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

  QMenu *fileMenu_;
  QMenu *viewMenu_;
  QMenu *languageMenu_;
  QMenu *themeMenu_;
  QMenu *helpMenu_;
  QAction *quitAction_;
  QAction *jobsAction_;
  QAction *aboutAction_;
  QAction *checkUpdatesAction_;
  QAction *updatesOnStartAction_;
  QList<QAction *> themeActions_;
  UpdateCheck *updates_;
};

} // namespace skg
