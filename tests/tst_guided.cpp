// The guided flow between pages: the Data page takes what a case needs from
// the Cases page and tells the study where the downloads went; the tool pages
// take their inputs from the open study and follow it; a tool page's
// settings become a study recipe.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

#include "core/module.h"
#include "core/theme.h"
#include "jobs/jobmodel.h"
#include "modules/features/featuresmodule.h"
#include "modules/indicators/indicatorsmodule.h"
#include "modules/study/studyswitcher.h"
#include "modules/tdvms/plandialog.h"
#include "modules/tdvms/tdvmsmodule.h"
#include "study/runs.h"
#include "study/store.h"
#include "studyui/studyinputs.h"
#include "tdvms/campaign.h"
#include "tdvms/fdsn.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;

static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

Case caseAt(const QString &id, const QString &time, double lat, double lon,
            const QStringList &stations) {
  Case c;
  c.id = id;
  c.name = id.toUpper();
  c.time = QDateTime::fromString(time, Qt::ISODate);
  c.lat = lat;
  c.lon = lon;
  c.magnitude = 6.0;
  c.zone.radiusKm = 80.0;
  for (const QString &code : stations) {
    StationRef st;
    st.code = code;
    st.lat = lat + 0.1;
    st.lon = lon + 0.2;
    c.stations << st;
  }
  return c;
}

// A campaign as the Data page starts one: with its (empty) ledger file,
// which is what makes it show up in the campaign list.
tdvms::Campaign campaignIn(const QString &root) {
  const tdvms::Campaign c =
      tdvms::newCampaign(root, QStringLiteral("c1"), QString());
  QFile ledger(c.ledgerPath);
  ledger.open(QIODevice::WriteOnly);
  return c;
}

template <class T> T *child(QWidget &w, const char *name) {
  return w.findChild<T *>(QLatin1String(name));
}

} // namespace

class TstGuided : public QObject {
  Q_OBJECT
private slots:
  void init();
  void cleanup();
  void dataPageTakesWhatACaseNeeds();
  void fdsnFetchNeedsValidInput();
  void aDownloadFolderBecomesAWaveformRoot();
  void planDialogStartsOnTheCaseAndValidates();
  void featuresFollowTheStudy();
  void indicatorsFollowTheStudy();
  void pageSettingsBecomeARecipe();
  void movedInputsAreAskedForOnce();

private:
  bool makeStudy(const QString &name, const QString &station);

  std::unique_ptr<QTemporaryDir> dir_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<JobModel> jobs_;
  std::unique_ptr<ToolRegistry> tools_;
  std::unique_ptr<StudyStore> store_;
  std::unique_ptr<Navigator> nav_;
  AppContext ctx_;
};

void TstGuided::init() {
  initResources();
  dir_ = std::make_unique<QTemporaryDir>();
  settings_ = std::make_unique<QSettings>(dir_->filePath("s.ini"),
                                          QSettings::IniFormat);
  settings_->setValue(QStringLiteral("tdvms/root"), dir_->filePath("dl"));
  theme_ = std::make_unique<Theme>(settings_.get());
  jobs_ = std::make_unique<JobModel>();
  tools_ = std::make_unique<ToolRegistry>(settings_.get());
  store_ = std::make_unique<StudyStore>(settings_.get());
  nav_ = std::make_unique<Navigator>();
  ctx_ = AppContext{jobs_.get(), tools_.get(), settings_.get(), theme_.get(),
                    store_.get(), nav_.get()};
}

void TstGuided::cleanup() {
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 20000);
  store_.reset();
  jobs_.reset();
}

bool TstGuided::makeStudy(const QString &name, const QString &station) {
  const QString folder = dir_->filePath(name);
  if (!store_->create(folder, name))
    return false;
  if (!QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                   QDir(folder).filePath("catalog.csv")))
    return false;
  QDir().mkpath(QDir(folder).filePath("mseed/" + station));
  return store_->update([&](Study &s) {
    s.catalog = QStringLiteral("catalog.csv");
    s.catalogUtcOffsetHours = 3.0;
    s.addWaveformRoot(QDir(s.dir).filePath("mseed"));
    s.cases << caseAt(QStringLiteral("a"),
                      QStringLiteral("2019-04-02T14:00:00Z"), 38.1, 38.5,
                      {station, QStringLiteral("ZZZ")});
    s.cases << caseAt(QStringLiteral("b"),
                      QStringLiteral("2020-06-01T03:00:00Z"), 39.4, 40.7,
                      {QStringLiteral("BBB")});
  });
}

