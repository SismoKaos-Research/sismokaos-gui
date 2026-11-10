// What makes the Runs and Compare pages easier to live with over hours:
// stopping one run, the menu of a cell, runs of a row or a column, a copy of
// a recipe, what ran and for how long, the study on disk and its leftovers,
// room for what a run unpacks, a log that follows, and reading a value off
// the plot. On small synthetic studies; with SKG_SHOTS set to a folder the
// pages are saved there as PNGs.

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cmath>
#include <memory>

#include "miniz.h"

#include "core/defaults.h"
#include "core/module.h"
#include "core/theme.h"
#include "jobs/job.h"
#include "jobs/jobmodel.h"
#include "modules/study/comparepage.h"
#include "modules/study/runlog.h"
#include "modules/study/runspage.h"
#include "modules/study/runtext.h"
#include "modules/study/studydisk.h"
#include "study/compare.h"
#include "study/runinfo.h"
#include "study/runs.h"
#include "study/store.h"
#include "study/waveindex.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;

static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

void shot(QWidget &w, const QString &name) {
  const QString dir = qEnvironmentVariable("SKG_SHOTS");
  if (dir.isEmpty())
    return;
  QDir().mkpath(dir);
  w.grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

void put(const QString &path, qsizetype bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QByteArray(bytes, 'x'));
}

// A run folder's record: what the run pages read back.
void record(const QString &dir, const Case &c, const QString &station,
            const Recipe &r, bool ok, const QDateTime &started, int seconds,
            const QString &error = {}) {
  QDir().mkpath(dir);
  RunRecord rec;
  rec.tool = r.tool;
  rec.recipe = r.id;
  rec.caseId = c.id;
  rec.station = station;
  rec.revision = r.revision;
  rec.started = started;
  rec.finished = started.addSecs(seconds);
  rec.ok = ok;
  rec.error = error;
  QVERIFY(rec.write(dir));
}

// One 512-byte miniSEED 2 record of 12 hours at 1 Hz (the header matters).
QByteArray halfDay(const QDateTime &start, const QByteArray &sta,
                   const QByteArray &cha) {
  QByteArray r;
  QDataStream s(&r, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::BigEndian);
  s.writeRawData("000001D ", 8);
  s.writeRawData(sta.leftJustified(5, ' ').constData(), 5);
  s.writeRawData("  ", 2);
  s.writeRawData(cha.constData(), 3);
  s.writeRawData("TU", 2);
  s << quint16(start.date().year()) << quint16(start.date().dayOfYear())
    << quint8(start.time().hour()) << quint8(0) << quint8(0) << quint8(0)
    << quint16(0);
  s << quint16(43200) << qint16(1) << qint16(1);
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1);
  s << qint32(0) << quint16(64) << quint16(48);
  s << quint16(1000) << quint16(0) << quint8(11) << quint8(1) << quint8(9)
    << quint8(0);
  return r.leftJustified(512, '\0');
}

QByteArray days(const QByteArray &sta, const QDateTime &first, int count) {
  QByteArray out;
  for (int d = 0; d < count; ++d)
    for (const char *cha : {"HHE", "HHN", "HHZ"})
      for (int half = 0; half < 2; ++half)
        out += halfDay(first.addDays(d).addSecs(half * 43200), sta, cha);
  return out;
}

// A zip with one member, written through Qt (any path).
void writeZip(const QString &path, const QByteArray &member,
              const QByteArray &content) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  mz_zip_archive zip{};
  QVERIFY(mz_zip_writer_init_heap(&zip, 0, 0));
  QVERIFY(mz_zip_writer_add_mem(&zip, member.constData(), content.constData(),
                                size_t(content.size()), MZ_BEST_SPEED));
  void *buffer = nullptr;
  size_t size = 0;
  QVERIFY(mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size));
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(static_cast<const char *>(buffer), qint64(size));
  mz_free(buffer);
  mz_zip_writer_end(&zip);
}

QAction *action(QMenu *menu, const char *name) {
  for (QAction *a : menu->actions())
    if (a->objectName() == QLatin1String(name))
      return a;
  return nullptr;
}

} // namespace

class TstRunsQol : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void durationsAndSizes();
  void activityAndDisk();
  void cleanUpKeepsResultsAndLiveRuns();
  void unpackedSizeOfAnArchive();
  void spaceIsLookedAtBeforeARun();
  void menuRunsStopsAndQueues();
  void stoppingOneRunLeavesTheOthers();
  void rowsColumnsAndACopyOfARecipe();
  void activityListAndFilter();
  void logThatFollows();
  void studyOnDiskDialog();
  void plotGuideAndLegend();
  void scoreRowsInWordsAndCopies();
  void cleanupTestCase();

private:
  // A study of four reference dates and an indicators recipe: real runs
  // that take a moment each.
  void makeStudy(StudyStore &store, const QString &name);

  QTemporaryDir dir_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<JobModel> jobs_;
  std::unique_ptr<ToolRegistry> tools_;
  std::unique_ptr<StudyStore> store_;
  AppContext ctx_;
};

void TstRunsQol::initTestCase() {
  initResources();
  settings_ =
      std::make_unique<QSettings>(dir_.filePath("s.ini"), QSettings::IniFormat);
  settings_->setValue(Theme::kSettingMode,
                      qEnvironmentVariable("SKG_SHOTS_THEME") ==
                              QLatin1String("dark")
                          ? QStringLiteral("dark")
                          : QStringLiteral("light"));
  theme_ = std::make_unique<Theme>(settings_.get());
  theme_->apply();
  jobs_ = std::make_unique<JobModel>();
  tools_ = std::make_unique<ToolRegistry>(settings_.get());
  store_ = std::make_unique<StudyStore>(settings_.get());
  ctx_ = AppContext{jobs_.get(), tools_.get(), settings_.get(), theme_.get(),
                    store_.get()};
}

