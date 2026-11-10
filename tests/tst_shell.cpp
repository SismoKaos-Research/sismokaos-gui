// The application shell: the main window's menus, title, drops and quit
// question; the jobs table; notifications for long jobs; the study lock and
// the "changed on disk" guard; the update steps and the installer's refusal
// to touch a running application; the diagnostics text; one number format
// under a Turkish locale.
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTranslator>
#include <QUrl>
#include <QtTest>

#include <clocale>
#include <cstdio>
#include <memory>

#include "analysis/events.h"
#include "app/diagnostics.h"
#include "app/mainwindow.h"
#include "app/notifier.h"
#include "app/updatecheck.h"
#include "core/appsettings.h"
#include "core/i18n.h"
#include "core/keepawake.h"
#include "core/moduleregistry.h"
#include "core/theme.h"
#include "core/ui.h"
#include "jobs/jobmodel.h"
#include "jobs/jobspanel.h"
#include "jobs/taskjob.h"
#include "modules/study/studyswitcher.h"
#include "study/store.h"
#include "study/study.h"
#include "tools/toolregistry.h"

using namespace skg;
using study::Study;
using study::StudyStore;

namespace {

// The smallest page: one in the study section, one among the tools.
class FakeModule : public Module {
public:
  FakeModule(QString id, Section section) : id_(std::move(id)), s_(section) {}
  QString id() const override { return id_; }
  QString title() const override { return id_.toUpper(); }
  QString description() const override { return id_ + " page"; }
  Section section() const override { return s_; }
  QWidget *createWidget(AppContext &, QWidget *parent) override {
    return new QLabel(id_, parent);
  }

private:
  QString id_;
  Section s_;
};

// A job that ends when told to, with the exit code asked for.
struct Latch {
  std::atomic<int> code{-100}; // -100: keep going
};

TaskJob *latchJob(const QString &name, const std::shared_ptr<Latch> &latch) {
  return new TaskJob(name, QStringLiteral("test"), name,
                     [latch](TaskContext &ctx) {
                       while (latch->code.load() == -100 && !ctx.cancelled())
                         QThread::msleep(5);
                       return ctx.cancelled() ? 1 : latch->code.load();
                     });
}

bool ended(const Job *j) {
  return j->state() == Job::State::Done || j->state() == Job::State::Failed ||
         j->state() == Job::State::Cancelled;
}

Study sampleStudy(const QString &dir) {
  Study s;
  s.dir = dir;
  s.name = QStringLiteral("Doğanyol ve İmranlı");
  study::Case c;
  c.id = QStringLiteral("case-a");
  c.name = QStringLiteral("Şırnak 2020");
  c.time = QDateTime(QDate(2020, 1, 24), QTime(17, 55, 14), QTimeZone::UTC);
  c.lat = 38.3593;
  c.lon = 39.0630;
  c.depthKm = 8.06;
  c.magnitude = 6.75;
  c.spanDays = 270;
  c.zone.radiusKm = 112.5;
  s.cases << c;
  return s;
}

} // namespace

class TstShell : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();

  // Numbers and files under a Turkish locale.
  void filesDoNotFollowTheLocale();
  void oneNumberFormatOnScreen();
  // File dialogs.
  void dialogsRememberTheirFolder();
  // Qt's own translation.
  void qtTranslationIsEmbedded();
  // Jobs.
  void jobsTableStartedAndClear();
  void longWorkIsAnnounced();
  void shortOrWatchedWorkIsNot();
  void aFailureAmongOthersIsAnnouncedAtOnce();
  // The study lock.
  void aStudyIsOpenInOneStore();
  void aLockLeftByACrashIsTakenOver();
  void aLockFromAnotherComputerNeedsAYes();
  void aStudyChangedOnDiskIsNotOverwritten();
  // The window.
  void titleMenusAndPages();
  void recentStudiesByName();
  void aDroppedStudyFolderOpens();
  void quittingNamesTheJobs();
  void firstRunCardUntilDismissed();
  void windowComesBackWhereItCanBeReached();
  void diagnosticsHoldNothingPersonal();
  // Updates.
  void versionsAndOffers();
  void updateStartsOnlyOnceClosing();
  void installerLeavesARunningAppAlone();
  // Settings keys.
  void keepAwakeSaysWhatItCanDo();

private:
  struct Shell {
    std::unique_ptr<QSettings> settings;
    std::unique_ptr<JobModel> jobs;
    std::unique_ptr<ToolRegistry> tools;
    std::unique_ptr<Theme> theme;
    std::unique_ptr<StudyStore> store;
    std::unique_ptr<Navigator> nav;
    std::unique_ptr<I18n> i18n;
    std::unique_ptr<ModuleRegistry> modules;
    std::unique_ptr<MainWindow> window;
  };
  Shell makeShell();
  void endJobs(JobModel &jobs);

  std::unique_ptr<QTemporaryDir> dir_;
};

void TstShell::init() {
  dir_ = std::make_unique<QTemporaryDir>();
  QVERIFY(dir_->isValid());
}

void TstShell::cleanup() {
  QLocale::setDefault(QLocale::c());
  std::setlocale(LC_ALL, "C");
  dir_.reset();
}

TstShell::Shell TstShell::makeShell() {
  Shell s;
  s.settings = std::make_unique<QSettings>(dir_->filePath("settings.ini"),
                                           QSettings::IniFormat);
  // No update check, no announcements on the desktop of whoever runs this.
  s.settings->setValue(UpdateCheck::kSettingOnStart, false);
  s.jobs = std::make_unique<JobModel>();
  s.tools = std::make_unique<ToolRegistry>(s.settings.get());
  s.theme = std::make_unique<Theme>(s.settings.get());
  s.store = std::make_unique<StudyStore>(s.settings.get());
  s.nav = std::make_unique<Navigator>();
  s.i18n = std::make_unique<I18n>(s.settings.get());
  s.i18n->setLanguage(QStringLiteral("en"));
  s.modules = std::make_unique<ModuleRegistry>();
  s.modules->emplace<FakeModule>(QStringLiteral("cases"),
                                 Module::Section::Study);
  s.modules->emplace<FakeModule>(QStringLiteral("runs"),
                                 Module::Section::Study);
  s.modules->emplace<FakeModule>(QStringLiteral("features"),
                                 Module::Section::Tools);
  AppContext ctx{s.jobs.get(),  s.tools.get(), s.settings.get(),
                 s.theme.get(), s.store.get(), s.nav.get()};
  s.window = std::make_unique<MainWindow>(ctx, *s.modules, s.i18n.get());
  s.window->notifier()->show = [](const QString &, const QString &, bool) {};
  return s;
}