void TstGuided::dataPageTakesWhatACaseNeeds() {
  // With no study open: the bar that offers the two forms. (With a study the
  // guided panel takes the case; tst_data_guided covers that.)
  campaignIn(dir_->filePath("dl"));
  TdvmsWidget page(ctx_);
  auto *bar = child<QWidget>(page, "wantedBar");
  auto *station = child<QLineEdit>(page, "fdsnStation");
  auto *network = child<QLineEdit>(page, "fdsnNetwork");
  auto *start = child<QDateEdit>(page, "fdsnStart");
  auto *end = child<QDateEdit>(page, "fdsnEnd");
  auto *which = child<QComboBox>(page, "wantedStation");
  QVERIFY(bar && station && network && start && end && which);
  QVERIFY(bar->isHidden());
  // Another page's request is not this page's.
  nav_->show(QStringLiteral("runs"),
             {{QStringLiteral("stations"), QStringList{QStringLiteral("X")}}});
  QVERIFY(bar->isHidden());

  // What the Cases page sends for a case: its stations and its span.
  nav_->show(QStringLiteral("tdvms"),
             {{QStringLiteral("stations"),
               QStringList{QStringLiteral("AAA"), QStringLiteral("BBB")}},
              {QStringLiteral("start"), QStringLiteral("2019-01-01T14:00:00Z")},
              {QStringLiteral("end"), QStringLiteral("2019-04-02T14:00:00Z")},
              {QStringLiteral("network"), QStringLiteral("TU")},
              {QStringLiteral("case"), QStringLiteral("Case A")}});
  QVERIFY(!bar->isHidden());
  QCOMPARE(which->count(), 2);
  QCOMPARE(station->text(), QStringLiteral("AAA"));
  QCOMPARE(network->text(), QStringLiteral("TU"));
  QCOMPARE(start->date(), QDate(2019, 1, 1));
  // Whole days: the day of the event is fetched, so the day after it ends.
  QCOMPARE(end->date(), QDate(2019, 4, 3));
  QVERIFY(child<QLabel>(page, "wantedText")
              ->text()
              .contains(QStringLiteral("Case A")));
  // The next station of the case.
  which->setCurrentIndex(1);
  QCOMPARE(station->text(), QStringLiteral("BBB"));
}

void TstGuided::fdsnFetchNeedsValidInput() {
  TdvmsWidget noCampaign(ctx_);
  auto *go0 = child<QPushButton>(noCampaign, "fdsnGo");
  QVERIFY(go0 && !go0->isEnabled());
  QVERIFY(!child<QLabel>(noCampaign, "fdsnWhy")->text().isEmpty());

  campaignIn(dir_->filePath("dl"));
  TdvmsWidget page(ctx_);
  auto *go = child<QPushButton>(page, "fdsnGo");
  auto *why = child<QLabel>(page, "fdsnWhy");
  auto *station = child<QLineEdit>(page, "fdsnStation");
  auto *start = child<QDateEdit>(page, "fdsnStart");
  auto *end = child<QDateEdit>(page, "fdsnEnd");
  QVERIFY(go && why && station && start && end);
  // No station yet: the button says why it cannot be pressed.
  QVERIFY(!go->isEnabled());
  QVERIFY(!why->text().isEmpty());
  station->setText(QStringLiteral("SVRC"));
  QVERIFY(go->isEnabled());
  QVERIFY(why->text().isEmpty());
  end->setDate(start->date());
  QVERIFY(!go->isEnabled());
  QVERIFY(!why->text().isEmpty());
  end->setDate(start->date().addDays(2));
  QVERIFY(go->isEnabled());
}

