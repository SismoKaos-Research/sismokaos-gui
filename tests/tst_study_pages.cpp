// The study pages on a small synthetic study: they build, show the study,
// and survive an edit. With SKG_SHOTS set to a folder, each page is also
// saved there as a PNG (used when changing the look).
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QDataStream>
#include <QDialogButtonBox>
#include <QLabel>
#include <QSpinBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QFileInfo>
#include <QTimeZone>

#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSvgGenerator>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "core/defaults.h"
#include "core/module.h"
#include "core/theme.h"
#include "fx/npy.h"
#include "jobs/job.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casespage.h"
#include "modules/study/comparepage.h"
#include "modules/study/runspage.h"
#include "modules/study/waveforms.h"
#include "study/compare.h"
#include "study/runs.h"
#include "study/store.h"
#include "study/waveindex.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;

// The station list embedded in the tdvms library.
static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

void shot(QWidget &w, const QString &name) {
  const QString dir = qEnvironmentVariable("SKG_SHOTS");
  if (dir.isEmpty())
    return;
  QDir().mkpath(dir);
  w.grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

Case eventCase(Study &s, const QString &name, const QString &iso, double lat,
               double lon, double mag, const QString &station) {
  Case c;
  c.name = name;
  c.id = s.freeCaseId(name);
  c.time = QDateTime::fromString(iso, Qt::ISODate).toUTC();
  c.lat = lat;
  c.lon = lon;
  c.depthKm = 10;
  c.magnitude = mag;
  c.place = QStringLiteral("Synthetic");
  c.spanDays = 270;
  StationRef st;
  st.code = station;
  st.lat = lat + 0.2;
  st.lon = lon + 0.15;
  st.distanceKm = 26;
  st.azimuthDeg = 30;
  c.stations << st;
  return c;
}

// An fx-style feature table for the 40 days before `event`: two columns
// that drift with the day, plus noise.
fx::NpyTable fxTable(const QDateTime &event, quint32 seed) {
  fx::NpyTable t;
  t.fields = {
      fx::NpyField{QStringLiteral("Pencere_ID"), QStringLiteral("<U50")},
      fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")},
      fx::NpyField{QStringLiteral("N_RMS"), QStringLiteral("<f8")},
      fx::NpyField{QStringLiteral("N_WOLF_LYE"), QStringLiteral("<f8")}};
  QStringList ids;
  std::vector<double> zaman, rms, wolf;
  QRandomGenerator rng(seed);
  const double t0 =
      double(QDateTime(event.date().addDays(-40), QTime(0, 0), QTimeZone::UTC)
                 .toSecsSinceEpoch());
  int n = 0;
  for (int d = 0; d <= 40; ++d)
    for (int w = 1; w <= 432; ++w) {
      ids << QStringLiteral("x_w%1").arg(++n);
      zaman.push_back((t0 + d * 86400.0 + w * 200.0) / 60.0);
      rms.push_back(100.0 + d + 3.0 * rng.generateDouble());
      wolf.push_back(0.1 * d + rng.generateDouble());
    }
  t.rows = n;
  t.strings = {ids, {}, {}, {}};
  t.numeric = {{}, zaman, rms, wolf};
  return t;
}

} // namespace

class TstStudyPages : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void casesPage();
  void addFromCatalog();
  void referenceDate();
  void runsPage();
  void runsPageGuards();
  void runsPageFromNothing();
  void recipeDialog();
  void comparePage();
  void compareExportsAndKeys();
  void compareManySeriesAndEmptyStates();
  void waveformsOnCases();
  void stationsWithData();
  void catalogOffsetRereadsCaseTimes();
  void runsQueue();
  void cleanupTestCase();

private:
  QTemporaryDir dir_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<JobModel> jobs_;
  std::unique_ptr<ToolRegistry> tools_;
  std::unique_ptr<StudyStore> store_;
  AppContext ctx_;
};

void TstStudyPages::initTestCase() {
  initResources();
  settings_ =
      std::make_unique<QSettings>(dir_.filePath("s.ini"), QSettings::IniFormat);
  if (qEnvironmentVariable("SKG_SHOTS_THEME") == QLatin1String("dark"))
    settings_->setValue(Theme::kSettingMode, QStringLiteral("dark"));
  else
    settings_->setValue(Theme::kSettingMode, QStringLiteral("light"));
  theme_ = std::make_unique<Theme>(settings_.get());
  theme_->apply();
  jobs_ = std::make_unique<JobModel>();
  tools_ = std::make_unique<ToolRegistry>(settings_.get());
  store_ = std::make_unique<StudyStore>(settings_.get());
  ctx_ = AppContext{jobs_.get(), tools_.get(), settings_.get(), theme_.get(),
                    store_.get()};
  const QString folder = dir_.filePath("study");
  QVERIFY(store_->create(folder, QStringLiteral("Synthetic demo")));
  QVERIFY(
      QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                  QDir(folder).filePath("catalog.csv")));
  QVERIFY(store_->update([](Study &s) {
    s.catalog = QStringLiteral("catalog.csv");
    s.cases << eventCase(s, QStringLiteral("Synthetic A 2019"),
                         QStringLiteral("2019-04-02T14:00:00Z"), 38.1228,
                         38.5626, 6.1, QStringLiteral("SYN1"));
    s.cases << eventCase(s, QStringLiteral("Synthetic B 2019"),
                         QStringLiteral("2019-12-30T22:10:00Z"), 38.4716,
                         39.3543, 5.8, QStringLiteral("SYN2"));
    Case r =
        referenceCase(QStringLiteral("Reference 2021"),
                      QDateTime(QDate(2021, 5, 1), QTime(0, 0), QTimeZone::UTC),
                      38.1228, 38.5626);
    r.id = s.freeCaseId(r.name);
    r.spanDays = 270;
    r.stations = s.cases[0].stations;
    s.cases << r;
  }));
}

