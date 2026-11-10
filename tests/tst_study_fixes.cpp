// What the first run on real data turned up, at the level of the libraries:
//
//   - Turkish letters: catalogs in the Turkish code page, the "?" an event
//     service writes for the letters it cannot encode, and what a study file
//     keeps of them;
//   - a station whose data lies outside a case's span is stopped before it
//     runs, with the dates it does have;
//   - a calibration that finds too few quiet windows says whether the data
//     or the catalog is why;
//   - a catalog whose magnitudes start high is said to be sparse, where its
//     results are shown;
//   - a features run has one progress bar that never starts again, and
//     keeps its calibration for the next run of the same cell.
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QUrl>
#include <QUrlQuery>
#include <QtTest>

#include "analysis/events.h"
#include "core/defaults.h"
#include "fx/calibrate.h"
#include "fx/config.h"
#include "fx/engine.h"
#include "study/catalogs.h"
#include "study/compare.h"
#include "study/runs.h"
#include "study/study.h"
#include "study/waveindex.h"
#include "tdvms/catalog.h"
#include "tdvms/http.h"
#include "text/text.h"

using namespace skg;
using namespace skg::study;

static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

const QString kFx = QStringLiteral(SKG_TEST_DATA "/fx/enz");

QDateTime utc(int y, int m, int d, int h = 0, int min = 0, int s = 0) {
  return QDateTime(QDate(y, m, d), QTime(h, min, s), QTimeZone::UTC);
}

QString u(const char *utf8) { return QString::fromUtf8(utf8); }

void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

// One 512-byte miniSEED 2 data record: fixed header plus blockette 1000.
QByteArray record(const QDateTime &start, const QByteArray &sta,
                  const QByteArray &cha = "HHZ") {
  QByteArray r;
  QDataStream s(&r, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::BigEndian);
  s.writeRawData("000001", 6);
  s.writeRawData("D", 1);
  s.writeRawData(" ", 1);
  s.writeRawData(sta.leftJustified(5, ' ').constData(), 5);
  s.writeRawData("  ", 2);
  s.writeRawData(cha.constData(), 3);
  s.writeRawData("TU", 2);
  s << quint16(start.date().year()) << quint16(start.date().dayOfYear())
    << quint8(start.time().hour()) << quint8(start.time().minute())
    << quint8(start.time().second()) << quint8(0) << quint16(0);
  s << quint16(43200) << qint16(1) << qint16(1);
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1);
  s << qint32(0) << quint16(64) << quint16(48);
  s << quint16(1000) << quint16(0) << quint8(11) << quint8(1) << quint8(9)
    << quint8(0);
  return r.leftJustified(512, '\0');
}

// Whole days of one station, all three components.
QByteArray days(const QByteArray &sta, const QDateTime &first, int count) {
  QByteArray out;
  for (int d = 0; d < count; ++d)
    for (const char *cha : {"HHE", "HHN", "HHZ"})
      for (int half = 0; half < 2; ++half)
        out += record(first.addDays(d).addSecs(half * 43200), sta, cha);
  return out;
}

class FakeHttp : public tdvms::Http {
public:
  QByteArray body;
  tdvms::HttpResult get(const QUrl &, int) override {
    tdvms::HttpResult r;
    r.status = 200;
    r.body = body;
    return r;
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

// A catalog of `n` events a day apart ending at `last`, magnitudes from
// `fromMag` in steps of 0.1 (cycling over ten values), all at one place.
QByteArray catalogCsv(int n, double fromMag, const QDateTime &last) {
  QByteArray csv = "Date,Latitude,Longitude,Depth,Type,Magnitude,Location\n";
  for (int i = 0; i < n; ++i)
    csv += QStringLiteral("%1,38.60,39.30,7.0,ML,%2,Somewhere\n")
               .arg(last.addDays(i - n + 1).toString(
                        QStringLiteral("dd/MM/yyyy HH:mm:ss")),
                    QString::number(fromMag + 0.1 * (i % 10), 'f', 1))
               .toUtf8();
  return csv;
}

} // namespace

class TstStudyFixes : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { initResources(); }
  void textIsReadInItsEncoding();
  void lostLettersComeBack();
  void builtInListSpellsItsPlaces();
  void downloadedCatalogKeepsTheLetters();
  void caseAndStudyKeepTheLetters();
  void anOlderStudyGetsItsLettersBack();
  void stationWithoutDataInTheSpanWaits();
  void littleDataInTheSpanIsAWarning();
  void calibrationSaysWhatIsWrong();
  void sparseCatalogIsSaidSo();
  void indicatorsNeedTheSmallEarthquakes();
  void warningsReachTheScores();
  void oneBarForAFeaturesRun();
  void calibrationIsKeptAndReused();
  void calibrationIsRedoneWhenItsInputsChange();

private:
  QTemporaryDir tmp_;
};

void TstStudyFixes::textIsReadInItsEncoding() {
  // An AFAD-style export saved by a spreadsheet in the Turkish code page:
  // 0xF0 ğ, 0xFD ı, 0xDD İ, 0xDE Ş, 0xFE ş, 0xE7 ç, 0xF6 ö, 0xFC ü.
  const QByteArray cp1254 =
      "Date,Latitude,Longitude,Depth,Type,Magnitude,Location\n"
      "01/02/2020 04:01:42,38.40,39.10,10.0,ML,3.2,Do\xF0"
      "anyol (Malatya)\n"
      "02/02/2020 04:01:42,39.88,38.12,10.0,ML,2.1,\xDDmranl\xFD (Sivas)\n"
      "03/02/2020 04:01:42,37.52,42.46,10.0,ML,2.5,\xDE\xFDrnak Merkez\n"
      "04/02/2020 04:01:42,39.92,44.05,10.0,ML,1.9,I\xF0"
      "d\xFDr - G\xF6ksu\n";
  QCOMPARE(text::decode(cp1254).count(QChar(0xFFFD)), 0);
  const QString a = tmp_.filePath("afad_1254.csv");
  write(a, cp1254);
  const std::vector<analysis::Event> events = analysis::loadEvents(a);
  QCOMPARE(int(events.size()), 4);
  QCOMPARE(events[0].place, u("Doğanyol (Malatya)"));
  QCOMPARE(events[1].place, u("İmranlı (Sivas)"));
  QCOMPARE(events[2].place, u("Şırnak Merkez"));
  QCOMPARE(events[3].place, u("Iğdır - Göksu"));
  // The same in UTF-8, with a byte order mark as Excel writes it.
  const QString b = tmp_.filePath("afad_utf8.csv");
  write(b, QByteArray("\xEF\xBB\xBF") + text::fromWindows1254(cp1254).toUtf8());
  const std::vector<analysis::Event> again = analysis::loadEvents(b);
  QCOMPARE(int(again.size()), 4);
  for (size_t i = 0; i < 4; ++i)
    QCOMPARE(again[i].place, events[i].place);
  // Plain ASCII and valid UTF-8 are what they are.
  QCOMPARE(text::decode("abc"), QStringLiteral("abc"));
  QCOMPARE(text::decode(u("Elazığ").toUtf8()), u("Elazığ"));
}

