// The study pages on a small synthetic study: they build, show the study,
// and survive an edit. With SKG_SHOTS set to a folder, each page is also
// saved there as a PNG (used when changing the look).
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QFileInfo>
#include <QTimeZone>

#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSettings>
#include <QSvgGenerator>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "core/module.h"
#include "core/theme.h"
#include "fx/npy.h"
#include "jobs/job.h"
#include "jobs/jobmodel.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casespage.h"
#include "modules/study/comparepage.h"
#include "modules/study/runspage.h"
#include "study/compare.h"
#include "study/runs.h"
#include "study/store.h"
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
  void recipeDialog();
  void comparePage();
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
  QVERIFY(near && near->rowCount() > 0);
  near->item(0, 0)->setCheckState(Qt::Checked);
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
    rec.caseSignature = caseSignature(c, code);
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