void TstStudyPages::casesPage() {
  CasesPage page(ctx_);
  page.resize(1040, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *cases = page.findChild<QTableWidget *>(QStringLiteral("caseTable"));
  auto *stations =
      page.findChild<QTableWidget *>(QStringLiteral("stationTable"));
  QVERIFY(cases && stations);
  QCOMPARE(cases->rowCount(), 3);
  QCOMPARE(cases->item(0, 0)->text(), QStringLiteral("Synthetic A 2019"));
  // The case's own station is listed and ticked.
  bool ticked = false;
  for (int i = 0; i < stations->rowCount(); ++i)
    ticked = ticked || (stations->item(i, 1)->text() == QLatin1String("SYN1") &&
                        stations->item(i, 0)->checkState() == Qt::Checked);
  QVERIFY(ticked);
  shot(page, QStringLiteral("cases"));
  // An edit elsewhere (another page) shows up here.
  QVERIFY(store_->update([](Study &s) { s.cases[0].spanDays = 200; }));
  QCOMPARE(cases->rowCount(), 3);
  QVERIFY(cases->item(0, 6)->text().contains(QStringLiteral("200")));
  QVERIFY(store_->update([](Study &s) { s.cases[0].spanDays = 270; }));
}

void TstStudyPages::addFromCatalog() {
  AddFromCatalogDialog dialog(store_->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *events = dialog.findChild<QTableWidget *>(QStringLiteral("eventTable"));
  QVERIFY(events);
  // The synthetic catalog has six hand-placed events of M >= 5.
  QCOMPARE(events->rowCount(), 6);
  QCOMPARE(events->item(0, 1)->text(), QStringLiteral("6.4"));
  // Filtering by a station keeps only the earthquakes close to it, says how
  // far each is, and starts that station ticked.
  auto *codes = dialog.findChild<QLineEdit *>(QStringLiteral("nearStations"));
  auto *km = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("nearKm"));
  auto *st = dialog.findChild<QTableWidget *>(QStringLiteral("stationTable"));
  QVERIFY(codes && km && st);
  // Take a station of the built-in list that is close to the largest event.
  events->selectRow(0);
  QVERIFY(st->rowCount() > 0);
  const QString code = st->item(0, 1)->text();
  codes->setText(code.toLower() + QStringLiteral(", NOPE"));
  km->setValue(60);
  const int nearCount = events->rowCount();
  QVERIFY(nearCount >= 1 && nearCount <= 6);
  QVERIFY(!events->isColumnHidden(3));
  for (int i = 0; i < nearCount; ++i)
    QVERIFY(events->item(i, 3)->text().startsWith(code));
  km->setValue(5);
  QVERIFY(events->rowCount() < 6);
  km->setValue(60);
  events->selectRow(0);
  bool filterTicked = false;
  for (int i = 0; i < st->rowCount(); ++i)
    filterTicked = filterTicked ||
                   (st->item(i, 1)->text() == code &&
                    st->item(i, 0)->checkState() == Qt::Checked);
  QVERIFY(filterTicked);
  shot(dialog, QStringLiteral("add_from_catalog_near"));
  // Typing a second code completes only that one: the list offers the
  // stations whose code starts with what was typed, with their place, and
  // accepting one keeps the first code.
  auto *completer = dialog.findChild<QCompleter *>(QStringLiteral("nearCompleter"));
  QVERIFY(completer);
  const QString other = st->item(1, 1)->text();
  QVERIFY(other != code && other.size() >= 2);
  codes->setText(code + QStringLiteral(", "));
  codes->setFocus();
  codes->setCursorPosition(int(codes->text().size()));
  QTest::keyClicks(codes, other.left(2).toLower());
  QCOMPARE(completer->completionPrefix(), other.left(2).toLower());
  QVERIFY(completer->completionCount() >= 1);
  int found = -1;
  for (int i = 0; i < completer->completionCount(); ++i) {
    QVERIFY(completer->setCurrentRow(i));
    QVERIFY(completer->currentIndex()
                .data(Qt::UserRole)
                .toString()
                .startsWith(other.left(2), Qt::CaseInsensitive));
    if (completer->currentIndex().data(Qt::UserRole).toString() == other)
      found = i;
  }
  QVERIFY(found >= 0);
  QVERIFY(completer->setCurrentRow(found));
  QVERIFY(completer->currentIndex().data(Qt::DisplayRole).toString().startsWith(
      other));
  emit completer->activated(completer->currentIndex());
  completer->popup()->hide();
  QCOMPARE(codes->text(), code + QStringLiteral(", ") + other);
  codes->setText(QStringLiteral("NOPE"));
  QCOMPARE(events->rowCount(), 0);
  codes->clear();
  QCOMPARE(events->rowCount(), 6);
  QVERIFY(events->isColumnHidden(3));
  for (int i = 0; i < st->rowCount(); ++i)
    st->item(i, 0)->setCheckState(Qt::Unchecked);
  events->selectRow(0);
  // The built-in station list offers stations near the chosen event.
  auto *near = dialog.findChild<QTableWidget *>(QStringLiteral("stationTable"));
  QVERIFY(near && near->rowCount() > 3);
  // With nothing chosen yet the three nearest start ticked, and each row
  // says what the study's waveform folders hold of the station.
  QCOMPARE(near->columnCount(), 5);
  QCOMPARE(near->item(0, 4)->text(), QStringLiteral("not found"));
  auto *ok = dialog.findChild<QDialogButtonBox *>()->button(
      QDialogButtonBox::Ok);
  auto *warning = dialog.findChild<QLabel *>(QStringLiteral("caseWarning"));
  QVERIFY(ok && warning && ok->isEnabled());
  // Without an earthquake selected there is nothing to add.
  events->clearSelection();
  QVERIFY(!ok->isEnabled());
  events->selectRow(0);
  QVERIFY(ok->isEnabled());
  for (int i = 0; i < near->rowCount(); ++i)
    QCOMPARE(near->item(i, 0)->checkState(),
             i < 3 ? Qt::Checked : Qt::Unchecked);
  // No station at all is allowed, but said.
  for (int i = 0; i < 3; ++i)
    near->item(i, 0)->setCheckState(Qt::Unchecked);
  QVERIFY(warning->text().contains(QStringLiteral("No station is ticked")));
  near->item(0, 0)->setCheckState(Qt::Checked);
  QVERIFY(!warning->text().contains(QStringLiteral("No station is ticked")));
  shot(dialog, QStringLiteral("add_from_catalog"));
  dialog.accept();
  QVERIFY(dialog.result().has_value());
  QCOMPARE(dialog.result()->magnitude, 6.4);
  QCOMPARE(dialog.result()->time.toString(Qt::ISODate),
           QStringLiteral("2020-07-07T07:07:07Z"));
  QVERIFY(!dialog.result()->reference);
  QCOMPARE(dialog.result()->stations.size(), 1);
  QVERIFY(dialog.result()->stations[0].distanceKm < 150.0);
}

void TstStudyPages::referenceDate() {
  ReferenceDateDialog dialog(store_->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  shot(dialog, QStringLiteral("reference_date"));
  dialog.accept();
  QVERIFY(dialog.result().has_value());
  QVERIFY(dialog.result()->reference);
  // The first choice copies the place and stations of the first case.
  QCOMPARE(dialog.result()->lat, 38.1228);
  QCOMPARE(dialog.result()->stations.size(), 1);
}

void TstStudyPages::runsPage() {
  // Three recipes; the indicators are run for real, the feature tables are
  // fabricated (no waveforms), the classification is run on them.
  QVERIFY(store_->update([](Study &s) {
    Recipe ind = defaultRecipe(kToolIndicators);
    ind.name = QStringLiteral("6 month window");
    ind.id = s.freeRecipeId(ind.name);
    ind.settings.insert(QStringLiteral("window_months"), 6);
    ind.settings.insert(QStringLiteral("figures"), false);
    s.recipes << ind;
    Recipe fea = defaultRecipe(kToolFeatures);
    fea.id = s.freeRecipeId(fea.name);
    s.recipes << fea;
    Recipe cls = defaultRecipe(kToolClassification);
    cls.id = s.freeRecipeId(cls.name);
    cls.settings.insert(QStringLiteral("features_recipe"), fea.id);
    cls.settings.insert(QStringLiteral("classes"),
                        QVariantList{QStringLiteral("1-10"),
                                     QStringLiteral("11-20"),
                                     QStringLiteral("21-30")});
    cls.settings.insert(QStringLiteral("days_per_class"), 10);
    cls.settings.insert(QStringLiteral("models"),
                        QVariantList{QStringLiteral("logistic"),
                                     QStringLiteral("random_forest")});
    cls.settings.insert(QStringLiteral("cv_folds"), 0);
    cls.settings.insert(QStringLiteral("quick"), true);
    s.recipes << cls;
  }));
  const Study &s = store_->study();
  const Recipe &ind = s.recipes[0], &fea = s.recipes[1], &cls = s.recipes[2];

  RunsPage page(ctx_);
  page.resize(1100, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *matrix = page.findChild<RunMatrix *>();
  QVERIFY(matrix);
  shot(page, QStringLiteral("runs_before"));

  // "Run missing and stale" starts the indicators (the only cells that can
  // run: no waveforms, so no features, so no classification).
  QPushButton *run = nullptr;
  for (QPushButton *b : page.findChildren<QPushButton *>())
    if (b->property("primary").toBool())
      run = b;
  QVERIFY(run && run->isEnabled());
  QVERIFY(run->text().contains(QStringLiteral("(3)")));
  run->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->rowCount(), 3, 60000);
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);
  QTest::qWait(200); // the page refreshes after the last job settles
  for (const Case &c : s.cases)
    QCOMPARE(cellOf(s, c, c.stations[0].code, ind).state, CellState::Done);
  for (int i = 0; i < jobs_->rowCount(); ++i)
    QCOMPARE(jobs_->jobAt(i)->state(), Job::State::Done);
  QVERIFY(run->text().contains(QStringLiteral("(0)")));

  // Stand-ins for the feature runs, then the classification through the
  // page's "run the selected cell".
  quint32 seed = 7;
  for (const Case &c : s.cases) {
    const QString code = c.stations[0].code;
    const QString dir = runDir(s, c, code, fea);
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(fx::writeNpy(QDir(dir).filePath("features.npy"),
                         fxTable(c.time, seed++)));
    RunRecord rec;
    rec.tool = kToolFeatures;
    rec.recipe = fea.id;
    rec.caseId = c.id;
    rec.station = code;
    rec.revision = fea.revision;
    rec.caseSignature = runSignature(s, c, code, kToolFeatures);
    rec.ok = true;
    rec.started = rec.finished = QDateTime::currentDateTimeUtc();
    rec.outputs.insert(QStringLiteral("features"),
                       QStringLiteral("features.npy"));
    rec.fitted.insert(QStringLiteral("tau"), 5);
    rec.fitted.insert(QStringLiteral("m"), 4);
    QVERIFY(rec.write(dir));
  }
  QVERIFY(store_->update([](Study &) {})); // makes the page look again
  QVERIFY(run->text().contains(QStringLiteral("(3)")));
  run->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->rowCount(), 6, 60000);
  shot(page, QStringLiteral("runs_running"));
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 300000);
  QTest::qWait(200);
  for (const Case &c : s.cases)
    QCOMPARE(cellOf(s, c, c.stations[0].code, cls).state, CellState::Done);
  matrix->select(0, 2);
  shot(page, QStringLiteral("runs"));

  // Editing a recipe makes its runs stale.
  QVERIFY(store_->update([&ind](Study &st) {
    for (Recipe &r : st.recipes)
      if (r.id == ind.id) {
        r.settings.insert(QStringLiteral("step_months"), 2);
        r.revision += 1;
      }
  }));
  QCOMPARE(cellOf(store_->study(), store_->study().cases[0], QString(),
                  store_->study().recipes[0])
               .state,
           CellState::Stale);
  shot(page, QStringLiteral("runs_stale"));
  QVERIFY(store_->update([&ind](Study &st) {
    for (Recipe &r : st.recipes)
      if (r.id == ind.id) {
        r.settings.insert(QStringLiteral("step_months"), 1);
        r.revision -= 1;
      }
  }));
}