void TstStudyFixes::lostLettersComeBack() {
  // As USGS gives them, with the earthquake's position.
  QCOMPARE(text::repairPlace(QStringLiteral("13 km N of Do?anyol, Turkey"),
                             38.4312, 39.0609),
           u("13 km N of Doğanyol, Turkey"));
  QCOMPARE(text::repairPlace(QStringLiteral("28 km SSW of ?mranl?, Turkey"),
                             39.645, 37.98),
           u("28 km SSW of İmranlı, Turkey"));
  QCOMPARE(text::repairPlace(QStringLiteral("4 km E of ??rnak, Turkey"), 37.52,
                             42.50),
           u("4 km E of Şırnak, Turkey"));
  QCOMPARE(text::repairPlace(QStringLiteral("3 km N of I?d?r, Turkey"), 39.95,
                             44.04),
           u("3 km N of Iğdır, Turkey"));
  // The name alone, as other catalogs give it.
  QCOMPARE(text::repairPlace(QStringLiteral("Do?anyol"), 38.31, 39.03),
           u("Doğanyol"));
  // Far from any place of that name, or fitting none: left alone. So is a
  // question mark that is punctuation, and a name that is whole.
  QCOMPARE(text::repairPlace(QStringLiteral("13 km N of Do?anyol, Turkey"),
                             41.0, 29.0),
           QStringLiteral("13 km N of Do?anyol, Turkey"));
  QCOMPARE(text::repairPlace(QStringLiteral("5 km N of Q?x?z, Nowhere"), 38.0,
                             39.0),
           QStringLiteral("5 km N of Q?x?z, Nowhere"));
  QCOMPARE(text::repairPlace(QStringLiteral("Where ?"), 38.0, 39.0),
           QStringLiteral("Where ?"));
  QVERIFY(!text::placeLooksDamaged(QStringLiteral("Where ?")));
  QVERIFY(text::placeLooksDamaged(QStringLiteral("Do?anyol")));
  QCOMPARE(text::repairPlace(u("10 km SSW of Karakoçan, Turkey"), 38.86, 39.99),
           u("10 km SSW of Karakoçan, Turkey"));
}

void TstStudyFixes::builtInListSpellsItsPlaces() {
  // The shipped list is USGS's, damaged names and all (a quarter of the
  // Turkish ones); read through the application, they are whole.
  int damagedInTurkey = 0, doganyol = 0;
  for (const tdvms::CatalogEvent &e : tdvms::loadEvents()) {
    if (e.place.endsWith(QLatin1String("Turkey")) &&
        text::placeLooksDamaged(e.place))
      ++damagedInTurkey;
    if (e.time.startsWith(QLatin1String("2020-01-24")) && e.magnitude > 6.5) {
      QCOMPARE(e.place, u("13 km N of Doğanyol, Turkey"));
      ++doganyol;
    }
  }
  QCOMPARE(damagedInTurkey, 0);
  QCOMPARE(doganyol, 1);
  bool found = false;
  for (const analysis::Event &e : builtInEvents())
    found = found || e.place == u("13 km N of Doğanyol, Turkey");
  QVERIFY(found);
}

void TstStudyFixes::downloadedCatalogKeepsTheLetters() {
  FakeHttp http;
  http.body =
      "#EventID|Time|Latitude|Longitude|Depth/km|Author|Catalog|Contributor|"
      "ContributorID|MagType|Magnitude|MagAuthor|EventLocationName\n"
      "usp000h6ug|2010-02-01T04:01:42|39.645|37.98|10|us|us|us|x|mb|4.4|us|"
      "28 km SSW of ?mranl?, Turkey\n"
      "usp000h8x1|2010-03-08T02:32:34|38.864|39.986|12|us|us|us|x|mwc|6.1|us|"
      "10 km SSW of Karako\xC3\xA7"
      "an, Turkey\n"
      "us60007ewc|2020-01-24T17:55:14|38.4312|39.0609|10|us|us|us|x|mww|6.7|"
      "us|13 km N of Do?anyol, Turkey\n";
  CatalogQuery q;
  q.serviceUrl = QStringLiteral("https://example.invalid/fdsnws/event/1/query");
  q.from = utc(2020, 1, 1);
  q.to = utc(2020, 6, 1);
  q.minLat = 36;
  q.maxLat = 40;
  q.minLon = 36;
  q.maxLon = 41;
  const QString out = tmp_.filePath("fetched");
  const FetchResult res = fetchCatalog(http, q, out, QStringLiteral("usgs"));
  QCOMPARE(res.events, 3);
  QFile f(res.csvPath);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QByteArray bytes = f.readAll();
  // UTF-8 on disk, the letters back, nothing lost on the way.
  QVERIFY(bytes.contains(u("İmranlı").toUtf8()));
  QVERIFY(bytes.contains(u("Doğanyol").toUtf8()));
  QVERIFY(bytes.contains(u("Karakoçan").toUtf8()));
  QVERIFY(!bytes.contains('?'));
  const std::vector<analysis::Event> events = analysis::loadEvents(res.csvPath);
  QCOMPARE(int(events.size()), 3);
  QCOMPARE(events[2].place, u("13 km N of Doğanyol, Turkey"));
  QFile p(res.provenancePath);
  QVERIFY(p.open(QIODevice::ReadOnly));
  const QJsonObject prov = QJsonDocument::fromJson(p.readAll()).object();
  QCOMPARE(prov.value("encoding").toString(), QStringLiteral("UTF-8"));
  QVERIFY(prov.value("place_names").toString().contains("GeoNames"));
}

