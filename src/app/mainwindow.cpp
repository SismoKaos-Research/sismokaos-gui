#include "app/mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/updatecheck.h"

#include "core/i18n.h"
#include "core/theme.h"
#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "jobs/jobspanel.h"

namespace skg {

MainWindow::MainWindow(AppContext ctx, ModuleRegistry &modules, I18n *i18n,
                       QWidget *parent)
    : QMainWindow(parent), ctx_(ctx), modules_(modules), i18n_(i18n) {
  auto *shell = new QWidget(this);
  shell->setObjectName(QStringLiteral("shell"));
  auto *root = new QHBoxLayout(shell);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // The sidebar: brand, the modules by section, the version.
  auto *side = new QFrame(shell);
  side->setObjectName(QStringLiteral("sidebar"));
  side->setFixedWidth(212);
  sideLayout_ = new QVBoxLayout(side);
  sideLayout_->setContentsMargins(0, 0, 0, 0);
  sideLayout_->setSpacing(0);
  auto *brandRow = new QHBoxLayout;
  brandRow->setContentsMargins(16, 18, 12, 10);
  logo_ = new QLabel(side);
  auto *brandText = new QVBoxLayout;
  brandText->setSpacing(0);
  auto *brand = new QLabel(QStringLiteral("SismoKaos"), side);
  brand->setObjectName(QStringLiteral("brand"));
  brandSub_ = new QLabel(side);
  brandSub_->setObjectName(QStringLiteral("brandSub"));
  brandText->addWidget(brand);
  brandText->addWidget(brandSub_);
  brandRow->addWidget(logo_);
  brandRow->addSpacing(6);
  brandRow->addLayout(brandText, 1);
  sideLayout_->addLayout(brandRow);

  pages_ = new QStackedWidget(shell);
  pages_->setObjectName(QStringLiteral("pages"));
  navGroup_ = new QButtonGroup(this);
  buildSidebar();
  sideFoot_ = new QLabel(side);
  sideFoot_->setObjectName(QStringLiteral("sideFoot"));
  sideLayout_->addWidget(sideFoot_);

  // The content: header, page, jobs panel (hidden until asked), strip.
  auto *content = new QWidget(shell);
  content->setObjectName(QStringLiteral("content"));
  auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(0);

  auto *header = new QFrame(content);
  header->setObjectName(QStringLiteral("header"));
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(20, 12, 20, 12);
  headerLayout->setSpacing(0);
  auto *titles = new QVBoxLayout;
  titles->setSpacing(2);
  pageTitle_ = new QLabel(header);
  pageTitle_->setObjectName(QStringLiteral("pageTitle"));
  pageDesc_ = new QLabel(header);
  pageDesc_->setObjectName(QStringLiteral("pageDesc"));
  titles->addWidget(pageTitle_);
  titles->addWidget(pageDesc_);
  headerLayout->addLayout(titles, 1);
  headerExtras_ = new QHBoxLayout;
  headerExtras_->setSpacing(8);
  headerLayout->addLayout(headerExtras_);
  headerLayout->addSpacing(14);
  const QStringList codes = I18n::available();
  for (int i = 0; i < codes.size(); ++i) {
    const QString code = codes[i];
    auto *b = new QPushButton(code.toUpper(), header);
    b->setObjectName(QStringLiteral("lang"));
    b->setCheckable(true);
    b->setChecked(code == i18n_->current());
    b->setToolTip(QLocale(code).nativeLanguageName());
    b->setProperty("code", code);
    if (i == 0)
      b->setProperty("first", true);
    if (i == codes.size() - 1)
      b->setProperty("last", true);
    connect(b, &QPushButton::clicked, this,
            [this, code] { i18n_->setLanguage(code); });
    headerLayout->addWidget(b, 0, Qt::AlignVCenter);
    langButtons_ << b;
  }
  contentLayout->addWidget(header);

  jobsPanel_ = new JobsPanel(ctx_.jobs, content);
  auto *jobsArea = new QWidget(content);
  jobsArea->setObjectName(QStringLiteral("jobsArea"));
  auto *jobsLayout = new QVBoxLayout(jobsArea);
  jobsLayout->setContentsMargins(14, 4, 14, 8);
  jobsLayout->addWidget(jobsPanel_);
  split_ = new QSplitter(Qt::Vertical, content);
  split_->setObjectName(QStringLiteral("mainSplit"));
  split_->addWidget(pages_);
  split_->addWidget(jobsArea);
  split_->setStretchFactor(0, 4);
  split_->setStretchFactor(1, 1);
  split_->setCollapsible(0, false);
  contentLayout->addWidget(split_, 1);

  auto *strip = new QFrame(content);
  strip->setObjectName(QStringLiteral("statusStrip"));
  auto *stripLayout = new QHBoxLayout(strip);
  stripLayout->setContentsMargins(16, 5, 16, 5);
  stripDot_ = new QLabel(QStringLiteral("●"), strip);
  stripDot_->setObjectName(QStringLiteral("statusDot"));
  stripText_ = new QLabel(strip);
  stripText_->setObjectName(QStringLiteral("statusText"));
  jobsToggle_ = new QPushButton(strip);
  jobsToggle_->setObjectName(QStringLiteral("jobsToggle"));
  jobsToggle_->setCursor(Qt::PointingHandCursor);
  stripLayout->addWidget(stripDot_);
  stripLayout->addWidget(stripText_, 1);
  stripLayout->addWidget(jobsToggle_);
  contentLayout->addWidget(strip);

  root->addWidget(side);
  root->addWidget(content, 1);
  setCentralWidget(shell);

  buildMenus();

  connect(jobsToggle_, &QPushButton::clicked, this,
          [this] { setJobsVisible(!jobsPanel_->parentWidget()->isVisible()); });
  connect(ctx_.jobs, &JobModel::jobAdded, this, [this](Job *job) {
    connect(job, &Job::progressChanged, this, &MainWindow::updateStrip);
    connect(job, &Job::stateChanged, this, &MainWindow::updateStrip);
    stripText_->setProperty("last", tr("Started: %1").arg(job->name()));
    updateStrip();
  });
  connect(ctx_.jobs, &JobModel::jobFinished, this, [this](Job *job) {
    stripText_->setProperty(
        "last", tr("%1: %2").arg(job->name(), stateName(job->state())));
    updateStrip();
  });
  connect(i18n_, &I18n::languageChanged, this, [this](const QString &code) {
    for (QPushButton *b : langButtons_)
      b->setChecked(b->property("code").toString() == code);
  });
  if (ctx_.theme)
    connect(ctx_.theme, &Theme::changed, this, &MainWindow::restyle);

  restyle();
  retranslate();
  restoreGeometry(ctx_.settings->value("ui/geometry").toByteArray());
  if (size().isEmpty() || width() < 400)
    resize(1280, 820);
  setJobsVisible(ctx_.settings->value("ui/jobsVisible", false).toBool());
  const QByteArray splitState = ctx_.settings->value("ui/split").toByteArray();
  if (!splitState.isEmpty())
    split_->restoreState(splitState);
  const QString last = ctx_.settings->value("ui/page").toString();
  showModule(modules_.find(last)          ? last
             : modules_.modules().empty() ? QString()
                                          : modules_.modules().front()->id());
  if (updatesOnStartAction_->isChecked())
    QTimer::singleShot(3000, this, [this] { updates_->check(true); });
}

void MainWindow::buildSidebar() {
  const auto &mods = modules_.modules();
  navButtons_.resize(qsizetype(mods.size()));
  const auto addSection = [this](Module::Section section, QLabel **label) {
    const auto &all = modules_.modules();
    bool any = false;
    for (int i = 0; i < int(all.size()); ++i) {
      Module *m = all[static_cast<size_t>(i)].get();
      if (m->section() != section)
        continue;
      if (!any && label) {
        *label = new QLabel(this);
        (*label)->setObjectName(QStringLiteral("navSection"));
        sideLayout_->addWidget(*label);
      }
      any = true;
      auto *b = new QToolButton(this);
      b->setObjectName(QStringLiteral("nav"));
      b->setIconSize(QSize(18, 18));
      b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      b->setCheckable(true);
      b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
      b->setCursor(Qt::PointingHandCursor);
      if (i < 9)
        b->setShortcut(QKeySequence(Qt::ALT | (Qt::Key_1 + i)));
      navGroup_->addButton(b, i);
      sideLayout_->addWidget(b);
      navButtons_[i] = b;
      // The page, with a margin around the module's own widget.
      auto *holder = new QWidget(pages_);
      auto *hl = new QVBoxLayout(holder);
      hl->setContentsMargins(14, 10, 14, 10);
      QWidget *w = m->createWidget(ctx_, holder);
      hl->addWidget(w);
      ui::polishPage(w);
      holder->setProperty("moduleIndex", i);
      pages_->addWidget(holder);
      connect(b, &QToolButton::clicked, this, [this, i] { setCurrent(i); });
    }
  };
  addSection(Module::Section::Study, &studySection_);
  addSection(Module::Section::Tools, &toolsSection_);
  sideLayout_->addStretch(1);
  addSection(Module::Section::Footer, nullptr);
}

void MainWindow::buildMenus() {
  fileMenu_ = menuBar()->addMenu(QString());
  quitAction_ = fileMenu_->addAction(QString(), QKeySequence::Quit, this,
                                     &QWidget::close);
  viewMenu_ = menuBar()->addMenu(QString());
  jobsAction_ = viewMenu_->addAction(QString());
  jobsAction_->setCheckable(true);
  jobsAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
  connect(jobsAction_, &QAction::triggered, this,
          [this](bool on) { setJobsVisible(on); });
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
  themeMenu_ = viewMenu_->addMenu(QString());
  auto *themeGroup = new QActionGroup(this);
  for (Theme::Mode m :
       {Theme::Mode::System, Theme::Mode::Light, Theme::Mode::Dark}) {
    QAction *a = themeMenu_->addAction(QString());
    a->setCheckable(true);
    a->setData(int(m));
    a->setChecked(ctx_.theme && ctx_.theme->mode() == m);
    themeGroup->addAction(a);
    themeActions_ << a;
    connect(a, &QAction::triggered, this, [this, m] {
      if (ctx_.theme)
        ctx_.theme->setMode(m);
    });
  }
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
                     QLatin1String(qVersion())) +
            tr("<br><br>Copyright (C) 2026 H. Oğuz Bolat.<br>This program "
               "comes with ABSOLUTELY NO WARRANTY. It is free software, and "
               "you are welcome to redistribute it under the terms of the "
               "GNU General Public License, version 3 or (at your option) "
               "any later version: "
               "<a href=\"https://www.gnu.org/licenses/gpl-3.0.html\">"
               "gnu.org/licenses/gpl-3.0</a>. Source: "
               "<a href=\"https://github.com/SismoKaos-Research/"
               "sismokaos-gui\">github.com/SismoKaos-Research/"
               "sismokaos-gui</a>."));
  });
}