void TstRunsQol::makeStudy(StudyStore &store, const QString &name) {
  const QString folder = dir_.filePath(name);
  QVERIFY(store.create(folder, name));
  QVERIFY(
      QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                  QDir(folder).filePath("catalog.csv")));
  QVERIFY(store.update([](Study &s) {
    s.catalog = QStringLiteral("catalog.csv");
    const char *names[] = {"One", "Two", "Three", "Four"};
    for (int i = 0; i < 4; ++i) {
      Case r = referenceCase(
          QString::fromLatin1(names[i]),
          QDateTime(QDate(2019 + i % 2, 3 + i, 1), QTime(0, 0), QTimeZone::UTC),
          38.1228, 38.5626);
      r.id = s.freeCaseId(r.name);
      r.spanDays = 270;
      s.cases << r;
    }
    Recipe ind = defaultRecipe(kToolIndicators);
    ind.name = QStringLiteral("Indicators");
    ind.id = s.freeRecipeId(ind.name);
    ind.settings.insert(QStringLiteral("window_months"), 6);
    s.recipes << ind;
  }));
}

void TstRunsQol::durationsAndSizes() {
  QCOMPARE(RunWords::duration(-1), QString());
  QCOMPARE(RunWords::duration(45), QStringLiteral("45 s"));
  QCOMPARE(RunWords::duration(12 * 60), QStringLiteral("12 min"));
  QCOMPARE(RunWords::duration(2 * 3600 + 14 * 60), QStringLiteral("2 h 14 min"));
  QCOMPARE(RunWords::duration(3 * 3600), QStringLiteral("3 h"));
  QCOMPARE(RunWords::duration(3 * 86400 + 4 * 3600), QStringLiteral("3 d 4 h"));
  QVERIFY(RunWords::zoneNote().contains(QStringLiteral("UTC")));
  const QLocale was;
  QLocale::setDefault(QLocale::c());
  QCOMPARE(RunWords::bytes(14200000000LL), QStringLiteral("14 GB"));
  QCOMPARE(RunWords::bytes(1400000000LL), QStringLiteral("1.4 GB"));
  QCOMPARE(RunWords::bytes(310000000), QStringLiteral("310 MB"));
  QCOMPARE(RunWords::bytes(12000), QStringLiteral("12 kB"));
  QLocale::setDefault(was);
  QCOMPARE(peakNeed({5, 3, 9}, 2), qint64(14));
  QCOMPARE(peakNeed({5, 3, 9}, 1), qint64(9));
  QCOMPARE(peakNeed({}, 2), qint64(0));
}

void TstRunsQol::activityAndDisk() {
  StudyStore store(settings_.get());
  makeStudy(store, QStringLiteral("disk"));
  const Study &s = store.study();
  const Recipe &r = s.recipes[0];
  const QDateTime t0(QDate(2026, 11, 10), QTime(20, 0), QTimeZone::UTC);
  // One finished run, one failed, one working folder a crash left, archives
  // a crash left inside a result, a calibration, and results of a recipe and
  // of a case that are no longer in the study.
  const QString done = runDir(s, s.cases[0], {}, r);
  record(done, s.cases[0], {}, r, true, t0, 2 * 3600 + 14 * 60);
  put(done + "/table.csv", 4000);
  put(done + "/.unzipped-123/a.mseed", 9000);
  const QString failedBase = runDir(s, s.cases[1], {}, r);
  record(runFailedDir(failedBase), s.cases[1], {}, r, false, t0.addSecs(60),
         42, QStringLiteral("too little data"));
  put(runFailedDir(failedBase) + "/run.log", 500);
  const QString working = runDir(s, s.cases[2], {}, r);
  put(runTmpDir(working) + "/part.bin", 7000);
  put(runCalibrationDir(done) + "/k.json", 300);
  put(QFileInfo(done).absolutePath() + "/old-recipe/x.csv", 1500);
  put(QDir(s.dir).filePath("gone-case/_case/indicators/r/x.csv"), 2500);
  // Not run folders: never listed, never touched.
  put(QDir(s.dir).filePath("exports/figure.png"), 800);
  put(QDir(s.dir).filePath("downloads/c1/raw/STA/a.zip"), 800);

  const QList<Activity> acts = activity(s);
  QCOMPARE(acts.size(), 2);
  // The one that ended last comes first: the long finished run.
  QCOMPARE(acts[0].caseName, QStringLiteral("One"));
  QVERIFY(acts[0].ok);
  QCOMPARE(acts[0].seconds(), qint64(2 * 3600 + 14 * 60));
  QCOMPARE(acts[0].recipeName, r.name);
  QCOMPARE(acts[1].caseName, QStringLiteral("Two"));
  QVERIFY(!acts[1].ok);
  QCOMPARE(acts[1].error, QStringLiteral("too little data"));
  QCOMPARE(acts[1].seconds(), qint64(42));

  const QList<DiskItem> items = studyOnDisk(s);
  const auto find = [&items](DiskKind kind) {
    QList<DiskItem> out;
    for (const DiskItem &d : items)
      if (d.kind == kind)
        out << d;
    return out;
  };
  QCOMPARE(find(DiskKind::Result).size(), 1);
  QVERIFY(!find(DiskKind::Result)[0].leftover);
  // The result's own size, without the archives listed beside it.
  QVERIFY(find(DiskKind::Result)[0].bytes >= 4000);
  QVERIFY(find(DiskKind::Result)[0].bytes < 9000);
  QCOMPARE(find(DiskKind::Unpacked).size(), 1);
  QCOMPARE(find(DiskKind::Unpacked)[0].bytes, qint64(9000));
  QVERIFY(find(DiskKind::Unpacked)[0].leftover);
  QCOMPARE(find(DiskKind::Working).size(), 1);
  QVERIFY(find(DiskKind::Working)[0].leftover);
  QCOMPARE(find(DiskKind::Failed).size(), 1);
  QVERIFY(find(DiskKind::Failed)[0].leftover);
  QCOMPARE(find(DiskKind::Calibration).size(), 1);
  QVERIFY(!find(DiskKind::Calibration)[0].leftover);
  QCOMPARE(find(DiskKind::Orphan).size(), 2);
  for (const DiskItem &d : items) {
    QVERIFY2(!d.path.contains(QStringLiteral("/exports")), qPrintable(d.path));
    QVERIFY2(!d.path.contains(QStringLiteral("/downloads")),
             qPrintable(d.path));
  }
  QCOMPARE(folderBytes(runTmpDir(working)), qint64(7000));
}