void TstStudyFixes::caseAndStudyKeepTheLetters() {
  analysis::Event e;
  e.epoch = double(utc(2020, 1, 24, 17, 55, 14).toSecsSinceEpoch());
  e.lat = 38.4312;
  e.lon = 39.0609;
  e.mag = 6.7;
  e.place = u("13 km N of Doğanyol, Turkey");
  Study s;
  s.dir = tmp_.filePath("letters");
  QVERIFY(QDir().mkpath(s.dir));
  s.name = u("Şırnak ve Iğdır");
  Case c = caseFromEvent(e);
  QCOMPARE(c.name, u("13 km N of Doğanyol, Turkey 2020"));
  c.id = s.freeCaseId(c.name);
  // The folder name is plain ASCII: each letter by its base letter, none
  // dropped.
  QCOMPARE(c.id, QStringLiteral("13-km-n-of-doganyol-turkey-2020"));
  QCOMPARE(slug(u("İmranlı Şırnak Iğdır Göksün Çüngüş")),
           QStringLiteral("imranli-sirnak-igdir-goksun-cungus"));
  QCOMPARE(slug(u("Al Mawşil al Jadīdah")),
           QStringLiteral("al-mawsil-al-jadidah"));
  s.cases << c;
  const QString toml = s.toToml();
  QVERIFY(toml.toUtf8().contains(u("Doğanyol").toUtf8()));
  const Study back = Study::fromToml(toml, s.dir);
  QCOMPARE(back.name, s.name);
  QCOMPARE(back.cases[0].name, c.name);
  QCOMPARE(back.cases[0].place, c.place);
  QCOMPARE(back.cases[0].id, c.id);
}

void TstStudyFixes::anOlderStudyGetsItsLettersBack() {
  // A study file written while the names still came through damaged.
  const QString toml = QStringLiteral(
      "name = \"old\"\n"
      "[[case]]\n"
      "id = \"13-km-n-of-do-anyol-turkey-2020\"\n"
      "name = \"13 km N of Do?anyol, Turkey 2020\"\n"
      "time = \"2020-01-24T17:55:14Z\"\n"
      "latitude = 38.4312\n"
      "longitude = 39.0609\n"
      "magnitude = 6.7\n"
      "place = \"13 km N of Do?anyol, Turkey\"\n"
      "[[case]]\n"
      "id = \"mine\"\n"
      "name = \"Is this it?\"\n"
      "time = \"2020-01-24T17:55:14Z\"\n"
      "latitude = 38.4312\n"
      "longitude = 39.0609\n"
      "place = \"13 km N of Do?anyol, Turkey\"\n");
  const Study s = Study::fromToml(toml, tmp_.filePath("old"));
  QCOMPARE(int(s.cases.size()), 2);
  // What is shown is whole again; the id, and so the case's folder and its
  // runs, is the one it had.
  QCOMPARE(s.cases[0].name, u("13 km N of Doğanyol, Turkey 2020"));
  QCOMPARE(s.cases[0].place, u("13 km N of Doğanyol, Turkey"));
  QCOMPARE(s.cases[0].id, QStringLiteral("13-km-n-of-do-anyol-turkey-2020"));
  // A name the user typed is not touched.
  QCOMPARE(s.cases[1].name, QStringLiteral("Is this it?"));
  QCOMPARE(s.cases[1].place, u("13 km N of Doğanyol, Turkey"));
}

// A study with a waveform folder holding LATE (data from June 2020 on),
// EARLY (all of the year before the event) and THIN (three days of it), a
// dense catalog, and a case on 2020-01-24 with the three stations.
static Study spanStudy(const QString &dir, Case *theCase) {
  Study s;
  s.dir = dir;
  s.name = QStringLiteral("Span");
  QDir().mkpath(dir);
  const QString root = QDir(dir).filePath("waves");
  write(root + "/LATE/late.mseed", days("LATE", utc(2020, 6, 27), 5));
  write(root + "/EARLY/early.mseed", days("EARLY", utc(2019, 3, 1), 200));
  write(root + "/THIN/thin.mseed", days("THIN", utc(2019, 12, 1), 3));
  write(QDir(dir).filePath("catalog.csv"),
        catalogCsv(600, 1.0, utc(2020, 1, 23)));
  s.catalog = QStringLiteral("catalog.csv");
  s.addWaveformRoot(root);
  WaveIndex index;
  QList<ScanFolder> folders;
  for (const WaveRoot &r : s.waveRoots)
    folders << ScanFolder{r.id, s.resolved(r.path)};
  scanRoots(index, folders, ScanPass::Identity);
  scanRoots(index, folders, ScanPass::Coverage);
  index.save(dir);
  Case c = referenceCase(QStringLiteral("Winter"), utc(2020, 1, 24, 17, 55),
                         38.43, 39.06);
  c.id = s.freeCaseId(c.name);
  c.spanDays = 365;
  for (const char *code : {"LATE", "EARLY", "THIN"}) {
    StationRef st;
    st.code = QString::fromLatin1(code);
    st.lat = 38.5;
    st.lon = 39.1;
    c.stations << st;
  }
  s.cases << c;
  Recipe f = defaultRecipe(kToolFeatures);
  f.id = QStringLiteral("features");
  f.name = QStringLiteral("Features");
  s.recipes << f;
  if (theCase)
    *theCase = c;
  return s;
}

void TstStudyFixes::stationWithoutDataInTheSpanWaits() {
  Case c;
  const Study s = spanStudy(tmp_.filePath("span"), &c);
  const Recipe &f = s.recipes[0];
  const auto index = WaveIndex::shared(s.dir);
  QVERIFY(index->hasStation("LATE") && index->hasStation("EARLY"));
  // LATE has data, none of it in the year before the event.
  const SpanData late = spanData(*index, "LATE", c.spanStart(), c.time);
  QVERIFY(late.known);
  QCOMPARE(late.days, 0);
  QCOMPARE(late.first, QDate(2020, 6, 27));
  QCOMPARE(late.last, QDate(2020, 7, 1));
  const Cell cell = cellOf(s, c, QStringLiteral("LATE"), f);
  QCOMPARE(cell.state, CellState::Blocked);
  QCOMPARE(cell.fix, Fix::Cases);
  QVERIFY2(cell.why.contains(QStringLiteral(
               "LATE has waveforms from 2020-06-27 to 2020-07-01, none in "
               "this case's span (2019-01-24 to 2020-01-24)")),
           qPrintable(cell.why));
  // The others are free to run.
  QCOMPARE(cellOf(s, c, QStringLiteral("EARLY"), f).state, CellState::Missing);
  QCOMPARE(cellOf(s, c, QStringLiteral("THIN"), f).state, CellState::Missing);
  // A run started all the same (from an older queue, say) says the same.
  try {
    runCell(s, c, QStringLiteral("LATE"), f, QStringLiteral("test"), {});
    QFAIL("the run should not have started");
  } catch (const std::exception &e) {
    QVERIFY2(QString::fromUtf8(e.what()).contains("none in this case's span"),
             e.what());
  }
  // It failed and is set aside; the cell still says what it waits for, not
  // just that it failed, and its log is at hand.
  const Cell after = cellOf(s, c, QStringLiteral("LATE"), f);
  QCOMPARE(after.state, CellState::Blocked);
  QVERIFY(!after.log.isEmpty());
  // A station nothing is known of waits for its waveforms, as before.
  Case d = c;
  StationRef nope;
  nope.code = QStringLiteral("NOPE");
  d.stations << nope;
  const Cell none = cellOf(s, d, QStringLiteral("NOPE"), f);
  QCOMPARE(none.state, CellState::Blocked);
  QVERIFY2(none.why.contains(QStringLiteral("no waveforms for NOPE")),
           qPrintable(none.why));
}

