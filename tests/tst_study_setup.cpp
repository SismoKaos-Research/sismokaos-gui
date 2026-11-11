// Starting a study without typing paths: the built-in earthquake list for
// picking cases, what it must not be used for, downloading a catalog from an
// FDSN event service (through a fake HTTP layer, no network), several
// waveform folders at once, and the setup dialog. With SKG_SHOTS set to a
// folder, the dialogs are also saved there as PNGs.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrlQuery>
#include <QtTest>

#include <cmath>
#include <functional>
#include <memory>

#include "core/module.h"
#include "core/theme.h"
#include "fx/catalog.h"
#include "jobs/job.h"
#include "jobs/jobmodel.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casespage.h"
#include "modules/study/studysetup.h"
#include "modules/study/studyswitcher.h"
#include "study/catalogs.h"
#include "study/runs.h"
#include "study/store.h"
#include "study/waveindex.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;

// The station and earthquake lists embedded in the tdvms library.
static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

void shot(QWidget &w, const QString &name) {
  const QString dir = qEnvironmentVariable("SKG_SHOTS");
  if (dir.isEmpty())
    return;
  QDir().mkpath(dir);
  w.grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
}

// An FDSN event service that answers from a function of the request.
class FakeHttp : public tdvms::Http {
public:
  std::function<tdvms::HttpResult(const QUrl &)> answer;
  QList<QUrl> urls;
  tdvms::HttpResult get(const QUrl &url, int) override {
    urls << url;
    return answer(url);
  }
  tdvms::HttpResult postJson(const QUrl &, const QByteArray &, int,
                             const QMap<QString, QString> &) override {
    return {};
  }
  tdvms::HttpResult postBody(const QUrl &, const QByteArray &, int,
                             const QMap<QString, QString> &) override {
    return {};
  }
  tdvms::HttpResult getToDevice(const QUrl &, QIODevice *, int,
                                const std::function<bool()> &) override {
    return {};
  }
};

QDateTime param(const QUrl &url, const char *name) {
  QDateTime t = QDateTime::fromString(
      QUrlQuery(url).queryItemValue(QString::fromLatin1(name)), Qt::ISODate);
  t.setTimeZone(QTimeZone::UTC);
  return t;
}

tdvms::HttpResult ok(const QByteArray &body) {
  tdvms::HttpResult r;
  r.status = 200;
  r.body = body;
  return r;
}

// Refuses more than 100 days at once (as USGS does past 20000 events), has
// nothing in 2019-H2, and otherwise two earthquakes per piece plus one that
// every piece repeats.
tdvms::HttpResult service(const QUrl &url) {
  const QDateTime from = param(url, "starttime"), to = param(url, "endtime");
  if (from.daysTo(to) > 100) {
    tdvms::HttpResult r;
    r.status = 400;
    r.body = "Error 400: Bad Request\n\n1234 matching events exceeds search "
             "limit of 20000. Modify the search to match fewer events.";
    return r;
  }
  if (from.date() >= QDate(2019, 7, 1) && from.date().year() == 2019) {
    tdvms::HttpResult r;
    r.status = 204;
    return r;
  }
  QByteArray body = "#EventID|Time|Latitude|Longitude|Depth/km|Author|"
                    "Catalog|Contributor|ContributorID|MagType|Magnitude|"
                    "MagAuthor|EventLocationName\n";
  const QString a = from.addDays(1).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
  const QString b =
      from.addDays(2).addSecs(3723).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));
  body += QStringLiteral("ev%1a|%2|38.1|38.6|10.5|us|us|us|x|mb|2.4|us|5 km N of "
                         "Somewhere, Turkey\n")
              .arg(from.toSecsSinceEpoch())
              .arg(a)
              .toUtf8();
  body += QStringLiteral("ev%1b|%2|38.25|38.75|7|us|us|us|x|ml|4.1|us|Place, with a "
                         "comma\n")
              .arg(from.toSecsSinceEpoch())
              .arg(b)
              .toUtf8();
  body += "dup1|2018-03-03T03:03:03|38.0|38.0|5|us|us|us|x|mw|5.5|us|Repeated\n";
  body += "broken line\n";
  return ok(body);
}