void TstRunsQol::cleanUpKeepsResultsAndLiveRuns() {
  StudyStore store(settings_.get());
  makeStudy(store, QStringLiteral("clean"));
  const Study &s = store.study();
  const Recipe &r = s.recipes[0];
  const QDateTime t0(QDate(2026, 11, 10), QTime(20, 0), QTimeZone::UTC);
  const QString done = runDir(s, s.cases[0], {}, r);
  record(done, s.cases[0], {}, r, true, t0, 60);
  put(done + "/table.csv", 4000);
  const QString live = runDir(s, s.cases[1], {}, r);
  put(runTmpDir(live) + "/part.bin", 7000);
  const QString dead = runDir(s, s.cases[2], {}, r);
  put(runTmpDir(dead) + "/part.bin", 6000);
  put(runFailedDir(dead) + "/run.log", 100);
  put(runCalibrationDir(done) + "/k.json", 300);
  put(QDir(s.dir).filePath("exports/figure.png"), 800);

  // A run of this instance owns the second working folder.
  LiveRuns::enter(live);
  QVERIFY(LiveRuns::contains(live));
  QStringList all;
  for (const DiskItem &d : studyOnDisk(s))
    all << d.path;
  // Asked to remove everything it lists, and more: only the leftovers go.
  all << QDir(s.dir).filePath("exports") << s.dir << done;
  QStringList refused;
  const qint64 freed = cleanUp(s, all, &refused);
  QCOMPARE(freed, qint64(6100));
  QVERIFY(QFileInfo::exists(done + "/table.csv"));
  QVERIFY(QFileInfo::exists(done + "/run.json"));
  QVERIFY(QFileInfo::exists(runTmpDir(live) + "/part.bin"));
  QVERIFY(QFileInfo::exists(runCalibrationDir(done) + "/k.json"));
  QVERIFY(QFileInfo::exists(QDir(s.dir).filePath("exports/figure.png")));
  QVERIFY(QFileInfo::exists(QDir(s.dir).filePath("study.toml")));
  QVERIFY(!QFileInfo::exists(runTmpDir(dead)));
  QVERIFY(!QFileInfo::exists(runFailedDir(dead)));
  QVERIFY(refused.contains(QDir::cleanPath(done)));
  QVERIFY(refused.contains(QDir::cleanPath(runTmpDir(live))));
  // When the run is over its working folder is a leftover like any other.
  LiveRuns::leave(live);
  QVERIFY(!LiveRuns::contains(live));
  QCOMPARE(cleanUp(s, {runTmpDir(live)}), qint64(7000));
  QVERIFY(!QFileInfo::exists(runTmpDir(live)));
  // A path outside the study is never removed, whatever it is.
  const QString outside = dir_.filePath("outside/_case/indicators/x.tmp");
  put(outside + "/f", 10);
  QCOMPARE(cleanUp(s, {outside}), qint64(0));
  QVERIFY(QFileInfo::exists(outside + "/f"));
}

void TstRunsQol::unpackedSizeOfAnArchive() {
  // Under a folder with Turkish letters: the archive is read through Qt.
  const QString zip = dir_.filePath(QStringLiteral("İstasyon ğüşöç/a.zip"));
  writeZip(zip, "big.mseed", QByteArray(300000, '\0'));
  QVERIFY(QFileInfo(zip).size() < 10000);
  QCOMPARE(unpackedBytes(zip), qint64(300000));
  QCOMPARE(unpackedBytes(zip), qint64(300000)); // remembered
  const QString plain = dir_.filePath("plain.mseed");
  put(plain, 5000);
  QCOMPARE(unpackedBytes(plain), qint64(0)); // read where it is
  QCOMPARE(unpackedBytes(dir_.filePath("missing.zip")), qint64(0));
  QVERIFY(freeBytesAt(dir_.path()) > 0);
  QVERIFY(freeBytesAt(dir_.filePath("not/yet/there")) > 0);
}