void TstStudyFixes::littleDataInTheSpanIsAWarning() {
  Case c;
  const Study s = spanStudy(tmp_.filePath("thin"), &c);
  const Recipe &f = s.recipes[0];
  const SpanData thin =
      spanData(*WaveIndex::shared(s.dir), "THIN", c.spanStart(), c.time);
  QCOMPARE(thin.days, 3);
  QCOMPARE(thin.spanDays, 365);
  QVERIFY(thin.thin());
  const SpanData early =
      spanData(*WaveIndex::shared(s.dir), "EARLY", c.spanStart(), c.time);
  QCOMPARE(early.days, 200);
  QVERIFY(!early.thin());
  // Not a reason to stop: a warning on the cell.
  const Cell cell = cellOf(s, c, QStringLiteral("THIN"), f);
  QCOMPARE(cell.state, CellState::Missing);
  QVERIFY2(cell.warnings.join('\n').contains(QStringLiteral(
               "THIN has waveforms on only 3 of the 365 days of the span")),
           qPrintable(cell.warnings.join('\n')));
  QVERIFY(cellOf(s, c, QStringLiteral("EARLY"), f).warnings.isEmpty());
  QVERIFY(double(3) < defaults::kMinSpanCoverage * 365);
}

void TstStudyFixes::calibrationSaysWhatIsWrong() {
  const QTemporaryDir out;
  fx::Config cfg;
  cfg.station = QStringLiteral("SYN1");
  cfg.fs = 5.0;
  cfg.freqmin = 0.1;
  cfg.freqmax = 2.0;
  cfg.dataDir = kFx;
  cfg.outputRoot = out.path();
  cfg.calibration.catalog = kFx + QStringLiteral("/catalog.csv");
  cfg.calibration.stationCoords = kFx + QStringLiteral("/stations.csv");
  cfg.calibration.minMag = 2.0;
  fx::PreprocessOptions opt;
  opt.stagingRoot = out.path();

  // Two hours of data and windows of an hour and a half: one window where
  // ten are needed. That is the data's doing, not the catalog's.
  cfg.winSec = 5400;
  cfg.stepSec = 5400;
  try {
    fx::calibrate(cfg, opt);
    QFAIL("a calibration on one window");
  } catch (const fx::CalibrationError &e) {
    QCOMPARE(e.reason(), fx::CalibrationError::Reason::LittleData);
    QCOMPARE(fx::CalibrationError::code(e.reason()),
             QStringLiteral("little_data"));
    const QString what = QString::fromUtf8(e.what());
    QVERIFY2(what.contains(QStringLiteral("too little waveform data")),
             e.what());
    QVERIFY2(what.contains(QStringLiteral("h of data were read for SYN1")),
             e.what());
    QVERIFY2(what.contains(QStringLiteral("(E ")), e.what());
    QVERIFY2(!what.contains(QStringLiteral("min_mag")), e.what());
  }

  // Enough windows, and an earthquake next to the station every minute:
  // now it is the catalog, and the advice is the recipe's settings.
  cfg.winSec = 200;
  cfg.stepSec = 200;
  QByteArray busy = "Date,Latitude,Longitude,Depth,Type,Magnitude,Location\n";
  for (int m = -60; m < 240; ++m)
    busy += utc(2024, 3, 5)
                .addSecs(m * 60)
                .toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"))
                .toUtf8() +
            ",38.60,39.20,7.0,ML,5.0,Under SYN1\n";
  const QString busyCsv = out.filePath("busy.csv");
  write(busyCsv, busy);
  cfg.calibration.catalog = busyCsv;
  try {
    fx::calibrate(cfg, opt);
    QFAIL("a calibration with no quiet window");
  } catch (const fx::CalibrationError &e) {
    QCOMPARE(e.reason(), fx::CalibrationError::Reason::Events);
    const QString what = QString::fromUtf8(e.what());
    QVERIFY2(what.contains(QStringLiteral("fall near a catalog earthquake")),
             e.what());
    QVERIFY2(what.contains(QStringLiteral("min_mag")), e.what());
  }

  // Through a study: the reason is kept with the failed run, and the cell
  // points to where it is put right.
  Study s;
  s.dir = tmp_.filePath("calib");
  s.name = QStringLiteral("Calibration");
  const QString station = QStringLiteral("SYN1");
  QVERIFY(QDir().mkpath(QDir(s.dir).filePath("waves/" + station)));
  for (const QString &name : QDir(kFx).entryList(
           {QStringLiteral("*.mseed"), QStringLiteral("*.zip")}, QDir::Files))
    QVERIFY(QFile::copy(QDir(kFx).filePath(name),
                        QDir(s.dir).filePath("waves/" + station + "/" + name)));
  QVERIFY(QFile::copy(busyCsv, QDir(s.dir).filePath("catalog.csv")));
  s.catalog = QStringLiteral("catalog.csv");
  QVERIFY(s.addWaveformRoot(QDir(s.dir).filePath("waves")));
  Case c = referenceCase(QStringLiteral("After the data"), utc(2024, 3, 6),
                         38.60, 39.30);
  c.id = s.freeCaseId(c.name);
  c.spanDays = 30;
  StationRef st;
  st.code = station;
  st.lat = 38.60;
  st.lon = 39.20;
  c.stations << st;
  s.cases << c;
  fx::Config small;
  small.fs = 5.0;
  small.winSec = 200;
  small.stepSec = 200;
  small.freqmin = 0.1;
  small.freqmax = 2.0;
  small.calibration.minMag = 2.0;
  Recipe features = defaultRecipe(kToolFeatures);
  features.id = QStringLiteral("features");
  features.settings.insert(
      QStringLiteral("config_json"),
      QString::fromUtf8(QJsonDocument(small.toJson()).toJson()));
  s.recipes << features;
  QVERIFY_EXCEPTION_THROWN(
      runCell(s, c, station, features, QStringLiteral("test"), {}),
      std::runtime_error);
  Cell cell = cellOf(s, c, station, features);
  QCOMPARE(cell.state, CellState::Failed);
  QCOMPARE(cell.fix, Fix::Recipe);
  const std::optional<RunRecord> failed =
      RunRecord::read(runFailedDir(cell.dir));
  QVERIFY(failed.has_value());
  QCOMPARE(failed->errorReason, QStringLiteral("events"));

  // The same station with windows too long for its data: the remedy is
  // more data, on the Cases page.
  small.winSec = 5400;
  small.stepSec = 5400;
  features.settings.insert(
      QStringLiteral("config_json"),
      QString::fromUtf8(QJsonDocument(small.toJson()).toJson()));
  features.revision += 1;
  s.recipes[0] = features;
  QVERIFY_EXCEPTION_THROWN(
      runCell(s, c, station, features, QStringLiteral("test"), {}),
      std::runtime_error);
  cell = cellOf(s, c, station, features);
  QCOMPARE(cell.state, CellState::Failed);
  QCOMPARE(cell.fix, Fix::Cases);
  QVERIFY2(cell.why.contains(QStringLiteral("too little waveform data")),
           qPrintable(cell.why));
  QCOMPARE(RunRecord::read(runFailedDir(cell.dir))->errorReason,
           QStringLiteral("little_data"));
}