void TstShell::endJobs(JobModel &jobs) {
  for (int i = 0; i < jobs.rowCount(); ++i) {
    jobs.jobAt(i)->cancel();
    if (auto *task = qobject_cast<TaskJob *>(jobs.jobAt(i)))
      task->wait(10000);
  }
  QCoreApplication::processEvents();
}

// ---- numbers ----

void TstShell::filesDoNotFollowTheLocale() {
  // What the files look like with the plain C locale.
  const QString plainDir = dir_->filePath("plain");
  sampleStudy(plainDir).save();
  QFile plain(QDir(plainDir).filePath(Study::kFileName));
  QVERIFY(plain.open(QIODevice::ReadOnly));
  const QByteArray plainBytes = plain.readAll();
  const std::vector<analysis::Event> plainEvents = analysis::loadEvents(
      QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"));
  QVERIFY(plainEvents.size() > 100);

  // A Turkish system: decimal comma for Qt and, where the system has the
  // locale installed, for the C library too.
  QLocale::setDefault(QLocale(QStringLiteral("tr_TR")));
  QCOMPARE(QLocale().decimalPoint(), QStringLiteral(","));
  const bool cLocale = std::setlocale(LC_ALL, "tr_TR.UTF-8") != nullptr;
  if (!cLocale)
    qInfo("tr_TR.UTF-8 is not installed here: only Qt's locale is Turkish");

  const QString trDir = dir_->filePath("tr");
  sampleStudy(trDir).save();
  QFile tr(QDir(trDir).filePath(Study::kFileName));
  QVERIFY(tr.open(QIODevice::ReadOnly));
  QCOMPARE(tr.readAll(), plainBytes);
  QVERIFY(plainBytes.contains("112.5"));
  QVERIFY(plainBytes.contains("38.3593"));

  const Study back = Study::load(trDir);
  QCOMPARE(back.name, QStringLiteral("Doğanyol ve İmranlı"));
  QCOMPARE(back.cases.size(), 1);
  QCOMPARE(back.cases[0].name, QStringLiteral("Şırnak 2020"));
  QCOMPARE(back.cases[0].lat, 38.3593);
  QCOMPARE(back.cases[0].magnitude, 6.75);
  QCOMPARE(back.cases[0].zone.radiusKm, 112.5);

  const std::vector<analysis::Event> trEvents = analysis::loadEvents(
      QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"));
  QCOMPARE(trEvents.size(), plainEvents.size());
  for (size_t i = 0; i < plainEvents.size(); i += 97) {
    QCOMPARE(trEvents[i].mag, plainEvents[i].mag);
    QCOMPARE(trEvents[i].lat, plainEvents[i].lat);
    QCOMPARE(trEvents[i].epoch, plainEvents[i].epoch);
  }
  // Station codes are compared without regard to case; the Turkish dotless
  // i must not get in the way.
  QCOMPARE(QStringLiteral("bingol").toUpper(), QStringLiteral("BINGOL"));
  QCOMPARE(QStringLiteral("ISK").toLower(), QStringLiteral("isk"));
  QCOMPARE(QStringLiteral("isk").compare(QStringLiteral("ISK"),
                                         Qt::CaseInsensitive),
           0);
}

void TstShell::oneNumberFormatOnScreen() {
  QLocale::setDefault(QLocale(QStringLiteral("tr_TR")));
  std::setlocale(LC_ALL, "tr_TR.UTF-8"); // when the system has it
  ui::pinNumberFormat();
  QCOMPARE(QLocale().decimalPoint(), QStringLiteral("."));
  QCOMPARE(QByteArray(std::localeconv()->decimal_point), QByteArray("."));
  // A field made from now on shows and reads a decimal point, like the
  // tables, which print with QString::number.
  QDoubleSpinBox box;
  box.setDecimals(2);
  box.setRange(0, 100000);
  box.setValue(1234.5);
  QCOMPARE(box.text(), QStringLiteral("1234.50"));
  QCOMPARE(QString::number(1234.5, 'f', 2), box.text());
  QCOMPARE(QStringLiteral("%L1").arg(1234567), QStringLiteral("1234567"));
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.2f", 1.5);
  QCOMPARE(QByteArray(buf), QByteArray("1.50"));
}

// ---- file dialogs ----

void TstShell::dialogsRememberTheirFolder() {
  QSettings settings(dir_->filePath("d.ini"), QSettings::IniFormat);
  const QString a = dir_->filePath("a");
  const QString b = dir_->filePath("b/inner");
  QVERIFY(QDir().mkpath(a) && QDir().mkpath(b));
  // A first use starts at the fallback, or at home without one.
  QCOMPARE(ui::dialogFolder(&settings, "study/open", a), a);
  QCOMPARE(ui::dialogFolder(&settings, "study/open"), QDir::homePath());
  // A chosen folder is remembered by its parent; a chosen file by its folder.
  ui::rememberDialogFolder(&settings, "study/open", b);
  QCOMPARE(ui::dialogFolder(&settings, "study/open", a), dir_->filePath("b"));
  QFile f(QDir(b).filePath("catalog.csv"));
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.close();
  ui::rememberDialogFolder(&settings, "cases/catalog", f.fileName());
  QCOMPARE(ui::dialogFolder(&settings, "cases/catalog"), b);
  // Each dialog has its own; a cancel changes nothing.
  QCOMPARE(ui::dialogFolder(&settings, "study/open"), dir_->filePath("b"));
  ui::rememberDialogFolder(&settings, "study/open", QString());
  QCOMPARE(ui::dialogFolder(&settings, "study/open"), dir_->filePath("b"));
  // A remembered folder that has gone falls back.
  QVERIFY(QDir(b).removeRecursively());
  QCOMPARE(ui::dialogFolder(&settings, "cases/catalog", a), a);
}

void TstShell::qtTranslationIsEmbedded() {
  const QString qm = I18n::qtTranslationResource(QStringLiteral("tr"));
  if (!QFileInfo::exists(qm))
    QSKIP("the Qt this was built with has no qtbase_tr.qm; the standard "
          "buttons stay English in this build");
  QTranslator t;
  QVERIFY(t.load(qm));
  const QString cancel = t.translate("QPlatformTheme", "Cancel");
  QVERIFY2(!cancel.isEmpty() && cancel != QLatin1String("Cancel"),
           qPrintable(cancel));
}

// ---- jobs ----

void TstShell::jobsTableStartedAndClear() {
  JobModel jobs;
  JobsPanel panel(&jobs);
  auto *clear = panel.findChild<QPushButton *>("clearFinishedJobs");
  QVERIFY(clear);
  QVERIFY(!clear->isEnabled());
  auto one = std::make_shared<Latch>(), two = std::make_shared<Latch>();
  Job *first = jobs.add(latchJob("first", one));
  Job *second = jobs.add(latchJob("second", two));
  QTRY_COMPARE(jobs.runningCount(), 2);
  QCOMPARE(jobs.finishedCount(), 0);
  // When it started, by the clock.
  QVERIFY(first->startedAt().isValid());
  const QString started =
      jobs.data(jobs.index(0, JobModel::Started), Qt::DisplayRole).toString();
  QVERIFY2(QTime::fromString(started, "HH:mm:ss").isValid(),
           qPrintable(started));
  QCOMPARE(jobs.headerData(JobModel::Started, Qt::Horizontal, Qt::DisplayRole)
               .toString(),
           QStringLiteral("Started"));
  one->code = 0;
  QTRY_VERIFY(ended(first));
  QTRY_VERIFY(clear->isEnabled());
  QCOMPARE(jobs.finishedCount(), 1);
  // Clearing takes the ended one out of the table and leaves the running
  // one; the object lives on for whoever still points at it.
  clear->click();
  QCOMPARE(jobs.rowCount(), 1);
  QCOMPARE(jobs.jobAt(0), second);
  QCOMPARE(jobs.rowOf(first), -1);
  QCOMPARE(first->name(), QStringLiteral("first"));
  QVERIFY(!clear->isEnabled());
  QCOMPARE(JobModel::elapsedText(12000), QStringLiteral("12 s"));
  QCOMPARE(JobModel::elapsedText(187000), QStringLiteral("3:07"));
  QCOMPARE(JobModel::elapsedText(8049000), QStringLiteral("2:14:09"));
  two->code = 0;
  QTRY_VERIFY(ended(second));
  endJobs(jobs);
}

void TstShell::longWorkIsAnnounced() {
  QSettings settings(dir_->filePath("n.ini"), QSettings::IniFormat);
  settings.setValue(appsettings::kNotifyAfterKey, 0);
  JobModel jobs;
  JobNotifier notifier(&jobs, &settings, nullptr);
  notifier.userIsLooking = [] { return false; };
  QStringList shown;
  notifier.show = [&shown](const QString &title, const QString &text, bool) {
    shown << title + " | " + text;
  };
  QSignalSpy announced(&notifier, &JobNotifier::announced);

  // One job: announced when it ends, by name.
  auto one = std::make_shared<Latch>();
  Job *only = jobs.add(latchJob("Features · SVRC", one));
  QTRY_COMPARE(jobs.runningCount(), 1);
  one->code = 0;
  QTRY_COMPARE(announced.size(), 1);
  QVERIFY(announced[0][1].toString().contains("Features · SVRC"));
  QCOMPARE(announced[0][3].toBool(), false);
  QCOMPARE(announced[0][2].value<Job *>(), only);
  QCOMPARE(shown.size(), 1);

  // Several: one announcement at the end, with the counts; the job to show
  // is the one that failed.
  auto a = std::make_shared<Latch>(), b = std::make_shared<Latch>(),
       c = std::make_shared<Latch>();
  Job *ja = jobs.add(latchJob("a", a));
  Job *jb = jobs.add(latchJob("b", b));
  Job *jc = jobs.add(latchJob("c", c));
  QTRY_COMPARE(jobs.runningCount(), 3);
  a->code = 0;
  QTRY_VERIFY(ended(ja));
  c->code = 0;
  QTRY_VERIFY(ended(jc));
  // A failure is announced on its own only after the threshold, which is 0
  // here, so it is; the summary still follows when the last one ends.
  QCOMPARE(announced.size(), 1);
  b->code = 3;
  QTRY_VERIFY(ended(jb));
  QTRY_COMPARE(announced.size(), 2);
  const QString text = announced[1][1].toString();
  QVERIFY2(text.contains("2 finished") && text.contains("1 failed"),
           qPrintable(text));
  QCOMPARE(announced[1][3].toBool(), true);
  QCOMPARE(announced[1][2].value<Job *>(), jb);
  endJobs(jobs);
}

void TstShell::shortOrWatchedWorkIsNot() {
  QSettings settings(dir_->filePath("n.ini"), QSettings::IniFormat);
  JobModel jobs;
  JobNotifier notifier(&jobs, &settings, nullptr);
  notifier.show = [](const QString &, const QString &, bool) {};
  bool looking = false;
  notifier.userIsLooking = [&looking] { return looking; };
  QSignalSpy announced(&notifier, &JobNotifier::announced);
  const auto runOne = [&jobs](int code, bool cancel = false) {
    auto latch = std::make_shared<Latch>();
    Job *job = jobs.add(latchJob("job", latch));
    QTRY_VERIFY_WITH_TIMEOUT(job->state() == Job::State::Running, 10000);
    if (cancel)
      job->cancel();
    else
      latch->code = code;
    QTRY_VERIFY_WITH_TIMEOUT(ended(job), 10000);
  };
  // Shorter than the default minute: not announced.
  runOne(0);
  QCoreApplication::processEvents();
  QCOMPARE(announced.size(), 0);
  // Long enough, but the user is looking at the window.
  settings.setValue(appsettings::kNotifyAfterKey, 0);
  looking = true;
  runOne(0);
  QCoreApplication::processEvents();
  QCOMPARE(announced.size(), 0);
  // Stopped by the user: they know.
  looking = false;
  runOne(0, true);
  QCoreApplication::processEvents();
  QCOMPARE(announced.size(), 0);
  // Switched off in the settings.
  settings.setValue(appsettings::kNotifyKey, false);
  runOne(0);
  QCoreApplication::processEvents();
  QCOMPARE(announced.size(), 0);
  // And on again.
  settings.setValue(appsettings::kNotifyKey, true);
  runOne(0);
  QTRY_COMPARE(announced.size(), 1);
  endJobs(jobs);
}

void TstShell::aFailureAmongOthersIsAnnouncedAtOnce() {
  QSettings settings(dir_->filePath("n.ini"), QSettings::IniFormat);
  settings.setValue(appsettings::kNotifyAfterKey, 0);
  JobModel jobs;
  JobNotifier notifier(&jobs, &settings, nullptr);
  notifier.userIsLooking = [] { return false; };
  notifier.show = [](const QString &, const QString &, bool) {};
  QSignalSpy announced(&notifier, &JobNotifier::announced);
  auto a = std::make_shared<Latch>(), b = std::make_shared<Latch>();
  Job *ja = jobs.add(latchJob("stays", a));
  Job *jb = jobs.add(latchJob("breaks", b));
  QTRY_COMPARE(jobs.runningCount(), 2);
  b->code = 2;
  QTRY_COMPARE(announced.size(), 1);
  QVERIFY(announced[0][1].toString().contains("breaks"));
  QCOMPARE(announced[0][3].toBool(), true);
  QCOMPARE(announced[0][2].value<Job *>(), jb);
  QCOMPARE(ja->state(), Job::State::Running);
  a->code = 0;
  QTRY_COMPARE(announced.size(), 2);
  endJobs(jobs);
}

// ---- the study lock ----

void TstShell::aStudyIsOpenInOneStore() {
  QSettings settings(dir_->filePath("s.ini"), QSettings::IniFormat);
  const QString dir = dir_->filePath("study");
  StudyStore first(&settings), second(&settings);
  QVERIFY(first.create(dir, "One at a time"));
  QVERIFY(first.holdsLock());
  QVERIFY(QFileInfo::exists(QDir(dir).filePath(StudyStore::kLockFileName)));

  // A second window is refused, with who holds it, and stays as it was.
  QString error;
  QVERIFY(!second.open(dir, &error));
  QVERIFY(!second.isOpen());
  QVERIFY(second.refusedBy().refused);
  QVERIFY(!second.refusedBy().otherHost);
  QCOMPARE(second.refusedBy().pid, QCoreApplication::applicationPid());
  QVERIFY2(error.contains("already open"), qPrintable(error));
  // Not even on request: it is in use on this very computer.
  QVERIFY(!second.open(dir, &error, true));
  QVERIFY(!StudySwitcher::openPath(nullptr, &second, dir, 1));

  // Opening it again where it is open is not a second holder.
  QVERIFY(first.open(dir, &error));
  QVERIFY(first.holdsLock());
  QVERIFY(first.update([](Study &s) { s.name = "Still mine"; }));

  // Closed there, it opens here; and the other way round is then refused.
  first.close();
  QVERIFY(!first.holdsLock());
  QVERIFY(second.open(dir, &error));
  QVERIFY(second.holdsLock());
  QCOMPARE(second.study().name, QStringLiteral("Still mine"));
  QVERIFY(!first.open(dir, &error));

  // Moving on to another study releases the first one.
  const QString other = dir_->filePath("other");
  QVERIFY(second.create(other, "Other"));
  QVERIFY(first.open(dir, &error));
  // A store that goes away lets go.
  {
    StudyStore third(&settings);
    QVERIFY(!third.open(other, &error));
  }
  second.close();
  StudyStore fourth(&settings);
  QVERIFY(fourth.open(other, &error));
}

void TstShell::aLockLeftByACrashIsTakenOver() {
  QSettings settings(dir_->filePath("s.ini"), QSettings::IniFormat);
  const QString dir = dir_->filePath("study");
  {
    StudyStore maker(&settings);
    QVERIFY(maker.create(dir, "Crashed"));
  }
  // A lock file of a process that no longer exists, on this computer: what
  // a crash leaves. A process that has just ended gives a pid that is free.
  QProcess gone;
  gone.start(QCoreApplication::applicationFilePath(), {"-functions"});
  QVERIFY(gone.waitForStarted(10000));
  const qint64 pid = gone.processId();
  QVERIFY(gone.waitForFinished(20000));
  QFile lock(QDir(dir).filePath(StudyStore::kLockFileName));
  QVERIFY(lock.open(QIODevice::WriteOnly));
  lock.write(QByteArray::number(pid) + "\nsismokaos-gui\n" +
             QSysInfo::machineHostName().toUtf8() + "\n");
  lock.close();

  StudyStore store(&settings);
  QString error;
  QVERIFY2(store.open(dir, &error), qPrintable(error));
  QVERIFY(store.holdsLock());
  QVERIFY(!store.refusedBy().refused);
}

void TstShell::aLockFromAnotherComputerNeedsAYes() {
  QSettings settings(dir_->filePath("s.ini"), QSettings::IniFormat);
  const QString dir = dir_->filePath("study");
  {
    StudyStore maker(&settings);
    QVERIFY(maker.create(dir, "On a shared disk"));
  }
  const QString lockPath = QDir(dir).filePath(StudyStore::kLockFileName);
  const auto foreignLock = [&lockPath] {
    QFile lock(lockPath);
    QVERIFY(lock.open(QIODevice::WriteOnly | QIODevice::Truncate));
    lock.write("4242\nsismokaos-gui\nsome-other-computer\n");
  };
  foreignLock();
  StudyStore store(&settings);
  QString error;
  QVERIFY(!store.open(dir, &error));
  QVERIFY(store.refusedBy().refused);
  QVERIFY(store.refusedBy().otherHost);
  QCOMPARE(store.refusedBy().host, QStringLiteral("some-other-computer"));
  QVERIFY2(error.contains("some-other-computer"), qPrintable(error));
  QVERIFY(!store.isOpen());
  // "No" leaves it alone; "Open anyway" takes it over.
  QVERIFY(!StudySwitcher::openPath(nullptr, &store, dir, 0));
  QVERIFY(!store.isOpen());
  QVERIFY(StudySwitcher::openPath(nullptr, &store, dir, 1));
  QVERIFY(store.isOpen());
  QVERIFY(store.holdsLock());
}

void TstShell::aStudyChangedOnDiskIsNotOverwritten() {
  QSettings settings(dir_->filePath("s.ini"), QSettings::IniFormat);
  const QString dir = dir_->filePath("study");
  StudyStore store(&settings);
  QVERIFY(store.create(dir, "Edited elsewhere"));
  QVERIFY(store.update([](Study &s) { s.name = "First"; }));
  QVERIFY(!store.changedOnDisk());

  // Something else writes the file (an editor, a copy that broke the lock).
  Study other = Study::load(dir);
  other.name = QStringLiteral("Written by someone else, at greater length");
  other.save();
  QVERIFY(store.changedOnDisk());
  QString error;
  QVERIFY(!store.update([](Study &s) { s.name = "Mine"; }, &error));
  QVERIFY2(error.contains("changed on disk"), qPrintable(error));
  // Nothing was written over it, and the store still shows what it had.
  QCOMPARE(Study::load(dir).name,
           QStringLiteral("Written by someone else, at greater length"));
  QCOMPARE(store.study().name, QStringLiteral("First"));

  // Reloading takes the other change in, keeps the lock, and edits work.
  QSignalSpy changed(&store, &StudyStore::changed);
  QVERIFY(store.reload(&error));
  QCOMPARE(changed.size(), 1);
  QVERIFY(store.holdsLock());
  QVERIFY(!store.changedOnDisk());
  QCOMPARE(store.study().name,
           QStringLiteral("Written by someone else, at greater length"));
  QVERIFY(store.update([](Study &s) { s.name = "Mine"; }, &error));
  QCOMPARE(Study::load(dir).name, QStringLiteral("Mine"));
}

// ---- the window ----

void TstShell::titleMenusAndPages() {
  Shell s = makeShell();
  MainWindow &w = *s.window;
  QCOMPARE(MainWindow::titleFor(QString()), QStringLiteral("sismokaos-gui"));
  QCOMPARE(w.windowTitle(), QStringLiteral("sismokaos-gui"));

  const auto action = [&w](const QString &text) -> QAction * {
    for (QAction *menu : w.menuBar()->actions())
      for (QAction *a : menu->menu()->actions())
        if (a->text().remove('&') == text)
          return a;
    return nullptr;
  };
  for (const char *name :
       {"New study…", "Open study…", "Close study", "Open the study folder",
        "Study inputs…", "Reload the study from disk", "Quit",
        "Open the manual", "Report a problem…", "Copy diagnostics"})
    QVERIFY2(action(name), name);
  QCOMPARE(action("New study…")->shortcut(), QKeySequence(QKeySequence::New));
  QCOMPARE(action("Open study…")->shortcut(),
           QKeySequence(QKeySequence::Open));
  // With no study there is nothing to close, show or set up.
  for (const char *name : {"Close study", "Open the study folder",
                           "Study inputs…", "Reload the study from disk"})
    QVERIFY2(!action(name)->isEnabled(), name);

  QSignalSpy newAsked(&w, &MainWindow::newStudyRequested);
  QSignalSpy inputsAsked(&w, &MainWindow::studyInputsRequested);
  action("New study…")->trigger();
  QCOMPARE(newAsked.size(), 1);

  // A study: its name leads the title, and the study actions come alive.
  QVERIFY(s.store->create(dir_->filePath("study"), "Elazığ 2020"));
  QCOMPARE(w.windowTitle(), QStringLiteral("Elazığ 2020 — sismokaos-gui"));
  for (const char *name : {"Close study", "Open the study folder",
                           "Study inputs…", "Reload the study from disk"})
    QVERIFY2(action(name)->isEnabled(), name);
  action("Study inputs…")->trigger();
  QCOMPARE(inputsAsked.size(), 1);
  QVERIFY(s.store->update([](Study &st) { st.name = "Renamed"; }));
  QCOMPARE(w.windowTitle(), QStringLiteral("Renamed — sismokaos-gui"));
  action("Close study")->trigger();
  QVERIFY(!s.store->isOpen());
  QCOMPARE(w.windowTitle(), QStringLiteral("sismokaos-gui"));

  // View lists the pages in order, the study ones numbered, with Ctrl+1 ….
  QAction *cases = action("1 · CASES");
  QAction *runs = action("2 · RUNS");
  QAction *features = action("FEATURES");
  QVERIFY(cases && runs && features);
  QCOMPARE(runs->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_2));
  QCOMPARE(features->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_3));
  features->trigger();
  auto *title = w.findChild<QLabel *>("pageTitle");
  QVERIFY(title);
  QCOMPARE(title->text(), QStringLiteral("FEATURES"));
  runs->trigger();
  QCOMPARE(title->text(), QStringLiteral("RUNS"));
}

