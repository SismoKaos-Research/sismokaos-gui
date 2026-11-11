// Defects a survey of the code found that the other tests did not: times
// read in the machine's zone, the earthquake counted among the events before
// itself, windows holding the shaking labelled "before", features computed
// on what was recorded after the event or on two sensors at once, a finished
// run thrown away, a stop recorded as a failure, a quit that aborted, a
// catalog cut short without a word.
//
// The checks that involve time run under several time zones (set in the
// test itself where the platform lets a process change its zone, and once
// more by ctest with TZ=Europe/Istanbul): CI machines are in UTC, where
// reading a time as local time goes unnoticed.
#include <QDataStream>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QLockFile>
#include <QSysInfo>
#include <QtTest>
#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#include <cmath>
#include <ctime>
#include <limits>

#include "analysis/events.h"
#include "analysis/label.h"
#include "fx/catalog.h"
#include "fx/config.h"
#include "fx/csv.h"
#include "fx/engine.h"
#include "fx/mseed.h"
#include "fx/npy.h"
#include "fx/preprocess.h"
#include "jobs/taskjob.h"
#include "miniz.h"
#include "sfc/dataset.h"
#include "sfc/settings.h"
#include "study/runs.h"
#include "study/runinfo.h"
#include "study/store.h"
#include "study/study.h"
#include "study/waveindex.h"

using namespace skg;
using namespace skg::study;

static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

const QString kFx = QStringLiteral(SKG_TEST_DATA "/fx/enz");
const QString kSynthetic =
    QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv");

QDateTime utc(int y, int m, int d, int h = 0, int min = 0, int s = 0) {
  return QDateTime(QDate(y, m, d), QTime(h, min, s), QTimeZone::UTC);
}

double epochOf(int y, int m, int d, int h = 0, int min = 0, int s = 0) {
  return double(fx::daysFromCivil(y, m, d) * 86400 + h * 3600 + min * 60 + s);
}

void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

// Puts the process in another time zone, where a process can do that to
// itself. False where it cannot (Windows takes its zone from the system, not
// from TZ) or where the zone is not installed.
bool setZone(const char *zone, int expectOffsetSeconds) {
#ifdef Q_OS_WIN
  Q_UNUSED(zone);
  Q_UNUSED(expectOffsetSeconds);
  return false;
#else
  qputenv("TZ", zone);
  tzset();
  return QDateTime(QDate(2023, 2, 6), QTime(12, 0)).offsetFromUtc() ==
         expectOffsetSeconds;
#endif
}

struct TimeZoneCase {
  const char *name;
  int winterOffset; // seconds east of UTC on 2023-02-06
};
const TimeZoneCase kZones[] = {{"UTC", 0},
                       {"Europe/Istanbul", 3 * 3600},
                       {"America/New_York", -5 * 3600}};

// Restores the zone the test started in.
struct ZoneGuard {
  QByteArray was = qgetenv("TZ");
  bool had = qEnvironmentVariableIsSet("TZ");
  ~ZoneGuard() {
#ifndef Q_OS_WIN
    if (had)
      qputenv("TZ", was);
    else
      qunsetenv("TZ");
    tzset();
#endif
  }
};

enum class Format { Afad, IsoZ, IsoBare };

// The events as a catalog file. `shiftHours` writes the times that many
// hours ahead of UTC, as a catalog kept in local time has them (to be read
// with that offset).
void writeCatalog(const QString &path, const std::vector<analysis::Event> &ev,
                  Format format, double shiftHours = 0) {
  QByteArray csv = format == Format::Afad
                       ? "Date,Longitude,Latitude,Depth,Rms,Type,Magnitude,"
                         "Location,EventID\n"
                       : "time,latitude,longitude,depth,mag,place\n";
  for (const analysis::Event &e : ev) {
    const QDateTime t = QDateTime::fromSecsSinceEpoch(
        qint64(e.epoch + shiftHours * 3600.0), QTimeZone::UTC);
    const QString lat = QString::number(e.lat, 'f', 4),
                  lon = QString::number(e.lon, 'f', 4),
                  mag = QString::number(e.mag, 'f', 1);
    if (format == Format::Afad)
      csv += QStringLiteral("%1,%2,%3,10.00,0.3,ML,%4,Synthetic,%5\n")
                 .arg(t.toString(QStringLiteral("dd/MM/yyyy HH:mm:ss")), lon,
                      lat, mag, e.id)
                 .toUtf8();
    else
      csv += QStringLiteral("%1,%2,%3,10.0,%4,Synthetic\n")
                 .arg(t.toString(format == Format::IsoZ
                                     ? QStringLiteral("yyyy-MM-ddTHH:mm:ss'Z'")
                                     : QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                      lat, lon, mag)
                 .toUtf8();
  }
  write(path, csv);
}

struct Variant {
  const char *name;
  Format format;
  double shiftHours;
};
const Variant kVariants[] = {{"afad", Format::Afad, 0},
                             {"iso-z", Format::IsoZ, 0},
                             {"iso-bare", Format::IsoBare, 0},
                             {"afad-plus3", Format::Afad, 3},
                             {"iso-bare-plus3", Format::IsoBare, 3}};

Case caseAt(Study &s, const QString &name, const QDateTime &time, double lat,
            double lon, double mag) {
  Case c;
  c.name = name;
  c.id = s.freeCaseId(name);
  c.time = time;
  c.lat = lat;
  c.lon = lon;
  c.depthKm = 10;
  c.magnitude = mag;
  c.spanDays = 270;
  c.zone.radiusKm = 150;
  return c;
}

Recipe quickIndicators(Study &s) {
  Recipe r = defaultRecipe(kToolIndicators);
  r.id = s.freeRecipeId(r.name);
  r.settings.insert(QStringLiteral("window_months"), 6);
  r.settings.insert(QStringLiteral("ma_windows"), QVariantList{3});
  r.settings.insert(QStringLiteral("figures"), false);
  return r;
}

// The times (seconds since 1970 UTC) of the events an indicators run took as
// "before" its case: the rows of the catalog.csv in its folder.
std::vector<double> beforeSet(const QString &runFolder) {
  const std::vector<analysis::Event> ev =
      analysis::loadEvents(QDir(runFolder).filePath("catalog.csv"), 0.0);
  std::vector<double> out;
  for (const analysis::Event &e : ev)
    out.push_back(e.epoch);
  return out;
}

// One 512-byte miniSEED 2 record of 32-bit integers (up to 112 samples).
QByteArray intRecord(const QDateTime &start, const QByteArray &sta,
                     const QByteArray &cha, const std::vector<qint32> &samples,
                     int rate = 1, const QByteArray &loc = "  ") {
  QByteArray r;
  QDataStream s(&r, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::BigEndian);
  s.writeRawData("000001", 6);
  s.writeRawData("D", 1);
  s.writeRawData(" ", 1);
  s.writeRawData(sta.leftJustified(5, ' ').constData(), 5);
  s.writeRawData(loc.leftJustified(2, ' ').constData(), 2);
  s.writeRawData(cha.constData(), 3);
  s.writeRawData("TU", 2);
  s << quint16(start.date().year()) << quint16(start.date().dayOfYear())
    << quint8(start.time().hour()) << quint8(start.time().minute())
    << quint8(start.time().second()) << quint8(0) << quint16(0);
  s << quint16(samples.size()) << qint16(rate) << qint16(1);
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1);
  s << qint32(0) << quint16(64) << quint16(48);
  s << quint16(1000) << quint16(0) << quint8(3) << quint8(1) << quint8(9)
    << quint8(0);
  r = r.leftJustified(64, '\0');
  QDataStream d(&r, QIODevice::Append);
  d.setByteOrder(QDataStream::BigEndian);
  for (qint32 v : samples)
    d << v;
  return r.leftJustified(512, '\0');
}

// `seconds` of a 0.2 Hz sine of `amplitude` on the three channels `code`E/N/Z
// of a station, one sample a second, in 512-byte records.
QByteArray sensor(const QDateTime &start, const QByteArray &sta,
                  const QByteArray &code, int seconds, double amplitude,
                  int rate = 1, const QByteArray &loc = "  ") {
  QByteArray out;
  const int perRecord = 112;
  const int total = seconds * rate;
  for (const char comp : {'E', 'N', 'Z'})
    for (int at = 0; at < total; at += perRecord) {
      std::vector<qint32> v;
      for (int i = at; i < std::min(total, at + perRecord); ++i)
        v.push_back(qint32(std::lround(
            amplitude * std::sin(2.0 * M_PI * 0.2 * double(i) / rate))));
      out += intRecord(start.addMSecs(qint64(at) * 1000 / rate), sta,
                       code + comp, v, rate, loc);
    }
  return out;
}