CatalogQuery twoYears() {
  CatalogQuery q;
  q.serviceUrl = QStringLiteral("https://example.invalid/fdsnws/event/1/query");
  q.from = QDateTime(QDate(2018, 1, 1), QTime(0, 0), QTimeZone::UTC);
  q.to = QDateTime(QDate(2020, 1, 1), QTime(0, 0), QTimeZone::UTC);
  q.minLat = 36;
  q.maxLat = 40;
  q.minLon = 36;
  q.maxLon = 41;
  q.minMagnitude = 1.0;
  return q;
}

} // namespace

class TstStudySetup : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void builtInList();
  void newStudyPicksFromBuiltIn();
  void denseToolsWaitForACatalog();
  void caseFromAnotherCatalogIsRecognised();
  void queryFromTheCases();
  void fetchInPiecesWithProvenance();
  void fetchStopsAndFails();
  void fetchAsJobBecomesTheCatalog();
  void foldersNeverOverlap();
  void setupDialog();
  void droppedFolders();
  void cleanupTestCase();

private:
  void newStore(const QString &name);

  QTemporaryDir dir_;
  std::unique_ptr<QSettings> settings_;
  std::unique_ptr<Theme> theme_;
  std::unique_ptr<JobModel> jobs_;
  std::unique_ptr<ToolRegistry> tools_;
  std::unique_ptr<StudyStore> store_;
  AppContext ctx_;
};

void TstStudySetup::initTestCase() {
  // Never the user's own refreshed lists.
  QStandardPaths::setTestModeEnabled(true);
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
  StudySwitcher::setupAfterCreate = false;
  newStore(QStringLiteral("first"));
}

void TstStudySetup::newStore(const QString &name) {
  store_ = std::make_unique<StudyStore>(settings_.get());
  ctx_ = AppContext{jobs_.get(), tools_.get(), settings_.get(), theme_.get(),
                    store_.get()};
  QVERIFY(store_->create(dir_.filePath(name), name));
}

void TstStudySetup::cleanupTestCase() {
  store_.reset();
  jobs_.reset();
}

void TstStudySetup::builtInList() {
  const BuiltInCatalog b = builtInCatalog();
  QVERIFY(b.count > 100);
  QVERIFY(b.minMagnitude >= 5.0);
  QCOMPARE(b.first.year(), 1990);
  QVERIFY(b.updated.isValid());
  const std::vector<analysis::Event> events = builtInEvents();
  QCOMPARE(int(events.size()), b.count);
  for (size_t i = 1; i < events.size(); ++i)
    QVERIFY(events[i - 1].epoch <= events[i].epoch);
  // The first line of the shipped file: 1990-04-20T23:30:03, M 5.4.
  QCOMPARE(QDateTime::fromSecsSinceEpoch(qint64(events.front().epoch),
                                         QTimeZone::UTC)
               .toString(Qt::ISODate),
           QStringLiteral("1990-04-20T23:30:03Z"));
  QCOMPARE(events.front().mag, 5.4);
  QVERIFY(!events.front().place.isEmpty());
}