void TstShell::recentStudiesByName() {
  Shell s = makeShell();
  const QString a = dir_->filePath("deep/in/a/folder");
  const QString b = dir_->filePath("b");
  QVERIFY(s.store->create(a, "Sivrice & Doğanyol"));
  QVERIFY(s.store->create(b, "Van 2011"));
  QCOMPARE(StudyStore::nameOf(a), QStringLiteral("Sivrice & Doğanyol"));
  QCOMPARE(StudyStore::nameOf(dir_->filePath("nothing-here")),
           QStringLiteral("nothing-here"));

  QMenu *recent = nullptr;
  for (QMenu *m : s.window->findChildren<QMenu *>())
    if (m->title().remove('&') == QLatin1String("Open recent"))
      recent = m;
  QVERIFY(recent);
  QVERIFY(recent->toolTipsVisible());
  emit recent->aboutToShow();
  QCOMPARE(recent->actions().size(), 2);
  // Newest first, by the study's own name; where it is in the tooltip.
  QVERIFY(recent->actions()[0]->text().startsWith("Van 2011"));
  QVERIFY(recent->actions()[0]->isChecked());
  QVERIFY(recent->actions()[1]->text().startsWith("Sivrice && Doğanyol"));
  QCOMPARE(QDir::fromNativeSeparators(recent->actions()[1]->toolTip()), a);
  QSignalSpy asked(s.window.get(), &MainWindow::openStudyPathRequested);
  recent->actions()[1]->trigger();
  QCOMPARE(asked.size(), 1);
  QCOMPARE(asked[0][0].toString(), a);
  // The request is carried out by the application: here, as it does.
  QVERIFY(StudySwitcher::openPath(nullptr, s.store.get(), a, 0));
  QCOMPARE(s.window->windowTitle(),
           QStringLiteral("Sivrice & Doğanyol — sismokaos-gui"));
}