// What keeps a run from doing harm, and what tells the user where to go:
// the question before a finished run is run again, Stop, the reason and the
// button of a waiting cell, the log, the keys.
void TstStudyPages::runsPageGuards() {
  const Study &s = store_->study();
  Navigator nav;
  AppContext ctx = ctx_;
  ctx.nav = &nav;
  QSignalSpy went(&nav, &Navigator::requested);
  RunsPage page(ctx);
  page.resize(1100, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  QStringList asked;
  bool answer = false;
  page.confirm = [&asked, &answer](const QString &, const QString &text) {
    asked << text;
    return answer;
  };
  auto *matrix = page.findChild<RunMatrix *>();
  auto *runAll = page.findChild<QPushButton *>(QStringLiteral("runAll"));
  auto *stop = page.findChild<QPushButton *>(QStringLiteral("stopBatch"));
  auto *runOne = page.findChild<QPushButton *>(QStringLiteral("runOne"));
  auto *fix = page.findChild<QPushButton *>(QStringLiteral("fixCell"));
  auto *showLog = page.findChild<QPushButton *>(QStringLiteral("showLog"));
  auto *info = page.findChild<QLabel *>(QStringLiteral("cellInfo"));
  auto *guide = page.findChild<QLabel *>(QStringLiteral("runsGuide"));
  auto *guideGo = page.findChild<QPushButton *>(QStringLiteral("runsGuideGo"));
  QVERIFY(matrix && runAll && stop && runOne && fix && showLog && info &&
          guide && guideGo);
  const auto waitForWorkers = [this] {
    for (int i = 0; i < jobs_->rowCount(); ++i)
      if (auto *task = qobject_cast<TaskJob *>(jobs_->jobAt(i)))
        task->wait();
    QTest::qWait(900); // the page's tick notices that the workers have left
  };

  // Everything is done: the page says so and points to Compare.
  QVERIFY(!runAll->isEnabled());
  QVERIFY(!stop->isEnabled());
  QVERIFY(guide->isVisible());
  QVERIFY(guideGo->isVisible());
  guideGo->click();
  QCOMPARE(went.size(), 1);
  QCOMPARE(went[0][0].toString(), QStringLiteral("compare"));

  // A finished cell is run again only after a yes.
  matrix->select(0, 0);
  QVERIFY(runOne->isEnabled());
  QVERIFY(info->text().contains(QStringLiteral("Done")));
  const int jobsBefore = jobs_->rowCount();
  const Cell first = cellOf(s, s.cases[0], QString(), s.recipes[0]);
  runOne->click();
  QCOMPARE(asked.size(), 1);
  QVERIFY(asked[0].contains(QStringLiteral("finished run")));
  QCOMPARE(jobs_->rowCount(), jobsBefore);

  // The log of the last run can be read.
  QVERIFY(showLog->isEnabled());
  showLog->click();
  auto *logDialog = page.findChild<QDialog *>(QStringLiteral("runLog"));
  QVERIFY(logDialog);
  QVERIFY(logDialog->findChild<QPlainTextEdit *>()->toPlainText().contains(
      QStringLiteral("catalog events from")));
  shot(*logDialog, QStringLiteral("runs_log"));
  logDialog->close();

  // The keys: arrows move the selection, Enter runs (and asks, here).
  matrix->setFocus();
  QTest::keyClick(matrix, Qt::Key_Right);
  QCOMPARE(matrix->selectedColumn(), 1);
  QTest::keyClick(matrix, Qt::Key_Down);
  QCOMPARE(matrix->selectedRow(), 1);
  QTest::keyClick(matrix, Qt::Key_Left);
  QTest::keyClick(matrix, Qt::Key_Up);
  QCOMPARE(matrix->selectedRow(), 0);
  QCOMPARE(matrix->selectedColumn(), 0);
  QTest::keyClick(matrix, Qt::Key_Return);
  QCOMPARE(asked.size(), 2);
  QCOMPARE(jobs_->rowCount(), jobsBefore);

  // Stop ends a batch: what runs is cancelled, nothing further starts, and
  // the finished runs are as they were.
  const QString ind = s.recipes[0].id;
  QVERIFY(store_->update([&ind](Study &st) {
    for (Recipe &r : st.recipes)
      if (r.id == ind) {
        r.revision += 1;
        // With its figures the run takes seconds, not milliseconds: there
        // is something to stop. (An optimised build finishes the bare
        // tables before Stop can be pressed.)
        r.settings.insert(QStringLiteral("figures"), true);
      }
  }));
  QVERIFY(runAll->isEnabled());
  QVERIFY(runAll->text().contains(QStringLiteral("(3)")));
  runAll->click();
  QVERIFY(page.batchRunning());
  QVERIFY(stop->isEnabled());
  QCOMPARE(jobs_->rowCount(), jobsBefore + 2); // two at a time
  stop->click();
  QVERIFY(!page.batchRunning());
  for (int i = jobsBefore; i < jobs_->rowCount(); ++i)
    QCOMPARE(jobs_->jobAt(i)->state(), Job::State::Cancelled);
  waitForWorkers();
  QCOMPARE(jobs_->rowCount(), jobsBefore + 2); // the third never started
  for (const Case &c : store_->study().cases) {
    const Cell cell = cellOf(store_->study(), c, QString(),
                             *store_->study().findRecipe(ind));
    QCOMPARE(cell.state, CellState::Stale);
    QVERIFY(!QDir(runTmpDir(cell.dir)).exists());
    QVERIFY(!QDir(runFailedDir(cell.dir)).exists());
  }
  matrix->select(0, 0);
  QVERIFY(info->text().contains(QStringLiteral("recipe was edited")));
  QVERIFY(runOne->isEnabled()); // the worker has left: it can be run again
  QVERIFY(store_->update([&ind](Study &st) {
    for (Recipe &r : st.recipes)
      if (r.id == ind) {
        r.revision -= 1;
        r.settings.insert(QStringLiteral("figures"), false);
      }
  }));
  const Cell again = cellOf(s, s.cases[0], QString(), s.recipes[0]);
  QCOMPARE(again.state, CellState::Done);
  QCOMPARE(again.record->finished, first.record->finished);

  // One job cancelled as from the Jobs panel while the other goes on: the
  // batch ends there, the other run finishes and counts, the third cell is
  // not started behind the user's back.
  QVERIFY(store_->update([&ind](Study &st) {
    for (Recipe &r : st.recipes)
      if (r.id == ind) {
        r.revision += 1;
        // With its figures the run takes seconds, not milliseconds: there
        // is something to stop. (An optimised build finishes the bare
        // tables before Stop can be pressed.)
        r.settings.insert(QStringLiteral("figures"), true);
      }
  }));
  const int jobsThen = jobs_->rowCount();
  runAll->click();
  QCOMPARE(jobs_->rowCount(), jobsThen + 2);
  jobs_->jobAt(jobsThen)->cancel();
  QTRY_VERIFY(!page.batchRunning());
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->jobAt(jobsThen + 1)->state(),
                            Job::State::Done, 120000);
  waitForWorkers();
  QCOMPARE(jobs_->rowCount(), jobsThen + 2);
  QCOMPARE(jobs_->jobAt(jobsThen)->state(), Job::State::Cancelled);
  int doneNow = 0, staleNow = 0;
  for (const Case &c : store_->study().cases) {
    const Cell cell = cellOf(store_->study(), c, QString(),
                             *store_->study().findRecipe(ind));
    doneNow += cell.state == CellState::Done ? 1 : 0;
    staleNow += cell.state == CellState::Stale ? 1 : 0;
    QVERIFY(!QDir(runTmpDir(cell.dir)).exists());
  }
  QCOMPARE(doneNow, 1);
  QCOMPARE(staleNow, 2);
  QVERIFY(runAll->text().contains(QStringLiteral("(2)")));
  // The rest of the batch, to the end this time.
  runAll->click();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->rowCount(), jobsThen + 4, 60000);
  QTRY_VERIFY_WITH_TIMEOUT(!page.batchRunning(), 240000);
  waitForWorkers();
  for (const Case &c : store_->study().cases)
    QCOMPARE(cellOf(store_->study(), c, QString(),
                    *store_->study().findRecipe(ind))
                 .state,
             CellState::Done);

  // Editing a recipe: the page says how many finished runs the change makes
  // stale and asks. A no goes back to the dialog with what was typed; a yes
  // saves it.
  matrix->select(0, 0);
  QPushButton *edit = nullptr;
  for (QPushButton *b : page.findChildren<QPushButton *>())
    if (b->text().startsWith(QLatin1String("Edit recipe")))
      edit = b;
  QVERIFY(edit && edit->isEnabled());
  const int revisionBefore = store_->study().findRecipe(ind)->revision;
  const auto typeNine = [] {
    auto *dialog =
        qobject_cast<RecipeDialog *>(QApplication::activeModalWidget());
    if (!dialog)
      return;
    auto *t = dialog->findChild<QTableWidget *>(QStringLiteral("settingsTable"));
    for (int i = 0; i < t->rowCount(); ++i)
      if (t->item(i, 0)->data(Qt::UserRole).toString() ==
          QLatin1String("window_months"))
        t->item(i, 1)->setText(QStringLiteral("9"));
    dialog->accept();
  };
  QString shownAgain;
  asked.clear();
  page.confirm = [&](const QString &, const QString &text) {
    asked << text;
    // The dialog comes back: note what it shows, then give up.
    QTimer::singleShot(200, &page, [&shownAgain] {
      auto *dialog =
          qobject_cast<RecipeDialog *>(QApplication::activeModalWidget());
      if (!dialog)
        return;
      auto *t =
          dialog->findChild<QTableWidget *>(QStringLiteral("settingsTable"));
      for (int i = 0; i < t->rowCount(); ++i)
        if (t->item(i, 0)->data(Qt::UserRole).toString() ==
            QLatin1String("window_months"))
          shownAgain = t->item(i, 1)->text();
      dialog->reject();
    });
    return false;
  };
  QTimer::singleShot(200, &page, typeNine);
  edit->click();
  QCOMPARE(asked.size(), 1);
  QVERIFY2(asked[0].contains(QStringLiteral("3 finished run(s)")),
           qPrintable(asked[0]));
  QCOMPARE(shownAgain, QStringLiteral("9"));
  QCOMPARE(store_->study().findRecipe(ind)->revision, revisionBefore);
  QCOMPARE(store_->study().findRecipe(ind)->settings.value("window_months").toInt(),
           6);
  page.confirm = [&](const QString &, const QString &text) {
    asked << text;
    return true;
  };
  QTimer::singleShot(200, &page, typeNine);
  edit->click();
  QCOMPARE(asked.size(), 2);
  QCOMPARE(store_->study().findRecipe(ind)->revision, revisionBefore + 1);
  QCOMPARE(store_->study().findRecipe(ind)->settings.value("window_months").toInt(),
           9);
  QCOMPARE(cellOf(store_->study(), store_->study().cases[0], QString(),
                  *store_->study().findRecipe(ind))
               .state,
           CellState::Stale);
  QVERIFY(store_->update([&ind](Study &st) {
    for (Recipe &r : st.recipes)
      if (r.id == ind) {
        r.settings.insert(QStringLiteral("window_months"), 6);
        r.revision -= 1;
      }
  }));
  QCOMPARE(cellOf(store_->study(), store_->study().cases[0], QString(),
                  *store_->study().findRecipe(ind))
               .state,
           CellState::Done);
  page.confirm = [&asked, &answer](const QString &, const QString &text) {
    asked << text;
    return answer;
  };

  // A case without stations: its per-station cells wait, say for what, and
  // the button goes to the page where stations are ticked.
  QVERIFY(store_->update([](Study &st) {
    Case bare = st.cases[0];
    bare.name = QStringLiteral("No stations");
    bare.id = st.freeCaseId(bare.name);
    bare.stations.clear();
    st.cases << bare;
  }));
  const int bareRow = 3;
  matrix->select(bareRow, 1);
  QVERIFY2(info->text().contains(QStringLiteral("no stations")),
           qPrintable(info->text()));
  QVERIFY(info->text().contains(QStringLiteral("Cases page")));
  QVERIFY(!runOne->isEnabled());
  QVERIFY(fix->isVisible());
  QCOMPARE(fix->text(), QStringLiteral("Go to Cases"));
  shot(page, QStringLiteral("runs_waiting"));
  fix->click();
  QCOMPARE(went.size(), 2);
  QCOMPARE(went[1][0].toString(), QStringLiteral("cases"));
  QCOMPARE(went[1][1].toMap().value(QStringLiteral("case")).toString(),
           store_->study().cases[3].id);
  // The indicators of that case can run (they need no station): the batch
  // would take them.
  QVERIFY(runAll->text().contains(QStringLiteral("(1)")));
  QVERIFY(store_->update([](Study &st) { st.cases.removeLast(); }));
  QCOMPARE(store_->study().cases.size(), 3);
}

