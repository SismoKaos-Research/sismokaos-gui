#include "app/mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>

#include "app/updatecheck.h"

#include "core/i18n.h"
#include "jobs/jobmodel.h"
#include "jobs/jobspanel.h"

namespace skg {

MainWindow::MainWindow(AppContext ctx, ModuleRegistry &modules, I18n *i18n,
                       QWidget *parent)
    : QMainWindow(parent), ctx_(ctx), modules_(modules), i18n_(i18n) {
  tabs_ = new QTabWidget(this);
  tabs_->setDocumentMode(true);
  for (const auto &m : modules_.modules())
    tabs_->addTab(m->createWidget(ctx_, tabs_), m->title());
  setCentralWidget(tabs_);

  jobsPanel_ = new JobsPanel(ctx_.jobs, this);
  jobsDock_ = new QDockWidget(this);
  jobsDock_->setObjectName(QStringLiteral("jobsDock"));
  jobsDock_->setWidget(jobsPanel_);
  jobsDock_->setFeatures(QDockWidget::DockWidgetMovable |
                         QDockWidget::DockWidgetFloatable);
  addDockWidget(Qt::BottomDockWidgetArea, jobsDock_);

  fileMenu_ = menuBar()->addMenu(QString());
  quitAction_ = fileMenu_->addAction(QString(), QKeySequence::Quit, this,
                                     &QWidget::close);
  viewMenu_ = menuBar()->addMenu(QString());
  viewMenu_->addAction(jobsDock_->toggleViewAction());
  languageMenu_ = viewMenu_->addMenu(QString());
  auto *group = new QActionGroup(this);
  for (const QString &code : I18n::available()) {
    QAction *a = languageMenu_->addAction(QLocale(code).nativeLanguageName());
    a->setCheckable(true);
    a->setChecked(code == i18n_->current());
    a->setData(code);
    group->addAction(a);
    connect(a, &QAction::triggered, this,
            [this, code] { i18n_->setLanguage(code); });
  }
  connect(i18n_, &I18n::languageChanged, this, [group](const QString &code) {
    for (QAction *a : group->actions())
      a->setChecked(a->data().toString() == code);
  });
  helpMenu_ = menuBar()->addMenu(QString());
  updates_ = new UpdateCheck(ctx_.settings, this, this);
  checkUpdatesAction_ =
      helpMenu_->addAction(QString(), this, [this] { updates_->check(false); });
  updatesOnStartAction_ = helpMenu_->addAction(QString());
  updatesOnStartAction_->setCheckable(true);
  updatesOnStartAction_->setChecked(
      ctx_.settings->value(UpdateCheck::kSettingOnStart, true).toBool());
  connect(updatesOnStartAction_, &QAction::toggled, this, [this](bool on) {
    ctx_.settings->setValue(UpdateCheck::kSettingOnStart, on);
  });
  helpMenu_->addSeparator();
  aboutAction_ = helpMenu_->addAction(QString(), this, [this] {
    QMessageBox::about(
        this, tr("About sismokaos-gui"),
        tr("<b>sismokaos-gui %1</b><br>A desktop front end for the sismokaos "
           "tools: feature extraction, classification and AFAD/TDVMS "
           "downloads.<br>Qt %2")
            .arg(QCoreApplication::applicationVersion(),
                 QLatin1String(qVersion())));
  });

  connect(ctx_.jobs, &JobModel::jobAdded, this, [this](Job *job) {
    jobsDock_->show();
    statusBar()->showMessage(tr("Started: %1").arg(job->name()), 5000);
  });
  connect(ctx_.jobs, &JobModel::jobFinished, this, [this](Job *job) {
    statusBar()->showMessage(
        tr("%1: %2").arg(job->name(), stateName(job->state())), 8000);
  });

  retranslate();
  restoreGeometry(ctx_.settings->value("ui/geometry").toByteArray());
  restoreState(ctx_.settings->value("ui/state").toByteArray());
  if (size().isEmpty() || width() < 400)
    resize(1100, 750);
  if (updatesOnStartAction_->isChecked())
    QTimer::singleShot(3000, this, [this] { updates_->check(true); });
}

void MainWindow::changeEvent(QEvent *e) {
  if (e->type() == QEvent::LanguageChange)
    retranslate();
  QMainWindow::changeEvent(e);
}

void MainWindow::closeEvent(QCloseEvent *e) {
  if (ctx_.jobs->runningCount() > 0) {
    const auto r = QMessageBox::question(
        this, tr("Jobs still running"),
        tr("%n job(s) are still running. Quit and stop them?", nullptr,
           ctx_.jobs->runningCount()));
    if (r != QMessageBox::Yes) {
      e->ignore();
      return;
    }
    for (int i = 0; i < ctx_.jobs->rowCount(); ++i)
      ctx_.jobs->jobAt(i)->cancel();
  }
  ctx_.settings->setValue("ui/geometry", saveGeometry());
  ctx_.settings->setValue("ui/state", saveState());
  QMainWindow::closeEvent(e);
}

void MainWindow::retranslate() {
  setWindowTitle(QStringLiteral("sismokaos-gui"));
  const auto &mods = modules_.modules();
  for (int i = 0; i < int(mods.size()); ++i) {
    tabs_->setTabText(i, mods[i]->title());
    tabs_->setTabToolTip(i, mods[i]->description());
  }
  jobsDock_->setWindowTitle(tr("Jobs"));
  fileMenu_->setTitle(tr("&File"));
  quitAction_->setText(tr("&Quit"));
  viewMenu_->setTitle(tr("&View"));
  languageMenu_->setTitle(tr("&Language"));
  helpMenu_->setTitle(tr("&Help"));
  aboutAction_->setText(tr("&About"));
  checkUpdatesAction_->setText(tr("Check for &updates…"));
  updatesOnStartAction_->setText(tr("Check for updates at start"));
}

} // namespace skg