void TstShell::aDroppedStudyFolderOpens() {
  Shell s = makeShell();
  const QString dir = dir_->filePath("study");
  QVERIFY(s.store->create(dir, "Dropped"));
  s.store->close();
  const QString plain = dir_->filePath("plain");
  QVERIFY(QDir().mkpath(plain));

  const auto mimeFor = [](const QStringList &paths) {
    auto mime = std::make_unique<QMimeData>();
    QList<QUrl> urls;
    for (const QString &p : paths)
      urls << QUrl::fromLocalFile(p);
    mime->setUrls(urls);
    return mime;
  };
  // The folder, or the study file in it; nothing else.
  QCOMPARE(MainWindow::studyFolderOf(mimeFor({dir}).get()), dir);
  QCOMPARE(MainWindow::studyFolderOf(
               mimeFor({QDir(dir).filePath("study.toml")}).get()),
           dir);
  QVERIFY(MainWindow::studyFolderOf(mimeFor({plain}).get()).isEmpty());
  QVERIFY(MainWindow::studyFolderOf(mimeFor({dir, plain}).get()).isEmpty());
  QMimeData text;
  text.setText("study");
  QVERIFY(MainWindow::studyFolderOf(&text).isEmpty());
  QVERIFY(MainWindow::studyFolderOf(nullptr).isEmpty());

  QVERIFY(s.window->acceptDrops());
  QSignalSpy asked(s.window.get(), &MainWindow::openStudyPathRequested);
  // What the window's drop handler does with it (the event itself comes
  // from the platform's drag and drop, which a test cannot stage reliably).
  QVERIFY(s.window->openDropped(mimeFor({dir}).get()));
  QTRY_COMPARE(asked.size(), 1);
  QCOMPARE(asked[0][0].toString(), dir);
  // Something that is not a study is left to whoever else wants it.
  QVERIFY(!s.window->openDropped(mimeFor({plain}).get()));
  QCoreApplication::processEvents();
  QCOMPARE(asked.size(), 1);
}