void TstStudyFixes::sparseCatalogIsSaidSo() {
  // What a catalog holds: a dense one and a list of large earthquakes.
  const QString dense = tmp_.filePath("dense.csv");
  write(dense, catalogCsv(400, 1.0, utc(2024, 3, 4)));
  const CatalogSummary d = catalogSummary(dense);
  QVERIFY(d.ok);
  QCOMPARE(d.events, 400);
  QCOMPARE(d.floor, 1.0);
  QCOMPARE(d.last, QDate(2024, 3, 4));
  const QString large = tmp_.filePath("large.csv");
  write(large, catalogCsv(373, 4.4, utc(2024, 3, 4)));
  const CatalogSummary l = catalogSummary(large);
  QCOMPARE(l.events, 373);
  QCOMPARE(l.floor, 4.4);
  QVERIFY(l.floor > defaults::kSparseCatalogMag);
  QVERIFY(!catalogSummary(tmp_.filePath("nope.csv")).ok);
  // The file is read once: a changed file is read again.
  write(large, catalogCsv(10, 5.0, utc(2024, 3, 4)));
  QTRY_COMPARE(catalogSummary(large).events, 10);

  // A features run under the sparse catalog: it runs, and says so in its
  // log, its record and on its cell.
  Study s;
  s.dir = tmp_.filePath("sparse");
  s.name = QStringLiteral("Sparse");
  const QString station = QStringLiteral("SYN1");
  QVERIFY(QDir().mkpath(QDir(s.dir).filePath("waves/" + station)));
  for (const QString &name : QDir(kFx).entryList(
           {QStringLiteral("*.mseed"), QStringLiteral("*.zip")}, QDir::Files))
    QVERIFY(QFile::copy(QDir(kFx).filePath(name),
                        QDir(s.dir).filePath("waves/" + station + "/" + name)));
  write(QDir(s.dir).filePath("catalog.csv"),
        catalogCsv(373, 4.4, utc(2023, 12, 30)));
  s.catalog = QStringLiteral("catalog.csv");
  QVERIFY(s.addWaveformRoot(QDir(s.dir).filePath("waves")));
  Case c = referenceCase(QStringLiteral("After the data"), utc(2024, 3, 6),
                         38.60, 39.30);
  c.id = s.freeCaseId(c.name);
  c.spanDays = 30;
  StationRef st;
  st.code = station;
  st.lat = 38.60;
  st.lon = 39.20;
  c.stations << st;
  s.cases << c;
  fx::Config cfg;
  cfg.fs = 5.0;
  cfg.winSec = 200;
  cfg.stepSec = 200;
  cfg.freqmin = 0.1;
  cfg.freqmax = 2.0;
  cfg.calibration.minMag = 2.0;
  Recipe features = defaultRecipe(kToolFeatures);
  features.id = QStringLiteral("features");
  features.settings.insert(
      QStringLiteral("config_json"),
      QString::fromUtf8(QJsonDocument(cfg.toJson()).toJson()));
  s.recipes << features;
  Cell cell = cellOf(s, c, station, features);
  QCOMPARE(cell.state, CellState::Missing);
  QVERIFY2(cell.warnings.join('\n').contains(
               QStringLiteral("from about magnitude 4.4 up (373 events)")),
           qPrintable(cell.warnings.join('\n')));
  QStringList log;
  RunHooks hooks;
  hooks.log = [&log](const QString &line) { log << line; };
  QVERIFY(runCell(s, c, station, features, QStringLiteral("test"), hooks));
  QVERIFY2(log.join('\n').contains(
               QStringLiteral("warning: the catalog lists earthquakes from "
                              "about magnitude 4.4 up")),
           qPrintable(log.join('\n')));
  cell = cellOf(s, c, station, features);
  QCOMPARE(cell.state, CellState::Done);
  QCOMPARE(cell.warnings.size(), cell.record->warnings.size());
  QVERIFY(!cell.record->warnings.isEmpty());
  QFile runLog(cell.log);
  QVERIFY(runLog.open(QIODevice::ReadOnly));
  QVERIFY(runLog.readAll().contains("warning: the catalog lists"));
  // A dense catalog in its place: the finished run is stale (the catalog
  // changed), and a run made now would carry no such warning.
  write(QDir(s.dir).filePath("catalog.csv"),
        catalogCsv(400, 1.0, utc(2023, 12, 30)));
  QTRY_COMPARE(cellOf(s, c, station, features).state, CellState::Stale);
  QVERIFY(runWarnings(s, c, station, features).isEmpty());
}