void TstStudySetup::newStudyPicksFromBuiltIn() {
  // A brand-new study names no catalog, and a case can be added at once.
  QVERIFY(store_->study().catalog.isEmpty());
  QCOMPARE(pickingEvents(store_->study()).size(), builtInEvents().size());

  CasesPage page(ctx_);
  page.resize(1100, 820);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  auto *catalog = page.findChild<QLineEdit *>(QStringLiteral("studyCatalog"));
  auto *update = page.findChild<QPushButton *>(QStringLiteral("catalogUpdate"));
  auto *own = page.findChild<QPushButton *>(QStringLiteral("catalogOwn"));
  auto *get = page.findChild<QPushButton *>(QStringLiteral("catalogGet"));
  QVERIFY(catalog && update && own && get);
  QVERIFY2(catalog->text().startsWith(QStringLiteral(
               "Built-in list: M ≥ 5 since 1990, as shipped (to 20")),
           qPrintable(catalog->text()));
  QVERIFY(update->isVisibleTo(&page));
  auto *time = page.findChild<QComboBox *>(QStringLiteral("catalogTime"));
  QVERIFY(time && !time->isVisibleTo(&page));
  QPushButton *add = nullptr;
  for (QPushButton *b : page.findChildren<QPushButton *>())
    if (b->text() == QStringLiteral("Add from catalog…"))
      add = b;
  QVERIFY(add && add->isEnabled());
  shot(page, QStringLiteral("cases_new_study"));

  AddFromCatalogDialog dialog(store_->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *events = dialog.findChild<QTableWidget *>(QStringLiteral("eventTable"));
  auto *mine = dialog.findChild<QPushButton *>(QStringLiteral("nearMine"));
  QVERIFY(events && mine);
  QVERIFY(events->rowCount() > 100);
  QVERIFY(!mine->isEnabled()); // no waveform folders yet
  bool says = false;
  for (QLabel *l : dialog.findChildren<QLabel *>())
    says = says || l->text().startsWith(QStringLiteral("Built-in list"));
  QVERIFY(says);
  events->selectRow(0);
  shot(dialog, QStringLiteral("add_from_builtin"));
  dialog.accept();
  QVERIFY(dialog.result().has_value());
  QVERIFY(dialog.result()->magnitude >= 7.0); // the largest comes first
  QVERIFY(!dialog.result()->stations.isEmpty()); // nearest ones pre-ticked
  const Case picked = *dialog.result();
  QVERIFY(store_->update([&picked](Study &s) {
    Case c = picked;
    c.id = s.freeCaseId(c.name);
    s.cases << c;
  }));
  // With a catalog of its own the row shows the file again.
  QVERIFY(QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                      QDir(store_->study().dir).filePath("c.csv")));
  QVERIFY(store_->update([](Study &s) { s.catalog = QStringLiteral("c.csv"); }));
  QTRY_VERIFY(catalog->text().endsWith(QStringLiteral("c.csv")));
  QVERIFY(!update->isVisibleTo(&page));
  QVERIFY(time->isVisibleTo(&page));
  QVERIFY(store_->update([](Study &s) { s.catalog.clear(); }));
}

void TstStudySetup::denseToolsWaitForACatalog() {
  // The built-in list is for picking; nothing is computed on it.
  QVERIFY(!store_->study().cases.isEmpty());
  QVERIFY(store_->update([](Study &s) {
    Recipe ind;
    ind.id = QStringLiteral("ind");
    ind.name = QStringLiteral("Indicators");
    ind.tool = kToolIndicators;
    Recipe fx;
    fx.id = QStringLiteral("fx");
    fx.name = QStringLiteral("Features");
    fx.tool = kToolFeatures;
    s.recipes << ind << fx;
  }));
  const Study &s = store_->study();
  const Case &c = s.cases.first();
  const Cell ind = cellOf(s, c, QString(), *s.findRecipe(QStringLiteral("ind")));
  QCOMPARE(ind.state, CellState::Blocked);
  QVERIFY2(ind.why.contains(QStringLiteral("built-in list")),
           qPrintable(ind.why));
  QVERIFY(ind.why.contains(QStringLiteral("Cases page")));
  QCOMPARE(ind.fix, Fix::Cases);
  const Cell fx = cellOf(s, c, c.stations.first().code,
                         *s.findRecipe(QStringLiteral("fx")));
  QCOMPARE(fx.state, CellState::Blocked);
  QCOMPARE(fx.fix, Fix::Cases);
}