void MainWindow::showModule(const QString &id) {
  const auto &mods = modules_.modules();
  for (int i = 0; i < int(mods.size()); ++i)
    if (mods[static_cast<size_t>(i)]->id() == id) {
      setCurrent(i);
      return;
    }
}

void MainWindow::addHeaderWidget(QWidget *w) {
  headerExtras_->addWidget(w, 0, Qt::AlignVCenter);
}

void MainWindow::setCurrent(int index) {
  const auto &mods = modules_.modules();
  if (index < 0 || index >= int(mods.size()))
    return;
  current_ = index;
  if (navButtons_[index])
    navButtons_[index]->setChecked(true);
  for (int p = 0; p < pages_->count(); ++p)
    if (pages_->widget(p)->property("moduleIndex").toInt() == index)
      pages_->setCurrentIndex(p);
  const Module *m = mods[static_cast<size_t>(index)].get();
  pageTitle_->setText(m->title());
  pageDesc_->setText(m->description());
  pageDesc_->setVisible(!m->description().isEmpty());
}

void MainWindow::setJobsVisible(bool on) {
  jobsPanel_->parentWidget()->setVisible(on);
  jobsAction_->setChecked(on);
  jobsToggle_->setText(on ? tr("Jobs ▾") : tr("Jobs ▴"));
}