// From an empty application: no study, then a study with no cases, then the
// recipes in the order that works.
void TstStudyPages::runsPageFromNothing() {
  QSettings settings(dir_.filePath("s2.ini"), QSettings::IniFormat);
  StudyStore store(&settings);
  Navigator nav;
  AppContext ctx = ctx_;
  ctx.study = &store;
  ctx.nav = &nav;
  QSignalSpy went(&nav, &Navigator::requested);
  RunsPage page(ctx);
  page.resize(1100, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  // No study: the two ways on are buttons, not only words.
  // No study: the page says so (the window's bar has New and Open).
  auto *stack = page.findChild<QStackedWidget *>();
  QVERIFY(stack);
  QCOMPARE(stack->currentIndex(), 0);
  shot(page, QStringLiteral("runs_no_study"));

  QVERIFY(store.create(dir_.filePath("fresh"), QStringLiteral("Fresh")));
  auto *guide = page.findChild<QLabel *>(QStringLiteral("runsGuide"));
  auto *guideGo = page.findChild<QPushButton *>(QStringLiteral("runsGuideGo"));
  QCOMPARE(stack->currentIndex(), 1);
  QVERIFY(guide->isVisible());
  QVERIFY(guide->text().contains(QStringLiteral("no cases")));
  QVERIFY(guideGo->isVisible());
  shot(page, QStringLiteral("runs_no_cases"));
  guideGo->click();
  QCOMPARE(went.size(), 1);
  QCOMPARE(went[0][0].toString(), QStringLiteral("cases"));

  // A classification first: the page offers the features recipe it needs,
  // and the new classification is bound to it.
  QStringList asked;
  page.confirm = [&asked](const QString &, const QString &text) {
    asked << text;
    return true;
  };
  auto *newRecipe = page.findChild<QToolButton *>(QStringLiteral("studySwitch"));
  QVERIFY(newRecipe && newRecipe->menu());
  emit newRecipe->menu()->aboutToShow();
  QAction *classification = nullptr;
  for (QAction *a : newRecipe->menu()->actions())
    if (a->text() == QLatin1String("Classification"))
      classification = a;
  QVERIFY(classification);
  bool sawList = false;
  QTimer::singleShot(200, &page, [&sawList] {
    auto *dialog = qobject_cast<RecipeDialog *>(QApplication::activeModalWidget());
    if (!dialog)
      return;
    auto *box = dialog->findChild<QComboBox *>(QStringLiteral("featuresRecipe"));
    sawList = box && box->count() == 2 && box->currentIndex() == 1;
    dialog->accept();
  });
  classification->trigger();
  QVERIFY(sawList);
  QCOMPARE(asked.size(), 1);
  QVERIFY(asked[0].contains(QStringLiteral("features recipe")));
  QCOMPARE(store.study().recipes.size(), 2);
  QCOMPARE(store.study().recipes[0].tool, kToolFeatures);
  QCOMPARE(store.study().recipes[1].tool, kToolClassification);
  QCOMPARE(store.study().recipes[1].settings.value("features_recipe").toString(),
           store.study().recipes[0].id);

  // Removing the features recipe names the recipe that depends on it.
  auto *matrix = page.findChild<RunMatrix *>();
  matrix->select(-1, 0);
  page.confirm = [&asked](const QString &, const QString &text) {
    asked << text;
    return false;
  };
  QPushButton *remove = nullptr;
  for (QPushButton *b : page.findChildren<QPushButton *>())
    if (b->text() == QLatin1String("Remove recipe"))
      remove = b;
  QVERIFY(remove && remove->isEnabled());
  remove->click();
  QCOMPARE(asked.size(), 2);
  QVERIFY2(asked[1].contains(store.study().recipes[1].name),
           qPrintable(asked[1]));
  QCOMPARE(store.study().recipes.size(), 2);
  store.close();
  QCOMPARE(stack->currentIndex(), 0);
}

void TstStudyPages::recipeDialog() {
  const Study &s = store_->study();
  RecipeDialog dialog(s, s.recipes[0]);
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  shot(dialog, QStringLiteral("recipe"));
  auto *table =
      dialog.findChild<QTableWidget *>(QStringLiteral("settingsTable"));
  QVERIFY(table);
  // Unchanged: the same revision. A changed number: one more.
  dialog.accept();
  QCOMPARE(dialog.recipe().revision, s.recipes[0].revision);
  RecipeDialog second(s, s.recipes[0]);
  auto *t2 = second.findChild<QTableWidget *>(QStringLiteral("settingsTable"));
  for (int i = 0; i < t2->rowCount(); ++i) {
    if (t2->item(i, 0)->data(Qt::UserRole).toString() ==
        QLatin1String("window_months"))
      t2->item(i, 1)->setText(QStringLiteral("9"));
    if (t2->item(i, 0)->data(Qt::UserRole).toString() ==
        QLatin1String("ma_windows"))
      t2->item(i, 1)->setText(QStringLiteral("5, 9"));
    if (t2->item(i, 0)->data(Qt::UserRole).toString() ==
        QLatin1String("bollinger"))
      t2->item(i, 1)->setText(QStringLiteral("yes"));
  }
  second.accept();
  const Recipe edited = second.recipe();
  QCOMPARE(edited.revision, s.recipes[0].revision + 1);
  QCOMPARE(edited.settings.value("window_months").toInt(), 9);
  QCOMPARE(edited.settings.value("ma_windows").toList().size(), 2);
  QCOMPARE(edited.settings.value("ma_windows").toList()[1].toInt(), 9);
  QCOMPARE(edited.settings.value("bollinger"), QVariant(true));
  QCOMPARE(edited.settings.value("mc"), s.recipes[0].settings.value("mc"));

  // A classification chooses its features recipe from a list.
  RecipeDialog third(s, s.recipes[2]);
  auto *box = third.findChild<QComboBox *>(QStringLiteral("featuresRecipe"));
  QVERIFY(box);
  QCOMPARE(box->count(), 2); // none, and the study's one features recipe
  QCOMPARE(box->currentData().toString(), s.recipes[1].id);
  QCOMPARE(box->currentText(), s.recipes[1].name);
  QVERIFY(!box->toolTip().isEmpty());
  box->setCurrentIndex(0);
  third.accept();
  QCOMPARE(third.recipe().settings.value("features_recipe").toString(),
           QString());
  QCOMPARE(third.recipe().revision, s.recipes[2].revision + 1);

  // A features configuration with a catalog of its own: the study's is used
  // unless the override is kept, and an override is said in the dialog.
  const QString config = QStringLiteral(
      "{\"FREQMIN\": 1.0, \"chaos_calibration\": {\"catalog\": "
      "\"/elsewhere/own.csv\", \"min_mag\": 1.5}}");
  RecipeDialog fourth(s, s.recipes[1]);
  auto *said = fourth.findChild<QLabel *>(QStringLiteral("calibrationCatalog"));
  auto *clear =
      fourth.findChild<QPushButton *>(QStringLiteral("clearOverride"));
  QVERIFY(said && clear);
  fourth.show();
  QVERIFY(QTest::qWaitForWindowExposed(&fourth));
  QVERIFY(fourth.ownCatalog().isEmpty());
  QVERIFY(!clear->isVisible());
  QVERIFY(!fourth.loadText(QStringLiteral("no_such_setting"), config, true));
  QVERIFY(fourth.loadText(QStringLiteral("config_json"), config, false));
  QVERIFY(fourth.ownCatalog().isEmpty());
  QVERIFY(fourth.loadText(QStringLiteral("config_json"), config, true));
  QCOMPARE(fourth.ownCatalog(), QStringLiteral("/elsewhere/own.csv"));
  QVERIFY(said->text().contains(QStringLiteral("own.csv")));
  QVERIFY(clear->isVisible());
  shot(fourth, QStringLiteral("recipe_override"));
  clear->click();
  QVERIFY(fourth.ownCatalog().isEmpty());
  QVERIFY(!clear->isVisible());
  fourth.accept();
  // The rest of the configuration is kept.
  const QString kept =
      fourth.recipe().settings.value("config_json").toString();
  QVERIFY(kept.contains(QStringLiteral("min_mag")));
  QVERIFY(kept.contains(QStringLiteral("FREQMIN")));
  QVERIFY(!kept.contains(QStringLiteral("own.csv")));
  QVERIFY(calibrationOverride(fourth.recipe()).isEmpty());
}

namespace {

// One 512-byte miniSEED 2 record of 12 hours at 1 Hz (header only matters).
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

void writeDays(const QString &path, const QByteArray &sta,
               const QDateTime &first, int count) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  for (int d = 0; d < count; ++d)
    for (const char *cha : {"HHE", "HHN", "HHZ"})
      for (int half = 0; half < 2; ++half)
        f.write(halfDay(first.addDays(d).addSecs(half * 43200), sta, cha));
}

} // namespace