void TstRunsQol::spaceIsLookedAtBeforeARun() {
  // One earthquake studied from SYN1, whose waveforms are a zip archive.
  StudyStore store(settings_.get());
  const QString folder = dir_.filePath("space");
  QVERIFY(store.create(folder, QStringLiteral("Space")));
  QVERIFY(
      QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                  QDir(folder).filePath("catalog.csv")));
  const QString root = dir_.filePath("space-data");
  const QDateTime event(QDate(2019, 4, 2), QTime(14, 0), QTimeZone::UTC);
  const QByteArray content = days("SYN1", event.addDays(-40), 30);
  writeZip(root + "/SYN1/SYN1_2019-02-21.zip", "SYN1.mseed", content);
  QVERIFY(store.update([&](Study &s) {
    s.catalog = QStringLiteral("catalog.csv");
    Case c;
    c.name = QStringLiteral("Quake");
    c.id = s.freeCaseId(c.name);
    c.time = event;
    c.lat = 38.1228;
    c.lon = 38.5626;
    c.magnitude = 6.1;
    c.spanDays = 60;
    StationRef st;
    st.code = QStringLiteral("SYN1");
    st.lat = 38.3;
    st.lon = 38.7;
    c.stations << st;
    s.cases << c;
    s.addWaveRoot(root);
    Recipe f = defaultRecipe(kToolFeatures);
    f.id = s.freeRecipeId(f.name);
    s.recipes << f;
  }));
  {
    WaveIndex index;
    QList<ScanFolder> folders;
    for (const WaveRoot &w : store.study().waveRoots)
      folders << ScanFolder{w.id, store.study().resolved(w.path)};
    scanRoots(index, folders, ScanPass::Identity);
    scanRoots(index, folders, ScanPass::Coverage);
    QVERIFY(index.save(folder));
  }
  const Study &s = store.study();
  const SpaceNeed need = tempSpaceFor(s, s.cases[0], QStringLiteral("SYN1"),
                                      s.recipes[0]);
  QCOMPARE(need.archives, 1);
  QCOMPARE(need.bytes, qint64(content.size()));

  AppContext ctx = ctx_;
  ctx.study = &store;
  RunsPage page(ctx);
  page.resize(1100, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *matrix = page.findChild<RunMatrix *>();
  auto *guide = page.findChild<QLabel *>(QStringLiteral("runsGuide"));
  auto *runOne = page.findChild<QPushButton *>(QStringLiteral("runOne"));
  QVERIFY(matrix && guide && runOne);
  matrix->select(0, 0);
  QVERIFY2(runOne->isEnabled(), "the features cell should be runnable");
  const int jobsBefore = jobs_->rowCount();
  QStringList asked;
  bool answer = false;
  page.confirm = [&](const QString &title, const QString &text) {
    asked << title + QLatin1Char('\n') + text;
    return answer;
  };
  // Clearly too little: nothing starts, and the figures are said.
  qint64 freeNow = need.bytes / 2;
  page.freeSpace = [&freeNow](const QString &) { return freeNow; };
  runOne->click();
  QCOMPARE(jobs_->rowCount(), jobsBefore);
  QVERIFY(page.queued().isEmpty());
  QVERIFY(asked.isEmpty());
  QVERIFY2(guide->text().contains(QStringLiteral("Not started")),
           qPrintable(guide->text()));
  QVERIFY2(guide->text().contains(RunWords::bytes(need.bytes)),
           qPrintable(guide->text()));
  QVERIFY2(guide->text().contains(RunWords::bytes(freeNow)),
           qPrintable(guide->text()));
  // "Run missing and stale" is held to the same.
  page.findChild<QPushButton *>(QStringLiteral("runAll"))->click();
  QCOMPARE(jobs_->rowCount(), jobsBefore);
  QVERIFY(!page.batchRunning());
  // Just enough: asked first, with the figures; a no starts nothing.
  freeNow = need.bytes + need.bytes / 10;
  runOne->click();
  QCOMPARE(asked.size(), 1);
  QVERIFY2(asked[0].contains(QStringLiteral("Little disk space")),
           qPrintable(asked[0]));
  QVERIFY(asked[0].contains(RunWords::bytes(need.bytes)));
  QCOMPARE(jobs_->rowCount(), jobsBefore);
  QVERIFY(page.queued().isEmpty());
  shot(page, QStringLiteral("runs_space"));
}

void TstRunsQol::menuRunsStopsAndQueues() {
  StudyStore store(settings_.get());
  makeStudy(store, QStringLiteral("menu"));
  const Study &s = store.study();
  const Recipe &ind = s.recipes[0];
  AppContext ctx = ctx_;
  ctx.study = &store;
  Navigator nav;
  ctx.nav = &nav;
  QSignalSpy went(&nav, &Navigator::requested);
  settings_->setValue(QLatin1String(defaults::kRunsAtOnceSetting), 1);
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);
  const auto key = [&](int i) {
    return s.cases[i].id + QStringLiteral("||") + ind.id;
  };
  RunsPage page(ctx);
  page.resize(1100, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *matrix = page.findChild<RunMatrix *>();
  QVERIFY(matrix);
  const int jobsBefore = jobs_->rowCount();

  // A cell's menu runs it; the second one waits, and its menu says so.
  {
    std::unique_ptr<QMenu> menu(page.menuFor(0, 0));
    QAction *run = action(menu.get(), "actRun");
    QVERIFY(run && run->isEnabled());
    QCOMPARE(run->text(), QStringLiteral("Run"));
    QVERIFY(action(menu.get(), "actLog") && !action(menu.get(), "actLog")->isEnabled());
    QVERIFY(action(menu.get(), "actCopyPath")->isEnabled());
    QVERIFY(action(menu.get(), "actCases")->isEnabled());
    QVERIFY(!action(menu.get(), "actRecalibrate")); // not a features cell
    run->trigger();
  }
  QCOMPARE(jobs_->rowCount(), jobsBefore + 1);
  {
    std::unique_ptr<QMenu> menu(page.menuFor(1, 0));
    action(menu.get(), "actRun")->trigger();
  }
  QCOMPARE(page.queued(), QStringList{key(1)});
  {
    std::unique_ptr<QMenu> menu(page.menuFor(1, 0));
    QAction *stop = action(menu.get(), "actStop");
    QVERIFY(stop && !action(menu.get(), "actRun"));
    QCOMPARE(stop->text(), QStringLiteral("Take out of the queue"));
    stop->trigger();
  }
  QVERIFY(page.queued().isEmpty());
  // The path of a cell's folder goes to the clipboard as the system writes it.
  {
    std::unique_ptr<QMenu> menu(page.menuFor(2, 0));
    action(menu.get(), "actCopyPath")->trigger();
    QCOMPARE(QDir::fromNativeSeparators(QApplication::clipboard()->text()),
             runDir(s, s.cases[2], {}, ind));
    action(menu.get(), "actCases")->trigger();
    QCOMPARE(went.size(), 1);
    QCOMPARE(went[0][0].toString(), QStringLiteral("cases"));
    QCOMPARE(went[0][1].toMap().value(QStringLiteral("case")).toString(),
             s.cases[2].id);
  }
  // A row's name and a column's heading have menus of their own.
  if (!qEnvironmentVariable("SKG_SHOTS").isEmpty()) {
    std::unique_ptr<QMenu> menu(page.menuFor(0, 0));
    menu->popup(page.mapToGlobal(QPoint(300, 200)));
    QVERIFY(QTest::qWaitForWindowExposed(menu.get()));
    shot(*menu, QStringLiteral("runs_cell_menu"));
    std::unique_ptr<QMenu> head(page.menuFor(-1, 0));
    head->popup(page.mapToGlobal(QPoint(500, 200)));
    QVERIFY(QTest::qWaitForWindowExposed(head.get()));
    shot(*head, QStringLiteral("runs_column_menu"));
  }
  {
    std::unique_ptr<QMenu> rowMenu(page.menuFor(3, -1));
    QVERIFY(action(rowMenu.get(), "actRunRow"));
    QVERIFY(action(rowMenu.get(), "actRunCase"));
    std::unique_ptr<QMenu> colMenu(page.menuFor(-1, 0));
    for (const char *name :
         {"actRunRecipe", "actEdit", "actDuplicate", "actRemove"})
      QVERIFY2(action(colMenu.get(), name), name);
  }
  // The names in full where the matrix cuts them short.
  QCOMPARE(matrix->labelTip(0, -1), QStringLiteral("One"));
  QVERIFY(matrix->labelTip(-1, 0).contains(QStringLiteral("Indicators")));
  // The job says what it runs by name, not by id.
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);
  shot(page, QStringLiteral("runs_after_menu"));
}

