// The main window: one tab per registered module, the jobs panel docked at
// the bottom, menus for language and quitting.
#pragma once

#include <QMainWindow>

#include "core/module.h"
#include "core/moduleregistry.h"

class QTabWidget;
class QDockWidget;
class QMenu;

namespace skg {

class I18n;
class JobsPanel;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  MainWindow(AppContext ctx, ModuleRegistry &modules, I18n *i18n,
             QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *e) override;
  void closeEvent(QCloseEvent *e) override;

private:
  void retranslate();

  AppContext ctx_;
  ModuleRegistry &modules_;
  I18n *i18n_;
  QTabWidget *tabs_;
  QDockWidget *jobsDock_;
  JobsPanel *jobsPanel_;
  QMenu *fileMenu_;
  QMenu *viewMenu_;
  QMenu *languageMenu_;
  QMenu *helpMenu_;
  QAction *quitAction_;
  QAction *aboutAction_;
};

} // namespace skg