void TstStudyPages::waveformsOnCases() {
  // The user's own data, in a layout the application did not make: SYN1 in
  // its folder, SYN2 in a folder named for something else, and a note.
  const QString root = dir_.filePath("own-data");
  const QDateTime inA(QDate(2019, 3, 1), QTime(0, 0), QTimeZone::UTC);
  writeDays(root + "/SYN1/a.mseed", "SYN1", inA, 3);
  writeDays(root + "/SYN1/old.mseed", "SYN1", inA.addYears(-3), 2);
  writeDays(root + "/unsorted/x.dat", "SYN2",
            QDateTime(QDate(2019, 12, 1), QTime(0, 0), QTimeZone::UTC), 2);
  {
    QFile note(root + "/unsorted/readme.txt");
    QVERIFY(note.open(QIODevice::WriteOnly));
    note.write("not waveforms\n");
  }
  QVERIFY(store_->update([&root](Study &s) { s.addWaveRoot(root); }));

  Navigator nav;
  AppContext ctx = ctx_;
  ctx.nav = &nav;
  QSignalSpy went(&nav, &Navigator::requested);
  CasesPage page(ctx);
  page.resize(1100, 820);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *stations =
      page.findChild<QTableWidget *>(QStringLiteral("stationTable"));
  auto *summary = page.findChild<QLabel *>(QStringLiteral("waveSummary"));
  QVERIFY(stations && summary);
  // The page scans a folder it has no index of, in two passes, by itself.
  const QString studyDir = store_->study().dir;
  QTRY_VERIFY_WITH_TIMEOUT(WaveIndex::load(studyDir).hasStation("SYN2") &&
                               WaveIndex::load(studyDir).pendingCoverage() == 0,
                           20000);
  const auto rowOf = [stations](const QString &code) {
    for (int i = 0; i < stations->rowCount(); ++i)
      if (stations->item(i, 1)->text() == code)
        return i;
    return -1;
  };
  // Case A: 270 days before 2019-04-02; three of them are in the folder,
  // the 2016 file is not counted.
  QVERIFY(rowOf("SYN1") >= 0);
  QCOMPARE(stations->columnCount(), 5);
  QTRY_COMPARE(stations->item(rowOf("SYN1"), 4)->text(),
               QStringLiteral("found: 3 of 271 days in span  ⚠ little of the span"));
  QTRY_VERIFY2(summary->text().contains(QStringLiteral("2 station")),
               qPrintable(summary->text()));
  QVERIFY2(summary->text().contains(QStringLiteral("1 file")),
           qPrintable(summary->text())); // the note, skipped
  shot(page, QStringLiteral("cases_waveforms"));
  // Only the span's file would be read by a Features run of case A.
  const Study &s = store_->study();
  const QStringList files = waveFilesFor(
      s, *WaveIndex::shared(s.dir), s.cases[0].stations[0],
      s.cases[0].spanStart(), s.cases[0].time);
  QCOMPARE(files, QStringList{root + "/SYN1/a.mseed"});
  QCOMPARE(page.findChild<QPushButton *>(QStringLiteral("nextRecipes"))->text(),
           QStringLiteral("Next: recipes →"));
  // Case B's station is found although its folder is not named for it.
  QVERIFY(hasWaveforms(s, s.cases[1].stations[0]));

  // A download that lands in a folder the study already lists (the Data
  // page then saves the study without changing it): the page notices the
  // new file at its next refresh and reads just that one.
  writeDays(root + "/SYN1/later.mseed", "SYN1", inA.addDays(9), 2);
  QVERIFY(store_->update([&root](Study &st) { st.addWaveformRoot(root); }));
  QTRY_COMPARE_WITH_TIMEOUT(stations->item(rowOf("SYN1"), 4)->text(),
                            QStringLiteral("found: 5 of 271 days in span  ⚠ little of the span"),
                            20000);
  // Every station of the case has waveforms: recipes are the next step.
  QCOMPARE(page.findChild<QPushButton *>(QStringLiteral("nextRecipes"))->text(),
           QStringLiteral("Next: recipes →"));

  // "Get waveforms" hands the case's stations and span to the Data page.
  auto *get = page.findChild<QPushButton *>(QStringLiteral("getWaveforms"));
  auto *next = page.findChild<QPushButton *>(QStringLiteral("nextRecipes"));
  QVERIFY(get && next && get->isEnabled() && next->isEnabled());
  get->click();
  QCOMPARE(went.size(), 1);
  QCOMPARE(went[0][0].toString(), QStringLiteral("tdvms"));
  const QVariantMap args = went[0][1].toMap();
  QCOMPARE(args.value("stations").toStringList(), QStringList{"SYN1"});
  QCOMPARE(args.value("end").toString(), QStringLiteral("2019-04-02T14:00:00Z"));
  QCOMPARE(args.value("start").toString(),
           QStringLiteral("2018-07-06T14:00:00Z"));
  QCOMPARE(args.value("network").toString(), QStringLiteral("TU"));
  next->click();
  QCOMPARE(went.size(), 2);
  QCOMPARE(went[1][0].toString(), QStringLiteral("runs"));

  // A change that would make finished runs stale asks first; "no" keeps the
  // case and puts the field back.
  auto *span = page.findChild<QSpinBox *>(QStringLiteral("caseSpan"));
  QVERIFY(span);
  QCOMPARE(span->value(), 270);
  const auto saved = CasesPage::confirm;
  int asked = 0;
  bool answer = false;
  CasesPage::confirm = [&](QWidget *, const QString &, const QString &text) {
    ++asked;
    if (!text.contains(QStringLiteral("stale")))
      qWarning() << "unexpected question:" << text;
    return answer;
  };
  span->setValue(200);
  emit span->editingFinished();
  QCOMPARE(asked, 1);
  QCOMPARE(store_->study().cases[0].spanDays, 270);
  QCOMPARE(span->value(), 270);
  QVERIFY(CasesPage::runsMadeStale(store_->study(), store_->study(),
                                   store_->study().cases[0].id) == 0);
  answer = true;
  span->setValue(200);
  emit span->editingFinished();
  QCOMPARE(asked, 2);
  QCOMPARE(store_->study().cases[0].spanDays, 200);
  QTRY_COMPARE(stations->item(rowOf("SYN1"), 4)->text(),
               QStringLiteral("found: 5 of 201 days in span  ⚠ little of the span"));
  // Leaving a field without changing it saves nothing and asks nothing.
  emit span->editingFinished();
  QCOMPARE(asked, 2);
  CasesPage::confirm = saved;

  // The review dialog lists what the headers say.
  WaveScan scan(ctx);
  WaveformsDialog dialog(ctx, &scan);
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *found = dialog.findChild<QTableWidget *>(QStringLiteral("waveStations"));
  QVERIFY(found);
  QCOMPARE(found->rowCount(), 2);
  QCOMPARE(found->item(0, 0)->text(), QStringLiteral("SYN1"));
  QCOMPARE(found->item(0, 1)->text(), QStringLiteral("TU"));
  QCOMPARE(found->item(0, 2)->text(), QStringLiteral("HHE HHN HHZ"));
  QCOMPARE(found->item(0, 4)->text(), QStringLiteral("2016-03-01"));
  QCOMPARE(found->item(0, 5)->text(), QStringLiteral("2019-03-11"));
  QCOMPARE(found->item(0, 6)->text(), QStringLiteral("7"));
  QCOMPARE(found->item(0, 7)->text(), QStringLiteral("3"));
  // Neither is in the built-in list, and SYN2 sits in a folder named
  // differently: both are said.
  QVERIFY(found->item(0, 9)->text().contains(QStringLiteral("no coordinates")));
  QVERIFY(found->item(1, 9)->text().contains(QStringLiteral("unsorted")));
  shot(dialog, QStringLiteral("waveforms_dialog"));
  // Coordinates typed once make the station known to the study.
  QVERIFY(store_->update([](Study &st) {
    st.extraStations << ExtraStation{"SYN2", "TU", 38.4, 39.3, "typed"};
  }));
  QCOMPARE(found->item(1, 8)->text(), QStringLiteral("typed"));
  QVERIFY(!found->item(1, 9)->text().contains(QStringLiteral("no coordinates")));
  // Sent here from another page with a case and a station to fix: both get
  // selected; then back to the first case for what follows.
  {
    auto *cases = page.findChild<QTableWidget *>(QStringLiteral("caseTable"));
    QVERIFY(cases && cases->rowCount() >= 2);
    const QString second = store_->study().cases[1].id;
    nav.show(QStringLiteral("cases"), {{QStringLiteral("case"), second},
                                       {QStringLiteral("station"),
                                        QStringLiteral("SYN2")}});
    QCOMPARE(cases->currentRow(), 1);
    QVERIFY(rowOf("SYN2") >= 0);
    QCOMPARE(stations->currentRow(), rowOf("SYN2"));
    nav.show(QStringLiteral("cases"),
             {{QStringLiteral("case"), store_->study().cases[0].id}});
    QCOMPARE(cases->currentRow(), 0);
  }
  // A file that arrives after the user has moved between cases is still
  // picked up.
  writeDays(root + "/SYN1/last.mseed", "SYN1", inA.addDays(20), 2);
  QVERIFY(store_->update([&root](Study &st) { st.addWaveformRoot(root); }));
  QTRY_COMPARE_WITH_TIMEOUT(stations->item(rowOf("SYN1"), 4)->text(),
                            QStringLiteral("found: 7 of 201 days in span  ⚠ little of the span"),
                            20000);
}