void TstStudyFixes::indicatorsNeedTheSmallEarthquakes() {
  Study s;
  s.dir = tmp_.filePath("indicators");
  QVERIFY(QDir().mkpath(s.dir));
  s.name = QStringLiteral("Indicators");
  write(QDir(s.dir).filePath("catalog.csv"),
        catalogCsv(373, 4.4, utc(2020, 1, 23)));
  s.catalog = QStringLiteral("catalog.csv");
  Case c = referenceCase(QStringLiteral("Winter"), utc(2020, 1, 24), 38.6, 39.3);
  c.id = s.freeCaseId(c.name);
  s.cases << c;
  Recipe r = defaultRecipe(kToolIndicators);
  r.id = QStringLiteral("ind");
  r.settings.insert(QStringLiteral("estimate_mc"), false);
  r.settings.insert(QStringLiteral("mc"), 1.3);
  // Magnitudes from 4.4, counted from 1.3: the b-value cannot be had.
  Cell cell = cellOf(s, c, QString(), r);
  QCOMPARE(cell.state, CellState::Blocked);
  QCOMPARE(cell.fix, Fix::Recipe);
  QVERIFY2(cell.why.contains(QStringLiteral("from about magnitude 4.4 up")) &&
               cell.why.contains(QStringLiteral("completeness magnitude 1.3")),
           qPrintable(cell.why));
  QVERIFY_EXCEPTION_THROWN(
      runCell(s, c, QString(), r, QStringLiteral("test"), {}),
      std::runtime_error);
  // Counted from 4.0: within the margin, so it runs, with a warning.
  r.settings.insert(QStringLiteral("mc"), 4.0);
  r.revision += 1;
  cell = cellOf(s, c, QString(), r);
  QVERIFY(cell.state != CellState::Blocked);
  QVERIFY2(cell.warnings.join('\n').contains(
               QStringLiteral("thins out below about magnitude 4.4")),
           qPrintable(cell.warnings.join('\n')));
  // Counted from where the catalog starts: nothing to say.
  r.settings.insert(QStringLiteral("mc"), 4.4);
  QVERIFY(runWarnings(s, c, QString(), r).isEmpty());
  // Estimated per case: it runs; the warning says what the estimate will be.
  r.settings.insert(QStringLiteral("estimate_mc"), true);
  r.settings.insert(QStringLiteral("mc"), 1.3);
  cell = cellOf(s, c, QString(), r);
  QVERIFY(cell.state != CellState::Blocked);
  QVERIFY2(cell.warnings.join('\n').contains(
               QStringLiteral("will be estimated near that")),
           qPrintable(cell.warnings.join('\n')));
  // A dense catalog: neither.
  write(QDir(s.dir).filePath("catalog.csv"),
        catalogCsv(600, 1.0, utc(2020, 1, 23)));
  r.settings.insert(QStringLiteral("estimate_mc"), false);
  QTRY_VERIFY(cellOf(s, c, QString(), r).state != CellState::Blocked);
  QVERIFY(runWarnings(s, c, QString(), r).isEmpty());
}

void TstStudyFixes::warningsReachTheScores() {
  // A run record keeps its warnings and the reason of its failure.
  const QString dir = tmp_.filePath("record");
  QVERIFY(QDir().mkpath(dir));
  RunRecord rec;
  rec.tool = kToolFeatures;
  rec.warnings = QStringList{QStringLiteral("first"), u("ikinci: ğış")};
  rec.errorReason = QStringLiteral("little_data");
  QVERIFY(rec.write(dir));
  const std::optional<RunRecord> back = RunRecord::read(dir);
  QVERIFY(back.has_value());
  QCOMPARE(back->warnings, rec.warnings);
  QCOMPARE(back->errorReason, rec.errorReason);
  // The scores of a classification say what its features were made under.
  ScoreRow row;
  row.caseName = QStringLiteral("A");
  row.station = QStringLiteral("S");
  row.bestModel = QStringLiteral("svm");
  row.testF1 = row.testAuc = row.baseF1 = row.baseAuc = 0.5;
  row.warnings = QStringList{QStringLiteral("its features: sparse; catalog")};
  const QString csv = scoresCsv({row});
  QVERIFY(csv.section('\n', 0, 0).endsWith(QStringLiteral(";warnings")));
  QVERIFY2(csv.section('\n', 1, 1)
               .endsWith(QStringLiteral(";its features: sparse, catalog")),
           qPrintable(csv));
}

// The fx test waveforms (two hours of SYN1, one component partly in a zip)
// as a study with one reference case and one features recipe.
struct FxStudy {
  Study s;
  Case c;
  Recipe features;
  fx::Config cfg;
  QString station = QStringLiteral("SYN1");
  void setConfig(const fx::Config &config) {
    cfg = config;
    features.settings.insert(
        QStringLiteral("config_json"),
        QString::fromUtf8(QJsonDocument(cfg.toJson()).toJson()));
    s.recipes[0] = features;
  }
};

static FxStudy fxStudy(const QString &dir) {
  FxStudy f;
  f.s.dir = dir;
  f.s.name = QStringLiteral("Fx");
  QDir().mkpath(QDir(dir).filePath("waves/" + f.station));
  for (const QString &name : QDir(kFx).entryList(
           {QStringLiteral("*.mseed"), QStringLiteral("*.zip")}, QDir::Files))
    QFile::copy(QDir(kFx).filePath(name),
                QDir(dir).filePath("waves/" + f.station + "/" + name));
  QFile::copy(QDir(kFx).filePath("catalog.csv"),
              QDir(dir).filePath("catalog.csv"));
  f.s.catalog = QStringLiteral("catalog.csv");
  f.s.addWaveformRoot(QDir(dir).filePath("waves"));
  f.c = referenceCase(QStringLiteral("After the data"), utc(2024, 3, 6), 38.60,
                      39.30);
  f.c.id = f.s.freeCaseId(f.c.name);
  f.c.spanDays = 30;
  StationRef st;
  st.code = f.station;
  st.lat = 38.60;
  st.lon = 39.20;
  f.c.stations << st;
  f.s.cases << f.c;
  f.features = defaultRecipe(kToolFeatures);
  f.features.id = QStringLiteral("features");
  f.s.recipes << f.features;
  fx::Config cfg;
  cfg.fs = 5.0;
  cfg.winSec = 200;
  cfg.stepSec = 200;
  cfg.freqmin = 0.1;
  cfg.freqmax = 2.0;
  cfg.calibration.minMag = 2.0;
  f.setConfig(cfg);
  return f;
}