// A gzip member holding `text`.
QByteArray gzipMember(const QByteArray &text) {
  size_t n = 0;
  void *raw = tdefl_compress_mem_to_heap(text.constData(), size_t(text.size()),
                                         &n, 6);
  QByteArray gz;
  gz.append("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10);
  gz.append(static_cast<const char *>(raw), qsizetype(n));
  mz_free(raw);
  const quint32 crc = quint32(mz_crc32(
      MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(text.constData()),
      size_t(text.size())));
  const quint32 size = quint32(text.size());
  for (const quint32 v : {crc, size})
    for (int b = 0; b < 4; ++b)
      gz.append(char((v >> (8 * b)) & 0xff));
  return gz;
}

// The study the feature tests run: the repository's two hours of synthetic
// station data on 2024-03-05, a catalog beside it, one reference case.
struct FxStudy {
  Study s;
  Case c;
  Recipe features;
  QString station = QStringLiteral("SYN1");
};

FxStudy fxStudy(const QString &dir, const QDateTime &caseTime) {
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
  f.c = referenceCase(QStringLiteral("After the data"), caseTime, 38.60, 39.30);
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
  fx::Config cfg;
  cfg.fs = 5.0;
  cfg.winSec = 200;
  cfg.stepSec = 200;
  cfg.freqmin = 0.1;
  cfg.freqmax = 2.0;
  cfg.calibration.minMag = 2.0;
  f.features.settings.insert(
      QStringLiteral("config_json"),
      QString::fromUtf8(QJsonDocument(cfg.toJson()).toJson()));
  f.s.recipes << f.features;
  return f;
}

} // namespace

class TstCorrectness : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() { initResources(); }
  void aTimeWithoutAZoneIsUtc();
  void oneCatalogReadsTheSameForEveryTool();
  void theCalibrationReadsTheCatalogAsTheOthersDo();
  void theMainshockIsNeverItsOwnForerunner();
  void aWindowThatEndsAfterTheEventIsNotBeforeIt();
  void classificationNeedsTheWindowLength();
  void featuresReadOnlyThePeriodAskedFor();
  void aStudyRunStopsAtTheEarthquake();
  void oneSensorIsRead();
  void olderFeatureRunsSayWhyTheyAreStale();
  void archiveMembersOfOneNameBothStay();
  void anotherRunsUnpackedFilesAreLeftAlone();
  void aGzipIsReadToItsEndOrNotAtAll();
  void aFinishedRunIsPutInPlaceNotMadeAgain();
  void stoppingTheCalibrationLeavesNothing();
  void aJobIsWaitedForNotAborted();
  void olderRunsOfEveryToolSayWhatChanged();
  void aCopiedArchiveIsTheSameInput();
  void aStudyOpenElsewhereIsNotRunInto();
  void aFinishedRunThatWaitsIsNoLeftover();

private:
  QTemporaryDir tmp_;
};

// --- time zones ----------------------------------------------------------

void TstCorrectness::aTimeWithoutAZoneIsUtc() {
  const ZoneGuard guard;
  int zonesTried = 0;
  for (const TimeZoneCase &z : kZones) {
    if (!setZone(z.name, z.winterOffset) && qstrcmp(z.name, "UTC") != 0)
      continue; // this platform or machine cannot be put in that zone
    ++zonesTried;
    const double t = epochOf(2023, 2, 6, 1, 17, 34);
    // What pandas writes, the same with a T, with a Z, and with an offset.
    QCOMPARE(fx::parseIsoUtc(QStringLiteral("2023-02-06 01:17:34")).value(), t);
    QCOMPARE(fx::parseIsoUtc(QStringLiteral("2023-02-06T01:17:34")).value(), t);
    QCOMPARE(fx::parseIsoUtc(QStringLiteral(" 2023-02-06T01:17:34Z ")).value(),
             t);
    QCOMPARE(
        fx::parseIsoUtc(QStringLiteral("2023-02-06T04:17:34+03:00")).value(),
        t);
    QCOMPARE(fx::parseIsoUtc(QStringLiteral("2023-02-05T20:17:34-0500")).value(),
             t);
    QCOMPARE(fx::parseIsoUtc(QStringLiteral("2023-02-06T01:17:34.250")).value(),
             t + 0.25);
    QCOMPARE(fx::parseIsoUtc(QStringLiteral("2023-02-06 01:17")).value(),
             epochOf(2023, 2, 6, 1, 17));
    QCOMPARE(fx::parseIsoUtc(QStringLiteral("2023-02-06")).value(),
             epochOf(2023, 2, 6));
    // An hour that does not exist on Istanbul's clocks that day (they went
    // from 03:00 to 04:00): parsed as local time and relabelled, it moved.
    QCOMPARE(fx::parseIsoUtc(QStringLiteral("2015-03-29 03:30:00")).value(),
             epochOf(2015, 3, 29, 3, 30));
    QCOMPARE(fx::isoUtcDateTime(QStringLiteral("2015-03-29 03:30:00")),
             utc(2015, 3, 29, 3, 30));
    QCOMPARE(fx::isoUtcDateTime(QStringLiteral("2023-02-06 01:17:34"))
                 .timeSpec(),
             Qt::UTC);
    // Not such a date: nothing, rather than a guess.
    QVERIFY(!fx::parseIsoUtc(QStringLiteral("06/02/2023 01:17:34")));
    QVERIFY(!fx::parseIsoUtc(QStringLiteral("2023-13-01")));
    QVERIFY(!fx::parseIsoUtc(QStringLiteral("2023-02-30 00:00:00")));
    QVERIFY(!fx::parseIsoUtc(QString()));
    QVERIFY(!fx::isoUtcDateTime(QStringLiteral("soon")).isValid());

    // A window id of the SCAP tables carries its hour: the same instant in
    // every zone, also for an hour inside Istanbul's gap.
    fx::NpyTable table;
    table.fields = {
        fx::NpyField{QStringLiteral("Pencere_ID"), QStringLiteral("<U50")},
        fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")}};
    table.strings = {{QStringLiteral("2015_03_29_03_w01")}, {}};
    table.numeric = {{}, {10.0}};
    table.rows = 1;
    const auto span = analysis::windowSpan(table, 0, 200);
    QVERIFY(span.has_value());
    QCOMPARE(span->end, epochOf(2015, 3, 29, 3, 10));

    // The settings of a classification: an event time without a zone.
    const sfc::Settings st = sfc::Settings::fromToml(
        QStringLiteral("eq_time = \"2020-01-24 17:55:14\"\n"), sfc::Settings());
    QCOMPARE(st.eqDate, QDate(2020, 1, 24));
    QCOMPARE(st.eqCutoffHour, 17);
    const sfc::Settings late = sfc::Settings::fromToml(
        QStringLiteral("eq_time = 2020-01-24T23:30:00\n"), sfc::Settings());
    QCOMPARE(late.eqDate, QDate(2020, 1, 24));
    QCOMPARE(late.eqCutoffHour, 23);
    const sfc::Settings offset = sfc::Settings::fromToml(
        QStringLiteral("eq_time = 2020-01-25T01:30:00+03:00\n"),
        sfc::Settings());
    QCOMPARE(offset.eqDate, QDate(2020, 1, 24));
    QCOMPARE(offset.eqCutoffHour, 22);
  }
  QVERIFY(zonesTried >= 1);
#ifndef Q_OS_WIN
  if (zonesTried < 2)
    QSKIP("no time zone but UTC could be set on this machine; the checks ran "
          "in UTC only");
#endif
}

void TstCorrectness::oneCatalogReadsTheSameForEveryTool() {
  const ZoneGuard guard;
  QVERIFY(setZone("UTC", 0) || true);
  const std::vector<analysis::Event> base = analysis::loadEvents(kSynthetic);
  QVERIFY(base.size() > 2000);
  const QString dir = tmp_.filePath("catalogs");
  for (const Variant &v : kVariants)
    writeCatalog(QDir(dir).filePath(QString::fromLatin1(v.name) + ".csv"),
                 base, v.format, v.shiftHours);

  // The same file, whichever reader, whichever zone the machine is in.
  for (const TimeZoneCase &z : kZones) {
    if (!setZone(z.name, z.winterOffset) && qstrcmp(z.name, "UTC") != 0)
      continue;
    for (const Variant &v : kVariants) {
      const QString path =
          QDir(dir).filePath(QString::fromLatin1(v.name) + ".csv");
      const QByteArray what =
          QByteArray(v.name) + " in " + QByteArray(z.name);
      const std::vector<analysis::Event> got =
          analysis::loadEvents(path, v.shiftHours);
      QVERIFY2(got.size() == base.size(), what.constData());
      double worst = 0;
      for (size_t i = 0; i < base.size(); ++i)
        worst = std::max(worst, std::abs(got[i].epoch - base[i].epoch));
      QVERIFY2(worst < 1e-6, what.constData());
      if (v.shiftHours != 0)
        continue;
      // The calibration's own reader takes its times as UTC.
      const std::vector<fx::CatalogEvent> cal = fx::loadCatalog(path);
      QVERIFY2(cal.size() == base.size(), what.constData());
      worst = 0;
      for (size_t i = 0; i < base.size(); ++i)
        worst = std::max(worst, std::abs(cal[i].epoch - base[i].epoch));
      QVERIFY2(worst < 1e-6, what.constData());
    }
  }

  // And a run: the events an indicators run takes as "before" the case are
  // the same whichever way the catalog is written, and none of them is the
  // earthquake or later.
  qsizetype expected = -1;
  for (const char *zone : {"UTC", "Europe/Istanbul"}) {
    if (!setZone(zone, qstrcmp(zone, "UTC") == 0 ? 0 : 3 * 3600) &&
        qstrcmp(zone, "UTC") != 0)
      continue;
    for (const Variant &v : kVariants) {
      Study s;
      s.dir = tmp_.filePath(QStringLiteral("study-%1-%2")
                                .arg(QString::fromLatin1(v.name),
                                     QString::fromLatin1(zone).replace('/', '-')));
      s.name = QStringLiteral("Zones");
      QVERIFY(QDir().mkpath(s.dir));
      s.catalog = QDir(dir).filePath(QString::fromLatin1(v.name) + ".csv");
      s.catalogUtcOffsetHours = v.shiftHours;
      Case c = caseAt(s, QStringLiteral("B"), utc(2020, 7, 7, 7, 7, 7), 38.5001,
                      39.4906, 6.4);
      s.cases << c;
      const Recipe r = quickIndicators(s);
      s.recipes << r;
      const QByteArray what = QByteArray(v.name) + " in " + zone;
      const std::vector<analysis::Event> events =
          analysis::loadEvents(s.catalogPath(), s.catalogUtcOffsetHours);
      const analysis::Event *own = eventOfCase(c, events);
      QVERIFY2(own != nullptr, what.constData());
      QCOMPARE(own->epoch, double(c.time.toSecsSinceEpoch()));
      QVERIFY2(runCell(s, c, QString(), r, QStringLiteral("test"), {}),
               what.constData());
      const std::vector<double> before = beforeSet(runDir(s, c, QString(), r));
      QVERIFY2(before.size() > 100, what.constData());
      for (double t : before)
        QVERIFY2(t < double(c.time.toSecsSinceEpoch()), what.constData());
      if (expected < 0)
        expected = qsizetype(before.size());
      QVERIFY2(qsizetype(before.size()) == expected, what.constData());
    }
  }
}

void TstCorrectness::theCalibrationReadsTheCatalogAsTheOthersDo() {
  // A catalog written by pandas (ISO times, lower-case columns) is one the
  // Cases page and the indicators read; the calibration of a features run
  // read catalogs with its own reader, which knew AFAD's layout only, and
  // said this one held no readable events. And kept in Turkey time, it has
  // to come out in UTC whichever zone the machine is in.
  const ZoneGuard guard;
  const std::vector<analysis::Event> base =
      analysis::loadEvents(kFx + QStringLiteral("/catalog.csv"));
  int n = 0;
  for (const Variant &v : {kVariants[2], kVariants[4]}) {
    const bool east = setZone("Europe/Istanbul", 3 * 3600);
    Q_UNUSED(east);
    FxStudy f = fxStudy(tmp_.filePath(QStringLiteral("fx-catalog-%1").arg(++n)),
                        utc(2024, 3, 6));
    writeCatalog(f.s.catalogPath(), base, v.format, v.shiftHours);
    f.s.catalogUtcOffsetHours = v.shiftHours;
    QStringList log;
    RunHooks hooks;
    hooks.log = [&log](const QString &l) { log << l; };
    QVERIFY2(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                     hooks),
             v.name);
    const QString copy = QDir(runDir(f.s, f.c, f.station, f.features))
                             .filePath("catalog_utc.csv");
    QVERIFY2(QFileInfo::exists(copy), v.name);
    const std::vector<fx::CatalogEvent> read = fx::loadCatalog(copy);
    QCOMPARE(read.size(), base.size());
    for (size_t i = 0; i < base.size(); ++i)
      QCOMPARE(read[i].epoch, std::floor(base[i].epoch));
  }
}