void TstGuided::aDownloadFolderBecomesAWaveformRoot() {
  const tdvms::Campaign c = campaignIn(dir_->filePath("dl"));
  QVERIFY(makeStudy(QStringLiteral("one"), QStringLiteral("AAA")));
  const QStringList before = store_->study().waveformRoots();
  QCOMPARE(before.size(), 1);
  TdvmsWidget page(ctx_);
  child<QLineEdit>(page, "fdsnStation")->setText(QStringLiteral("SVRC"));
  child<QDateEdit>(page, "fdsnStart")->setDate(QDate(2020, 1, 1));
  child<QDateEdit>(page, "fdsnEnd")->setDate(QDate(2020, 1, 4));
  // "Only add the days" plans without touching the network.
  child<QCheckBox>(page, "fdsnPlanOnly")->setChecked(true);
  auto *go = child<QPushButton>(page, "fdsnGo");
  QVERIFY(go->isEnabled());
  go->click();
  const QString out = tdvms::fdsnDefaultOut(c);
  QCOMPARE(store_->study().waveformRoots(), before + QStringList{out});
  QTRY_COMPARE_WITH_TIMEOUT(jobs_->runningCount(), 0, 20000);
  QCOMPARE(int(jobs_->jobAt(0)->state()), int(Job::State::Done));
  // A second pull into the same folder adds nothing.
  QVERIFY(!page.registerDownloadFolder(out));
  // The campaign's own archive folder, as a TDVMS run registers it.
  QVERIFY(page.registerDownloadFolder(c.raw));
  QCOMPARE(store_->study().waveformRoots().size(), 3);
  // It survives a reload of the study file.
  QCOMPARE(Study::load(store_->study().dir).waveformRoots(),
           store_->study().waveformRoots());
  // With no study open nothing is registered, and nothing breaks.
  store_->close();
  QVERIFY(!page.registerDownloadFolder(dir_->filePath("elsewhere")));
}

void TstGuided::planDialogStartsOnTheCaseAndValidates() {
  const tdvms::Campaign c = campaignIn(dir_->filePath("dl"));
  PlanDialog dlg(c.ledgerPath);
  QVERIFY(!dlg.blockedReason().isEmpty()); // no station yet
  dlg.prefill(QStringLiteral("QQQQ"), QDate(2019, 1, 1), QDate(2019, 4, 3));
  QVERIFY(dlg.blockedReason().isEmpty());
  const PlanRequest r = dlg.request();
  QCOMPARE(r.station, QStringLiteral("QQQQ"));
  QCOMPARE(r.startIso, QStringLiteral("2019-01-01"));
  QCOMPARE(r.endIso, QStringLiteral("2019-04-03"));
  dlg.prefill(QStringLiteral("QQQQ"), QDate(2019, 4, 3), QDate(2019, 4, 3));
  QVERIFY(!dlg.blockedReason().isEmpty());
}