void TstStudySetup::caseFromAnotherCatalogIsRecognised() {
  // A case picked from the built-in list (one agency), then a denser catalog
  // of another agency: the same earthquake a second later, a few km away and
  // 0.2 smaller must still be found, so it is kept out of its own span.
  Case c;
  c.time = QDateTime(QDate(2020, 1, 24), QTime(17, 55, 14), QTimeZone::UTC);
  c.lat = 38.431;
  c.lon = 39.061;
  c.magnitude = 6.7;
  const double t = double(c.time.toSecsSinceEpoch());
  std::vector<analysis::Event> events(3);
  events[0].epoch = t - 3600;
  events[0].lat = 38.4;
  events[0].lon = 39.0;
  events[0].mag = 3.0; // a foreshock
  events[1].epoch = t - 2.7;
  events[1].lat = 38.359;
  events[1].lon = 39.063;
  events[1].mag = 6.8; // the mainshock, as the other agency has it
  events[2].epoch = t + 600;
  events[2].lat = 38.4;
  events[2].lon = 39.1;
  events[2].mag = 5.1; // an aftershock
  QCOMPARE(eventOfCase(c, events), &events[1]);
  // The other agency's catalog three hours off (its time offset set wrong):
  // still the same earthquake, and still kept out of its own span.
  events[1].epoch = t - 3 * 3600.0 - 2.7;
  QCOMPARE(eventOfCase(c, events), &events[1]);
  // Nothing within the hours an offset can move it: not found, rather than
  // the wrong event.
  events[1].epoch = t - 16 * 3600.0;
  QVERIFY(eventOfCase(c, events) == nullptr);
  QVERIFY(largeEventJustBefore(c, events) == nullptr);
  // An event much larger than the case within the hours before it is not
  // "the same within a magnitude unit", yet no forerunner to count either.
  events[1].epoch = t - 3 * 3600.0;
  events[1].mag = 7.9;
  QVERIFY(eventOfCase(c, events) == nullptr);
  QCOMPARE(largeEventJustBefore(c, events), &events[1]);
  events[1].mag = 6.8;
  // The exact match still wins over a larger neighbour.
  events[1].epoch = t - 2.7;
  events[0].epoch = t;
  events[0].lat = c.lat;
  events[0].lon = c.lon;
  events[0].mag = c.magnitude;
  QCOMPARE(eventOfCase(c, events), &events[0]);
}

void TstStudySetup::queryFromTheCases() {
  Study s;
  QVERIFY(!queryForStudy(s).valid());
  Case a;
  a.time = QDateTime(QDate(2020, 1, 24), QTime(17, 55), QTimeZone::UTC);
  a.lat = 38.4;
  a.lon = 39.1;
  a.spanDays = 365;
  a.zone.radiusKm = 100;
  Case b = a;
  b.time = QDateTime(QDate(2023, 2, 6), QTime(1, 17), QTimeZone::UTC);
  b.lat = 37.2;
  b.lon = 37.0;
  s.cases << a << b;
  Recipe r;
  r.tool = kToolIndicators;
  r.settings.insert(QStringLiteral("window_months"), 6);
  s.recipes << r;
  const CatalogQuery q = queryForStudy(s);
  QVERIFY(q.valid());
  // 150 km (the zone and the margin) is at least 1.3 degrees of latitude.
  QVERIFY(q.minLat <= 37.2 - 1.3 && q.minLat > 37.2 - 2.0);
  QVERIFY(q.maxLat >= 38.4 + 1.3 && q.maxLat < 38.4 + 2.0);
  QVERIFY(q.minLon <= 37.0 - 1.6 && q.maxLon >= 39.1 + 1.6);
  // A year of span and six months of window before the first earthquake.
  QCOMPARE(q.from.date(), QDate(2018, 7, 24));
  QVERIFY(q.to > b.time && q.to < b.time.addDays(3));
}