void TstCorrectness::theMainshockIsNeverItsOwnForerunner() {
  const std::vector<analysis::Event> base = analysis::loadEvents(kSynthetic);
  const QDateTime when = utc(2020, 7, 7, 7, 7, 7);
  const double t = double(when.toSecsSinceEpoch());
  // The earthquake as the study's catalog has it.
  const analysis::Event *truth = nullptr;
  for (const analysis::Event &e : base)
    if (std::abs(e.epoch - t) < 0.5 && e.mag > 6.3)
      truth = &e;
  QVERIFY(truth);

  // 1. The case comes from another agency's list (a second off, a few km
  // away, 0.2 larger), and the study's catalog is kept three hours behind
  // UTC while its offset is left at zero. The earthquake sits in the
  // catalog three hours BEFORE the case's time, with what followed it.
  {
    Study s;
    s.dir = tmp_.filePath("mainshock-shifted");
    QVERIFY(QDir().mkpath(s.dir));
    s.catalog = QStringLiteral("catalog.csv");
    writeCatalog(s.catalogPath(), base, Format::Afad, -3);
    Case c = caseAt(s, QStringLiteral("B"), when.addMSecs(1500), truth->lat + 0.05,
                    truth->lon - 0.04, 6.6);
    s.cases << c;
    const Recipe r = quickIndicators(s);
    s.recipes << r;
    const std::vector<analysis::Event> events =
        analysis::loadEvents(s.catalogPath(), 0.0);
    const analysis::Event *own = eventOfCase(c, events);
    QVERIFY(own != nullptr);
    QCOMPARE(own->epoch, t - 3 * 3600.0);
    QStringList log;
    RunHooks hooks;
    hooks.log = [&log](const QString &l) { log << l; };
    QVERIFY(runCell(s, c, QString(), r, QStringLiteral("test"), hooks));
    const std::vector<double> before = beforeSet(runDir(s, c, QString(), r));
    QVERIFY(before.size() > 100);
    // Nothing at or after the earthquake as this catalog times it: not the
    // earthquake, and not the three hours that followed it.
    for (double e : before)
      QVERIFY(e < t - 3 * 3600.0);
    QVERIFY2(log.join('\n').contains(QStringLiteral("time offset")),
             qPrintable(log.join('\n')));
  }

  // 2. The catalog holds no event that could be the case's earthquake, but
  // a much larger one near it a few hours earlier. That is not counted as a
  // forerunner without a word: the events stop before it, and the run's
  // record says so.
  {
    std::vector<analysis::Event> changed = base;
    for (analysis::Event &e : changed)
      if (std::abs(e.epoch - t) < 0.5 && e.mag > 6.3) {
        e.epoch = t - 5 * 3600.0;
        e.mag = 7.9;
      }
    std::sort(changed.begin(), changed.end(),
              [](const analysis::Event &a, const analysis::Event &b) {
                return a.epoch < b.epoch;
              });
    Study s;
    s.dir = tmp_.filePath("mainshock-larger");
    QVERIFY(QDir().mkpath(s.dir));
    s.catalog = QStringLiteral("catalog.csv");
    writeCatalog(s.catalogPath(), changed, Format::Afad);
    Case c = caseAt(s, QStringLiteral("B"), when, truth->lat, truth->lon, 6.4);
    s.cases << c;
    const Recipe r = quickIndicators(s);
    s.recipes << r;
    QVERIFY(eventOfCase(c, changed) == nullptr);
    QVERIFY(largeEventJustBefore(c, changed) != nullptr);
    QVERIFY(runCell(s, c, QString(), r, QStringLiteral("test"), {}));
    for (double e : beforeSet(runDir(s, c, QString(), r)))
      QVERIFY(e < t - 5 * 3600.0);
    const Cell cell = cellOf(s, c, QString(), r);
    QCOMPARE(cell.state, CellState::Done);
    QVERIFY2(cell.warnings.join('\n').contains(
                 QStringLiteral("taken to be the earthquake itself")),
             qPrintable(cell.warnings.join('\n')));
  }
}

// --- classification ------------------------------------------------------