// What a run reported: every fraction, and the phases in the order they
// were first named.
struct Seen {
  QList<double> fractions;
  QStringList phases, log;
  RunHooks hooks() {
    RunHooks h;
    h.log = [this](const QString &l) { log << l; };
    h.progress = [this](double f, const QString &what) {
      fractions << f;
      const QString phase = what.section(QStringLiteral(" · "), 0, 0);
      if (phases.isEmpty() || phases.last() != phase)
        phases << phase;
    };
    return h;
  }
  bool neverBack() const {
    for (int i = 1; i < fractions.size(); ++i)
      if (fractions[i] + 1e-12 < fractions[i - 1])
        return false;
    return true;
  }
  bool logged(const QString &text) const {
    return log.join('\n').contains(text);
  }
};

void TstStudyFixes::oneBarForAFeaturesRun() {
  FxStudy f = fxStudy(tmp_.filePath("bar"));
  Seen seen;
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                  seen.hooks()));
  // One bar over the three phases: it starts at nothing, never goes back
  // (it used to run to the end for the calibration and start again for the
  // extraction), and ends full.
  QVERIFY(seen.fractions.size() > 5);
  QVERIFY(seen.neverBack());
  QCOMPARE(seen.fractions.first(), 0.0);
  QCOMPARE(seen.fractions.last(), 1.0);
  QCOMPARE(seen.phases,
           (QStringList{
               QStringLiteral("1/3 Unpacking the archives"),
               QStringLiteral(
                   "2/3 Calibrating chaos parameters on quiet periods"),
               QStringLiteral("3/3 Extracting features")}));
  // The calibration ends where the extraction begins, well short of the end.
  double calibrationEnd = 0;
  // (the shares are 5, 10 and 85 of 100)
  for (const double v : seen.fractions)
    if (v <= 0.15 + 1e-9)
      calibrationEnd = v;
  QVERIFY(qAbs(calibrationEnd - 0.15) < 1e-9);

  // The engine alone, without archives and with a calibration to reuse: the
  // phases that are left are numbered anew.
  const QTemporaryDir out;
  fx::Config cfg = f.cfg;
  cfg.station = f.station;
  cfg.dataDir = kFx;
  cfg.outputRoot = out.path();
  cfg.calibration.catalog = kFx + QStringLiteral("/catalog.csv");
  cfg.calibration.stationCoords = kFx + QStringLiteral("/stations.csv");
  for (const QString &name :
       QDir(kFx).entryList({QStringLiteral("*.mseed")}, QDir::Files))
    cfg.inputFiles << QDir(kFx).filePath(name);
  QList<fx::RunProgress> all;
  QStringList started;
  fx::RunHooks h;
  h.overall = [&all](const fx::RunProgress &p) { all << p; };
  h.phase = [&started](const QString &p) { started << p; };
  QString calibration;
  h.calibrated = [&calibration](const QString &path) { calibration = path; };
  fx::RunResult res = fx::runPipeline(cfg, h);
  QVERIFY(!res.featuresNpy.isEmpty());
  QCOMPARE(started,
           (QStringList{QStringLiteral(
                            "Calibrating chaos parameters on quiet periods"),
                        QStringLiteral("Extracting features")}));
  QCOMPARE(all.first().phases, 2);
  QCOMPARE(all.first().phase, 1);
  QCOMPARE(all.last().phase, 2);
  QCOMPARE(all.last().fraction, 1.0);
  QVERIFY(all.first().text.startsWith(QStringLiteral("1/2 Calibrating")));
  QCOMPARE(calibration, res.calibration);
  for (int i = 1; i < all.size(); ++i)
    QVERIFY(all[i].fraction >= all[i - 1].fraction);
  all.clear();
  started.clear();
  calibration.clear();
  cfg.calibrationFile = res.calibration;
  cfg.outputRoot = out.filePath("again");
  res = fx::runPipeline(cfg, h);
  QCOMPARE(started, QStringList{QStringLiteral("Extracting features")});
  QCOMPARE(all.first().phases, 1);
  QVERIFY(all.first().text.startsWith(QStringLiteral("1/1 Extracting")));
  QCOMPARE(all.last().fraction, 1.0);
  QVERIFY(calibration.isEmpty()); // loaded, not computed
}