void MainWindow::updateStrip() {
  const int running = ctx_.jobs->runningCount();
  QString text;
  if (running > 0) {
    text = tr("%n job(s) running", nullptr, running);
    for (int i = ctx_.jobs->rowCount() - 1; i >= 0; --i) {
      const Job *job = ctx_.jobs->jobAt(i);
      if (job->state() != Job::State::Running)
        continue;
      text += QStringLiteral(" · ") + job->name();
      const ProgressState &p = job->progress();
      if (!p.text.isEmpty())
        text += QStringLiteral(" · ") + p.text;
      if (p.fraction >= 0)
        text += QStringLiteral(" · %1%").arg(qRound(p.fraction * 100));
      break;
    }
  } else {
    const QString last = stripText_->property("last").toString();
    text = last.isEmpty() ? tr("No jobs running") : last;
  }
  stripText_->setText(text);
  const bool idle = running == 0;
  if (stripDot_->property("idle").toBool() != idle) {
    stripDot_->setProperty("idle", idle);
    stripDot_->style()->unpolish(stripDot_);
    stripDot_->style()->polish(stripDot_);
  }
}

void MainWindow::restyle() {
  if (!ctx_.theme)
    return;
  logo_->setPixmap(ctx_.theme
                       ->icon(QStringLiteral("brand"),
                              ctx_.theme->color(QStringLiteral("accentHover")))
                       .pixmap(26, 26));
  const auto &mods = modules_.modules();
  for (int i = 0; i < int(mods.size()); ++i)
    if (navButtons_[i])
      navButtons_[i]->setIcon(
          ctx_.theme->navIcon(mods[static_cast<size_t>(i)]->iconId()));
  for (QAction *a : themeActions_)
    a->setChecked(Theme::Mode(a->data().toInt()) == ctx_.theme->mode());
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
  ctx_.settings->setValue("ui/split", split_->saveState());
  ctx_.settings->setValue("ui/jobsVisible",
                          jobsPanel_->parentWidget()->isVisible());
  const auto &mods = modules_.modules();
  if (current_ >= 0 && current_ < int(mods.size()))
    ctx_.settings->setValue("ui/page",
                            mods[static_cast<size_t>(current_)]->id());
  QMainWindow::closeEvent(e);
}