void TstStudyPages::stationsWithData() {
  // The waveform folders of the test before this one hold SYN1 and SYN2.
  const Study &s = store_->study();
  QCOMPARE(stationsWithWaveforms(s), QStringList({"SYN1", "SYN2"}));
  {
    AddFromCatalogDialog dialog(s);
    auto *mine = dialog.findChild<QPushButton *>(QStringLiteral("nearMine"));
    auto *codes = dialog.findChild<QLineEdit *>(QStringLiteral("nearStations"));
    QVERIFY(mine && codes);
    QVERIFY(mine->isEnabled());
    QVERIFY(codes->text().isEmpty());
    mine->click();
    QCOMPARE(codes->text(), QStringLiteral("SYN1, SYN2"));
  }
  {
    // Opened "from stations", the filter starts with them.
    AddFromCatalogDialog dialog(s, nullptr, {QStringLiteral("SYN2")});
    QCOMPARE(dialog.findChild<QLineEdit *>(QStringLiteral("nearStations"))
                 ->text(),
             QStringLiteral("SYN2"));
  }
  // The Waveforms dialog hands the selected stations on.
  WaveScan scan(ctx_);
  WaveformsDialog dialog(ctx_, &scan);
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("waveStations"));
  auto *near = dialog.findChild<QPushButton *>(QStringLiteral("waveFindNear"));
  QVERIFY(table && near);
  QTRY_COMPARE(table->rowCount(), 2);
  QVERIFY(dialog.findNear().isEmpty());
  int syn2 = -1;
  for (int i = 0; i < table->rowCount(); ++i)
    if (table->item(i, 0)->text() == QLatin1String("SYN2"))
      syn2 = i;
  QVERIFY(syn2 >= 0);
  table->selectRow(syn2);
  near->click();
  QCOMPARE(dialog.findNear(), QStringList({"SYN2"}));
  QCOMPARE(dialog.result(), int(QDialog::Accepted));
  // With nothing selected it is every station.
  WaveformsDialog all(ctx_, &scan);
  QTRY_COMPARE(
      all.findChild<QTableWidget *>(QStringLiteral("waveStations"))->rowCount(),
      2);
  all.findChild<QPushButton *>(QStringLiteral("waveFindNear"))->click();
  QCOMPARE(all.findNear(), QStringList({"SYN1", "SYN2"}));
  // Adding a folder inside a listed one adds nothing and says so.
  const int before = int(store_->study().waveRoots.size());
  WaveformsDialog third(ctx_, &scan);
  third.addFolders({dir_.filePath("own-data/SYN1")});
  QCOMPARE(int(store_->study().waveRoots.size()), before);
  bool said = false;
  for (QLabel *l : third.findChildren<QLabel *>())
    said = said || l->text().contains(QStringLiteral("Already covered"));
  QVERIFY(said);
}

void TstStudyPages::catalogOffsetRereadsCaseTimes() {
  // The cases were picked while the catalog's times were read as UTC. Told
  // that they are Turkey time, the earthquake cases are the same events
  // three hours earlier; the reference date, typed in UTC, stays.
  CasesPage page(ctx_);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *offset = page.findChild<QComboBox *>(QStringLiteral("catalogTime"));
  QVERIFY(offset);
  QCOMPARE(offset->currentData().toDouble(), 0.0);
  const auto iso = [this](int i) {
    return store_->study().cases[i].time.toString(Qt::ISODate);
  };
  QCOMPARE(iso(0), QStringLiteral("2019-04-02T14:00:00Z"));
  QCOMPARE(iso(1), QStringLiteral("2019-12-30T22:10:00Z"));
  const QString reference = iso(2);
  QVERIFY(store_->study().cases[2].reference);
  const auto saved = CasesPage::confirm;
  int asked = 0;
  bool answer = false;
  CasesPage::confirm = [&](QWidget *, const QString &, const QString &) {
    ++asked;
    return answer;
  };
  // "No" changes nothing and puts the choice back.
  offset->setCurrentIndex(1);
  emit offset->activated(1);
  QCOMPARE(asked, 1);
  QCOMPARE(store_->study().catalogUtcOffsetHours, 0.0);
  QCOMPARE(offset->currentData().toDouble(), 0.0);
  QCOMPARE(iso(0), QStringLiteral("2019-04-02T14:00:00Z"));
  // "Yes": the offset is saved and the times are read from the catalog again.
  answer = true;
  offset->setCurrentIndex(1);
  emit offset->activated(1);
  QCOMPARE(asked, 2);
  QCOMPARE(store_->study().catalogUtcOffsetHours, 3.0);
  QCOMPARE(iso(0), QStringLiteral("2019-04-02T11:00:00Z"));
  QCOMPARE(iso(1), QStringLiteral("2019-12-30T19:10:00Z"));
  QCOMPARE(iso(2), reference);
  // The case is still the catalog's event: read with the new offset, the
  // catalog has an earthquake at exactly that time and place.
  const auto events = skg::analysis::loadEvents(
      store_->study().catalogPath(), store_->study().catalogUtcOffsetHours);
  const Case &a = store_->study().cases[0];
  QVERIFY(std::any_of(events.begin(), events.end(), [&a](const auto &e) {
    return std::abs(e.epoch - double(a.time.toSecsSinceEpoch())) < 1.0 &&
           std::abs(e.lat - a.lat) < 1e-3;
  }));
  // And back again.
  offset->setCurrentIndex(0);
  emit offset->activated(0);
  QCOMPARE(asked, 3);
  QCOMPARE(iso(0), QStringLiteral("2019-04-02T14:00:00Z"));
  QCOMPARE(iso(1), QStringLiteral("2019-12-30T22:10:00Z"));
  // A case that is not in the catalog keeps its time, and the page says so.
  QVERIFY(store_->update([](Study &s) { s.cases[1].lat += 1.0; }));
  offset->setCurrentIndex(1);
  emit offset->activated(1);
  QCOMPARE(iso(0), QStringLiteral("2019-04-02T11:00:00Z"));
  QCOMPARE(iso(1), QStringLiteral("2019-12-30T22:10:00Z"));
  auto *banner = page.findChild<QLabel *>(QStringLiteral("banner"));
  QVERIFY(banner && banner->isVisible());
  QVERIFY2(banner->text().contains(QStringLiteral("1 earthquake case")),
           qPrintable(banner->text()));
  // An offset that is neither of the two usual ones is shown as it is.
  QVERIFY(store_->update([](Study &s) { s.catalogUtcOffsetHours = 2.0; }));
  QCOMPARE(offset->currentData().toDouble(), 2.0);
  QVERIFY2(offset->currentText().contains(QStringLiteral("+2")),
           qPrintable(offset->currentText()));
  CasesPage::confirm = saved;
}