void TstRunsQol::stoppingOneRunLeavesTheOthers() {
  StudyStore store(settings_.get());
  makeStudy(store, QStringLiteral("stopone"));
  const Study &s = store.study();
  const Recipe &ind = s.recipes[0];
  AppContext ctx = ctx_;
  ctx.study = &store;
  settings_->setValue(QLatin1String(defaults::kRunsAtOnceSetting), 2);
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);
  RunsPage page(ctx);
  page.resize(1100, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *matrix = page.findChild<RunMatrix *>();
  auto *runOne = page.findChild<QPushButton *>(QStringLiteral("runOne"));
  auto *guide = page.findChild<QLabel *>(QStringLiteral("runsGuide"));
  QVERIFY(matrix && runOne && guide);
  QStringList asked;
  page.confirm = [&asked](const QString &, const QString &text) {
    asked << text;
    return true;
  };
  const int jobsBefore = jobs_->rowCount();
  // Three cells selected (a click, then Ctrl and two more) and run together.
  matrix->select(0, 0);
  matrix->toggle(1, 0);
  matrix->toggle(2, 0);
  QCOMPARE(matrix->selectedCells().size(), 3);
  QCOMPARE(runOne->text(), QStringLiteral("Run the 3 selected cell(s)"));
  runOne->click();
  QCOMPARE(asked.size(), 1);
  QVERIFY2(asked[0].contains(QStringLiteral("Run 3 cell(s)?")),
           qPrintable(asked[0]));
  QVERIFY2(asked[0].contains(QStringLiteral("2 run at a time")),
           qPrintable(asked[0]));
  QCOMPARE(jobs_->rowCount(), jobsBefore + 2);
  QCOMPARE(page.queued().size(), 1);
  QVERIFY2(guide->text().contains(QStringLiteral("2 running, 1 queued")),
           qPrintable(guide->text()));
  // The first is stopped, alone: its button says so, and the queue goes on.
  matrix->select(0, 0);
  Job *first = jobs_->jobAt(jobsBefore);
  const bool stillRunning = first->state() == Job::State::Running ||
                            first->state() == Job::State::Queued;
  if (stillRunning)
    QCOMPARE(runOne->text(), QStringLiteral("Stop this run"));
  shot(page, QStringLiteral("runs_running"));
  page.stopCells({{0, 0}});
  // The third cell, which waited, runs all the same; the second finishes.
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->rowCount(), jobsBefore + 3, 120000);
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 240000);
  QTRY_VERIFY_WITH_TIMEOUT(page.queued().isEmpty(), 10000);
  QCOMPARE(jobs_->jobAt(jobsBefore + 1)->state(), Job::State::Done);
  QCOMPARE(jobs_->jobAt(jobsBefore + 2)->state(), Job::State::Done);
  QCOMPARE(cellOf(s, s.cases[1], {}, ind).state, CellState::Done);
  QCOMPARE(cellOf(s, s.cases[2], {}, ind).state, CellState::Done);
  if (first->state() == Job::State::Cancelled) {
    // A stopped run leaves nothing.
    QCOMPARE(cellOf(s, s.cases[0], {}, ind).state, CellState::Missing);
    QVERIFY(!QFileInfo::exists(runTmpDir(runDir(s, s.cases[0], {}, ind))));
  }
  // Nothing of it is taken for live any more.
  QTRY_VERIFY_WITH_TIMEOUT(
      !LiveRuns::contains(runDir(s, s.cases[1], {}, ind)), 10000);
}

void TstRunsQol::rowsColumnsAndACopyOfARecipe() {
  StudyStore store(settings_.get());
  makeStudy(store, QStringLiteral("rows"));
  AppContext ctx = ctx_;
  ctx.study = &store;
  settings_->setValue(QLatin1String(defaults::kRunsAtOnceSetting), 1);
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);
  RunsPage page(ctx);
  page.resize(1100, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *matrix = page.findChild<RunMatrix *>();
  auto *stop = page.findChild<QPushButton *>(QStringLiteral("stopBatch"));
  QVERIFY(matrix && stop);
  int asked = 0;
  page.confirm = [&asked](const QString &, const QString &) {
    ++asked;
    return true;
  };
  // A copy of the recipe: the same settings under another name, as new.
  matrix->select(-1, 0);
  const Recipe original = store.study().recipes[0];
  const QString copyId = page.duplicateRecipe(QStringLiteral("Indicators B"));
  QVERIFY(!copyId.isEmpty());
  QCOMPARE(store.study().recipes.size(), 2);
  const Recipe copy = store.study().recipes[1];
  QCOMPARE(copy.id, copyId);
  QVERIFY(copy.id != original.id);
  QCOMPARE(copy.name, QStringLiteral("Indicators B"));
  QCOMPARE(copy.tool, original.tool);
  QCOMPARE(copy.settings, original.settings);
  QCOMPARE(copy.revision, 1);
  QCOMPARE(matrix->selectedColumn(), 1);

  // One column: this recipe on every row, through the queue.
  const int jobsBefore = jobs_->rowCount();
  page.runColumn(0);
  QCOMPARE(asked, 1);
  QCOMPARE(jobs_->rowCount(), jobsBefore + 1);
  QCOMPARE(page.queued().size(), 3);
  for (const QString &k : page.queued())
    QVERIFY2(k.endsWith(QStringLiteral("||") + original.id), qPrintable(k));
  stop->click();
  QVERIFY(page.queued().isEmpty());
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);
  // One row: every recipe on it. The first may have finished meanwhile.
  QTRY_VERIFY_WITH_TIMEOUT(page.queued().isEmpty(), 10000);
  // Asked for right after a Stop: the stopped run's ending must not take
  // the new cells out of the queue again.
  const int then = jobs_->rowCount();
  page.runRow(3);
  QCOMPARE(asked, 2);
  // Both recipes on that row, one at a time: one runs (as soon as the
  // stopped run's worker has left its slot), the other waits or follows.
  QTRY_VERIFY_WITH_TIMEOUT(jobs_->rowCount() >= then + 1, 120000);
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->rowCount(), then + 2, 240000);
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 240000);
  for (const Recipe &r : store.study().recipes)
    QCOMPARE(cellOf(store.study(), store.study().cases[3], {}, r).state,
             CellState::Done);
  stop->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);
}