void TstCorrectness::aWindowThatEndsAfterTheEventIsNotBeforeIt() {
  // Windows of 200 s every 50 s around an earthquake at 10:01:00. The one
  // from 09:59:00 to 10:02:20 starts in the hour before the event and holds
  // its first 80 seconds: it was kept, and labelled "before".
  const QDateTime event = utc(2020, 1, 24, 10, 1, 0);
  for (const double win : {200.0, 100.0}) {
    fx::NpyTable t;
    t.fields = {
        fx::NpyField{QStringLiteral("Pencere_ID"), QStringLiteral("<U50")},
        fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")},
        fx::NpyField{QStringLiteral("N_RMS"), QStringLiteral("<f8")}};
    QStringList ids;
    std::vector<double> ends, rms;
    const double day0 = double(utc(2020, 1, 23).toSecsSinceEpoch());
    for (double end = day0 + win; end <= day0 + 2 * 86400.0; end += 50.0) {
      ids << QStringLiteral("w%1").arg(ids.size());
      ends.push_back(end / 60.0);
      rms.push_back(1.0);
    }
    t.rows = int(ids.size());
    t.strings = {ids, {}, {}};
    t.numeric = {{}, ends, rms};

    sfc::Settings st;
    st.station = QStringLiteral("SYN1");
    st.eqDate = event.date();
    st.eqCutoffHour = event.time().hour();
    sfc::DaySelection sel;
    sfc::DaySelection::Row eve, day;
    eve.date = eve.planned = event.date().addDays(-1);
    eve.label = 1;
    eve.daysToEq = 1;
    day.date = day.planned = event.date();
    day.label = 0;
    sel.rows << eve << day;
    const double eventEpoch = double(event.toSecsSinceEpoch());
    const double hourStart = double(utc(2020, 1, 24, 10).toSecsSinceEpoch());

    // With the event's own time: nothing that ends after it.
    st.eqEpoch = eventEpoch;
    sfc::Dataset d =
        sfc::fromFxTable(t, st, sel, {QStringLiteral("N_RMS")}, win);
    QVERIFY(d.rows() > 1000);
    double latestEnd = 0;
    for (int r = 0; r < d.rows(); ++r) {
      const QDateTime start = fx::isoUtcDateTime(d.windowStart[size_t(r)]);
      QVERIFY(start.isValid());
      latestEnd = std::max(latestEnd, double(start.toSecsSinceEpoch()) + win);
    }
    QVERIFY2(latestEnd <= eventEpoch,
             qPrintable(QStringLiteral("a window of %1 s ends %2 s after the "
                                       "event")
                            .arg(win)
                            .arg(latestEnd - eventEpoch)));
    // ... and everything that ends before it is still there.
    QVERIFY(latestEnd > eventEpoch - 50.0);

    // With the hour alone (the tool on its own page): nothing that ends
    // after the start of that hour.
    st.eqEpoch.reset();
    d = sfc::fromFxTable(t, st, sel, {QStringLiteral("N_RMS")}, win);
    latestEnd = 0;
    for (int r = 0; r < d.rows(); ++r)
      latestEnd = std::max(
          latestEnd,
          double(fx::isoUtcDateTime(d.windowStart[size_t(r)]).toSecsSinceEpoch()) +
              win);
    QVERIFY(latestEnd <= hourStart);
    QVERIFY(latestEnd > hourStart - 50.0);
  }
}

void TstCorrectness::classificationNeedsTheWindowLength() {
  // The classification places each window in time from the length the
  // features were made with; it assumed 200 s whatever the recipe said. It
  // now reads the length from the features run, and without it refuses.
  Study s;
  s.dir = tmp_.filePath("window-length");
  QVERIFY(QDir().mkpath(s.dir));
  s.name = QStringLiteral("W");
  Case c = caseAt(s, QStringLiteral("A"), utc(2020, 1, 24, 10, 1, 0), 38.4,
                  39.1, 6.7);
  StationRef station;
  station.code = QStringLiteral("SYN1");
  station.lat = 38.5;
  station.lon = 39.2;
  c.stations << station;
  s.cases << c;
  Recipe fea = defaultRecipe(kToolFeatures);
  fea.id = QStringLiteral("features");
  s.recipes << fea;
  Recipe cls = defaultRecipe(kToolClassification);
  cls.id = QStringLiteral("classes");
  cls.settings.insert(QStringLiteral("features_recipe"), fea.id);
  cls.settings.insert(QStringLiteral("classes"),
                      QVariantList{QStringLiteral("1-5"), QStringLiteral("6-10")});
  cls.settings.insert(QStringLiteral("models"),
                      QVariantList{QStringLiteral("logistic")});
  cls.settings.insert(QStringLiteral("quick"), true);
  cls.settings.insert(QStringLiteral("cv_folds"), 0);
  s.recipes << cls;

  // A features run stood in for: windows of 100 s every 100 s over twelve
  // days up to the end of the event day.
  const QString dir = runDir(s, c, station.code, fea);
  QVERIFY(QDir().mkpath(dir));
  fx::NpyTable t;
  t.fields = {fx::NpyField{QStringLiteral("Pencere_ID"), QStringLiteral("<U50")},
              fx::NpyField{QStringLiteral("Zaman_Dk"), QStringLiteral("<f8")},
              fx::NpyField{QStringLiteral("N_RMS"), QStringLiteral("<f8")},
              fx::NpyField{QStringLiteral("N_WOLF_LYE"), QStringLiteral("<f8")}};
  QStringList ids;
  std::vector<double> ends, rms, wolf;
  QRandomGenerator rng(3);
  const double first = double(utc(2020, 1, 13).toSecsSinceEpoch());
  for (double end = first + 100.0; end <= first + 12 * 86400.0; end += 100.0) {
    const double dayNo = std::floor((end - first) / 86400.0);
    ids << QStringLiteral("w%1").arg(ids.size());
    ends.push_back(end / 60.0);
    rms.push_back(100.0 + dayNo + rng.generateDouble());
    wolf.push_back(0.1 * dayNo + 0.5 * rng.generateDouble());
  }
  t.rows = int(ids.size());
  t.strings = {ids, {}, {}, {}};
  t.numeric = {{}, ends, rms, wolf};
  QVERIFY(fx::writeNpy(QDir(dir).filePath("SYN1_features.npy"), t));
  RunRecord rec;
  rec.tool = kToolFeatures;
  rec.recipe = fea.id;
  rec.caseId = c.id;
  rec.station = station.code;
  rec.revision = fea.revision;
  rec.caseSignature = runSignature(s, c, station.code, kToolFeatures);
  rec.ok = true;
  rec.started = rec.finished = QDateTime::currentDateTimeUtc();
  rec.outputs.insert(QStringLiteral("features"),
                     QStringLiteral("SYN1_features.npy"));
  QVERIFY(rec.write(dir));

  // No metadata beside the table: the run says what is missing.
  try {
    runCell(s, c, station.code, cls, QStringLiteral("test"), {});
    QFAIL("a classification ran without knowing how long the windows are");
  } catch (const std::exception &e) {
    QVERIFY2(QString::fromUtf8(e.what()).contains(
                 QStringLiteral("_run_metadata.json")),
             e.what());
  }

  // With it (100 s windows): the labelled table's windows are 100 s long,
  // and none ends after the earthquake.
  fx::Config cfg;
  cfg.winSec = 100;
  cfg.stepSec = 100;
  QVERIFY(cfg.save(QDir(dir).filePath("SYN1_run_metadata.json")));
  QVERIFY(runCell(s, c, station.code, cls, QStringLiteral("test"), {}));
  const QString out = runDir(s, c, station.code, cls);
  const std::optional<RunRecord> done = RunRecord::read(out);
  QVERIFY(done && done->ok);
  QCOMPARE(done->fitted.value(QStringLiteral("window_seconds")).toDouble(),
           100.0);
  QString labelled;
  QDirIterator it(out, {QStringLiteral("*_N_features_labeled.csv")}, QDir::Files,
                  QDirIterator::Subdirectories);
  if (it.hasNext())
    labelled = it.next();
  QVERIFY(!labelled.isEmpty());
  QFile f(labelled);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QList<QByteArray> lines = f.readAll().split('\n');
  QVERIFY(lines.size() > 1000);
  const QList<QByteArray> header = lines.first().split(';');
  const qsizetype is = header.indexOf("Window_Start_UTC"),
                  ie = header.indexOf("Window_End_UTC");
  QVERIFY(is >= 0 && ie >= 0);
  const double eventEpoch = double(c.time.toSecsSinceEpoch());
  double latest = 0;
  for (const QByteArray &line : lines.mid(1)) {
    if (line.trimmed().isEmpty())
      continue;
    const QList<QByteArray> cols = line.split(';');
    const auto a = fx::parseIsoUtc(QString::fromLatin1(cols.value(is)));
    const auto b = fx::parseIsoUtc(QString::fromLatin1(cols.value(ie)));
    QVERIFY(a && b);
    QCOMPARE(*b - *a, 100.0);
    latest = std::max(latest, *b);
  }
  QVERIFY(latest <= eventEpoch);
  QVERIFY(latest > eventEpoch - 100.0);
}

// --- features ------------------------------------------------------------