void TstShell::quittingNamesTheJobs() {
  Shell s = makeShell();
  MainWindow &w = *s.window;
  w.show();
  QVERIFY(QTest::qWaitForWindowExposed(&w));
  auto latch = std::make_shared<Latch>();
  Job *job = s.jobs->add(latchJob("Features · Van 2011 · TVAN", latch));
  QTRY_COMPARE(s.jobs->runningCount(), 1);
  const QString question = w.quitQuestion();
  QVERIFY2(question.contains("Features · Van 2011 · TVAN"),
           qPrintable(question));
  QVERIFY2(question.contains("running for"), qPrintable(question));

  // "Keep running": the window stays and the job goes on.
  QStringList asked;
  bool answer = false;
  w.confirmQuit = [&](const QString &q) {
    asked << q;
    return answer;
  };
  QVERIFY(!w.close());
  QCOMPARE(asked.size(), 1);
  QVERIFY(w.isVisible());
  QCOMPARE(job->state(), Job::State::Running);

  // "Stop them and quit": the job is told to stop and the window closes.
  answer = true;
  QVERIFY(w.close());
  QCOMPARE(asked.size(), 2);
  QTRY_VERIFY(ended(job));
  QCOMPARE(job->state(), Job::State::Cancelled);
  // With nothing running there is no question.
  w.show();
  QVERIFY(w.close());
  QCOMPARE(asked.size(), 2);
  endJobs(*s.jobs);
}

