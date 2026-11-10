#include "app/mainwindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "app/diagnostics.h"
#include "core/keepawake.h"
#include "app/notifier.h"
#include "app/updatecheck.h"

#include "core/appsettings.h"
#include "core/i18n.h"
#include "core/theme.h"
#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "jobs/jobspanel.h"
#include "study/store.h"

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

  // Two bars under the header, shown when they have something to say: the
  // study pages with no study open, and a notice the user closes.
  const auto makeBar = [&](const char *name) {
    auto *bar = new QFrame(content);
    bar->setObjectName(QLatin1String(name));
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(20, 8, 20, 8);
    l->setSpacing(8);
    contentLayout->addWidget(bar);
    bar->hide();
    return bar;
  };
  studyBar_ = makeBar("studyBar");
  studyBarText_ = new QLabel(studyBar_);
  studyBarNew_ = new QPushButton(studyBar_);
  studyBarNew_->setObjectName(QStringLiteral("studyBarNew"));
  ui::setPrimary(studyBarNew_);
  studyBarOpen_ = new QPushButton(studyBar_);
  studyBarOpen_->setObjectName(QStringLiteral("studyBarOpen"));
  studyBar_->layout()->addWidget(studyBarText_);
  static_cast<QHBoxLayout *>(studyBar_->layout())->addStretch(1);
  studyBar_->layout()->addWidget(studyBarNew_);
  studyBar_->layout()->addWidget(studyBarOpen_);
  connect(studyBarNew_, &QPushButton::clicked, this,
          &MainWindow::newStudyRequested);
  connect(studyBarOpen_, &QPushButton::clicked, this,
          &MainWindow::openStudyRequested);
  // What to do first, under the "no study" bar until the user closes it.
  firstRun_ = makeBar("firstRunCard");
  firstRunText_ = new QLabel(firstRun_);
  firstRunText_->setObjectName(QStringLiteral("firstRunText"));
  firstRunText_->setWordWrap(true);
  firstRunText_->setTextFormat(Qt::RichText);
  firstRunText_->setOpenExternalLinks(true);
  firstRunClose_ = new QPushButton(firstRun_);
  firstRunClose_->setObjectName(QStringLiteral("firstRunClose"));
  static_cast<QHBoxLayout *>(firstRun_->layout())->addWidget(firstRunText_, 1);
  static_cast<QHBoxLayout *>(firstRun_->layout())
      ->addWidget(firstRunClose_, 0, Qt::AlignTop);
  connect(firstRunClose_, &QPushButton::clicked, this, [this] {
    ctx_.settings->setValue(QLatin1String(appsettings::kFirstRunDismissedKey),
                            true);
    updateStudyBar();
  });
  notice_ = makeBar("noticeBar");
  noticeText_ = new QLabel(notice_);
  noticeText_->setWordWrap(true);
  noticeText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  noticeClose_ = new QPushButton(notice_);
  static_cast<QHBoxLayout *>(notice_->layout())->addWidget(noticeText_, 1);
  notice_->layout()->addWidget(noticeClose_);
  connect(noticeClose_, &QPushButton::clicked, notice_, &QWidget::hide);

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
  stripText_->setTextFormat(Qt::RichText);
  connect(stripText_, &QLabel::linkActivated, this,
          [this] { showJobs(lastFailed_); });
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
    updateKeepAwake();
  });
  connect(ctx_.jobs, &JobModel::jobFinished, this, [this](Job *job) {
    stripText_->setProperty(
        "last", tr("%1: %2").arg(job->name(), stateName(job->state())));
    // A failure must not pass unseen: the strip becomes a link to its log.
    lastFailed_ = job->state() == Job::State::Failed ? job : nullptr;
    updateStrip();
    updateKeepAwake();
  });
  // Long work that ends while the user is elsewhere is announced; a click on
  // the announcement brings the window back, on the job.
  notifier_ = new JobNotifier(ctx_.jobs, ctx_.settings, this, this);
  connect(notifier_, &JobNotifier::activated, this, [this](Job *job) {
    showNormal();
    raise();
    activateWindow();
    showJobs(job);
  });
  keepAwake_ = new KeepAwake(this);
  setAcceptDrops(true);
  if (ctx_.nav)
    connect(ctx_.nav, &Navigator::requested, this,
            [this](const QString &id, const QVariantMap &args) {
              if (id == kNavJobsPanel) {
                const int row = args.value(QStringLiteral("job"), -1).toInt();
                showJobs(row >= 0 && row < ctx_.jobs->rowCount()
                             ? ctx_.jobs->jobAt(row)
                             : nullptr);
              } else {
                showModule(id);
              }
            });
  if (ctx_.study)
    connect(ctx_.study, &study::StudyStore::changed, this,
            &MainWindow::updateStudyState);
  connect(i18n_, &I18n::languageChanged, this, [this](const QString &code) {
    for (QPushButton *b : langButtons_)
      b->setChecked(b->property("code").toString() == code);
  });
  if (ctx_.theme)
    connect(ctx_.theme, &Theme::changed, this, &MainWindow::restyle);

  restyle();
  retranslate();
  restoreWindowGeometry();
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
  // File: the study.
  fileMenu_ = menuBar()->addMenu(QString());
  fileMenu_->setToolTipsVisible(true);
  newStudyAction_ = fileMenu_->addAction(QString(), QKeySequence::New, this,
                                         &MainWindow::newStudyRequested);
  openStudyAction_ = fileMenu_->addAction(QString(), QKeySequence::Open, this,
                                          &MainWindow::openStudyRequested);
  recentMenu_ = fileMenu_->addMenu(QString());
  recentMenu_->setToolTipsVisible(true);
  connect(recentMenu_, &QMenu::aboutToShow, this, &MainWindow::fillRecentMenu);
  fileMenu_->addSeparator();
  studyInputsAction_ = fileMenu_->addAction(
      QString(), this, &MainWindow::studyInputsRequested);
  studyFolderAction_ = fileMenu_->addAction(QString(), this, [this] {
    if (ctx_.study && ctx_.study->isOpen())
      QDesktopServices::openUrl(
          QUrl::fromLocalFile(ctx_.study->study().dir));
  });
  studyFolderAction_->setShortcut(
      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
  reloadStudyAction_ = fileMenu_->addAction(QString(), this, [this] {
    QString error;
    if (ctx_.study && !ctx_.study->reload(&error))
      showNotice(error);
  });
  closeStudyAction_ = fileMenu_->addAction(QString(), this, [this] {
    if (ctx_.study)
      ctx_.study->close();
  });
  closeStudyAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
  fileMenu_->addSeparator();
  quitAction_ = fileMenu_->addAction(QString(), QKeySequence::Quit, this,
                                     &QWidget::close);

  // View: the pages (Ctrl+1 …), the jobs panel, language and theme.
  viewMenu_ = menuBar()->addMenu(QString());
  {
    const auto &mods = modules_.modules();
    for (int i = 0; i < int(mods.size()); ++i) {
      QAction *a = viewMenu_->addAction(QString());
      if (i < 9)
        a->setShortcut(QKeySequence(Qt::CTRL | (Qt::Key_1 + i)));
      connect(a, &QAction::triggered, this, [this, i] { setCurrent(i); });
      pageActions_ << a;
    }
    viewMenu_->addSeparator();
  }
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
  helpMenu_->setToolTipsVisible(true);
  manualAction_ = helpMenu_->addAction(QString(), this, [] {
    QDesktopServices::openUrl(QUrl(manualUrl()));
  });
  manualAction_->setShortcut(QKeySequence::HelpContents);
  reportAction_ = helpMenu_->addAction(QString(), this, [this] {
    // The report asks for the diagnostics: have them on the clipboard.
    QGuiApplication::clipboard()->setText(diagnostics());
    QDesktopServices::openUrl(QUrl(reportUrl()));
  });
  diagnosticsAction_ = helpMenu_->addAction(QString(), this, [this] {
    QGuiApplication::clipboard()->setText(diagnostics());
    stripText_->setProperty("last", tr("Diagnostics copied to the clipboard"));
    updateStrip();
  });
  helpMenu_->addSeparator();
  updates_ = new UpdateCheck(ctx_.settings, this, this);
  updates_->runningJobs = [this] { return ctx_.jobs->runningCount(); };
  updates_->closeForUpdate = [this] { return closeForUpdate(); };
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

void MainWindow::showJobs(Job *job) {
  setJobsVisible(true);
  if (job && ctx_.jobs->rowOf(job) >= 0)
    jobsPanel_->showJob(job);
}

void MainWindow::showNotice(const QString &text) {
  noticeText_->setText(text);
  notice_->setVisible(!text.isEmpty());
}

void MainWindow::updateStudyBar() {
  const auto &mods = modules_.modules();
  const bool studyPage =
      current_ >= 0 && current_ < int(mods.size()) &&
      mods[static_cast<size_t>(current_)]->section() == Module::Section::Study;
  const bool noStudy = studyPage && ctx_.study && !ctx_.study->isOpen();
  studyBar_->setVisible(noStudy);
  firstRun_->setVisible(
      noStudy &&
      !ctx_.settings->value(QLatin1String(appsettings::kFirstRunDismissedKey))
           .toBool());
}

QString MainWindow::titleFor(const QString &studyName) {
  return studyName.isEmpty()
             ? QStringLiteral("sismokaos-gui")
             : studyName + QStringLiteral(" — sismokaos-gui");
}

void MainWindow::updateStudyState() {
  const bool open = ctx_.study && ctx_.study->isOpen();
  setWindowTitle(titleFor(open ? ctx_.study->study().name : QString()));
  for (QAction *a : {closeStudyAction_, reloadStudyAction_, studyFolderAction_,
                     studyInputsAction_})
    a->setEnabled(open);
  updateStudyBar();
}

void MainWindow::fillRecentMenu() {
  recentMenu_->clear();
  const QStringList dirs =
      ctx_.study ? ctx_.study->recent() : QStringList();
  const QFontMetrics fm(recentMenu_->font());
  for (const QString &dir : dirs) {
    // The study's own name, and where it is, shortened in the middle.
    const QString where = fm.elidedText(
        QDir::toNativeSeparators(QFileInfo(dir).absolutePath()),
        Qt::ElideMiddle, 320);
    QAction *a = recentMenu_->addAction(
        QStringLiteral("%1   —   %2")
            .arg(study::StudyStore::nameOf(dir).replace(
                     QLatin1Char('&'), QStringLiteral("&&")),
                 QString(where).replace(QLatin1Char('&'),
                                        QStringLiteral("&&"))));
    a->setToolTip(QDir::toNativeSeparators(dir));
    a->setCheckable(true);
    a->setChecked(ctx_.study->isOpen() && ctx_.study->study().dir == dir);
    connect(a, &QAction::triggered, this,
            [this, dir] { emit openStudyPathRequested(dir); });
  }
  if (dirs.isEmpty()) {
    QAction *none = recentMenu_->addAction(tr("No study has been opened yet"));
    none->setEnabled(false);
  }
}

QString MainWindow::studyFolderOf(const QMimeData *mime) {
  if (!mime || !mime->hasUrls() || mime->urls().size() != 1)
    return {};
  const QUrl url = mime->urls().first();
  if (!url.isLocalFile())
    return {};
  const QFileInfo fi(url.toLocalFile());
  const QString file = study::Study::kFileName;
  if (fi.isDir() && QFileInfo::exists(QDir(fi.filePath()).filePath(file)))
    return fi.absoluteFilePath();
  if (fi.isFile() && fi.fileName() == file)
    return fi.absolutePath();
  return {};
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
  if (!studyFolderOf(e->mimeData()).isEmpty())
    e->acceptProposedAction();
  else
    QMainWindow::dragEnterEvent(e);
}

bool MainWindow::openDropped(const QMimeData *mime) {
  const QString dir = studyFolderOf(mime);
  if (dir.isEmpty())
    return false;
  // After the drop has returned to the file manager it came from.
  QTimer::singleShot(0, this,
                     [this, dir] { emit openStudyPathRequested(dir); });
  return true;
}

void MainWindow::dropEvent(QDropEvent *e) {
  if (openDropped(e->mimeData()))
    e->acceptProposedAction();
  else
    QMainWindow::dropEvent(e);
}

void MainWindow::updateKeepAwake() {
  const bool wanted =
      ctx_.settings
          ->value(QLatin1String(appsettings::kKeepAwakeKey),
                  appsettings::kKeepAwakeDefault)
          .toBool() &&
      ctx_.jobs->runningCount() > 0;
  keepAwake_->setActive(wanted);
}

QString MainWindow::diagnostics() const {
  DiagnosticsInput in;
  in.jobs = ctx_.jobs;
  in.study = ctx_.study;
  in.language = i18n_->current();
  in.theme = !ctx_.theme                               ? QStringLiteral("none")
             : ctx_.theme->mode() == Theme::Mode::Dark ? QStringLiteral("dark")
             : ctx_.theme->mode() == Theme::Mode::Light
                 ? QStringLiteral("light")
                 : QStringLiteral("system");
  return diagnosticsText(in);
}

// The window is put back where it was on the same arrangement of screens;
// on another one (a laptop without its monitor) it starts at a default size
// on the primary screen rather than where a screen used to be.
QString MainWindow::geometryKey() const {
  QByteArray layout;
  for (const QScreen *s : QGuiApplication::screens()) {
    const QRect g = s->geometry();
    layout += QByteArray::number(g.x()) + ',' + QByteArray::number(g.y()) +
              ',' + QByteArray::number(g.width()) + ',' +
              QByteArray::number(g.height()) + ';';
  }
  return QStringLiteral("ui/geometry-") +
         QString::fromLatin1(
             QCryptographicHash::hash(layout, QCryptographicHash::Sha1)
                 .toHex()
                 .left(10));
}

void MainWindow::restoreWindowGeometry() {
  QByteArray saved = ctx_.settings->value(geometryKey()).toByteArray();
  bool restored = !saved.isEmpty() && restoreGeometry(saved);
  if (restored) {
    // Enough of the title bar on some screen to take hold of.
    bool reachable = false;
    const QRect top(frameGeometry().topLeft(), QSize(width(), 40));
    for (const QScreen *s : QGuiApplication::screens()) {
      const QRect hit = s->availableGeometry().intersected(top);
      reachable = reachable || (hit.width() >= 120 && hit.height() >= 20);
    }
    restored = reachable && width() >= 400 && height() >= 300;
  }
  if (!restored) {
    setWindowState(windowState() & ~(Qt::WindowMaximized | Qt::WindowFullScreen));
    QSize size(1280, 820);
    if (const QScreen *s = QGuiApplication::primaryScreen()) {
      const QRect avail = s->availableGeometry();
      size = size.boundedTo(avail.size() - QSize(40, 60));
      resize(size);
      move(avail.center() - QPoint(size.width() / 2, size.height() / 2));
    } else {
      resize(size);
    }
  }
}

void MainWindow::saveWindowGeometry() {
  ctx_.settings->setValue(geometryKey(), saveGeometry());
}

QString MainWindow::quitQuestion() const {
  QStringList lines;
  for (int i = 0; i < ctx_.jobs->rowCount(); ++i) {
    const Job *job = ctx_.jobs->jobAt(i);
    if (job->state() != Job::State::Running)
      continue;
    lines << tr("• %1, running for %2")
                 .arg(job->name(), JobModel::elapsedText(job->elapsedMs()));
  }
  // A long list says the same with its first lines.
  const int more = int(lines.size()) - 8;
  if (more > 0) {
    lines = lines.mid(0, 8);
    lines << tr("… and %n more", nullptr, more);
  }
  return tr("%n job(s) are still running:", nullptr,
            ctx_.jobs->runningCount()) +
         QLatin1Char('\n') + lines.join(QLatin1Char('\n')) +
         QStringLiteral("\n\n") +
         tr("Quitting stops them. What a run has not finished is lost and "
            "starts again next time; finished runs are kept.");
}

bool MainWindow::closeForUpdate() {
  closingForUpdate_ = true;
  const bool closed = close();
  closingForUpdate_ = false;
  return closed;
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
  updateStudyBar();
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
    // Jobs that wait for their turn (Job::State::Queued) are counted too.
    if (const int queued = ctx_.jobs->queuedCount())
      text += QStringLiteral(", ") + tr("%n queued", nullptr, queued);
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
  text = text.toHtmlEscaped();
  if (running == 0 && lastFailed_)
    text = QStringLiteral("<a href=\"job\">%1</a>")
               .arg(text + QStringLiteral(" · ") +
                    tr("show what went wrong").toHtmlEscaped());
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
    // An update has asked already; anything else names what would be lost.
    bool stop = closingForUpdate_;
    if (!stop) {
      const QString question = quitQuestion();
      if (confirmQuit) {
        stop = confirmQuit(question);
      } else {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Jobs still running"));
        box.setText(question);
        QPushButton *quit =
            box.addButton(tr("Stop them and quit"), QMessageBox::DestructiveRole);
        QPushButton *stay =
            box.addButton(tr("Keep running"), QMessageBox::RejectRole);
        box.setDefaultButton(stay);
        box.setEscapeButton(stay);
        box.exec();
        stop = box.clickedButton() == quit;
      }
    }
    if (!stop) {
      e->ignore();
      return;
    }
    for (int i = 0; i < ctx_.jobs->rowCount(); ++i)
      ctx_.jobs->jobAt(i)->cancel();
  }
  keepAwake_->setActive(false);
  saveWindowGeometry();
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
  updateStudyState();
  const auto &mods = modules_.modules();
  for (int i = 0; i < int(mods.size()); ++i) {
    if (!navButtons_[i])
      continue;
    const Module *m = mods[static_cast<size_t>(i)].get();
    // The study pages are steps, in order.
    QString step;
    if (m->section() == Module::Section::Study) {
      int n = 0;
      for (int k = 0; k <= i; ++k)
        n += mods[static_cast<size_t>(k)]->section() == Module::Section::Study;
      step = QStringLiteral("%1 · ").arg(n);
    }
    navButtons_[i]->setText(QStringLiteral("  ") + step + m->title());
    navButtons_[i]->setToolTip(m->description());
    if (i < pageActions_.size()) {
      pageActions_[i]->setText(
          (step + m->title()).replace(QLatin1Char('&'), QStringLiteral("&&")));
      pageActions_[i]->setStatusTip(m->description());
    }
  }
  if (studySection_)
    studySection_->setText(tr("STUDY"));
  if (toolsSection_)
    toolsSection_->setText(tr("TOOLS"));
  brandSub_->setText(tr("Seismology toolkit"));
  studyBarText_->setText(
      tr("No study is open. A study is a folder that keeps the earthquakes "
         "you compare, their settings and their results."));
  studyBarNew_->setText(tr("New study…"));
  studyBarNew_->setToolTip(ui::helpText(
      tr("Starts a study in a folder you choose; its results are written "
         "there.")));
  studyBarOpen_->setText(tr("Open study…"));
  studyBarOpen_->setToolTip(
      ui::helpText(tr("Opens an existing study folder (the one holding "
                      "study.toml).")));
  noticeClose_->setText(tr("Dismiss"));
  sideFoot_->setText(QStringLiteral("v%1 · GPL-3.0-or-later")
                         .arg(QCoreApplication::applicationVersion()));
  if (current_ >= 0)
    setCurrent(current_);
  jobsAction_->setText(tr("Jobs"));
  setJobsVisible(jobsPanel_->parentWidget()->isVisible());
  updateStrip();
  // What to do first; the walk-through in the manual has the detail.
  firstRunText_->setText(
      QStringLiteral("<b>%1</b><br>1. %2<br>2. %3<br>3. %4<br>"
                     "<a href=\"%5\">%6</a>")
          .arg(tr("First time here?").toHtmlEscaped(),
               tr("New study… and choose an empty folder: the study and all "
                  "its results live there.")
                   .toHtmlEscaped(),
               tr("On Cases, add an earthquake from the built-in list and "
                  "tick the stations near it; then add the folder with your "
                  "waveforms, or get them on the Data page.")
                   .toHtmlEscaped(),
               tr("On Runs, create a recipe and run it; Compare shows the "
                  "results side by side. Every option explains itself when "
                  "the pointer rests on it.")
                   .toHtmlEscaped(),
               manualUrl(),
               tr("The manual walks through a first study in ten minutes")
                   .toHtmlEscaped()));
  firstRunClose_->setText(tr("Got it"));
  firstRunClose_->setToolTip(ui::helpText(
      tr("Hides these first steps for good. The manual stays under Help.")));
  fileMenu_->setTitle(tr("&File"));
  newStudyAction_->setText(tr("&New study…"));
  newStudyAction_->setToolTip(
      tr("Starts a study in a folder you choose; its results are written "
         "there."));
  openStudyAction_->setText(tr("&Open study…"));
  openStudyAction_->setToolTip(
      tr("Opens an existing study folder (the one holding study.toml). A "
         "study folder can also be dropped on the window."));
  recentMenu_->setTitle(tr("Open &recent"));
  studyInputsAction_->setText(tr("Study &inputs…"));
  studyInputsAction_->setToolTip(
      tr("The earthquake catalog and the waveform folders of the open "
         "study."));
  studyFolderAction_->setText(tr("Open the study &folder"));
  studyFolderAction_->setToolTip(
      tr("Shows the open study's folder, with its results, in the file "
         "manager."));
  reloadStudyAction_->setText(tr("Re&load the study from disk"));
  reloadStudyAction_->setToolTip(
      tr("Reads study.toml again, after it was edited outside the "
         "application."));
  closeStudyAction_->setText(tr("&Close study"));
  quitAction_->setText(tr("&Quit"));
  manualAction_->setText(tr("Open the &manual"));
  manualAction_->setToolTip(
      tr("The manual of this version, on the web, at the first-study "
         "walk-through."));
  reportAction_->setText(tr("&Report a problem…"));
  reportAction_->setToolTip(
      tr("Opens the report form on the web and copies the diagnostics, so "
         "they can be pasted into it."));
  diagnosticsAction_->setText(tr("Copy &diagnostics"));
  diagnosticsAction_->setToolTip(
      tr("Copies the version, the system and the size of the open study to "
         "the clipboard, for a problem report. It holds no personal paths "
         "and no addresses."));
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