void TstCorrectness::featuresReadOnlyThePeriodAskedFor() {
  // The fixture runs from 00:00:00 to 02:10:00 on 2024-03-05 at 10 Hz.
  fx::Config cfg;
  cfg.station = QStringLiteral("SYN1");
  cfg.fs = 5.0;
  cfg.freqmin = 0.1;
  cfg.freqmax = 2.0;
  cfg.dataDir = kFx;
  for (const QString &name :
       QDir(kFx).entryList({QStringLiteral("*.mseed")}, QDir::Files))
    cfg.inputFiles << QDir(kFx).filePath(name);
  const auto span = [&cfg]() {
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    qint64 samples = 0;
    fx::PreprocessOptions opt;
    const bool whole = fx::preprocessDirectory(
        cfg.dataDir, cfg, opt, [&](fx::ChannelChunk &&chunk) {
          if (chunk.z.empty())
            return;
          lo = std::min(lo, chunk.startEpoch);
          hi = std::max(hi, chunk.startEpoch +
                                double(chunk.z.size() - 1) / chunk.fs);
          samples += qint64(chunk.z.size());
        });
    Q_ASSERT(whole);
    return std::array<double, 3>{lo, hi, double(samples)};
  };
  const double day = epochOf(2024, 3, 5);
  const auto all = span();
  QCOMPARE(all[0], day);
  QVERIFY(all[1] > day + 7799.0 && all[1] < day + 7800.0);

  // Up to 01:00:00 and not a sample at or after it; from 00:30:00 on and
  // not a sample before it. Both ends fall inside records.
  cfg.endEpoch = day + 3600.0 + 0.05;
  auto part = span();
  QCOMPARE(part[0], day);
  QVERIFY2(part[1] < *cfg.endEpoch && part[1] > *cfg.endEpoch - 0.5,
           qPrintable(QString::number(part[1] - day, 'f', 3)));
  cfg.startEpoch = day + 1800.0 + 0.05;
  part = span();
  QVERIFY2(part[0] >= *cfg.startEpoch && part[0] < *cfg.startEpoch + 0.5,
           qPrintable(QString::number(part[0] - day, 'f', 3)));
  QVERIFY(part[1] < *cfg.endEpoch);
  QVERIFY(qAbs(part[2] - 1800.0 * 5.0) <= 2.0);

  // A period the files hold nothing of: said, not an empty result.
  cfg.startEpoch = day + 86400.0;
  cfg.endEpoch = day + 2 * 86400.0;
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, span());

  // The period is part of the configuration a run writes down, and comes
  // back from it.
  cfg.startEpoch = day + 1800.0;
  cfg.endEpoch = day + 3600.0;
  const fx::Config back = fx::Config::fromJson(cfg.toJson());
  QCOMPARE(back.startEpoch.value(), day + 1800.0);
  QCOMPARE(back.endEpoch.value(), day + 3600.0);
  QCOMPARE(cfg.toJson().value("to").toString(),
           QStringLiteral("2024-03-05T01:00:00Z"));
  cfg.endEpoch = day;
  QVERIFY(!cfg.validate().isEmpty());
}

void TstCorrectness::aStudyRunStopsAtTheEarthquake() {
  // The case's time is 01:00:00, an hour and ten minutes before the files
  // end. The features, and the calibration made for them, must hold nothing
  // from after it. (The span selected whole files only: with three-week
  // archives the table ran weeks past the earthquake, and the parameters
  // were fitted on the aftershocks too.)
  const QDateTime event = utc(2024, 3, 5, 1, 0, 0);
  FxStudy f = fxStudy(tmp_.filePath("span"), event);
  QStringList log;
  RunHooks hooks;
  hooks.log = [&log](const QString &l) { log << l; };
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                  hooks));
  const QString dir = runDir(f.s, f.c, f.station, f.features);
  const std::optional<RunRecord> rec = RunRecord::read(dir);
  QVERIFY(rec && rec->ok);
  const fx::NpyTable table = fx::readNpy(
      QDir(dir).filePath(rec->outputs.value(QStringLiteral("features")).toString()));
  QVERIFY(table.rows >= 10);
  int tCol = -1;
  for (int i = 0; i < table.fields.size(); ++i)
    if (table.fields[i].name == QLatin1String("Zaman_Dk"))
      tCol = i;
  QVERIFY(tCol >= 0);
  const double eventMinutes = double(event.toSecsSinceEpoch()) / 60.0;
  double last = 0;
  for (int r = 0; r < table.rows; ++r)
    last = std::max(last, table.numeric[tCol][size_t(r)]);
  QVERIFY2(last <= eventMinutes + 1e-9,
           qPrintable(QStringLiteral("a window ends %1 min after the event")
                          .arg(last - eventMinutes)));
  QVERIFY(last > eventMinutes - 200.0 / 60.0 - 1e-9);
  // 3600 s of data in windows of 200 s: 18, not the 39 of the whole files.
  QCOMPARE(table.rows, 18);
  // The calibration counted its windows in the same hour.
  QVERIFY(rec->fitted.value(QStringLiteral("windowsTotal"), 18).toInt() <= 18);
  // What the run read is written down with it.
  const QJsonObject meta = [&] {
    QFile m(QDir(dir).filePath("SYN1_run_metadata.json"));
    return m.open(QIODevice::ReadOnly)
               ? QJsonDocument::fromJson(m.readAll()).object()
               : QJsonObject();
  }();
  QCOMPARE(meta.value("to").toString(), QStringLiteral("2024-03-05T01:00:00Z"));
  QCOMPARE(rec->channelsUsed,
           (QStringList{QStringLiteral("TT.SYN1..HHE"),
                        QStringLiteral("TT.SYN1..HHN"),
                        QStringLiteral("TT.SYN1..HHZ")}));
  QVERIFY(rec->channelsDropped.isEmpty());
}

void TstCorrectness::oneSensorIsRead() {
  // A station with a broadband (HH?, two hours, amplitude 100) and an
  // accelerometer (HN?, one hour, amplitude 5000) at the same rate. Records
  // were placed by the channel's last letter alone, so the two were written
  // over each other sample by sample.
  const QString dir = tmp_.filePath("sensors");
  const QDateTime start = utc(2022, 5, 1);
  write(QDir(dir).filePath("SYN9.mseed"),
        sensor(start, "SYN9", "HH", 7200, 100.0) +
            sensor(start, "SYN9", "HN", 3600, 5000.0));
  fx::Config cfg;
  cfg.station = QStringLiteral("SYN9");
  cfg.fs = 1.0;
  cfg.freqmin = 0.1;
  cfg.freqmax = 0.4;
  cfg.dataDir = dir;
  cfg.inputFiles = {QDir(dir).filePath("SYN9.mseed")};
  const auto read = [&cfg](fx::ChannelChoice *choice, QStringList *log) {
    double peak = 0;
    qint64 samples = 0;
    fx::PreprocessOptions opt;
    opt.channels = choice;
    if (log)
      opt.log = [log](const QString &l) { *log << l; };
    fx::preprocessDirectory(cfg.dataDir, cfg, opt,
                            [&](fx::ChannelChunk &&chunk) {
                              for (const auto *v : {&chunk.e, &chunk.n, &chunk.z})
                                for (double x : *v)
                                  if (std::isfinite(x))
                                    peak = std::max(peak, std::abs(x));
                              samples += qint64(chunk.z.size());
                            });
    return std::pair<double, qint64>{peak, samples};
  };

  // Nothing set: the sensor with the most data, said so, the other left out.
  fx::ChannelChoice choice;
  QStringList log;
  auto got = read(&choice, &log);
  QCOMPARE(choice.family, QStringLiteral("TU.SYN9..HH"));
  QCOMPARE(choice.used, (QStringList{QStringLiteral("TU.SYN9..HHE"),
                                     QStringLiteral("TU.SYN9..HHN"),
                                     QStringLiteral("TU.SYN9..HHZ")}));
  QCOMPARE(choice.dropped, QStringList{QStringLiteral("TU.SYN9..HN?")});
  QVERIFY(choice.guessed);
  QVERIFY2(log.join('\n').contains(QStringLiteral("TU.SYN9..HN?")),
           qPrintable(log.join('\n')));
  // Only the broadband's samples: nothing near the accelerometer's size.
  QVERIFY2(got.first > 50.0 && got.first < 200.0,
           qPrintable(QString::number(got.first)));
  QCOMPARE(got.second, qint64(7200));

  // Named: that one, and no guess.
  for (const char *named : {"HN", "hn?", "HN*"}) {
    cfg.channels = QString::fromLatin1(named);
    got = read(&choice, nullptr);
    QCOMPARE(choice.family, QStringLiteral("TU.SYN9..HN"));
    QVERIFY(!choice.guessed);
    QCOMPARE(choice.dropped, QStringList{QStringLiteral("TU.SYN9..HH?")});
    QVERIFY2(got.first > 2500.0 && got.first < 10000.0,
             qPrintable(QString::number(got.first)));
    QCOMPARE(got.second, qint64(3600));
  }
  // A sensor that is not there: the error names the ones that are.
  cfg.channels = QStringLiteral("BH");
  try {
    read(nullptr, nullptr);
    QFAIL("a sensor that is not in the files was read");
  } catch (const std::exception &e) {
    const QString what = QString::fromUtf8(e.what());
    QVERIFY2(what.contains(QStringLiteral("TU.SYN9..HH?")) &&
                 what.contains(QStringLiteral("TU.SYN9..HN?")),
             e.what());
  }

  // Two location codes of one instrument are two sensors as well.
  write(QDir(dir).filePath("LOC.mseed"),
        sensor(start, "SYN8", "HH", 1800, 100.0, 1, "00") +
            sensor(start, "SYN8", "HH", 3600, 5000.0, 1, "10"));
  cfg.station = QStringLiteral("SYN8");
  cfg.inputFiles = {QDir(dir).filePath("LOC.mseed")};
  cfg.channels.clear();
  got = read(&choice, nullptr);
  QCOMPARE(choice.family, QStringLiteral("TU.SYN8.10.HH"));
  QVERIFY(choice.guessed);
  cfg.channels = QStringLiteral("00.HH?");
  got = read(&choice, nullptr);
  QCOMPARE(choice.family, QStringLiteral("TU.SYN8.00.HH"));
  QVERIFY(got.first < 200.0);

  // One sensor at two sample rates is still refused, by name.
  write(QDir(dir).filePath("RATES.mseed"),
        sensor(start, "SYN7", "HH", 1800, 100.0) +
            sensor(start.addSecs(1800), "SYN7", "HH", 600, 100.0, 2));
  cfg.station = QStringLiteral("SYN7");
  cfg.inputFiles = {QDir(dir).filePath("RATES.mseed")};
  cfg.channels.clear();
  try {
    read(nullptr, nullptr);
    QFAIL("two sample rates were read as one");
  } catch (const std::exception &e) {
    QVERIFY2(QString::fromUtf8(e.what()).contains(
                 QStringLiteral("mixed sample rates")),
             e.what());
  }

  // In a study: the choice is in the run's record and, when it was a guess,
  // among its warnings, where the cell and the scores show it.
  Study s;
  s.dir = tmp_.filePath("sensors-study");
  QVERIFY(QDir().mkpath(s.dir));
  s.name = QStringLiteral("Sensors");
  const std::vector<analysis::Event> events =
      analysis::loadEvents(kFx + QStringLiteral("/catalog.csv"));
  s.catalog = QStringLiteral("catalog.csv");
  writeCatalog(s.catalogPath(), events, Format::Afad);
  write(QDir(s.dir).filePath("waves/SYN9/SYN9.mseed"),
        sensor(start, "SYN9", "HH", 7200, 100.0) +
            sensor(start, "SYN9", "HN", 3600, 5000.0));
  s.addWaveformRoot(QDir(s.dir).filePath("waves"));
  Case c = referenceCase(QStringLiteral("Later"), start.addSecs(7200), 38.6, 39.3);
  c.id = s.freeCaseId(c.name);
  c.spanDays = 2;
  StationRef st;
  st.code = QStringLiteral("SYN9");
  st.lat = 38.6;
  st.lon = 39.2;
  c.stations << st;
  s.cases << c;
  Recipe fea = defaultRecipe(kToolFeatures);
  fea.id = QStringLiteral("features");
  fx::Config small;
  small.fs = 1.0;
  small.winSec = 100;
  small.stepSec = 100;
  small.freqmin = 0.1;
  small.freqmax = 0.4;
  small.calibration.minMag = 2.0;
  fea.settings.insert(QStringLiteral("config_json"),
                      QString::fromUtf8(QJsonDocument(small.toJson()).toJson()));
  s.recipes << fea;
  QVERIFY(runCell(s, c, st.code, fea, QStringLiteral("test"), {}));
  Cell cell = cellOf(s, c, st.code, fea);
  QCOMPARE(cell.state, CellState::Done);
  QCOMPARE(cell.record->channelsUsed.size(), 3);
  QCOMPARE(cell.record->channelsDropped,
           QStringList{QStringLiteral("TU.SYN9..HN?")});
  QVERIFY2(cell.warnings.join('\n').contains(QStringLiteral("several sensors")),
           qPrintable(cell.warnings.join('\n')));
  // Named in the recipe: read, and nothing to warn about.
  fea.settings.insert(QStringLiteral("channels"), QStringLiteral("HN"));
  fea.revision += 1;
  s.recipes[0] = fea;
  QVERIFY(runCell(s, c, st.code, fea, QStringLiteral("test"), {}));
  cell = cellOf(s, c, st.code, fea);
  QCOMPARE(cell.state, CellState::Done);
  QVERIFY(cell.record->channelsUsed.first().endsWith(QStringLiteral("HNE")));
  QVERIFY2(!cell.warnings.join('\n').contains(QStringLiteral("several sensors")),
           qPrintable(cell.warnings.join('\n')));
}