void TstRunsQol::activityListAndFilter() {
  StudyStore store(settings_.get());
  makeStudy(store, QStringLiteral("activity"));
  const Study &s = store.study();
  const Recipe &r = s.recipes[0];
  // What ran while nobody watched: a finished run, and one that failed.
  const QDateTime t0 =
      QDateTime::currentDateTimeUtc().addSecs(-3 * 3600);
  record(runDir(s, s.cases[0], {}, r), s.cases[0], {}, r, true, t0,
         2 * 3600 + 14 * 60);
  record(runFailedDir(runDir(s, s.cases[1], {}, r)), s.cases[1], {}, r, false,
         t0.addSecs(600), 42, QStringLiteral("the disk was full"));
  AppContext ctx = ctx_;
  ctx.study = &store;
  RunsPage page(ctx);
  page.resize(1100, 820);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *table = page.findChild<QTableWidget *>(QStringLiteral("activityTable"));
  auto *matrix = page.findChild<RunMatrix *>();
  auto *info = page.findChild<QLabel *>(QStringLiteral("cellInfo"));
  auto *filter = page.findChild<QLineEdit *>(QStringLiteral("runsFilter"));
  auto *show = page.findChild<QComboBox *>(QStringLiteral("runsShow"));
  QVERIFY(table && matrix && info && filter && show);
  QCOMPARE(table->rowCount(), 2);
  // Latest first: the failed one ended later (it started later and was short
  // … no: the finished one ended last).
  QCOMPARE(table->item(0, 1)->text(), QStringLiteral("One"));
  QCOMPARE(table->item(0, 4)->text(), QStringLiteral("✓  Done"));
  QCOMPARE(table->item(0, 5)->text(), QStringLiteral("2 h 14 min"));
  QCOMPARE(table->item(1, 1)->text(), QStringLiteral("Two"));
  QCOMPARE(table->item(1, 4)->text(), QStringLiteral("✕  Failed"));
  QCOMPARE(table->item(1, 5)->text(), QStringLiteral("42 s"));
  QCOMPARE(table->item(1, 6)->text(), QStringLiteral("the disk was full"));
  // By duration, shortest first: by the number, not by the text.
  table->sortByColumn(5, Qt::AscendingOrder);
  QCOMPARE(table->item(0, 5)->text(), QStringLiteral("42 s"));
  // The cell of a finished run says when it ended and how long it took.
  matrix->select(0, 0);
  QVERIFY2(info->text().contains(QStringLiteral("took 2 h 14 min")),
           qPrintable(info->text()));
  shot(page, QStringLiteral("runs_activity"));

  // Rows are found by name, and by what they need.
  QCOMPARE(matrix->selectedCells().size(), 1);
  filter->setText(QStringLiteral("thr"));
  const QList<QLabel *> shown = page.findChildren<QLabel *>();
  bool said = false;
  for (QLabel *l : shown)
    said = said || l->text() == QStringLiteral("1 of 4 rows shown");
  QVERIFY(said);
  QCOMPARE(matrix->labelTip(0, -1), QStringLiteral("Three"));
  QVERIFY(matrix->labelTip(1, -1).isEmpty());
  filter->clear();
  // By what the rows need, as the cells themselves say it.
  const auto expect = [&](int mode) {
    QStringList names;
    for (const Case &c : s.cases) {
      const CellState st = cellOf(s, c, {}, r).state;
      const bool attention =
          st == CellState::Failed || st == CellState::Blocked;
      const bool notDone = st != CellState::Done;
      if (mode == 0 || (mode == 1 && attention) || (mode == 2 && notDone))
        names << c.name;
    }
    return names;
  };
  for (int mode : {1, 2, 3, 0}) {
    show->setCurrentIndex(mode);
    emit show->activated(mode);
    const QStringList want = mode == 3 ? QStringList() : expect(mode);
    for (int i = 0; i < 5; ++i)
      QCOMPARE(matrix->labelTip(i, -1), want.value(i));
  }
  QCOMPARE(expect(0).size(), 4);
}