void TstStudyPages::comparePage() {
  ComparePage page(ctx_);
  page.resize(1100, 800);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *scores = page.findChild<ScoreTable *>();
  auto *plot = page.findChild<SeriesPlot *>();
  auto *column = page.findChild<QComboBox *>(QStringLiteral("seriesColumn"));
  auto *series = page.findChild<QComboBox *>(QStringLiteral("seriesRecipe"));
  auto *fitted = page.findChild<QTableWidget *>(QStringLiteral("fittedTable"));
  QVERIFY(scores && plot && column && series && fitted);
  // The runs are read on a worker thread: the page is there at once, the
  // numbers a moment later.
  QTRY_VERIFY_WITH_TIMEOUT(!page.loading(), 30000);
  QCOMPARE(scores->rows().size(), 3);
  for (const ScoreRow &r : scores->rows()) {
    QCOMPARE(r.state, CellState::Done);
    QVERIFY(r.testF1 > 0.5 && r.testF1 <= 1.0);
    QCOMPARE(r.baselineModel, QStringLiteral("logistic"));
  }
  // The first series recipe is the indicators one; b_value is preselected.
  QCOMPARE(series->count(), 2);
  QCOMPARE(column->currentText(), QStringLiteral("b_value"));
  QVERIFY(!plot->isEmpty());
  QCOMPARE(fitted->rowCount(), 3);
  shot(page, QStringLiteral("compare"));
  // The features recipe: a column per day, and its fitted values.
  series->setCurrentIndex(1);
  emit series->activated(1);
  QVERIFY(page.loading());
  QTRY_VERIFY_WITH_TIMEOUT(!page.loading(), 30000);
  QCOMPARE(column->count(), 2);
  QVERIFY(!plot->isEmpty());
  QCOMPARE(fitted->columnCount(), 4); // Case, Station, m, tau
  shot(page, QStringLiteral("compare_features"));
  // The plot also draws into an SVG (the export path).
  QSvgGenerator svg;
  svg.setFileName(dir_.filePath("plot.svg"));
  svg.setSize(QSize(960, 540));
  {
    QPainter p(&svg);
    plot->paintPlot(p, QRectF(0, 0, 960, 540));
  }
  QVERIFY(QFileInfo(dir_.filePath("plot.svg")).size() > 2000);
}