void TstCorrectness::olderFeatureRunsSayWhyTheyAreStale() {
  FxStudy f = fxStudy(tmp_.filePath("older"), utc(2024, 3, 6));
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"), {}));
  const QString dir = runDir(f.s, f.c, f.station, f.features);
  QCOMPARE(cellOf(f.s, f.c, f.station, f.features).state, CellState::Done);
  // The record as the version before wrote its signature.
  RunRecord rec = RunRecord::read(dir).value();
  QVERIFY(rec.caseSignature.startsWith(QStringLiteral("features|v2|")));
  rec.caseSignature.remove(QStringLiteral("v2|"));
  QVERIFY(rec.write(dir));
  const Cell cell = cellOf(f.s, f.c, f.station, f.features);
  QCOMPARE(cell.state, CellState::Stale);
  QVERIFY2(cell.why.contains(QStringLiteral("limited to the case's span")),
           qPrintable(cell.why));
  // A calibration kept by that version is not taken for this one: its key
  // says what the calibration read.
  QDir store(runCalibrationDir(dir));
  const QStringList kept = store.entryList({QStringLiteral("*.json")});
  QCOMPARE(kept.size(), 1);
  QFile k(store.filePath(kept.first()));
  QVERIFY(k.open(QIODevice::ReadOnly));
  const QJsonObject settings = QJsonDocument::fromJson(k.readAll())
                                   .object()
                                   .value("kept_for")
                                   .toObject()
                                   .value("settings")
                                   .toObject();
  QCOMPARE(settings.value("reads").toString(), QStringLiteral("span only"));
}

// --- unpacking -----------------------------------------------------------

void TstCorrectness::archiveMembersOfOneNameBothStay() {
  const QString dir = tmp_.filePath("members");
  QVERIFY(QDir().mkpath(dir));
  const QString zipPath = QDir(dir).filePath("days.zip");
  const QByteArray one = sensor(utc(2023, 2, 5), "SYN6", "HH", 112, 10.0),
                   two = sensor(utc(2023, 2, 6), "SYN6", "HH", 224, 10.0);
  mz_zip_archive zip{};
  QVERIFY(mz_zip_writer_init_file(&zip, QFile::encodeName(zipPath).constData(),
                                  0));
  QVERIFY(mz_zip_writer_add_mem(&zip, "2023/036/SYN6.HH.mseed", one.constData(),
                                size_t(one.size()), MZ_DEFAULT_COMPRESSION));
  QVERIFY(mz_zip_writer_add_mem(&zip, "2023/037/SYN6.HH.mseed", two.constData(),
                                size_t(two.size()), MZ_DEFAULT_COMPRESSION));
  QVERIFY(mz_zip_writer_finalize_archive(&zip));
  QVERIFY(mz_zip_writer_end(&zip));

  const fx::Inputs in = fx::expandInputs({zipPath}, QDir(dir).filePath("out"));
  QCOMPARE(in.files.size(), 2);
  QVERIFY(in.files[0] != in.files[1]);
  QList<qint64> sizes;
  for (const QString &f : in.files) {
    QVERIFY(QFileInfo(f).fileName().endsWith(QStringLiteral("SYN6.HH.mseed")));
    sizes << QFileInfo(f).size();
  }
  std::sort(sizes.begin(), sizes.end());
  QCOMPARE(sizes, (QList<qint64>{one.size(), two.size()}));
}

void TstCorrectness::anotherRunsUnpackedFilesAreLeftAlone() {
  const QString dir = tmp_.filePath("staging");
  QVERIFY(QDir().mkpath(dir));
  const QString zipPath = QDir(dir).filePath("a.zip");
  const QByteArray data = sensor(utc(2023, 2, 5), "SYN6", "HH", 112, 10.0);
  mz_zip_archive zip{};
  QVERIFY(mz_zip_writer_init_file(&zip, QFile::encodeName(zipPath).constData(),
                                  0));
  QVERIFY(mz_zip_writer_add_mem(&zip, "a.mseed", data.constData(),
                                size_t(data.size()), MZ_DEFAULT_COMPRESSION));
  QVERIFY(mz_zip_writer_finalize_archive(&zip));
  QVERIFY(mz_zip_writer_end(&zip));
  const QString root = QDir(dir).filePath("out");

  // Two runs with one output folder, in one process (the Features page
  // started again while its cancelled run was still winding down): each has
  // its own folder, and the end of one leaves the other's files.
  auto first = std::make_unique<fx::Inputs>(fx::expandInputs({zipPath}, root));
  const fx::Inputs second = fx::expandInputs({zipPath}, root);
  QVERIFY(first->stagingDir != second.stagingDir);
  QVERIFY(QFileInfo::exists(first->files.first()));
  QVERIFY(QFileInfo::exists(second.files.first()));
  const QString firstDir = first->stagingDir;
  first.reset();
  QVERIFY(!QFileInfo::exists(firstDir));
  QVERIFY(QFileInfo::exists(second.files.first()));

  // A folder an older version left (named by a process id, no lock file):
  // not removed while it is fresh, whoever's it is. (It was removed on
  // every platform without /proc, which is where another running instance
  // lost its files.)
  const QString legacy = QDir(root).filePath(".unzipped-4242");
  write(QDir(legacy).filePath("x.mseed"), data);
  const fx::Inputs third = fx::expandInputs({zipPath}, root);
  QVERIFY(QFileInfo::exists(QDir(legacy).filePath("x.mseed")));
  QVERIFY(QFileInfo::exists(second.files.first()));
  QVERIFY(QFileInfo::exists(third.files.first()));

  // An Inputs given new contents lets go of the old ones.
  fx::Inputs moved = fx::expandInputs({zipPath}, root);
  const QString movedDir = moved.stagingDir;
  moved = fx::expandInputs({zipPath}, root);
  QVERIFY(!QFileInfo::exists(movedDir));
  QVERIFY(QFileInfo::exists(moved.files.first()));
}