void MainWindow::retranslate() {
  setWindowTitle(QStringLiteral("sismokaos-gui"));
  const auto &mods = modules_.modules();
  for (int i = 0; i < int(mods.size()); ++i) {
    if (!navButtons_[i])
      continue;
    const Module *m = mods[static_cast<size_t>(i)].get();
    navButtons_[i]->setText(QStringLiteral("  ") + m->title());
    navButtons_[i]->setToolTip(m->description());
  }
  if (studySection_)
    studySection_->setText(tr("STUDY"));
  if (toolsSection_)
    toolsSection_->setText(tr("TOOLS"));
  brandSub_->setText(tr("Seismology toolkit"));
  sideFoot_->setText(QStringLiteral("v%1 · GPL-3.0-or-later")
                         .arg(QCoreApplication::applicationVersion()));
  if (current_ >= 0)
    setCurrent(current_);
  jobsAction_->setText(tr("Jobs"));
  setJobsVisible(jobsPanel_->parentWidget()->isVisible());
  updateStrip();
  fileMenu_->setTitle(tr("&File"));
  quitAction_->setText(tr("&Quit"));
  viewMenu_->setTitle(tr("&View"));
  languageMenu_->setTitle(tr("&Language"));
  themeMenu_->setTitle(tr("&Theme"));
  const QStringList themeNames = {tr("Follow the system"), tr("Light"),
                                  tr("Dark")};
  for (int i = 0; i < themeActions_.size(); ++i)
    themeActions_[i]->setText(themeNames[i]);
  helpMenu_->setTitle(tr("&Help"));
  aboutAction_->setText(tr("&About"));
  checkUpdatesAction_->setText(tr("Check for &updates…"));
  updatesOnStartAction_->setText(tr("Check for updates at start"));
}

} // namespace skg