void TstStudySetup::fetchInPiecesWithProvenance() {
  FakeHttp http;
  http.answer = service;
  QStringList log;
  double lastProgress = -1;
  FetchHooks hooks;
  hooks.log = [&log](const QString &m) { log << m; };
  hooks.progress = [&lastProgress](double f, const QString &) {
    QVERIFY(f >= lastProgress);
    lastProgress = f;
  };
  const QString out = dir_.filePath("fetched");
  const FetchResult res =
      fetchCatalog(http, twoYears(), out, QStringLiteral("usgs"), hooks);
  QVERIFY(!res.stopped);
  // The year is refused, so is half of it; quarters (91 days) pass: eight
  // of them cover the two years, two of which have nothing.
  QCOMPARE(res.requests, 2 + 8);
  QCOMPARE(res.events, 6 * 2 + 1); // the repeated event once
  QCOMPARE(lastProgress, 1.0);
  QVERIFY(http.urls.first().toString().contains(
      QStringLiteral("format=text")));
  QVERIFY(http.urls.first().toString().contains(
      QStringLiteral("minmagnitude=1.0")));
  QCOMPARE(res.minMagnitudeFound, 2.4);

  // Every tool reads the file, in UTC.
  QVERIFY(QFileInfo::exists(res.csvPath));
  QVERIFY(res.csvPath.startsWith(out));
  const std::vector<analysis::Event> events =
      analysis::loadEvents(res.csvPath, 0.0);
  QCOMPARE(int(events.size()), res.events);
  QCOMPARE(int(fx::loadCatalog(res.csvPath).size()), res.events);
  QCOMPARE(QDateTime::fromSecsSinceEpoch(qint64(events.front().epoch),
                                         QTimeZone::UTC)
               .toString(Qt::ISODate),
           QStringLiteral("2018-01-02T00:00:00Z"));
  bool comma = false, dup = false;
  for (const analysis::Event &e : events) {
    comma = comma || e.place == QLatin1String("Place, with a comma");
    dup = dup || (e.id == QLatin1String("dup1") && e.mag == 5.5);
  }
  QVERIFY(comma && dup);

  // What was asked, of whom, when, and what came back.
  QFile pf(res.provenancePath);
  QVERIFY(pf.open(QIODevice::ReadOnly));
  const QJsonObject p = QJsonDocument::fromJson(pf.readAll()).object();
  QCOMPARE(p.value("service").toString(), twoYears().serviceUrl);
  QCOMPARE(p.value("events").toInt(), res.events);
  QCOMPARE(p.value("requests").toInt(), res.requests);
  QCOMPARE(p.value("times").toString(), QStringLiteral("UTC"));
  QCOMPARE(p.value("lines_not_read").toInt(), 6);
  QCOMPARE(p.value("query").toObject().value("starttime").toString(),
           QStringLiteral("2018-01-01T00:00:00"));
  QCOMPARE(p.value("query").toObject().value("minmagnitude").toDouble(), 1.0);
  QVERIFY(!p.value("retrieved_utc").toString().isEmpty());
  QCOMPARE(p.value("sha256").toString().size(), 64);
  QVERIFY(p.value("what").toString().contains(QStringLiteral("not part of")));
}

void TstStudySetup::fetchStopsAndFails() {
  const QString out = dir_.filePath("never");
  {
    // Stopped after the third request: nothing is written.
    FakeHttp http;
    http.answer = service;
    FetchHooks hooks;
    hooks.shouldStop = [&http] { return http.urls.size() >= 3; };
    const FetchResult res =
        fetchCatalog(http, twoYears(), out, QStringLiteral("usgs"), hooks);
    QVERIFY(res.stopped);
    QCOMPARE(http.urls.size(), 3);
    QVERIFY(!QDir(out).exists());
  }
  {
    FakeHttp http;
    http.answer = [](const QUrl &) {
      tdvms::HttpResult r;
      r.status = 503;
      return r;
    };
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        fetchCatalog(http, twoYears(), out, QStringLiteral("usgs")));
  }
  {
    // A page that is not the FDSN text format is an error, not an empty
    // catalog.
    FakeHttp http;
    http.answer = [](const QUrl &) { return ok("<html>maintenance</html>"); };
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        fetchCatalog(http, twoYears(), out, QStringLiteral("usgs")));
  }
  {
    // A service with nothing at all for the request.
    FakeHttp http;
    http.answer = [](const QUrl &) {
      tdvms::HttpResult r;
      r.status = 204;
      return r;
    };
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        fetchCatalog(http, twoYears(), out, QStringLiteral("usgs")));
  }
  {
    // Still too many in a single day: said, not looped on.
    FakeHttp http;
    http.answer = [](const QUrl &) {
      tdvms::HttpResult r;
      r.status = 413;
      return r;
    };
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        fetchCatalog(http, twoYears(), out, QStringLiteral("usgs")));
    QVERIFY(http.urls.size() < 20);
  }
  QVERIFY(!QDir(out).exists());
  CatalogQuery bad = twoYears();
  bad.to = bad.from;
  FakeHttp http;
  http.answer = service;
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                           fetchCatalog(http, bad, out, QString()));
}