void TstCorrectness::aGzipIsReadToItsEndOrNotAtAll() {
  QByteArray a = "Date,Latitude,Longitude,Magnitude\n", b;
  for (int i = 0; i < 4000; ++i)
    a += QByteArray("01/02/2020 00:00:00,38.1,39.2,") +
         QByteArray::number(1.0 + (i % 40) / 10.0, 'f', 1) + "\n";
  for (int i = 0; i < 3000; ++i)
    b += QByteArray("02/02/2020 00:00:00,38.1,39.2,") +
         QByteArray::number(1.0 + (i % 30) / 10.0, 'f', 1) + "\n";
  const QByteArray one = gzipMember(a), two = gzipMember(b);

  QCOMPARE(fx::gunzip(one), a);
  // Two members, as `cat a.gz b.gz` writes: both, not the first alone. (The
  // length in the last trailer sized the output, so the text was cut to the
  // second member's length and returned as if whole.)
  QCOMPARE(fx::gunzip(one + two), a + b);
  QCOMPARE(fx::gunzip(one + two + QByteArray(512, '\0')), a + b);
  // A length field that lies changes nothing.
  QByteArray lying = one;
  for (int i = 1; i <= 4; ++i)
    lying[lying.size() - i] = char(0xff);
  QCOMPARE(fx::gunzip(lying), a);
  lying = one;
  for (int i = 1; i <= 4; ++i)
    lying[lying.size() - i] = char(0x00);
  QCOMPARE(fx::gunzip(lying), a);
  // Cut short: an error, never a shorter catalog.
  for (const qsizetype cut : {qsizetype(9), qsizetype(40), one.size() / 2,
                              one.size() - 9, one.size() - 1})
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, fx::gunzip(one.left(cut)));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error,
                           fx::gunzip(one + two.left(two.size() / 2)));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, fx::gunzip(QByteArray("plain")));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, fx::gunzip(QByteArray()));
  // Through the catalog reader.
  const QString path = tmp_.filePath("two.csv.gz");
  write(path, one + two);
  QCOMPARE(int(analysis::loadEvents(path).size()), 7000);
  write(path, (one + two).left(one.size() + two.size() / 2));
  QVERIFY_THROWS_EXCEPTION(std::runtime_error, analysis::loadEvents(path));
}

// --- runs ----------------------------------------------------------------

void TstCorrectness::aFinishedRunIsPutInPlaceNotMadeAgain() {
  Study s;
  s.dir = tmp_.filePath("in-place");
  QVERIFY(QDir().mkpath(s.dir));
  s.name = QStringLiteral("P");
  s.catalog = kSynthetic;
  Case c = caseAt(s, QStringLiteral("B"), utc(2020, 7, 7, 7, 7, 7), 38.5001,
                  39.4906, 6.4);
  s.cases << c;
  const Recipe r = quickIndicators(s);
  s.recipes << r;
  QVERIFY(runCell(s, c, QString(), r, QStringLiteral("first"), {}));
  const QString dir = runDir(s, c, QString(), r), tmp = runTmpDir(dir);
  const QDateTime firstFinished = RunRecord::read(dir)->finished;

  // The second run finishes, but the folder of the first cannot be moved
  // aside (on Windows: one of its files is open in a spreadsheet).
  int refused = 0;
  RunHooks blocked;
  blocked.renameFolder = [&](const QString &from, const QString &to) {
    if (from == dir) {
      ++refused;
      return false;
    }
    return QDir().rename(from, to);
  };
  QTest::qWait(1100); // a later second in the records
  try {
    runCell(s, c, QString(), r, QStringLiteral("second"), blocked);
    QFAIL("the run replaced a folder that could not be moved");
  } catch (const std::exception &e) {
    const QString what = QString::fromUtf8(e.what());
    QVERIFY2(what.contains(QStringLiteral("kept, complete")) &&
                 what.contains(QDir::toNativeSeparators(tmp)),
             e.what());
  }
  QVERIFY(refused >= 2); // tried more than once before giving up
  // The first run is still the cell's; the second waits beside it, whole.
  QCOMPARE(RunRecord::read(dir)->finished, firstFinished);
  QCOMPARE(RunRecord::read(dir)->appVersion, QStringLiteral("first"));
  const std::optional<RunRecord> waiting = RunRecord::read(tmp);
  QVERIFY(waiting && waiting->ok);
  QCOMPARE(waiting->appVersion, QStringLiteral("second"));
  QVERIFY(waiting->finished > firstFinished);
  QCOMPARE(cellOf(s, c, QString(), r).state, CellState::Done);

  // Run again once the file is closed: the waiting run is put in place. It
  // is not computed again (it used to be deleted first, and made anew).
  QStringList log;
  RunHooks free;
  free.log = [&log](const QString &l) { log << l; };
  QVERIFY(runCell(s, c, QString(), r, QStringLiteral("third"), free));
  QVERIFY2(log.join('\n').contains(QStringLiteral("nothing is computed again")),
           qPrintable(log.join('\n')));
  const std::optional<RunRecord> now = RunRecord::read(dir);
  QVERIFY(now && now->ok);
  QCOMPARE(now->appVersion, QStringLiteral("second"));
  QCOMPARE(now->finished, waiting->finished);
  QVERIFY(!QFileInfo::exists(tmp));
  QVERIFY(!QFileInfo::exists(dir + QStringLiteral(".old")));

  // What waits there for another recipe revision is not taken: it is not
  // this run's result.
  Recipe edited = r;
  edited.revision += 1;
  s.recipes[0] = edited;
  QVERIFY(QDir().mkpath(tmp));
  RunRecord stale = *now;
  QVERIFY(stale.write(tmp));
  QVERIFY(runCell(s, c, QString(), edited, QStringLiteral("fourth"), free));
  QCOMPARE(RunRecord::read(dir)->appVersion, QStringLiteral("fourth"));
}

void TstCorrectness::stoppingTheCalibrationLeavesNothing() {
  FxStudy f = fxStudy(tmp_.filePath("stop-calibration"), utc(2024, 3, 6));
  bool stop = false;
  RunHooks hooks;
  hooks.shouldStop = [&stop] { return stop; };
  hooks.progress = [&stop](double, const QString &what) {
    if (what.contains(QStringLiteral("Calibrating")))
      stop = true;
  };
  QStringList log;
  hooks.log = [&log](const QString &l) { log << l; };
  bool finished = true;
  try {
    finished = runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                       hooks);
  } catch (const std::exception &e) {
    QFAIL(qPrintable(QStringLiteral("a stopped run failed: %1")
                         .arg(QString::fromUtf8(e.what()))));
  }
  QVERIFY(stop);
  QVERIFY(!finished);
  const QString dir = runDir(f.s, f.c, f.station, f.features);
  // No result, no failure set aside, nothing half-made, and no calibration
  // kept from the part that was read.
  QVERIFY(!QFileInfo::exists(dir));
  QVERIFY(!QFileInfo::exists(runFailedDir(dir)));
  QVERIFY(!QFileInfo::exists(runTmpDir(dir)));
  QVERIFY(QDir(runCalibrationDir(dir))
              .entryList({QStringLiteral("*.json")})
              .isEmpty());
  const Cell cell = cellOf(f.s, f.c, f.station, f.features);
  QVERIFY2(cell.state != CellState::Failed, qPrintable(cell.why));
  // And the cell runs to the end afterwards.
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"), {}));
  QCOMPARE(cellOf(f.s, f.c, f.station, f.features).state, CellState::Done);
}

void TstCorrectness::aJobIsWaitedForNotAborted() {
  // A job in a step that cannot be interrupted for longer than the five
  // seconds its destructor used to wait: the thread was then destroyed
  // while running, which ends the whole process.
  bool done = false;
  QElapsedTimer timer;
  {
    auto *job = new TaskJob(QStringLiteral("slow"), QStringLiteral("test"),
                            QStringLiteral("one long step"),
                            [&done](TaskContext &) {
                              QThread::msleep(5600);
                              done = true;
                              return 0;
                            });
    job->start();
    QTest::qWait(100);
    timer.start();
    delete job; // returns when the work has, and not before
  }
  QVERIFY(done);
  QVERIFY(timer.elapsed() >= 5000);
}