void TstStudyPages::compareExportsAndKeys() {
  ComparePage page(ctx_);
  page.resize(1100, 800);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  QTRY_VERIFY_WITH_TIMEOUT(!page.loading(), 30000);
  auto *scores = page.findChild<ScoreTable *>();
  auto *plot = page.findChild<SeriesPlot *>();
  QVERIFY(scores && plot && !plot->isEmpty());

  // Exports go to the study's exports folder by default.
  const QString exports = page.exportDir();
  QCOMPARE(exports, QDir(store_->study().dir).filePath("exports"));
  QVERIFY(QDir(exports).exists());
  QString error;
  const QString csv = QDir(exports).filePath("scores.csv");
  QVERIFY2(page.writeTable(csv, &error), qPrintable(error));
  QFile table(csv);
  QVERIFY(table.open(QIODevice::ReadOnly));
  QCOMPARE(table.readAll().split('\n').size(), 5); // heading, 3 rows, end

  // The figure is light whatever the theme, as a PNG and as an SVG.
  theme_->setMode(Theme::Mode::Dark);
  const QString png = QDir(exports).filePath("series.png");
  QVERIFY2(page.writeFigure(png, &error), qPrintable(error));
  const QImage img(png);
  QCOMPARE(img.size(), QSize(1920, 1080));
  QCOMPARE(img.pixelColor(2, 2), QColor(Qt::white));
  QVERIFY(img.pixelColor(300, 300).lightness() > 230); // the plot's ground
  const QString svgPath = QDir(exports).filePath("series.svg");
  QVERIFY2(page.writeFigure(svgPath, &error), qPrintable(error));
  QFile svg(svgPath);
  QVERIFY(svg.open(QIODevice::ReadOnly));
  const QByteArray svgText = svg.readAll();
  QVERIFY(svgText.size() > 2000);
  QVERIFY(svgText.contains("#ffffff") || svgText.contains("#FFFFFF"));
  QVERIFY(!svgText.contains("#1a2029") && !svgText.contains("#1A2029"));
  theme_->setMode(Theme::Mode::Light);
  // A file that cannot be written is an error with the path, for both.
  error.clear();
  QVERIFY(!page.writeFigure(dir_.filePath("no/such/folder/x.svg"), &error));
  QVERIFY2(error.contains(QStringLiteral("x.svg")), qPrintable(error));
  error.clear();
  QVERIFY(!page.writeFigure(dir_.filePath("no/such/folder/x.png"), &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(!page.writeTable(dir_.filePath("no/such/folder/x.csv"), &error));

  // The score table by keyboard: Right chooses the sort column, Space
  // turns the order, Down chooses a row, Enter opens it.
  QSignalSpy opened(scores, &ScoreTable::rowActivated);
  scores->setFocus();
  QCOMPARE(scores->sortColumn(), -1);
  QTest::keyClick(scores, Qt::Key_Right);
  QCOMPARE(scores->sortColumn(), 0);
  QCOMPARE(scores->rows()[0].caseName, QStringLiteral("Reference 2021"));
  QTest::keyClick(scores, Qt::Key_Space);
  QCOMPARE(scores->rows()[0].caseName, QStringLiteral("Synthetic B 2019"));
  QTest::keyClick(scores, Qt::Key_Right);
  QCOMPARE(scores->sortColumn(), 2); // magnitude, largest first
  QCOMPARE(scores->rows()[0].magnitude, 6.1);
  QCOMPARE(scores->currentRow(), -1);
  QTest::keyClick(scores, Qt::Key_Return);
  QCOMPARE(opened.size(), 0);
  QTest::keyClick(scores, Qt::Key_Down);
  QTest::keyClick(scores, Qt::Key_Down);
  QCOMPARE(scores->currentRow(), 1);
  QTest::qWait(100); // the layout settles after the two changes of theme
  shot(page, QStringLiteral("compare_keys"));
}

// More series than colours, a run that cannot be read, and the pages a
// user sees before there is anything to compare.
void TstStudyPages::compareManySeriesAndEmptyStates() {
  QSettings settings(dir_.filePath("s3.ini"), QSettings::IniFormat);
  StudyStore store(&settings);
  Navigator nav;
  AppContext ctx = ctx_;
  ctx.study = &store;
  ctx.nav = &nav;
  QSignalSpy went(&nav, &Navigator::requested);
  ComparePage page(ctx);
  page.resize(1100, 800);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *stack = page.findChild<QStackedWidget *>();
  QVERIFY(stack);
  QCOMPARE(stack->currentIndex(), 0);

  // A study with nothing to score yet: the page says what is missing and
  // has a button to the page where it is done.
  const QString folder = dir_.filePath("many");
  QVERIFY(store.create(folder, QStringLiteral("Many")));
  QTRY_VERIFY_WITH_TIMEOUT(!page.loading(), 30000);
  auto *hint = page.findChild<QLabel *>(QStringLiteral("scoresHint"));
  auto *go = page.findChild<QPushButton *>(QStringLiteral("scoresGo"));
  QVERIFY(hint && go);
  QVERIFY(hint->text().contains(QStringLiteral("No classification recipe")));
  QVERIFY(go->isVisible());
  go->click();
  QCOMPARE(went.size(), 1);
  QCOMPARE(went[0][0].toString(), QStringLiteral("runs"));

  // Ten cases with an indicators run each, copied from the first study's
  // first case (the same earthquake under ten names); one run's table is
  // then taken away.
  const Study &from = store_->study();
  const Recipe ind = from.recipes[0];
  const QString source = runDir(from, from.cases[0], QString(), ind);
  QVERIFY(QFile::copy(from.catalogPath(), QDir(folder).filePath("catalog.csv")));
  QVERIFY(store.update([&](Study &st) {
    st.catalog = QStringLiteral("catalog.csv");
    st.recipes << ind;
    for (int i = 0; i < 10; ++i) {
      Case c = from.cases[0];
      c.name = QStringLiteral("Copy %1").arg(i);
      c.id = st.freeCaseId(c.name);
      st.cases << c;
    }
  }));
  const Study &s = store.study();
  QString brokenTable;
  for (const Case &c : s.cases) {
    const QString dir = runDir(s, c, QString(), ind);
    QDirIterator it(source, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString file = it.next();
      const QString target =
          QDir(dir).filePath(QDir(source).relativeFilePath(file));
      QVERIFY(QDir().mkpath(QFileInfo(target).path()));
      QVERIFY(QFile::copy(file, target));
    }
    RunRecord rec = *RunRecord::read(dir);
    rec.caseId = c.id;
    rec.caseSignature = runSignature(s, c, QString(), kToolIndicators);
    QVERIFY(rec.write(dir));
    QCOMPARE(cellOf(s, c, QString(), ind).state, CellState::Done);
    brokenTable = QDir(dir).filePath(rec.outputs.value("table").toString());
  }
  QVERIFY(QFile::remove(brokenTable)); // the last case's
  QVERIFY(store.update([](Study &) {}));
  QTRY_VERIFY_WITH_TIMEOUT(!page.loading(), 30000);

  // Nine series can be read; eight are drawn, and which is the user's
  // choice. The tenth is named with the reason, not dropped silently.
  auto *plot = page.findChild<SeriesPlot *>();
  auto *pick = page.findChild<QToolButton *>(QStringLiteral("seriesPick"));
  auto *note = page.findChild<QLabel *>(QStringLiteral("seriesNote"));
  QVERIFY(plot && pick && note);
  QCOMPARE(plot->seriesCount(), 8);
  QVERIFY(pick->isVisible());
  QVERIFY2(pick->text().contains(QStringLiteral("8 of 9")),
           qPrintable(pick->text()));
  QVERIFY(note->isVisible());
  QVERIFY2(note->text().contains(QStringLiteral("Copy 9")),
           qPrintable(note->text()));
  shot(page, QStringLiteral("compare_many"));
  emit pick->menu()->aboutToShow();
  QList<QAction *> actions = pick->menu()->actions();
  QCOMPARE(actions.size(), 9);
  QVERIFY(actions[0]->isChecked());
  QVERIFY(!actions[8]->isChecked());
  QVERIFY(!actions[8]->isEnabled()); // eight are drawn already
  actions[0]->setChecked(false);
  QCOMPARE(plot->seriesCount(), 7);
  emit pick->menu()->aboutToShow();
  actions = pick->menu()->actions();
  QVERIFY(actions[8]->isEnabled());
  actions[8]->setChecked(true);
  QCOMPARE(plot->seriesCount(), 8);
  QVERIFY(pick->text().contains(QStringLiteral("8 of 9")));
  // The choice survives a new read of the runs.
  QVERIFY(store.update([](Study &) {}));
  QTRY_VERIFY_WITH_TIMEOUT(!page.loading(), 30000);
  emit pick->menu()->aboutToShow();
  QVERIFY(!pick->menu()->actions()[0]->isChecked());
  QVERIFY(pick->menu()->actions()[8]->isChecked());

  // The plot itself never draws more than its eight colours.
  QList<Series> twelve;
  for (int i = 0; i < 12; ++i) {
    Series one;
    one.caseId = QString::number(i);
    one.caseName = one.caseId;
    one.daysBefore = {-2, -1};
    one.values = {1, 2};
    twelve << one;
  }
  SeriesPlot bare(theme_.get());
  bare.setSeries(twelve, QStringLiteral("x"), 10, {});
  QCOMPARE(bare.seriesCount(), 8);
  store.close();
  QCOMPARE(stack->currentIndex(), 0);
}

// Cells asked for while every slot is taken wait in a queue and say so,
// whether they were started one by one or by "Run missing and stale"; a
// queued cell can be taken out again, and Stop empties the queue.
void TstStudyPages::runsQueue() {
  StudyStore store(settings_.get());
  const QString folder = dir_.filePath("queue-study");
  QVERIFY(store.create(folder, QStringLiteral("Queue")));
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
    // With figures: a run that takes a while.
    Recipe ind = defaultRecipe(kToolIndicators);
    ind.name = QStringLiteral("Indicators");
    ind.id = s.freeRecipeId(ind.name);
    ind.settings.insert(QStringLiteral("window_months"), 6);
    s.recipes << ind;
  }));
  const Study &s = store.study();
  const Recipe &ind = s.recipes[0];
  AppContext ctx = ctx_;
  ctx.study = &store;
  // One at a time, as the Settings page would set it.
  settings_->setValue(QLatin1String(defaults::kRunsAtOnceSetting), 1);
  const auto key = [&](int i) {
    return s.cases[i].id + QStringLiteral("||") + ind.id;
  };

  RunsPage page(ctx);
  page.resize(1100, 760);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  QCOMPARE(page.atOnce(), 1);
  auto *matrix = page.findChild<RunMatrix *>();
  auto *runAll = page.findChild<QPushButton *>(QStringLiteral("runAll"));
  auto *stop = page.findChild<QPushButton *>(QStringLiteral("stopBatch"));
  auto *runOne = page.findChild<QPushButton *>(QStringLiteral("runOne"));
  auto *info = page.findChild<QLabel *>(QStringLiteral("cellInfo"));
  auto *guide = page.findChild<QLabel *>(QStringLiteral("runsGuide"));
  QVERIFY(matrix && runAll && stop && runOne && info && guide);
  const int jobsBefore = jobs_->rowCount();
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);

  // Three cells started one by one: the first runs, the others wait their
  // turn, and say so (not "Missing").
  for (int i = 0; i < 3; ++i) {
    matrix->select(i, 0);
    QVERIFY(runOne->isEnabled());
    runOne->click();
  }
  QCOMPARE(jobs_->rowCount(), jobsBefore + 1);
  QCOMPARE(page.queued(), (QStringList{key(1), key(2)}));
  matrix->select(1, 0);
  QVERIFY2(info->text().contains(QStringLiteral("Queued, number 1 of 2")),
           qPrintable(info->text()));
  QVERIFY(!info->text().contains(QStringLiteral("Missing")));
  QCOMPARE(runOne->text(), QStringLiteral("Take out of the queue"));
  matrix->select(2, 0);
  QVERIFY2(info->text().contains(QStringLiteral("Queued, number 2 of 2")),
           qPrintable(info->text()));
  QVERIFY2(guide->isVisible() &&
               guide->text().contains(QStringLiteral("1 running, 2 queued")),
           qPrintable(guide->text()));
  QVERIFY(stop->isEnabled());
  // What the matrix draws and says on hover.
  MatrixCell drawn;
  drawn.state = CellState::Missing;
  drawn.queued = true;
  drawn.place = 2;
  QVERIFY(RunMatrix::stateText(drawn).contains(QStringLiteral("Queued")));
  QVERIFY(RunMatrix::cellTip(drawn).contains(
      QStringLiteral("Queued, number 2 in the queue")));
  drawn.place = 0;
  drawn.waitsFor = QStringLiteral("Features · SVRC");
  QVERIFY(RunMatrix::cellTip(drawn).contains(
      QStringLiteral("waiting for Features · SVRC")));
  shot(page, QStringLiteral("runs_queued"));

  // A queued cell taken out again is what it was before.
  matrix->select(1, 0);
  runOne->click();
  QCOMPARE(page.queued(), QStringList{key(2)});
  QVERIFY2(info->text().contains(QStringLiteral("Missing")),
           qPrintable(info->text()));
  QCOMPARE(runOne->text(), QStringLiteral("Run the selected cell"));

  // Stop: the queue is emptied, the running cell is cancelled, and none of
  // them has a run.
  stop->click();
  QVERIFY(page.queued().isEmpty());
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 120000);
  QTest::qWait(300);
  QCOMPARE(jobs_->rowCount(), jobsBefore + 1); // nothing started behind it
  for (int i = 0; i < 4; ++i)
    QCOMPARE(cellOf(s, s.cases[i], QString(), ind).state, CellState::Missing);
  matrix->select(2, 0);
  QVERIFY2(info->text().contains(QStringLiteral("Missing")),
           qPrintable(info->text()));

  // "Run missing and stale" with two at a time: two run, two are queued,
  // and each starts as a slot comes free.
  settings_->setValue(QLatin1String(defaults::kRunsAtOnceSetting), 2);
  QCOMPARE(page.atOnce(), 2);
  QTRY_VERIFY_WITH_TIMEOUT(runAll->isEnabled() &&
                               runAll->text().contains(QStringLiteral("(4)")),
                           20000);
  runAll->click();
  QCOMPARE(jobs_->rowCount(), jobsBefore + 3);
  QCOMPARE(page.queued(), (QStringList{key(2), key(3)}));
  QVERIFY2(guide->text().contains(QStringLiteral("2 running, 2 queued")),
           qPrintable(guide->text()));
  int most = 0;
  while (jobs_->runningCount() > 0 || !page.queued().isEmpty()) {
    most = std::max(most, jobs_->runningCount());
    QTest::qWait(50);
    QVERIFY(most <= 2);
  }
  QCOMPARE(jobs_->rowCount(), jobsBefore + 5);
  QTest::qWait(300);
  for (int i = 0; i < 4; ++i)
    QCOMPARE(cellOf(s, s.cases[i], QString(), ind).state, CellState::Done);
  QVERIFY(!page.batchRunning());
  settings_->remove(QLatin1String(defaults::kRunsAtOnceSetting));
  QCOMPARE(page.atOnce(), defaults::kRunsAtOnce);
}

void TstStudyPages::cleanupTestCase() {
  // With SKG_SHOTS, the study is kept beside the pictures so the whole
  // application can be opened on it.
  const QString keep = qEnvironmentVariable("SKG_SHOTS");
  if (keep.isEmpty())
    return;
  const QString from = store_->study().dir;
  const QString to = QDir(keep).filePath(QStringLiteral("study"));
  QDir(to).removeRecursively();
  QDirIterator it(from, QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString file = it.next();
    const QString target = QDir(to).filePath(QDir(from).relativeFilePath(file));
    QDir().mkpath(QFileInfo(target).path());
    QFile::copy(file, target);
  }
}

QTEST_MAIN(TstStudyPages)
#include "tst_study_pages.moc"
