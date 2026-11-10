// The study pages on a small synthetic study: they build, show the study,
// and survive an edit. With SKG_SHOTS set to a folder, each page is also
// saved there as a PNG (used when changing the look).
#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include "core/module.h"
#include "core/theme.h"
#include "jobs/jobmodel.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casespage.h"
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

} // namespace

class TstStudyPages : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void casesPage();
  void addFromCatalog();
  void referenceDate();

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

QTEST_MAIN(TstStudyPages)
#include "tst_study_pages.moc"