void TstCorrectness::olderRunsOfEveryToolSayWhatChanged() {
  // Runs of the version before were computed differently from the same
  // inputs. Each is stale once, and says what changed for its tool.
  Study s;
  s.dir = tmp_.filePath("older-tools");
  QVERIFY(QDir().mkpath(s.dir));
  s.catalog = kSynthetic;
  Case c = caseAt(s, QStringLiteral("B"), utc(2020, 7, 7, 7, 7, 7), 38.5001,
                  39.4906, 6.4);
  s.cases << c;
  const Recipe r = quickIndicators(s);
  s.recipes << r;
  QVERIFY(runCell(s, c, QString(), r, QStringLiteral("test"), {}));
  const QString dir = runDir(s, c, QString(), r);
  QCOMPARE(cellOf(s, c, QString(), r).state, CellState::Done);
  RunRecord rec = RunRecord::read(dir).value();
  QVERIFY(rec.caseSignature.startsWith(QStringLiteral("indicators|v2|")));
  rec.caseSignature.remove(QStringLiteral("v2|"));
  QVERIFY(rec.write(dir));
  const Cell cell = cellOf(s, c, QString(), r);
  QCOMPARE(cell.state, CellState::Stale);
  QVERIFY2(cell.why.contains(QStringLiteral("read as UTC")) &&
               cell.why.contains(QStringLiteral("kept out of the span")),
           qPrintable(cell.why));
  QVERIFY(runSignature(s, c, QStringLiteral("SYN1"), kToolClassification)
              .startsWith(QStringLiteral("classification|v2|")));
  QVERIFY(runSignature(s, c, QStringLiteral("SYN1"), kToolFeatures)
              .startsWith(QStringLiteral("features|v2|")));
}

void TstCorrectness::aCopiedArchiveIsTheSameInput() {
  // The waveform files of a finished run get other modification times (a
  // copy to a FAT drive rounds them, a restore sets new ones). The data is
  // the same: the run stays finished, and its calibration stays good.
  FxStudy f = fxStudy(tmp_.filePath("copied"), utc(2024, 3, 6));
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"), {}));
  QCOMPARE(cellOf(f.s, f.c, f.station, f.features).state, CellState::Done);
  const QString key =
      RunRecord::read(runDir(f.s, f.c, f.station, f.features))->calibrationKey;
  const QDir waves(QDir(f.s.dir).filePath("waves/" + f.station));
  for (const QFileInfo &fi : waves.entryInfoList(QDir::Files)) {
    QFile file(fi.filePath());
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(fi.lastModified().addSecs(-7201),
                             QFileDevice::FileModificationTime));
  }
  const Cell cell = cellOf(f.s, f.c, f.station, f.features);
  QVERIFY2(cell.state == CellState::Done, qPrintable(cell.why));
  QStringList log;
  RunHooks hooks;
  hooks.log = [&log](const QString &l) { log << l; };
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                  hooks));
  QVERIFY2(log.join('\n').contains(QStringLiteral("Calibration reused")),
           qPrintable(log.join('\n')));
  QCOMPARE(RunRecord::read(runDir(f.s, f.c, f.station, f.features))
               ->calibrationKey,
           key);
  // Other content under the same name and size is another input.
  const QFileInfo first = waves.entryInfoList({QStringLiteral("*HHZ*.mseed")},
                                              QDir::Files)
                              .first();
  QFile file(first.filePath());
  QVERIFY(file.open(QIODevice::ReadWrite));
  QVERIFY(file.seek(first.size() - 1));
  char last = 0;
  QVERIFY(file.getChar(&last));
  QVERIFY(file.seek(first.size() - 1));
  QVERIFY(file.putChar(char(last ^ 0x55)));
  file.close();
  // The cell is asked at every refresh and reads no file for it, so this is
  // noticed once the file has been read (by the scan, or by a run): then
  // the run is stale, and a run would calibrate again.
  waveFileLines({first.filePath()});
  QCOMPARE(cellOf(f.s, f.c, f.station, f.features).state, CellState::Stale);
  log.clear();
  QVERIFY(runCell(f.s, f.c, f.station, f.features, QStringLiteral("test"),
                  hooks));
  QVERIFY2(log.join('\n').contains(QStringLiteral("Calibration redone")),
           qPrintable(log.join('\n')));
}

void TstCorrectness::aStudyOpenElsewhereIsNotRunInto() {
  Study s;
  s.dir = tmp_.filePath("held");
  QVERIFY(QDir().mkpath(s.dir));
  s.catalog = kSynthetic;
  Case c = caseAt(s, QStringLiteral("B"), utc(2020, 7, 7, 7, 7, 7), 38.5001,
                  39.4906, 6.4);
  s.cases << c;
  const Recipe r = quickIndicators(s);
  s.recipes << r;
  const QString lockPath = QDir(s.dir).filePath(StudyStore::kLockFileName);
  // Held by this process (the store of this instance): runs. While it
  // runs, its folder is known as in use, so a clean-up leaves it alone.
  {
    QLockFile mine(lockPath);
    mine.setStaleLockTime(0);
    QVERIFY(mine.tryLock(0));
    bool wasLive = false;
    RunHooks hooks;
    const QString dir = runDir(s, c, QString(), r);
    hooks.log = [&wasLive, dir](const QString &) {
      wasLive = wasLive || LiveRuns::contains(dir);
    };
    QVERIFY(runCell(s, c, QString(), r, QStringLiteral("test"), hooks));
    QVERIFY(wasLive);
    QVERIFY(!LiveRuns::contains(dir));
  }
  // What a crashed instance left is nobody's: runs.
  write(lockPath, "999999999\nsismokaos-gui\n" +
                      QSysInfo::machineHostName().toUtf8() + "\n");
  QVERIFY(runCell(s, c, QString(), r, QStringLiteral("test"), {}));
#ifdef Q_OS_LINUX
  // Held by another process that is alive (this test's parent stands in
  // for a second instance): refused, with who holds it.
  const qint64 parent = qint64(getppid());
  const QString exe =
      QFileInfo(QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(parent)))
          .fileName();
  if (exe.isEmpty())
    QSKIP("the parent process's name cannot be read here");
  write(lockPath, QByteArray::number(parent) + "\n" + exe.toUtf8() + "\n" +
                      QSysInfo::machineHostName().toUtf8() + "\n");
  try {
    runCell(s, c, QString(), r, QStringLiteral("test"), {});
    QFAIL("a run went into a study another process holds");
  } catch (const std::exception &e) {
    QVERIFY2(QString::fromUtf8(e.what()).contains(
                 QStringLiteral("open in another instance")),
             e.what());
  }
  QFile::remove(lockPath);
#endif
}

void TstCorrectness::aFinishedRunThatWaitsIsNoLeftover() {
  // The working folder of a run that finished but could not take its
  // folder's place holds a result: the study's clean-up must not offer it,
  // and must not remove it when asked. Nor unpacked archives a run is using.
  Study s;
  s.dir = tmp_.filePath("leftovers");
  QVERIFY(QDir().mkpath(s.dir));
  s.catalog = kSynthetic;
  Case c = caseAt(s, QStringLiteral("B"), utc(2020, 7, 7, 7, 7, 7), 38.5001,
                  39.4906, 6.4);
  s.cases << c;
  const Recipe r = quickIndicators(s);
  s.recipes << r;
  QVERIFY(runCell(s, c, QString(), r, QStringLiteral("first"), {}));
  const QString dir = runDir(s, c, QString(), r), tmp = runTmpDir(dir);
  RunHooks blocked;
  blocked.renameFolder = [&](const QString &from, const QString &to) {
    return from != dir && QDir().rename(from, to);
  };
  QVERIFY_THROWS_EXCEPTION(
      std::runtime_error,
      runCell(s, c, QString(), r, QStringLiteral("second"), blocked));
  QVERIFY(QFileInfo::exists(tmp));
  bool listed = false;
  for (const DiskItem &d : studyOnDisk(s))
    if (QDir::cleanPath(d.path) == QDir::cleanPath(tmp)) {
      listed = true;
      QVERIFY(!d.leftover);
    }
  QVERIFY(listed);
  QStringList failed;
  cleanUp(s, {tmp}, &failed);
  QVERIFY(QFileInfo::exists(tmp));
  QCOMPARE(failed.size(), 1);

  // Unpacked archives whose lock is held are in use; released, they are not.
  const QString toolDir = QFileInfo(dir).absolutePath();
  const QString zipPath = QDir(s.dir).filePath("a.zip");
  const QByteArray data = sensor(utc(2023, 2, 5), "SYN6", "HH", 112, 10.0);
  mz_zip_archive zip{};
  QVERIFY(mz_zip_writer_init_file(&zip, QFile::encodeName(zipPath).constData(),
                                  0));
  QVERIFY(mz_zip_writer_add_mem(&zip, "a.mseed", data.constData(),
                                size_t(data.size()), MZ_DEFAULT_COMPRESSION));
  QVERIFY(mz_zip_writer_finalize_archive(&zip));
  QVERIFY(mz_zip_writer_end(&zip));
  QString staging;
  {
    const fx::Inputs in = fx::expandInputs({zipPath}, toolDir);
    staging = in.stagingDir;
    QVERIFY(fx::stagingInUse(staging));
    bool seen = false;
    for (const DiskItem &d : studyOnDisk(s))
      if (QDir::cleanPath(d.path) == QDir::cleanPath(staging)) {
        seen = true;
        QVERIFY(d.live);
      }
    QVERIFY(seen);
    failed.clear();
    cleanUp(s, {staging}, &failed);
    QVERIFY(QFileInfo::exists(in.files.first()));
  }
  QVERIFY(!QFileInfo::exists(staging));
}

QTEST_MAIN(TstCorrectness)
#include "tst_correctness.moc"