void TstShell::firstRunCardUntilDismissed() {
  Shell s = makeShell();
  MainWindow &w = *s.window;
  w.show();
  QVERIFY(QTest::qWaitForWindowExposed(&w));
  auto *card = w.findChild<QWidget *>("firstRunCard");
  auto *text = w.findChild<QLabel *>("firstRunText");
  auto *close = w.findChild<QPushButton *>("firstRunClose");
  QVERIFY(card && text && close);
  // On a study page with no study open: three steps and the manual.
  w.showModule("cases");
  QTRY_VERIFY(card->isVisible());
  QVERIFY(text->text().contains("1. "));
  QVERIFY(text->text().contains("3. "));
  QVERIFY(text->text().contains(manualUrl()));
  QVERIFY(manualUrl().contains("first-study-in-ten-minutes"));
  QVERIFY(!close->toolTip().isEmpty());
  // Not on a tool page, and not once a study is open.
  w.showModule("features");
  QTRY_VERIFY(!card->isVisible());
  w.showModule("runs");
  QTRY_VERIFY(card->isVisible());
  QVERIFY(s.store->create(dir_->filePath("study"), "Mine"));
  QTRY_VERIFY(!card->isVisible());
  s.store->close();
  QTRY_VERIFY(card->isVisible());
  // Closed once, closed for good.
  close->click();
  QTRY_VERIFY(!card->isVisible());
  QVERIFY(s.settings->value(appsettings::kFirstRunDismissedKey).toBool());
  w.showModule("cases");
  QCoreApplication::processEvents();
  QVERIFY(!card->isVisible());
}