void TstStudyFixes::calibrationIsKeptAndReused() {
  // An uninterrupted run, for what the features should be.
  FxStudy whole = fxStudy(tmp_.filePath("cal-whole"));
  Seen first;
  QVERIFY(runCell(whole.s, whole.c, whole.station, whole.features,
                  QStringLiteral("test"), first.hooks()));
  Cell done = cellOf(whole.s, whole.c, whole.station, whole.features);
  QCOMPARE(done.state, CellState::Done);
  QVERIFY(!done.record->calibrationKey.isEmpty());
  QVERIFY(!done.record->calibrationReused);
  QVERIFY2(first.logged(QStringLiteral(
               "Calibration redone: none is kept for this cell yet")),
           qPrintable(first.log.join('\n')));
  const QString wholeStore = runCalibrationDir(done.dir);
  QCOMPARE(QDir(wholeStore).entryList({"*.json"}, QDir::Files),
           QStringList{done.record->calibrationKey + ".json"});
  const auto table = [](const Cell &cell) {
    QFile f(QDir(cell.dir).filePath(
        cell.record->outputs.value("features").toString()));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  };
  const QByteArray expected = table(done);
  QVERIFY(expected.size() > 1000);

  // The same study elsewhere, stopped as the extraction starts: nothing of
  // the run is kept, except its calibration.
  FxStudy f = fxStudy(tmp_.filePath("cal-stopped"));
  Seen stopped;
  RunHooks hooks = stopped.hooks();
  hooks.shouldStop = [&stopped] {
    return stopped.logged(QStringLiteral("Extracting features"));
  };
  QVERIFY(!runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                   hooks));
  Cell cell = cellOf(f.s, f.c, f.station, f.features);
  QCOMPARE(cell.state, CellState::Missing);
  QVERIFY(!QDir(runTmpDir(cell.dir)).exists());
  const QString store = runCalibrationDir(cell.dir);
  const QStringList kept = QDir(store).entryList({"*.json"}, QDir::Files);
  QCOMPARE(kept.size(), 1);
  // (Its key is its own: the copies of the waveform files carry the time
  // they were copied, which is part of what a calibration is kept for.)
  const QString keptKey = QFileInfo(kept.first()).completeBaseName();
  QFile keptFile(QDir(store).filePath(kept.first()));
  QVERIFY(keptFile.open(QIODevice::ReadOnly));
  const QJsonObject keptJson = QJsonDocument::fromJson(keptFile.readAll()).object();
  QCOMPARE(keptJson.value("kept_key").toString(), keptKey);
  const QJsonObject parts = keptJson.value("kept_for").toObject();
  for (const char *part : {"settings", "catalog", "station", "span", "waveforms"})
    QVERIFY2(parts.contains(QLatin1String(part)), part);
  QVERIFY(keptJson.contains("tau")); // still a calibration file

  // Run again: the calibration is not made again, the bar has two phases,
  // and the features are those of the uninterrupted run, byte for byte.
  Seen second;
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                  second.hooks()));
  QVERIFY2(second.logged(QStringLiteral("Calibration reused")),
           qPrintable(second.log.join('\n')));
  QVERIFY(!second.logged(QStringLiteral("Calibrating chaos parameters")));
  QCOMPARE(second.phases,
           (QStringList{QStringLiteral("1/2 Unpacking the archives"),
                        QStringLiteral("2/2 Extracting features")}));
  QVERIFY(second.neverBack());
  QCOMPARE(second.fractions.last(), 1.0);
  cell = cellOf(f.s, f.c, f.station, f.features);
  QCOMPARE(cell.state, CellState::Done);
  QVERIFY(cell.record->calibrationReused);
  QCOMPARE(cell.record->calibrationKey, keptKey);
  QCOMPARE(table(cell), expected);
  // The values the case fitted for itself are still in the record.
  QCOMPARE(cell.record->fitted.value("tau"), done.record->fitted.value("tau"));
  QVERIFY(cell.record->fitted.contains("tau"));
  // The store survives the finished run taking the folder's place.
  QCOMPARE(QDir(store).entryList({"*.json"}, QDir::Files).size(), 1);

  // Asked for: made again, although nothing changed.
  Seen forced;
  RunHooks fresh = forced.hooks();
  fresh.freshCalibration = true;
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                  fresh));
  QVERIFY2(forced.logged(QStringLiteral("Calibration redone: asked for")),
           qPrintable(forced.log.join('\n')));
  QCOMPARE(forced.phases.size(), 3);
  cell = cellOf(f.s, f.c, f.station, f.features);
  QVERIFY(!cell.record->calibrationReused);
  QCOMPARE(table(cell), expected);
}

void TstStudyFixes::calibrationIsRedoneWhenItsInputsChange() {
  FxStudy f = fxStudy(tmp_.filePath("cal-changes"));
  const auto run = [&f](Seen &seen) {
    return runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                   seen.hooks());
  };
  const auto reason = [](const Seen &seen) {
    for (const QString &l : seen.log)
      if (l.contains(QStringLiteral("Calibration re")))
        return l.trimmed();
    return QString();
  };
  Seen a;
  QVERIFY(run(a));
  QCOMPARE(reason(a), QStringLiteral("Calibration redone: none is kept for "
                                     "this cell yet"));
  // Nothing changed: reused.
  Seen b;
  QVERIFY(run(b));
  QVERIFY2(reason(b).startsWith(QStringLiteral("Calibration reused")),
           qPrintable(reason(b)));
  // A setting the calibration does not depend on (the step between
  // windows): the recipe is another revision, the calibration is the same.
  fx::Config cfg = f.cfg;
  cfg.stepSec = 100;
  f.features.revision += 1;
  f.setConfig(cfg);
  Seen c;
  QVERIFY(run(c));
  QVERIFY2(reason(c).startsWith(QStringLiteral("Calibration reused")),
           qPrintable(reason(c)));
  // A calibration setting.
  cfg.calibration.minMag = 2.5;
  f.features.revision += 1;
  f.setConfig(cfg);
  Seen d;
  QVERIFY(run(d));
  QCOMPARE(reason(d), QStringLiteral("Calibration redone: the calibration "
                                     "settings changed"));
  // The catalog: one more earthquake, far away.
  {
    QFile cat(f.s.catalogPath());
    QVERIFY(cat.open(QIODevice::Append));
    cat.write("05/03/2024 01:30:00,36.00,30.00,10.0,ML,4.1,Elsewhere\n");
  }
  Seen e;
  QTRY_VERIFY(sha256Cached(f.s.catalogPath()) == sha256Of(f.s.catalogPath()));
  QVERIFY(run(e));
  QCOMPARE(reason(e), QStringLiteral("Calibration redone: the catalog changed"));
  // The station's position.
  f.c.stations[0].lat += 0.01;
  f.s.cases[0] = f.c;
  Seen g;
  QVERIFY(run(g));
  QCOMPARE(reason(g), QStringLiteral("Calibration redone: the station's "
                                     "position changed"));
  // The span.
  f.c.spanDays = 29;
  f.s.cases[0] = f.c;
  Seen h;
  QVERIFY(run(h));
  QCOMPARE(reason(h), QStringLiteral("Calibration redone: the span changed"));
  // The waveform files: one of them replaced by a copy with another time.
  const QString waves = QDir(f.s.dir).filePath("waves/" + f.station);
  const QString one =
      QDir(waves).entryList({QStringLiteral("*HHN*.mseed")}).first();
  {
    QFile file(QDir(waves).filePath(one));
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-86400),
                             QFileDevice::FileModificationTime));
  }
  Seen i;
  QVERIFY(run(i));
  QCOMPARE(reason(i), QStringLiteral("Calibration redone: the waveform files "
                                     "changed"));
  // And once more with nothing changed: reused again. Only the latest two
  // are kept.
  Seen j;
  QVERIFY(run(j));
  QVERIFY2(reason(j).startsWith(QStringLiteral("Calibration reused")),
           qPrintable(reason(j)));
  const Cell cell = cellOf(f.s, f.c, f.station, f.features);
  QCOMPARE(QDir(runCalibrationDir(cell.dir))
               .entryList({"*.json"}, QDir::Files)
               .size(),
           2);
  // Another station of the same case has a store of its own.
  QVERIFY(runCalibrationDir(runDir(f.s, f.c, QStringLiteral("OTHER"),
                                   f.features)) !=
          runCalibrationDir(cell.dir));
}

QTEST_GUILESS_MAIN(TstStudyFixes)
#include "tst_study_fixes.moc"