void TstStudySetup::fetchAsJobBecomesTheCatalog() {
  QVERIFY(store_->study().catalog.isEmpty());
  // The dialog proposes the request from the cases.
  CatalogFetchDialog dialog(store_->study());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  QCOMPARE(dialog.serviceId(), QStringLiteral("usgs"));
  QVERIFY(dialog.query().valid());
  QVERIFY(dialog.query().serviceUrl.contains(QStringLiteral("usgs.gov")));
  const Case &c = store_->study().cases.first();
  QVERIFY(dialog.query().from < c.spanStart());
  QVERIFY(dialog.query().minLat < c.lat && dialog.query().maxLat > c.lat);
  shot(dialog, QStringLiteral("get_catalog"));

  bool told = false, worked = false;
  QString text;
  Job *job = startCatalogFetch(
      ctx_, twoYears(), QStringLiteral("usgs"),
      [&](bool ok, const QString &t) {
        told = true;
        worked = ok;
        text = t;
      },
      [] {
        auto http = std::make_unique<FakeHttp>();
        http->answer = service;
        return std::unique_ptr<tdvms::Http>(std::move(http));
      });
  QVERIFY(job);
  QTRY_VERIFY_WITH_TIMEOUT(told, 20000);
  QVERIFY2(worked, qPrintable(text));
  QCOMPARE(job->state(), Job::State::Done);
  QVERIFY(text.contains(QStringLiteral("13 earthquakes")));
  const Study &s = store_->study();
  QVERIFY(s.catalog.startsWith(QStringLiteral("catalog/catalog_usgs_")));
  QCOMPARE(s.catalogUtcOffsetHours, 0.0);
  QCOMPARE(pickingEvents(s).size(), size_t(13));
  // The indicators are no longer held back for want of a catalog. This
  // little list starts at magnitude 2.4, so a recipe that counts from 1.3
  // still waits, for that reason and with the recipe as where to put it
  // right; one that lets the completeness magnitude be estimated runs.
  Recipe counted = *s.findRecipe(QStringLiteral("ind"));
  counted.settings.insert(QStringLiteral("estimate_mc"), false);
  counted.settings.insert(QStringLiteral("mc"), 1.3);
  const Cell ind = cellOf(s, s.cases.first(), QString(), counted);
  QVERIFY(!ind.why.contains(QStringLiteral("no earthquake catalog of its own")));
  QCOMPARE(ind.state, CellState::Blocked);
  QCOMPARE(ind.fix, Fix::Recipe);
  QVERIFY2(ind.why.contains(QStringLiteral("from about magnitude 2.4 up")),
           qPrintable(ind.why));
  counted.settings.insert(QStringLiteral("estimate_mc"), true);
  QVERIFY(cellOf(s, s.cases.first(), QString(), counted).state !=
          CellState::Blocked);
  counted.settings.insert(QStringLiteral("estimate_mc"), false);
  counted.settings.insert(QStringLiteral("mc"), 2.0);
  QVERIFY(cellOf(s, s.cases.first(), QString(), counted).state !=
          CellState::Blocked);

  // A failing service says so and leaves the study as it was.
  told = false;
  const QString before = s.catalog;
  job = startCatalogFetch(
      ctx_, twoYears(), QStringLiteral("usgs"),
      [&](bool ok, const QString &t) {
        told = true;
        worked = ok;
        text = t;
      },
      [] {
        auto http = std::make_unique<FakeHttp>();
        http->answer = [](const QUrl &) {
          tdvms::HttpResult r;
          r.error = QStringLiteral("no route to host");
          return r;
        };
        return std::unique_ptr<tdvms::Http>(std::move(http));
      });
  QTRY_VERIFY_WITH_TIMEOUT(told, 20000);
  QVERIFY(!worked);
  QVERIFY2(text.contains(QStringLiteral("no route to host")), qPrintable(text));
  QCOMPARE(store_->study().catalog, before);
}