void TstShell::windowComesBackWhereItCanBeReached() {
  const auto reachable = [](const QWidget &w) {
    const QRect top(w.frameGeometry().topLeft(), QSize(w.width(), 40));
    for (const QScreen *s : QGuiApplication::screens())
      if (s->availableGeometry().intersects(top))
        return true;
    return false;
  };
  // A first start has no saved place: a sane size, on a screen.
  {
    Shell s = makeShell();
    s.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(s.window.get()));
    QVERIFY(s.window->width() >= 400 && s.window->height() >= 300);
    QVERIFY(reachable(*s.window));
    // Left far outside every screen (a monitor that has gone since).
    s.window->move(-9000, -9000);
    QVERIFY(s.window->close());
    bool saved = false;
    for (const QString &key : s.settings->allKeys())
      saved = saved || key.startsWith("ui/geometry-");
    QVERIFY(saved);
  }
  // The next start does not put it back out there.
  {
    Shell s = makeShell();
    s.window->show();
    QVERIFY(QTest::qWaitForWindowExposed(s.window.get()));
    QTRY_VERIFY(reachable(*s.window));
    QVERIFY(s.window->width() >= 400 && s.window->height() >= 300);
  }
}

void TstShell::diagnosticsHoldNothingPersonal() {
  // The scrubber on its own.
  const QString raw = QStringLiteral(
      "catalog /home/ayse/veri/katalog.csv\n"
      "mailbox ayse.yilmaz@example.edu.tr and /media/ayse/disk/x\n"
      "C:\\Users\\ayse\\AppData and c:/users/AYSE/data\n"
      "ayseli is another word; /mnt/data/stations stays");
  const QString clean = scrubbed(raw, QStringLiteral("/home/ayse"));
  QVERIFY2(!clean.contains("/home/ayse"), qPrintable(clean));
  QVERIFY(clean.contains("~/veri/katalog.csv"));
  QVERIFY(!clean.contains("@"));
  QVERIFY(clean.contains("<address>"));
  QVERIFY(clean.contains("/media/<user>/disk/x"));
  QVERIFY(clean.contains("Users\\<user>\\AppData"));
  QVERIFY(clean.contains("users/<user>/data"));
  QVERIFY(clean.contains("ayseli is another word"));
  QVERIFY(clean.contains("/mnt/data/stations stays"));

  Shell s = makeShell();
  const QString where = dir_->filePath("somewhere/private/my-study");
  QVERIFY(s.store->create(where, "Tez çalışması"));
  QVERIFY(s.store->update([](Study &st) {
    study::Case c;
    c.id = "a";
    c.name = "A";
    c.time = QDateTime(QDate(2020, 1, 24), QTime(0, 0), QTimeZone::UTC);
    st.cases << c;
  }));
  // A failed job whose last words name the study's place and an address.
  const QString said = "error: cannot read " + where +
                       "/a/x.mseed, write to someone@example.org";
  Job *job = s.jobs->add(new TaskJob("Features · A", "features", "run",
                                     [said](TaskContext &ctx) {
                                       ctx.log(said);
                                       return 2;
                                     }));
  QTRY_COMPARE(job->state(), Job::State::Failed);
  QTRY_VERIFY(job->log().join('\n').contains("someone@example.org"));

  const QString text = s.window->diagnostics();
  QVERIFY2(text.contains(QCoreApplication::applicationVersion()),
           qPrintable(text));
  QVERIFY(text.contains("Qt " + QLatin1String(qVersion())));
  QVERIFY(text.contains("Language: en"));
  QVERIFY(text.contains("\"Tez çalışması\" (folder my-study): 1 case(s)"));
  QVERIFY(text.contains("1 failed"));
  QVERIFY(text.contains("Features · A"));
  QVERIFY(text.contains("<study>/a/x.mseed"));
  // Nothing of where things are, and no address.
  QVERIFY2(!text.contains(dir_->path()), qPrintable(text));
  QVERIFY(!text.contains("somewhere/private"));
  QVERIFY(!text.contains(QDir::homePath()) || QDir::homePath().size() < 2);
  QVERIFY(!text.contains("@"));
  endJobs(*s.jobs);
}

// ---- updates ----

void TstShell::versionsAndOffers() {
  // Pre-releases among themselves, and against their release.
  QVERIFY(UpdateCheck::isNewer("v0.4.0-beta.2", "0.4.0-beta.1"));
  QVERIFY(UpdateCheck::isNewer("v0.4.0-beta.10", "0.4.0-beta.9"));
  QVERIFY(UpdateCheck::isNewer("v0.4.0-rc.1", "0.4.0-beta.3"));
  QVERIFY(!UpdateCheck::isNewer("v0.4.0-beta.1", "0.4.0-beta.2"));
  QVERIFY(!UpdateCheck::isNewer("v0.4.0-beta.2", "0.4.0"));
  QVERIFY(UpdateCheck::isNewer("v0.4.0", "0.4.0-rc.1"));
  QVERIFY(UpdateCheck::isNewer("v0.4.1-beta.1", "0.4.0"));
  QVERIFY(UpdateCheck::isPreRelease("0.4.0-beta.1"));
  QVERIFY(!UpdateCheck::isPreRelease("0.4.0"));

  const QStringList tags{"v0.4.0-beta.2", "v0.4.0-beta.1", "v0.3.0",
                         "v0.2.1", "not-a-version", ""};
  // A released build is never offered a pre-release.
  QCOMPARE(UpdateCheck::bestOffer(tags, "0.2.1"), QStringLiteral("v0.3.0"));
  QCOMPARE(UpdateCheck::bestOffer(tags, "0.3.0"), QString());
  // A beta takes a later beta, and its release once it is out; never an
  // older released version.
  QCOMPARE(UpdateCheck::bestOffer(tags, "0.4.0-beta.1"),
           QStringLiteral("v0.4.0-beta.2"));
  QCOMPARE(UpdateCheck::bestOffer(tags, "0.4.0-beta.2"), QString());
  QCOMPARE(UpdateCheck::bestOffer(tags + QStringList{"v0.4.0"},
                                  "0.4.0-beta.2"),
           QStringLiteral("v0.4.0"));
}