void TstGuided::featuresFollowTheStudy() {
  QVERIFY(makeStudy(QStringLiteral("one"), QStringLiteral("AAA")));
  FeaturesWidget page(ctx_);
  auto *use = child<QCheckBox>(page, "useStudy");
  auto *cases = child<QComboBox>(page, "studyCase");
  auto *stations = child<QComboBox>(page, "studyStation");
  auto *bar = child<QWidget>(page, "studyInputs");
  QVERIFY(use && cases && stations && bar);
  QVERIFY(!bar->isHidden());
  QVERIFY(use->isChecked());
  QCOMPARE(cases->count(), 2);
  QCOMPARE(stations->count(), 2);
  const QString one = store_->study().dir;
  // The first case's first station: its folder, code and position, and the
  // study's catalog, without anything being typed.
  QJsonObject cfg = QJsonDocument::fromJson(page.recipeSettings()
                                                .value("config_json")
                                                .toString()
                                                .toUtf8())
                        .object();
  QCOMPARE(cfg.value("station").toString(), QStringLiteral("AAA"));
  QCOMPARE(page.findChildren<QLineEdit *>().isEmpty(), false);
  bool sawFolder = false;
  for (const QLineEdit *e : page.findChildren<QLineEdit *>())
    sawFolder = sawFolder || e->text() == one + "/mseed/AAA";
  QVERIFY(sawFolder);
  // Another case: its station.
  cases->setCurrentIndex(1);
  cfg = QJsonDocument::fromJson(
            page.recipeSettings().value("config_json").toString().toUtf8())
            .object();
  QCOMPARE(cfg.value("station").toString(), QStringLiteral("BBB"));
  // The recipe leaves out what belongs to one station or to the study.
  const QJsonObject cal = cfg.value("calibration").toObject();
  QVERIFY(cal.value("catalog").toString().isEmpty());
  QVERIFY(cfg.value("data_dir").toString().isEmpty());

  // Another study is opened: the page follows it.
  QVERIFY(makeStudy(QStringLiteral("two"), QStringLiteral("CCC")));
  QCOMPARE(stations->itemText(0), QStringLiteral("CCC"));
  sawFolder = false;
  for (const QLineEdit *e : page.findChildren<QLineEdit *>())
    sawFolder = sawFolder || e->text() == store_->study().dir + "/mseed/CCC";
  QVERIFY(sawFolder);
  // Unticked, the page keeps what it has and stops following.
  use->setChecked(false);
  QVERIFY(store_->open(one));
  sawFolder = false;
  for (const QLineEdit *e : page.findChildren<QLineEdit *>())
    sawFolder = sawFolder || e->text() == one + "/mseed/AAA";
  QVERIFY(!sawFolder);
  // No study: the bar goes away.
  store_->close();
  QVERIFY(bar->isHidden());
}

void TstGuided::indicatorsFollowTheStudy() {
  QVERIFY(makeStudy(QStringLiteral("one"), QStringLiteral("AAA")));
  IndicatorsWidget page(ctx_);
  auto *cases = child<QComboBox>(page, "studyCase");
  QVERIFY(cases);
  const auto has = [&](const QString &text) {
    for (const QLineEdit *e : page.findChildren<QLineEdit *>())
      if (e->text() == text)
        return true;
    return false;
  };
  const auto spinWith = [&](double v) {
    for (const QDoubleSpinBox *s : page.findChildren<QDoubleSpinBox *>())
      if (qFuzzyCompare(s->value(), v))
        return true;
    return false;
  };
  QVERIFY(has(store_->study().catalogPath()));
  // The case's circle: its epicentre and radius.
  QVERIFY(spinWith(38.1) && spinWith(38.5) && spinWith(80.0));
  cases->setCurrentIndex(1);
  QVERIFY(spinWith(39.4) && spinWith(40.7));
  // A change made on the Cases page arrives here.
  QVERIFY(store_->update([](Study &s) { s.cases[1].zone.radiusKm = 55.0; }));
  QVERIFY(spinWith(55.0));
  QVERIFY(makeStudy(QStringLiteral("two"), QStringLiteral("CCC")));
  QVERIFY(has(store_->study().catalogPath()));
}