void TstRunsQol::logThatFollows() {
  const QString path = dir_.filePath("run.log");
  RunLogDialog none(dir_.filePath("no-such.log"));
  QVERIFY(none.text().isEmpty()); // not an error: the run has not begun
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    for (int i = 0; i < 400; ++i)
      f.write(QStringLiteral("line %1\n").arg(i).toUtf8());
  }
  RunLogDialog log(path);
  log.resize(700, 400);
  log.show();
  QVERIFY(QTest::qWaitForWindowExposed(&log));
  auto *view = log.findChild<QPlainTextEdit *>(QStringLiteral("runLogText"));
  auto *follow = log.findChild<QCheckBox *>(QStringLiteral("runLogFollow"));
  QVERIFY(view && follow);
  QVERIFY(log.text().startsWith(QStringLiteral("line 0\n")));
  QVERIFY(log.text().endsWith(QStringLiteral("line 399")));
  QVERIFY(log.following());
  QTRY_VERIFY(view->verticalScrollBar()->maximum() > 0);
  // The run writes on: the new lines arrive, and the view stays at the end.
  const auto append = [&path](const QByteArray &more) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::Append));
    f.write(more);
  };
  append("line 400\nhalf a li");
  log.refresh();
  QVERIFY(log.text().endsWith(QStringLiteral("line 400"))); // whole lines only
  QTRY_COMPARE(view->verticalScrollBar()->value(),
               view->verticalScrollBar()->maximum());
  append("ne\n");
  log.refresh();
  QVERIFY(log.text().endsWith(QStringLiteral("line 400\nhalf a line")));
  // Reading further up stops the following; new lines do not pull back.
  view->verticalScrollBar()->setValue(0);
  QVERIFY(!log.following());
  append("line 402\n");
  log.refresh();
  QVERIFY(log.text().endsWith(QStringLiteral("line 402")));
  QCOMPARE(view->verticalScrollBar()->value(), 0);
  follow->setChecked(true);
  QTRY_COMPARE(view->verticalScrollBar()->value(),
               view->verticalScrollBar()->maximum());
  // Finding, with a wrap round; and what is not there.
  QVERIFY(log.findNext(QStringLiteral("line 42")));
  QCOMPARE(view->textCursor().selectedText(), QStringLiteral("line 42"));
  QVERIFY(log.findNext(QStringLiteral("half a line")));
  QVERIFY(log.findNext(QStringLiteral("line 0")));
  QVERIFY(!log.findNext(QStringLiteral("no such words")));
  // Copy: the selection, or the whole log.
  log.findChild<QPushButton *>(QStringLiteral("runLogCopy"))->click();
  QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("line 0"));
  shot(log, QStringLiteral("run_log"));
  // A new run of the cell starts the file again: so does the window.
  {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("a new run\n");
  }
  log.refresh();
  QCOMPARE(log.text(), QStringLiteral("a new run"));
}

void TstRunsQol::studyOnDiskDialog() {
  StudyStore store(settings_.get());
  makeStudy(store, QStringLiteral("diskdialog"));
  const Study &s = store.study();
  const Recipe &r = s.recipes[0];
  const QDateTime t0(QDate(2026, 11, 10), QTime(20, 0), QTimeZone::UTC);
  const QString done = runDir(s, s.cases[0], {}, r);
  record(done, s.cases[0], {}, r, true, t0, 60);
  put(done + "/table.csv", 40000);
  put(runTmpDir(runDir(s, s.cases[1], {}, r)) + "/part.bin", 70000);
  put(runFailedDir(runDir(s, s.cases[2], {}, r)) + "/run.log", 1000);
  put(QDir(s.dir).filePath("gone-case/_case/indicators/r/x.csv"), 25000);
  AppContext ctx = ctx_;
  ctx.study = &store;
  StudyDiskDialog dialog(ctx);
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  QTRY_VERIFY_WITH_TIMEOUT(!dialog.loading(), 20000);
  auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("diskTable"));
  auto *clean = dialog.findChild<QPushButton *>(QStringLiteral("cleanLeftovers"));
  auto *summary = dialog.findChild<QLabel *>(QStringLiteral("diskSummary"));
  QVERIFY(table && clean && summary);
  QCOMPARE(table->rowCount(), 4);
  QCOMPARE(dialog.leftovers().size(), 3);
  QVERIFY(clean->isEnabled());
  QVERIFY2(summary->text().contains(QStringLiteral("leftovers")),
           qPrintable(summary->text()));
  // The largest first.
  QVERIFY(table->item(0, 5)->data(Qt::UserRole).toLongLong() >=
          table->item(1, 5)->data(Qt::UserRole).toLongLong());
  shot(dialog, QStringLiteral("study_on_disk"));
  // A no removes nothing; a yes removes what was listed, and only that.
  QString asked;
  bool answer = false;
  dialog.confirm = [&](const QString &, const QString &text) {
    asked = text;
    return answer;
  };
  QCOMPARE(dialog.cleanUp(), qint64(0));
  QVERIFY2(asked.contains(QStringLiteral("3 leftover folder(s)")),
           qPrintable(asked));
  QVERIFY2(asked.contains(QStringLiteral("gone-case")), qPrintable(asked));
  QVERIFY(!asked.contains(QStringLiteral("table.csv")));
  QVERIFY(QFileInfo::exists(QDir(s.dir).filePath("gone-case")));
  answer = true;
  QCOMPARE(dialog.cleanUp(), qint64(96000));
  QTRY_VERIFY_WITH_TIMEOUT(!dialog.loading(), 20000);
  QCOMPARE(table->rowCount(), 1);
  QVERIFY(!clean->isEnabled());
  QVERIFY(QFileInfo::exists(done + "/table.csv"));
  QVERIFY(!QFileInfo::exists(QDir(s.dir).filePath("gone-case")));
}