void TstShell::updateStartsOnlyOnceClosing() {
  QSettings settings(dir_->filePath("u.ini"), QSettings::IniFormat);
  UpdateCheck updates(&settings, nullptr);
  QStringList steps;
  int jobs = 0;
  bool stopAnswer = false, closes = true, launches = true;
  updates.runningJobs = [&] { return jobs; };
  updates.confirmStopJobs = [&](int n) {
    steps << QStringLiteral("ask %1").arg(n);
    return stopAnswer;
  };
  updates.closeForUpdate = [&] {
    steps << "close";
    return closes;
  };
  updates.launch = [&](const QString &v) {
    steps << "launch " + v;
    return launches;
  };
  updates.quit = [&] { steps << "quit"; };

  // Nothing running: close first, then the updater, then out.
  QString why;
  QVERIFY(updates.startUpdate("v0.4.0", &why));
  QCOMPARE(steps, (QStringList{"close", "launch v0.4.0", "quit"}));
  QVERIFY(why.isEmpty());

  // Jobs running and "not now": nothing is started, nothing to explain.
  steps.clear();
  jobs = 2;
  QVERIFY(!updates.startUpdate("v0.4.0", &why));
  QCOMPARE(steps, (QStringList{"ask 2"}));
  QVERIFY(why.isEmpty());

  // "Stop them and update", but the window does not close: no updater.
  steps.clear();
  stopAnswer = true;
  closes = false;
  QVERIFY(!updates.startUpdate("v0.4.0", &why));
  QCOMPARE(steps, (QStringList{"ask 2", "close"}));
  QVERIFY(!why.isEmpty());

  // It closes: the updater follows.
  steps.clear();
  why.clear();
  closes = true;
  QVERIFY(updates.startUpdate("v0.4.0", &why));
  QCOMPARE(steps, (QStringList{"ask 2", "close", "launch v0.4.0", "quit"}));

  // No updater to start: said, and the application is not told to quit.
  steps.clear();
  jobs = 0;
  launches = false;
  QVERIFY(!updates.startUpdate("v0.4.0", &why));
  QCOMPARE(steps, (QStringList{"close", "launch v0.4.0"}));
  QVERIFY(!why.isEmpty());

  // The main window's part: closing for an update does not ask again.
  Shell s = makeShell();
  s.window->show();
  QVERIFY(QTest::qWaitForWindowExposed(s.window.get()));
  auto latch = std::make_shared<Latch>();
  Job *job = s.jobs->add(latchJob("long", latch));
  QTRY_COMPARE(s.jobs->runningCount(), 1);
  int asked = 0;
  s.window->confirmQuit = [&asked](const QString &) {
    ++asked;
    return false;
  };
  QVERIFY(s.window->updates()->runningJobs);
  QCOMPARE(s.window->updates()->runningJobs(), 1);
  QVERIFY(s.window->closeForUpdate());
  QCOMPARE(asked, 0);
  QTRY_COMPARE(job->state(), Job::State::Cancelled);
  endJobs(*s.jobs);
}

void TstShell::installerLeavesARunningAppAlone() {
  const QString pid = QString::number(QCoreApplication::applicationPid());
  const QString home = dir_->filePath("install-home");
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert("SISMOKAOS_HOME", home);
  QProcess p;
  p.setProcessEnvironment(env);
  p.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_WIN
  const QString script =
      QDir::toNativeSeparators(QStringLiteral(SKG_SOURCE_DIR "/install.ps1"));
  const QString shell = QStandardPaths::findExecutable("powershell");
  if (shell.isEmpty())
    QSKIP("no powershell on the PATH");
  const QStringList base{"-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                         script};
  const QStringList stillRunning = base + QStringList{"-WaitPid", pid,
                                                      "-WaitSeconds", "1",
                                                      "-DryRun"};
  const QStringList nobody = base + QStringList{"-DryRun"};
#else
  const QString script = QStringLiteral(SKG_SOURCE_DIR "/install.sh");
  const QString shell = QStringLiteral("/bin/sh");
  if (QStandardPaths::findExecutable("curl").isEmpty())
    QSKIP("the script needs curl, which is not on the PATH");
  const QStringList stillRunning{script,       "--wait-pid", pid,
                                 "--wait-seconds", "1",      "--dry-run"};
  const QStringList nobody{script, "--dry-run"};
#endif
  // This very process is "the application": it is still here after the
  // wait, so the script must stop, non-zero, having changed nothing.
  p.start(shell, stillRunning);
  QVERIFY2(p.waitForFinished(120000), qPrintable(p.errorString()));
  const QString out = QString::fromUtf8(p.readAll());
  if (out.contains("only x86_64 Linux builds are published") ||
      out.contains("unsupported system"))
    QSKIP("the installer does not serve this system");
  QCOMPARE(p.exitStatus(), QProcess::NormalExit);
  QVERIFY2(p.exitCode() != 0, qPrintable(out));
  QVERIFY2(out.contains("still running") && out.contains("nothing was changed"),
           qPrintable(out));
  QVERIFY(!QFileInfo::exists(home));

  // With nobody to wait for, the dry run gets past the wait and ends well,
  // again without touching anything.
  p.start(shell, nobody);
  QVERIFY2(p.waitForFinished(120000), qPrintable(p.errorString()));
  const QString out2 = QString::fromUtf8(p.readAll());
  QVERIFY2(p.exitCode() == 0, qPrintable(out2));
  QVERIFY2(out2.contains("dry run"), qPrintable(out2));
  QVERIFY(!QFileInfo::exists(home));
}

void TstShell::keepAwakeSaysWhatItCanDo() {
  const QString how = KeepAwake::mechanism();
#ifdef Q_OS_WIN
  QCOMPARE(how, QStringLiteral("SetThreadExecutionState"));
#else
  QVERIFY(how.isEmpty() || how == QLatin1String("caffeinate") ||
          how == QLatin1String("systemd-inhibit"));
#endif
  // Switching it on and off must be harmless whatever the system offers.
  KeepAwake awake;
  QVERIFY(!awake.active());
  awake.setActive(true);
  QVERIFY(awake.active());
  awake.setActive(true);
  awake.setActive(false);
  QVERIFY(!awake.active());
}

QTEST_MAIN(TstShell)
#include "tst_shell.moc"