void TstGuided::pageSettingsBecomeARecipe() {
  // Without a study there is nowhere to save it.
  QString error;
  QVERIFY(StudyInputsBar::addRecipe(store_.get(), kToolIndicators,
                                    QStringLiteral("x"), {}, &error)
              .isEmpty());
  QVERIFY(!error.isEmpty());

  QVERIFY(makeStudy(QStringLiteral("one"), QStringLiteral("AAA")));
  IndicatorsWidget indicators(ctx_);
  const QVariantMap ind = indicators.recipeSettings(&error);
  QVERIFY2(!ind.isEmpty(), qPrintable(error));
  // The keys a run reads (study/runs.h), with the page's values.
  for (const char *k : {"mc", "mag_bin", "window_months", "step_months",
                        "half_gauss_sigma", "ma_windows", "bollinger",
                        "bb_window", "bb_k", "figure_dpi"})
    QVERIFY2(ind.contains(QLatin1String(k)), k);
  QCOMPARE(ind.value("window_months").toInt(), 12);
  QCOMPARE(ind.value("ma_windows").toList().size(), 3);
  const QString id = StudyInputsBar::addRecipe(
      store_.get(), kToolIndicators, QStringLiteral("Twelve months"), ind);
  QVERIFY(!id.isEmpty());
  const Recipe *r = store_->study().findRecipe(id);
  QVERIFY(r);
  QCOMPARE(r->tool, kToolIndicators);
  QCOMPARE(r->name, QStringLiteral("Twelve months"));
  // The tool's defaults stay where the page says nothing.
  QVERIFY(r->settings.contains(QStringLiteral("estimate_mc")));
  // A second one of the same name gets an id of its own.
  const QString id2 = StudyInputsBar::addRecipe(
      store_.get(), kToolIndicators, QStringLiteral("Twelve months"), ind);
  QVERIFY(!id2.isEmpty() && id2 != id);

  FeaturesWidget features(ctx_);
  const QString fid = StudyInputsBar::addRecipe(
      store_.get(), kToolFeatures, QStringLiteral("F"),
      features.recipeSettings());
  const Recipe *fr = store_->study().findRecipe(fid);
  QVERIFY(fr);
  QVERIFY(fr->settings.value("config_json").toString().contains("station"));
  // It is in the file.
  QCOMPARE(Study::load(store_->study().dir).recipes.size(), 3);
}

void TstGuided::movedInputsAreAskedForOnce() {
  // The study names a catalog and two waveform folders on a "disk" far from
  // it; the disk is then mounted elsewhere.
  QTemporaryDir oldDisk, newDisk;
  QVERIFY(QDir().mkpath(oldDisk.filePath("lab/stations/AAA")));
  QVERIFY(QDir().mkpath(oldDisk.filePath("lab/fdsn_raw")));
  QVERIFY(QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                      oldDisk.filePath("lab/catalog.csv")));
  QVERIFY(store_->create(dir_->filePath("far"), QStringLiteral("Far")));
  QVERIFY(store_->update([&](Study &s) {
    s.catalog = oldDisk.filePath("lab/catalog.csv");
    s.addWaveformRoot(oldDisk.filePath("lab/stations"));
    s.addWaveformRoot(oldDisk.filePath("lab/fdsn_raw"));
  }));
  // Nothing is missing: nobody is asked.
  int asked = 0;
  QCOMPARE(StudySwitcher::locateMissing(nullptr, store_.get(),
                                        [&](const MissingInput &) {
                                          ++asked;
                                          return QString();
                                        }),
           0);
  QCOMPARE(asked, 0);

  QVERIFY(QDir().rename(oldDisk.filePath("lab"), newDisk.filePath("lab")));
  QVERIFY(store_->open(dir_->filePath("far")));
  QCOMPARE(store_->study().missingInputs().size(), 3);
  // Skipped: each missing input is asked for once, and nothing changes.
  QCOMPARE(StudySwitcher::locateMissing(nullptr, store_.get(),
                                        [&](const MissingInput &) {
                                          ++asked;
                                          return QString();
                                        }),
           0);
  QCOMPARE(asked, 3);
  QCOMPARE(store_->study().missingInputs().size(), 3);
  // Answered: the first answer finds the other two as well.
  asked = 0;
  QCOMPARE(StudySwitcher::locateMissing(
               nullptr, store_.get(),
               [&](const MissingInput &m) {
                 ++asked;
                 return m.kind == MissingInput::Kind::Catalog
                            ? newDisk.filePath("lab/catalog.csv")
                            : QString();
               }),
           3);
  QCOMPARE(asked, 1);
  QVERIFY(store_->study().missingInputs().isEmpty());
  // And it is saved.
  const Study back = Study::load(store_->study().dir);
  QCOMPARE(back.catalogPath(), newDisk.filePath("lab/catalog.csv"));
  QCOMPARE(back.waveformRoots(),
           (QStringList{newDisk.filePath("lab/stations"),
                        newDisk.filePath("lab/fdsn_raw")}));
}

QTEST_MAIN(TstGuided)
#include "tst_guided.moc"