void TstStudySetup::foldersNeverOverlap() {
  QDir base(dir_.filePath("data"));
  for (const char *d : {"a/b/c", "x", "y/z"})
    QVERIFY(base.mkpath(QString::fromLatin1(d)));
  const QString a = base.filePath("a"), ab = base.filePath("a/b"),
                abc = base.filePath("a/b/c"), x = base.filePath("x"),
                yz = base.filePath("y/z");
  Study s;
  s.dir = dir_.filePath("first");
  const QString idAb = s.addWaveRoot(ab);
  s.addWaveRoot(yz);
  // The one listed, one inside it, its parent, a new one, and one twice.
  const Study::RootsAdded r = s.addWaveRoots({ab, abc, a, x, x + "/"});
  QCOMPARE(r.added, QStringList({a, x}));
  QCOMPARE(r.replaced, QStringList({ab}));
  QCOMPARE(r.covered, QStringList({ab, abc}));
  QCOMPARE(s.waveformRoots(), QStringList({yz, a, x}));
  // The folder that left does not hand its id on: the index still has its
  // files under it until the next scan.
  for (const WaveRoot &w : s.waveRoots)
    QVERIFY(w.id != idAb);
  // The same again changes nothing.
  const Study::RootsAdded again = s.addWaveRoots({a, x, abc});
  QVERIFY(again.added.isEmpty() && again.replaced.isEmpty());
  QCOMPARE(again.covered.size(), 3);
  // A folder whose name merely starts like a listed one is not inside it.
  QVERIFY(base.mkpath("ab"));
  QCOMPARE(s.addWaveRoots({base.filePath("ab")}).added.size(), 1);
  // It survives the file.
  const Study back = Study::fromToml(s.toToml(), s.dir);
  QCOMPARE(back.waveformRoots(), s.waveformRoots());
}