void TstRunsQol::plotGuideAndLegend() {
  SeriesPlot plot(theme_.get());
  Series a, b;
  a.caseId = QStringLiteral("a");
  a.caseName = QStringLiteral("Alpha");
  b.caseId = QStringLiteral("b");
  b.caseName = QStringLiteral("Beta");
  b.station = QStringLiteral("STA");
  for (int d = -60; d <= 0; d += 2) {
    a.daysBefore.push_back(d);
    a.values.push_back(1.0 + d / 100.0);
  }
  for (int d = -61; d <= -1; d += 6) { // other days, with a gap
    b.daysBefore.push_back(d);
    b.values.push_back(d == -31 ? std::nan("") : 2.0);
  }
  plot.setSeries({a, b}, QStringLiteral("b_value"), 60,
                 {QStringLiteral("a"), QStringLiteral("b")});
  plot.resize(900, 420);
  plot.show();
  QVERIFY(QTest::qWaitForWindowExposed(&plot));
  QVERIFY(!plot.hasGuide());
  // The guide stands on the nearest day that has a value.
  QVERIFY(plot.setGuide(-41.6));
  QVERIFY(plot.hasGuide());
  SeriesPlot::Readout r = plot.readout();
  QCOMPARE(r.day, -42.0);
  QCOMPARE(r.names, (QStringList{QStringLiteral("Alpha"),
                                 QStringLiteral("Beta · STA")}));
  QCOMPARE(r.values[0], 1.0 - 0.42);
  QVERIFY(std::isnan(r.values[1])); // Beta has no value on that day
  shot(plot, QStringLiteral("compare_hover_plot"));
  QVERIFY(plot.setGuide(-43.2)); // Beta's day
  r = plot.readout();
  QCOMPARE(r.day, -43.0);
  QVERIFY(std::isnan(r.values[0]));
  QCOMPARE(r.values[1], 2.0);
  // The keys move it point by point, over both series' days.
  QTest::keyClick(&plot, Qt::Key_Right);
  QCOMPARE(plot.readout().day, -42.0);
  QTest::keyClick(&plot, Qt::Key_Left);
  QTest::keyClick(&plot, Qt::Key_Left);
  QCOMPARE(plot.readout().day, -44.0);
  for (int i = 0; i < 200; ++i)
    QTest::keyClick(&plot, Qt::Key_Right);
  QCOMPARE(plot.readout().day, 0.0); // stops at the event
  QTest::keyClick(&plot, Qt::Key_Escape);
  QVERIFY(!plot.hasGuide());
  // The pointer does the same (the plot knows where it drew).
  QTRY_VERIFY(plot.legendRect(1).isValid());
  QTest::mouseMove(&plot, QPoint(plot.width() / 2, plot.height() / 2));
  QTRY_VERIFY(plot.hasGuide());
  // A click on a name in the legend hides the series, and shows it again.
  QSignalSpy changed(&plot, &SeriesPlot::hiddenChanged);
  QTest::mouseClick(&plot, Qt::LeftButton, {},
                    plot.legendRect(1).center().toPoint());
  QCOMPARE(changed.size(), 1);
  QCOMPARE(plot.hiddenSeries(), QStringList{QStringLiteral("b|STA")});
  QVERIFY(plot.setGuide(-43.2)); // no longer a day with a value: Alpha's
  QCOMPARE(plot.readout().names, QStringList{QStringLiteral("Alpha")});
  QVERIFY(std::abs(plot.readout().day + 43.0) > 0.5);
  QTRY_VERIFY(plot.legendRect(1).isValid());
  QVERIFY(plot.toggleSeries(QStringLiteral("b|STA")));
  QVERIFY(plot.hiddenSeries().isEmpty());
  QVERIFY(!plot.toggleSeries(QStringLiteral("no|such")));
  // With every series hidden there is nothing to stand on.
  plot.setHiddenSeries({QStringLiteral("a|"), QStringLiteral("b|STA")});
  QVERIFY(!plot.setGuide(-10));
  QVERIFY(!plot.hasGuide());
}

void TstRunsQol::scoreRowsInWordsAndCopies() {
  ScoreRow row;
  row.caseName = QStringLiteral("A very long name of an earthquake, 2019");
  row.station = QStringLiteral("SYN1");
  row.magnitude = 6.1;
  row.distanceKm = 26.4;
  row.state = CellState::Done;
  row.bestModel = QStringLiteral("random_forest");
  row.baselineModel = QStringLiteral("svm_rbf");
  row.testF1 = 0.9312;
  row.baseF1 = 0.61;
  row.testAuc = 0.99;
  row.baseAuc = 0.82;
  row.warnings << QStringLiteral("a sparse catalog");
  const QLocale was;
  // What the page shows follows the language; what it writes does not.
  QLocale::setDefault(QLocale(QLocale::Turkish, QLocale::Turkey));
  const QString tip = ScoreTable::rowTip(row);
  QVERIFY2(tip.contains(row.caseName), qPrintable(tip));
  QVERIFY2(tip.contains(QStringLiteral("SYN1")), qPrintable(tip));
  QVERIFY2(tip.contains(QStringLiteral("0,931")), qPrintable(tip));
  QVERIFY2(tip.contains(QStringLiteral("random_forest")), qPrintable(tip));
  QVERIFY2(tip.contains(QStringLiteral("a sparse catalog")), qPrintable(tip));
  const QString csv = scoresCsv({row});
  QVERIFY2(csv.contains(QStringLiteral("0.9312")), qPrintable(csv));
  QVERIFY(!csv.contains(QStringLiteral("0,9312")));
  QLocale::setDefault(was);
  ScoreRow missing;
  missing.caseName = QStringLiteral("B");
  missing.state = CellState::Missing;
  missing.magnitude = missing.distanceKm = std::nan("");
  QVERIFY(ScoreTable::rowTip(missing).contains(
      QStringLiteral("No finished run")));

  // The page copies the table as it is shown, with tabs.
  StudyStore store(settings_.get());
  makeStudy(store, QStringLiteral("copy"));
  AppContext ctx = ctx_;
  ctx.study = &store;
  ComparePage page(ctx);
  page.resize(1200, 820);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  QTRY_VERIFY_WITH_TIMEOUT(!page.loading(), 30000);
  const QString tsv = page.scoresTsv();
  QVERIFY2(tsv.startsWith(QStringLiteral("case\tstation\tkind\t")),
           qPrintable(tsv));
  QVERIFY(!tsv.contains(QLatin1Char(';')));
  auto *copy = page.findChild<QPushButton *>(QStringLiteral("copyTable"));
  QVERIFY(copy);
  QVERIFY(!copy->isEnabled()); // no scores yet: nothing to copy
}

void TstRunsQol::cleanupTestCase() {
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 240000);
}

QTEST_MAIN(TstRunsQol)
#include "tst_runs_qol.moc"