void TstStudySetup::setupDialog() {
  newStore(QStringLiteral("second"));
  QDir data(dir_.filePath("own"));
  QVERIFY(data.mkpath("stations/STA1"));
  QVERIFY(data.mkpath("more"));
  {
    QFile f(data.filePath("stations/STA1/notes.txt"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("not waveforms\n");
  }
  const QString stations = data.filePath("stations"),
                more = data.filePath("more");

  StudySetupDialog dialog(store_.get());
  dialog.show();
  QVERIFY(QTest::qWaitForWindowExposed(&dialog));
  auto *builtIn =
      dialog.findChild<QRadioButton *>(QStringLiteral("setupBuiltIn"));
  auto *list = dialog.findChild<QListWidget *>(QStringLiteral("setupFolders"));
  auto *add = dialog.findChild<QPushButton *>(QStringLiteral("setupAddFolders"));
  QVERIFY(builtIn && list && add);
  QVERIFY(builtIn->isChecked());
  QCOMPARE(list->count(), 0);
  // Several folders from one visit to the picker, one of them inside
  // another.
  QString askedIn;
  pickFoldersHook = [&](QWidget *, const QString &, const QString &start) {
    askedIn = start;
    return QStringList{stations, more, stations + "/STA1"};
  };
  add->click();
  pickFoldersHook = {};
  QCOMPARE(askedIn, store_->study().dir);
  QCOMPARE(list->count(), 2);
  QVERIFY(dialog.folderList()->note().contains(QStringLiteral("STA1")));
  shot(dialog, QStringLiteral("setup"));
  dialog.accept();
  QCOMPARE(dialog.result(), int(QDialog::Accepted));
  const Study &s = store_->study();
  QVERIFY(s.catalog.isEmpty());
  QCOMPARE(s.waveformRoots(), QStringList({more, stations}));

  // The Cases page scans what was added, by itself.
  const int jobsBefore = jobs_->rowCount();
  CasesPage page(ctx_);
  page.resize(1100, 820);
  page.show();
  QVERIFY(QTest::qWaitForWindowExposed(&page));
  QTRY_VERIFY_WITH_TIMEOUT(
      QFileInfo::exists(QDir(s.dir).filePath("waveforms.index.json")), 20000);
  QVERIFY(jobs_->rowCount() > jobsBefore);
  auto *summary = page.findChild<QLabel *>(QStringLiteral("waveSummary"));
  QVERIFY(summary);
  QTRY_VERIFY_WITH_TIMEOUT(!summary->text().contains(QStringLiteral("…")),
                           20000);
  shot(page, QStringLiteral("cases_after_setup"));

  // Opened again as "Study inputs…", it shows what the study has; taking a
  // folder off the list takes it out of the study.
  auto *inputs = page.findChild<QPushButton *>(QStringLiteral("studyInputs"));
  QVERIFY(inputs);
  StudySetupDialog second(store_.get());
  auto *list2 = second.findChild<QListWidget *>(QStringLiteral("setupFolders"));
  QCOMPARE(list2->count(), 2);
  list2->item(0)->setSelected(true);
  for (QPushButton *b : second.findChildren<QPushButton *>())
    if (b->text() == QLatin1String("Remove"))
      b->click();
  QCOMPARE(list2->count(), 1);
  // And a catalog file of one's own, in Turkey time.
  QVERIFY(QFile::copy(QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv"),
                      QDir(s.dir).filePath("mine.csv")));
  second.findChild<QLineEdit *>(QStringLiteral("setupCatalog"))
      ->setText(QDir(s.dir).filePath("mine.csv"));
  second.findChild<QRadioButton *>(QStringLiteral("setupOwn"))->setChecked(true);
  second.findChild<QComboBox *>(QStringLiteral("setupCatalogTime"))
      ->setCurrentIndex(1);
  second.accept();
  QCOMPARE(store_->study().waveformRoots(), QStringList({stations}));
  QCOMPARE(store_->study().catalog, QStringLiteral("mine.csv"));
  QCOMPARE(store_->study().catalogUtcOffsetHours, 3.0);
  QTest::qWait(200); // let the page's rescan settle before it goes
}

void TstStudySetup::droppedFolders() {
  QDir data(dir_.filePath("dropped"));
  QVERIFY(data.mkpath("one"));
  QVERIFY(data.mkpath("two"));
  {
    QFile f(data.filePath("two/file.mseed"));
    QVERIFY(f.open(QIODevice::WriteOnly));
  }
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(data.filePath("one")),
                QUrl::fromLocalFile(data.filePath("two/file.mseed")),
                QUrl(QStringLiteral("https://example.invalid/x"))});
  // A dropped file stands for its folder; what is not local is ignored.
  QCOMPARE(foldersOf(&mime),
           QStringList({data.filePath("one"), data.filePath("two")}));
  QMimeData text;
  text.setText(QStringLiteral("hello"));
  QVERIFY(foldersOf(&text).isEmpty());

  FolderList list(dir_.path());
  list.resize(500, 200);
  list.show();
  QVERIFY(QTest::qWaitForWindowExposed(&list));
  QSignalSpy changed(&list, &FolderList::changed);
  // A drop goes to the widget that took the drag when it came in.
  QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton,
                        Qt::NoModifier);
  QApplication::sendEvent(&list, &enter);
  QVERIFY(enter.isAccepted());
  QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton,
                  Qt::NoModifier);
  QApplication::sendEvent(&list, &drop);
  QCOMPARE(changed.count(), 1);
  QCOMPARE(list.folders().size(), 2);
  list.addFolders(foldersOf(&mime));
  QCOMPARE(list.folders().size(), 2); // nothing twice
  QVERIFY(list.note().contains(QStringLiteral("Already covered")));
}

QTEST_MAIN(TstStudySetup)
#include "tst_study_setup.moc"
