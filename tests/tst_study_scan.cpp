// The fast waveform scan: the quick pass and its estimates from file names,
// the exact pass on several files at once, a stop request inside a large
// file, the cache shared by all studies, and what the pages and the runs
// make of an estimate. Fixtures are hand-made miniSEED 2 records.
#include <QApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cstring>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

#include "core/i18n.h"
#include "core/module.h"
#include "core/theme.h"
#include "jobs/job.h"
#include "jobs/jobmodel.h"
#include "miniz.h"
#include "modules/settings/settingsmodule.h"
#include "modules/study/casedialogs.h"
#include "modules/study/casespage.h"
#include "modules/study/waveforms.h"
#include "study/runs.h"
#include "study/scancache.h"
#include "study/store.h"
#include "study/study.h"
#include "study/waveindex.h"
#include "tools/toolregistry.h"

using namespace skg;
using namespace skg::study;

static void initResources() { Q_INIT_RESOURCE(data); }

namespace {

QDateTime utc(int y, int m, int d, int h = 0) {
  return QDateTime(QDate(y, m, d), QTime(h, 0), QTimeZone::UTC);
}

// One 512-byte miniSEED 2 data record: fixed header plus blockette 1000.
QByteArray record(const QDateTime &start, const QByteArray &sta,
                  const QByteArray &cha = "HHZ", int nsamp = 43200,
                  int rate = 1, const QByteArray &net = "TU") {
  QByteArray r;
  QDataStream s(&r, QIODevice::WriteOnly);
  s.setByteOrder(QDataStream::BigEndian);
  s.writeRawData("000001", 6);
  s.writeRawData("D", 1);
  s.writeRawData(" ", 1);
  s.writeRawData(sta.leftJustified(5, ' ').constData(), 5);
  s.writeRawData("  ", 2);
  s.writeRawData(cha.constData(), 3);
  s.writeRawData(net.constData(), 2);
  s << quint16(start.date().year()) << quint16(start.date().dayOfYear())
    << quint8(start.time().hour()) << quint8(start.time().minute())
    << quint8(start.time().second()) << quint8(0) << quint16(0);
  s << quint16(nsamp) << qint16(rate) << qint16(1);
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1); // flags, nblk=1
  s << qint32(0) << quint16(64) << quint16(48); // corr, data off, blk off
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

// `megabytes` of records whose samples are noise, so a zip of them stays
// about as large as they are (as real waveforms do): 100 Hz, 10 s each.
QByteArray noisyRecords(const QByteArray &sta, const QDateTime &first,
                        int megabytes, quint32 seed) {
  QRandomGenerator rng(seed);
  const int n = megabytes * 2048;
  QByteArray out;
  out.reserve(n * 512);
  quint32 noise[112];
  for (int i = 0; i < n; ++i) {
    QByteArray r = record(first.addSecs(qint64(i) * 10), sta, "HHZ", 1000, 100);
    rng.fillRange(noise);
    memcpy(r.data() + 64, noise, sizeof noise);
    out += r;
  }
  return out;
}

void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

void zipWith(const QString &path, const QMap<QString, QByteArray> &members,
             int level = MZ_DEFAULT_COMPRESSION) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile::remove(path);
  mz_zip_archive zip{};
  QVERIFY(mz_zip_writer_init_file(&zip, path.toLocal8Bit().constData(), 0));
  for (auto it = members.begin(); it != members.end(); ++it)
    QVERIFY(mz_zip_writer_add_mem(&zip, it.key().toUtf8().constData(),
                                  it.value().constData(),
                                  size_t(it.value().size()), mz_uint(level)));
  QVERIFY(mz_zip_writer_finalize_archive(&zip));
  mz_zip_writer_end(&zip);
}

const WaveStation *find(const QList<WaveStation> &all, const QString &code) {
  for (const WaveStation &w : all)
    if (w.code == code)
      return &w;
  return nullptr;
}

// Everything an index holds, in a form that can be compared.
QJsonObject dump(const WaveIndex &index) {
  QJsonObject out;
  for (auto r = index.roots.begin(); r != index.roots.end(); ++r) {
    QJsonObject files;
    for (auto f = r->begin(); f != r->end(); ++f)
      files.insert(f.key(), waveFileToJson(*f));
    out.insert(r.key(), files);
  }
  return out;
}

// The same without where each file is and when it was written.
QJsonObject contents(const WaveIndex &index) {
  QJsonObject out;
  for (auto r = index.roots.begin(); r != index.roots.end(); ++r)
    for (auto f = r->begin(); f != r->end(); ++f) {
      QJsonObject o = waveFileToJson(*f);
      o.remove("mtime");
      out.insert(f.key(), o);
    }
  return out;
}

// Both passes, as the application runs them.
void scanBoth(WaveIndex &index, const QString &id, const QString &path,
              const ScanOptions &options = {}) {
  scanRoot(index, id, path, ScanPass::Identity, {}, options);
  scanRoot(index, id, path, ScanPass::Coverage, {}, options);
}

// A folder of many small files over five stations, zips and plain files.
void manyFiles(const QString &root, int count) {
  for (int i = 0; i < count; ++i) {
    const QByteArray sta = QByteArray("S") + QByteArray::number(i % 5);
    const QDateTime day = utc(2021, 1, 1).addDays(i);
    const QString name =
        QStringLiteral("TU_%1_%2_000000_%3_000000_HH.mseed")
            .arg(QString::fromLatin1(sta),
                 day.toString(QStringLiteral("ddMMyyyy")),
                 day.addDays(2).toString(QStringLiteral("ddMMyyyy")));
    const QByteArray data = days(sta, day, 2);
    if (i % 3 == 0)
      zipWith(QStringLiteral("%1/%2/%2_%3.zip")
                  .arg(root, QString::fromLatin1(sta),
                       day.toString(QStringLiteral("yyyy-MM-dd"))),
              {{name, data}});
    else
      write(
          QStringLiteral("%1/%2/%3").arg(root, QString::fromLatin1(sta), name),
          data);
  }
}

} // namespace

class TstStudyScan : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void cleanup();
  void periodInNames();
  void quickPassEstimatesFromNames();
  void exactPassCorrectsANameThatLies();
  void anOlderIndexGetsItsEstimates();
  void parallelScanGivesTheSameIndex();
  void manyShortScansEnd();
  void waitingStationsAreReadFirst();
  void stopInsideALargeMember();
  void featuresFilesUnderEstimates();
  void runsAndEstimates();
  void cacheSparesASecondStudy();
  void cacheFollowsAMovedFolder();
  void cacheFollowsADriveWithCoarseTimes();
  void cacheIsNotFooledByALookAlike();
  void cacheTellsTwinsApart();
  void cacheThatCannotBeWritten();
  void firstResultsOfManyWorkersAreAllFiled();
  void twoScansShareTheCache();
  void waveScanUsesTheCache();
  void twoWaveScansAtOnce();
  void casesPageTakesUpACutShortScan();
  void removingAFolderDuringAScan();
  void stopAndWaitForAnEdit();
  void anIndexThatCannotBeSavedIsNotLost();
  void dialogAndAnEmptyFolder();
  void settingsPage();
  void scanBenchmark();

private:
  QTemporaryDir tmp_;
  QString root_;
};

void TstStudyScan::initTestCase() {
  initResources();
  QVERIFY(ScanCache::sharedDir().isEmpty()); // off unless a test turns it on
  root_ = tmp_.filePath("data");
  // A TDVMS archive: the member's name carries the request's period.
  zipWith(root_ + "/EST/EST_2021-03-01.zip",
          {{"TU_EST_01032021_000000_11032021_000000_HH.mseed",
            days("EST", utc(2021, 3, 1), 10)}});
  // An FDSN day file, plain, with one day of data.
  write(root_ + "/EST/TU_EST_12032021_000000_13032021_000000_HH.mseed",
        days("EST", utc(2021, 3, 12), 1));
  // A name that claims ten days; the file holds three.
  zipWith(root_ + "/LIE/LIE_2021-04-01.zip",
          {{"TU_LIE_01042021_000000_11042021_000000_HH.mseed",
            days("LIE", utc(2021, 4, 1), 3)}});
  // A name whose period the first record contradicts: data from 2019.
  zipWith(root_ + "/ODD/ODD_2021-05-01.zip",
          {{"TU_ODD_01052021_000000_11052021_000000_HH.mseed",
            days("ODD", utc(2019, 6, 1), 2)}});
  // A name that says nothing.
  write(root_ + "/NON/plain.mseed", days("NON", utc(2021, 6, 1), 2));
  write(root_ + "/EST/readme.txt", "not waveforms\n");
}

void TstStudyScan::cleanup() { ScanCache::setSharedDir(QString()); }

void TstStudyScan::periodInNames() {
  double a = 0, b = 0;
  QVERIFY(periodInFileName("TU_FRT_27062020_000000_18072020_000000_HH.mseed",
                           &a, &b));
  QCOMPARE(a, double(utc(2020, 6, 27).toSecsSinceEpoch()));
  QCOMPARE(b, double(utc(2020, 7, 18).toSecsSinceEpoch()));
  QVERIFY(periodInFileName("KO_ARMT_01012021_120000_02012021_000000_BH.mseed",
                           &a, &b));
  QCOMPARE(a, double(utc(2021, 1, 1, 12).toSecsSinceEpoch()));
  // No period, a single date, times out of order, or not a date at all.
  QVERIFY(!periodInFileName("FRT_2020-06-27.zip", &a, &b));
  QVERIFY(!periodInFileName("TU_FRT_27062020_000000_HH.mseed", &a, &b));
  QVERIFY(!periodInFileName("TU_FRT_18072020_000000_27062020_000000_HH.mseed",
                            &a, &b));
  QVERIFY(!periodInFileName("TU_FRT_99992020_000000_18072020_000000_HH.mseed",
                            &a, &b));
  QVERIFY(
      !periodInFileName("a_12345678_123456_12345678_1234567.mseed", &a, &b));
  QVERIFY(!periodInFileName("", &a, &b));
}

void TstStudyScan::quickPassEstimatesFromNames() {
  WaveIndex index;
  const ScanStats st = scanRoot(index, "w1", root_, ScanPass::Identity);
  QCOMPARE(st.listed, 6);
  QCOMPARE(st.skipped, 1);
  QCOMPARE(index.pendingCoverage(), 5);
  // Three files carry a period their first record agrees with.
  QCOMPARE(index.estimatedFiles(), 3);
  const QList<WaveStation> all = index.stations();
  const WaveStation *est = find(all, "EST");
  const WaveStation *odd = find(all, "ODD");
  const WaveStation *non = find(all, "NON");
  QVERIFY(est && odd && non);
  QVERIFY(!est->coverageKnown && est->extentEstimated);
  QCOMPARE(est->days, 11);
  QCOMPARE(est->last, double(utc(2021, 3, 13).toSecsSinceEpoch()));
  QVERIFY(!odd->coverageKnown && !odd->extentEstimated);
  QVERIFY(!non->coverageKnown && !non->extentEstimated);
  QCOMPARE(non->days, 0);

  bool approx = false, atLeast = false;
  QCOMPARE(index.daysCovered("EST", utc(2021, 3, 1), utc(2021, 4, 1), &approx,
                             &atLeast),
           11);
  QVERIFY(approx && !atLeast);
  // Clipped to the period asked for.
  QCOMPARE(index.daysCovered("EST", utc(2021, 3, 9), utc(2021, 3, 12)), 2);
  QCOMPARE(index.daysCovered("LIE", utc(2021, 4, 1), utc(2021, 5, 1), &approx,
                             &atLeast),
           10);
  QVERIFY(approx && !atLeast);
  // No estimate: the day the file starts on, as a lower bound.
  QCOMPARE(index.daysCovered("ODD", utc(2019, 5, 1), utc(2019, 7, 1), &approx,
                             &atLeast),
           1);
  QVERIFY(approx && atLeast);
  QCOMPARE(index.daysCovered("ODD", utc(2021, 5, 1), utc(2021, 6, 1)), 0);
}

void TstStudyScan::exactPassCorrectsANameThatLies() {
  QTemporaryDir studyDir;
  Study s;
  s.dir = studyDir.path();
  const QString id = s.addWaveRoot(root_);
  StationRef lie;
  lie.code = "LIE";
  const QDateTime from = utc(2021, 4, 1), to = utc(2021, 5, 1);

  WaveIndex index;
  scanRoot(index, id, root_, ScanPass::Identity);
  QVERIFY(index.save(s.dir));
  // What the Cases page and the add dialog say meanwhile.
  QCOMPARE(waveformNote(s, lie, from, to),
           QStringLiteral("about 10 of 30 days in span — exact scan running"));
  StationRef odd;
  odd.code = "ODD";
  QCOMPARE(waveformNote(s, odd, utc(2019, 5, 1), utc(2019, 7, 1)),
           QStringLiteral("found: at least 1 of 61 days in span (coverage not "
                          "read yet)"));

  const ScanStats st = scanRoot(index, id, root_, ScanPass::Coverage);
  QCOMPARE(st.read, 5);
  QCOMPARE(index.pendingCoverage(), 0);
  QCOMPARE(index.estimatedFiles(), 0);
  bool approx = true;
  QCOMPARE(index.daysCovered("LIE", from, to, &approx), 3);
  QVERIFY(!approx);
  QCOMPARE(index.daysCovered("EST", utc(2021, 3, 1), utc(2021, 4, 1)), 11);
  const QList<WaveStation> all = index.stations();
  QVERIFY(find(all, "LIE")->coverageKnown &&
          !find(all, "LIE")->extentEstimated);
  QCOMPARE(find(all, "LIE")->days, 3);
  QCOMPARE(find(all, "ODD")->days, 2);
  // No estimate outlives the exact pass.
  for (const auto &files : std::as_const(index.roots))
    for (const WaveFile &f : files)
      for (const WaveChannel &c : f.channels)
        QVERIFY(c.estimate.isEmpty());
  QVERIFY(index.save(s.dir));
  QCOMPARE(waveformNote(s, lie, from, to),
           QStringLiteral("found: 3 of 30 days in span"));
  QCOMPARE(waveformNote(s, odd, utc(2019, 5, 1), utc(2019, 7, 1)),
           QStringLiteral("found: 2 of 61 days in span"));
}

void TstStudyScan::anOlderIndexGetsItsEstimates() {
  // An index written before there were estimates: the quick pass had been
  // through every file, the exact pass through none.
  QTemporaryDir studyDir;
  WaveIndex index;
  scanRoot(index, "w1", root_, ScanPass::Identity);
  QVERIFY(index.save(studyDir.path()));
  const QString path = QDir(studyDir.path()).filePath(WaveIndex::kFileName);
  QFile f(path);
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QByteArray text = f.readAll();
  f.close();
  QVERIFY(text.contains("\"estimate\""));
  QJsonObject top = QJsonDocument::fromJson(text).object();
  QCOMPARE(top.value("version").toInt(), 2);
  QJsonObject roots = top.value("roots").toObject();
  QJsonObject files = roots.value("w1").toObject().value("files").toObject();
  for (auto it = files.begin(); it != files.end(); ++it) {
    QJsonObject file = it->toObject();
    QJsonObject channels = file.value("channels").toObject();
    for (auto c = channels.begin(); c != channels.end(); ++c) {
      QJsonObject ch = c->toObject();
      ch.remove("estimate");
      *c = ch;
    }
    file.insert("channels", channels);
    *it = file;
  }
  roots.insert("w1", QJsonObject{{"files", files}});
  top.insert("roots", roots);
  top.insert("version", 1);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
  f.write(QJsonDocument(top).toJson(QJsonDocument::Compact));
  f.close();

  WaveIndex old = WaveIndex::load(studyDir.path());
  // The stations are still known, with no estimate yet.
  QVERIFY(old.hasStation("EST") && old.hasStation("NON"));
  QCOMPARE(old.estimatedFiles(), 0);
  // The next quick pass reads the five again (a record each) and has them.
  const ScanStats st = scanRoot(old, "w1", root_, ScanPass::Identity);
  QCOMPARE(st.read, 5);
  QCOMPARE(old.estimatedFiles(), 3);
  QCOMPARE(dump(old), dump(index));
}

void TstStudyScan::parallelScanGivesTheSameIndex() {
  QTemporaryDir data;
  manyFiles(data.path(), 45);
  ScanOptions one, six;
  one.workers = 1;
  six.workers = 6;
  WaveIndex serial, parallel;
  scanRoot(serial, "w1", data.path(), ScanPass::Identity, {}, one);
  scanRoot(parallel, "w1", data.path(), ScanPass::Identity, {}, six);
  QCOMPARE(dump(parallel), dump(serial));
  QCOMPARE(serial.estimatedFiles(), 45);
  int checkpoints = 0;
  ScanHooks hooks;
  hooks.checkpoint = [&checkpoints] { ++checkpoints; };
  six.checkpointFiles = 10;
  const ScanStats a =
      scanRoot(serial, "w1", data.path(), ScanPass::Coverage, {}, one);
  const ScanStats b =
      scanRoot(parallel, "w1", data.path(), ScanPass::Coverage, hooks, six);
  QCOMPARE(a.read, 45);
  QCOMPARE(b.read, 45);
  QCOMPARE(b.bytes, a.bytes);
  QVERIFY(!a.stopped && !b.stopped);
  QCOMPARE(dump(parallel), dump(serial));
  QCOMPARE(parallel.pendingCoverage(), 0);
  QCOMPARE(parallel.stations().size(), 5);
  QVERIFY(checkpoints >= 1); // saved along the way, from the calling thread
  // Two folders in one call are one order of work and give the same again.
  QTemporaryDir other;
  manyFiles(other.path(), 12);
  WaveIndex both, apart;
  ScanStats st =
      scanRoots(both,
                {ScanFolder{"w1", data.path()}, ScanFolder{"w2", other.path()},
                 ScanFolder{"w3", other.filePath("nowhere")}},
                ScanPass::Coverage, {}, six);
  QCOMPARE(st.read, 57);
  QCOMPARE(st.missing, QStringList{other.filePath("nowhere")});
  scanRoot(apart, "w1", data.path(), ScanPass::Coverage, {}, one);
  scanRoot(apart, "w2", other.path(), ScanPass::Coverage, {}, one);
  QCOMPARE(dump(both), dump(apart));
}

void TstStudyScan::manyShortScansEnd() {
  // Scans that are over before all their workers have even started: the
  // first workers finish the few files while the rest are being launched.
  // Each must still end (a scan that waited for a worker that was never
  // started would hang here until the test's own time limit).
  QTemporaryDir data;
  manyFiles(data.path(), 3);
  ScanOptions options;
  options.workers = 16;
  WaveIndex reference;
  scanBoth(reference, "w1", data.path());
  for (int round = 0; round < 400; ++round) {
    WaveIndex index;
    const ScanStats st = scanRoot(
        index, "w1", data.path(),
        round % 2 ? ScanPass::Coverage : ScanPass::Identity, {}, options);
    QCOMPARE(st.read, 3);
    QVERIFY(!st.stopped);
    if (round % 2)
      QCOMPARE(dump(index), dump(reference));
  }
}

void TstStudyScan::waitingStationsAreReadFirst() {
  QTemporaryDir data;
  manyFiles(data.path(), 30);
  WaveIndex index;
  scanRoot(index, "w1", data.path(), ScanPass::Identity);
  QStringList order;
  ScanHooks hooks;
  hooks.progress = [&order](int, int, const QString &name) {
    if (!name.isEmpty())
      order << name.section(QLatin1Char('/'), 0, 0);
  };
  ScanOptions options;
  options.workers = 1;
  options.firstStations = {QStringLiteral("s3")}; // any case
  scanRoot(index, "w1", data.path(), ScanPass::Coverage, hooks, options);
  QCOMPARE(order.size(), 30);
  // The station a case waits for, then the others, each in one run.
  QCOMPARE(order.mid(0, 6), QStringList(6, QStringLiteral("S3")));
  const QStringList rest = order.mid(6);
  QStringList groups;
  for (const QString &station : rest)
    if (groups.isEmpty() || groups.last() != station)
      groups << station;
  QCOMPARE(groups, (QStringList{"S0", "S1", "S2", "S4"}));
}

void TstStudyScan::stopInsideALargeMember() {
  // Two archives of one large member each, about as incompressible as real
  // waveforms, so reading one takes a while.
  QTemporaryDir data;
  zipWith(data.filePath("BIG/BIG_2021-01-01.zip"),
          {{"TU_BIG_x_HH.mseed", noisyRecords("BIG", utc(2021, 1, 1), 40, 1)}},
          MZ_BEST_SPEED);
  QVERIFY(QFile::copy(data.filePath("BIG/BIG_2021-01-01.zip"),
                      data.filePath("BIG/BIG_2021-02-01.zip")));
  WaveIndex quick;
  scanRoot(quick, "w1", data.path(), ScanPass::Identity);
  QCOMPARE(quick.pendingCoverage(), 2);

  ScanOptions options;
  options.workers = 2;
  options.pollMs = 2;
  WaveIndex whole = quick;
  QElapsedTimer clock;
  clock.start();
  qint64 seen = 0, total = 0;
  ScanHooks watch;
  watch.bytes = [&](qint64 done, qint64 all, int, int) {
    QVERIFY(done >= seen && done <= all);
    seen = done;
    total = all;
  };
  const ScanStats full =
      scanRoot(whole, "w1", data.path(), ScanPass::Coverage, watch, options);
  const qint64 wholeMs = clock.elapsed();
  QVERIFY(!full.stopped);
  QCOMPARE(whole.pendingCoverage(), 0);
  // Progress is in bytes on disk and ends at the total.
  QCOMPARE(total, full.bytes);
  QCOMPARE(seen, full.bytes);
  QCOMPARE(full.bytes,
           2 * QFileInfo(data.filePath("BIG/BIG_2021-01-01.zip")).size());

  // The same again, stopped as soon as the readers are inside the members.
  WaveIndex part = quick;
  bool begun = false;
  ScanHooks hooks;
  hooks.bytes = [&begun](qint64, qint64, int, int) { begun = true; };
  hooks.shouldStop = [&begun] { return begun; };
  clock.restart();
  const ScanStats st =
      scanRoot(part, "w1", data.path(), ScanPass::Coverage, hooks, options);
  const qint64 stoppedMs = clock.elapsed();
  qInfo("two 40 MB members: read through in %lld ms, stopped after %lld ms",
        wholeMs, stoppedMs);
  QVERIFY(st.stopped);
  QCOMPARE(st.read, 0);
  // Well before the end of the members, not at it.
  QVERIFY2(
      stoppedMs * 2 < wholeMs || stoppedMs < 150,
      qPrintable(QStringLiteral("%1 ms of %2").arg(stoppedMs).arg(wholeMs)));
  // The index is as it was: nothing half-read got in.
  QCOMPARE(dump(part), dump(quick));
  QTemporaryDir studyDir;
  QVERIFY(part.save(studyDir.path()));
  QCOMPARE(dump(WaveIndex::load(studyDir.path())), dump(quick));
  // And the scan can be taken up again.
  options.pollMs = 200;
  QVERIFY(!scanRoot(part, "w1", data.path(), ScanPass::Coverage, {}, options)
               .stopped);
  QCOMPARE(dump(part), dump(whole));
}

void TstStudyScan::featuresFilesUnderEstimates() {
  QTemporaryDir studyDir;
  Study s;
  s.dir = studyDir.path();
  const QString id = s.addWaveRoot(root_);
  const QString zip = root_ + "/EST/EST_2021-03-01.zip";
  const QString day =
      root_ + "/EST/TU_EST_12032021_000000_13032021_000000_HH.mseed";
  StationRef est, lie;
  est.code = "EST";
  lie.code = "LIE";

  WaveIndex index;
  scanRoot(index, id, root_, ScanPass::Identity);
  // Inside the archive's period: the archive, on its estimate.
  WaveChoice c =
      chooseWaveFiles(s, index, est, utc(2021, 3, 3), utc(2021, 3, 5));
  QCOMPARE(c.files, QStringList{zip});
  QCOMPARE(c.uncertain, QStringList{zip});
  QVERIFY(c.how.contains("estimate"));
  // A span that ends where the archive's estimate ends and stops short of
  // the day file: a day of margin takes both, so that an estimate that is a
  // little off drops nothing.
  c = chooseWaveFiles(s, index, est, utc(2021, 3, 10, 12),
                      utc(2021, 3, 11, 12));
  QCOMPARE(c.files, (QStringList{zip, day}));
  QCOMPARE(c.uncertain.size(), 2);
  // Far from both: neither.
  QVERIFY(chooseWaveFiles(s, index, est, utc(2021, 3, 20), utc(2021, 3, 25))
              .files.isEmpty());
  // The name that lies is believed for now: better one file too many.
  QCOMPARE(chooseWaveFiles(s, index, lie, utc(2021, 4, 6), utc(2021, 4, 8))
               .files.size(),
           1);
  // waveFilesFor is the same choice.
  QString how;
  QCOMPARE(waveFilesFor(s, index, est, utc(2021, 3, 3), utc(2021, 3, 5), &how),
           QStringList{zip});
  QCOMPARE(
      how,
      chooseWaveFiles(s, index, est, utc(2021, 3, 3), utc(2021, 3, 5)).how);

  scanRoot(index, id, root_, ScanPass::Coverage);
  c = chooseWaveFiles(s, index, est, utc(2021, 3, 10, 12),
                      utc(2021, 3, 11, 12));
  QCOMPARE(c.files, QStringList{zip}); // the day file starts on the 12th
  QVERIFY(c.uncertain.isEmpty());
  QVERIFY(!c.how.contains("estimate"));
  QVERIFY(chooseWaveFiles(s, index, lie, utc(2021, 4, 6), utc(2021, 4, 8))
              .files.isEmpty());
}

void TstStudyScan::runsAndEstimates() {
  const QString zip = root_ + "/EST/EST_2021-03-01.zip";
  const QString day =
      root_ + "/EST/TU_EST_12032021_000000_13032021_000000_HH.mseed";
  const QString other = root_ + "/NON/plain.mseed";
  WaveChoice exact;
  exact.files = {zip};

  // A run that read the archive and, on an estimate, the day file as well;
  // the exact scan then says the span needed the archive only.
  RunRecord rec;
  rec.waveFiles = waveFileLines({zip, day});
  rec.waveEstimated = true;
  QVERIFY(!waveFilesChanged(rec, exact));
  // Had it chosen exactly, a file it read that is no longer wanted means the
  // files changed.
  rec.waveEstimated = false;
  QVERIFY(waveFilesChanged(rec, exact));
  // A run that read the archive; a new file arrives and may overlap the
  // span: not stale on that guess...
  rec.waveFiles = waveFileLines({zip});
  WaveChoice guess;
  guess.files = {zip, other};
  guess.uncertain = {other};
  QVERIFY(!waveFilesChanged(rec, guess));
  // ...stale once the scan has read it and it does.
  guess.uncertain.clear();
  QVERIFY(waveFilesChanged(rec, guess));
  // The same files: not stale, whichever way they were chosen.
  QVERIFY(!waveFilesChanged(rec, exact));
  guess.files = {zip};
  guess.uncertain = {zip};
  QVERIFY(!waveFilesChanged(rec, guess));
  // A record from before the files were listed has only its stamp.
  rec.waveFiles.clear();
  QVERIFY(waveFilesChanged(rec, exact));
  // The record keeps both in run.json.
  QTemporaryDir runDir;
  RunRecord out;
  out.tool = "features";
  out.waveFiles = waveFileLines({zip, day});
  out.waveEstimated = true;
  QVERIFY(out.write(runDir.path()));
  const auto back = RunRecord::read(runDir.path());
  QVERIFY(back.has_value());
  QCOMPARE(back->waveFiles, out.waveFiles);
  QVERIFY(back->waveEstimated);
}

void TstStudyScan::cacheSparesASecondStudy() {
  QTemporaryDir cacheDir;
  ScanCache cache(cacheDir.filePath("scans"));
  ScanOptions options;
  options.workers = 3;
  options.cache = &cache;

  WaveIndex first;
  const ScanStats q =
      scanRoot(first, "w1", root_, ScanPass::Identity, {}, options);
  QCOMPARE(q.cached, 0);
  QCOMPARE(cache.usage().files, 0); // the quick pass files nothing
  const ScanStats c =
      scanRoot(first, "w1", root_, ScanPass::Coverage, {}, options);
  QCOMPARE(c.read, 5);
  QCOMPARE(c.cached, 0);
  QCOMPARE(cache.usage().files, 5);
  QVERIFY(cache.usage().bytes > 0);

  // Another study on the same folder: its quick pass already has the exact
  // coverage, and nothing is left for the second pass.
  WaveIndex second;
  const ScanStats again =
      scanRoot(second, "other-id", root_, ScanPass::Identity, {}, options);
  QCOMPARE(again.cached, 5);
  QCOMPARE(again.read, 1); // the text file, looked at and skipped
  QCOMPARE(second.pendingCoverage(), 0);
  QCOMPARE(contents(second), contents(first));
  const ScanStats none =
      scanRoot(second, "other-id", root_, ScanPass::Coverage, {}, options);
  QCOMPARE(none.read + none.cached, 0);

  // Without the cache the same scan reads everything, and gets the same.
  WaveIndex plain;
  scanBoth(plain, "w1", root_);
  QCOMPARE(dump(plain), dump(first));

  // Cleared: the files are read again.
  QVERIFY(cache.clear());
  QCOMPARE(cache.usage().files, 0);
  WaveIndex third;
  QCOMPARE(scanRoot(third, "w1", root_, ScanPass::Identity, {}, options).cached,
           0);
}

void TstStudyScan::cacheFollowsAMovedFolder() {
  QTemporaryDir cacheDir, drives;
  ScanCache cache(cacheDir.path());
  ScanOptions options;
  options.cache = &cache;
  const QString before = drives.filePath("laptop/data");
  const QString after = drives.filePath("portable/data");
  manyFiles(before, 9);

  WaveIndex here;
  scanBoth(here, "w1", before, options);
  QCOMPARE(cache.usage().files, 9);
  QVERIFY(QDir().mkpath(drives.filePath("portable")));
  QVERIFY(QDir().rename(before, after));

  // A new study finds the data at its new place: no file is read through,
  // only the first record of each, to be sure it is the same file.
  WaveIndex there;
  const ScanStats st =
      scanRoot(there, "w1", after, ScanPass::Identity, {}, options);
  QCOMPARE(st.cached, 9);
  QCOMPARE(st.read, 0);
  QCOMPARE(there.pendingCoverage(), 0);
  QCOMPARE(contents(there), contents(here));
  // Now filed under the new path as well.
  QCOMPARE(cache.usage().files, 18);

  // A file that changed is read, not taken from the cache.
  const QString changed = after + "/S1/" +
                          QDir(after + "/S1")
                              .entryList({QStringLiteral("TU_*")}, QDir::Files)
                              .first();
  {
    QFile f(changed);
    QVERIFY(f.open(QIODevice::Append));
    f.write(record(utc(2022, 1, 1), "S1"));
  }
  WaveIndex fresh;
  const ScanStats q =
      scanRoot(fresh, "w1", after, ScanPass::Identity, {}, options);
  QCOMPARE(q.cached, 8);
  const ScanStats c =
      scanRoot(fresh, "w1", after, ScanPass::Coverage, {}, options);
  QCOMPARE(c.read, 1);
  QCOMPARE(fresh.pendingCoverage(), 0);
}

// The data copied to a drive whose file system keeps coarser times (FAT
// rounds to two seconds, HFS+ to one, exFAT to a hundredth): the files come
// back with their times rounded, and the cache still knows them, since the
// content is the same.
void TstStudyScan::cacheFollowsADriveWithCoarseTimes() {
  QTemporaryDir cacheDir, drives;
  ScanCache cache(cacheDir.path());
  ScanOptions options;
  options.cache = &cache;
  const QString before = drives.filePath("laptop/data");
  manyFiles(before, 9);
  const auto setTimes = [](const QString &root, auto &&timeOf) {
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    int n = 0;
    while (it.hasNext()) {
      QFile f(it.next());
      if (!f.open(QIODevice::ReadWrite) ||
          !f.setFileTime(QDateTime::fromMSecsSinceEpoch(timeOf(n++)),
                         QFileDevice::FileModificationTime))
        return false;
    }
    return n > 0;
  };
  // Times a fine-grained file system would hold: odd seconds, milliseconds.
  QVERIFY(setTimes(before, [](int n) {
    return 1700000001900LL + qint64(n) * 61007LL;
  }));
  WaveIndex here;
  scanBoth(here, "w1", before, options);
  QCOMPARE(cache.usage().files, 9);

  const auto copyTo = [&](const QString &to) {
    QDirIterator it(before, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QString from = it.next();
      const QString dest = to + QLatin1Char('/') +
                           QDir(before).relativeFilePath(from);
      if (!QDir().mkpath(QFileInfo(dest).absolutePath()) ||
          !QFile::copy(from, dest))
        return false;
    }
    return true;
  };
  struct Kind {
    const char *name;
    std::function<qint64(qint64)> keep; // what the drive makes of a time, ms
  };
  const QList<Kind> kinds{
      {"fat-up", [](qint64 ms) { return (ms + 1999) / 2000 * 2000; }},
      {"fat-down", [](qint64 ms) { return ms / 2000 * 2000; }},
      {"hfs", [](qint64 ms) { return ms / 1000 * 1000; }},
      {"exfat", [](qint64 ms) { return ms / 10 * 10; }},
  };
  for (const Kind &k : kinds) {
    const QString after = drives.filePath(QLatin1String(k.name) + "/data");
    QVERIFY(copyTo(after));
    // The copy keeps each file's own time, as the drive can hold it.
    {
      QDirIterator it(after, QDir::Files, QDirIterator::Subdirectories);
      while (it.hasNext()) {
        const QString path = it.next();
        const QString original =
            before + QLatin1Char('/') + QDir(after).relativeFilePath(path);
        const qint64 ms =
            QFileInfo(original).lastModified().toMSecsSinceEpoch();
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QVERIFY(f.setFileTime(QDateTime::fromMSecsSinceEpoch(k.keep(ms)),
                              QFileDevice::FileModificationTime));
      }
    }
    WaveIndex there;
    const ScanStats st =
        scanRoot(there, "w1", after, ScanPass::Identity, {}, options);
    QVERIFY2(st.cached == 9 && st.read == 0,
             qPrintable(QStringLiteral("%1: %2 from the cache, %3 read")
                            .arg(QLatin1String(k.name))
                            .arg(st.cached)
                            .arg(st.read)));
    QCOMPARE(there.pendingCoverage(), 0);
    // And the study's own index, when its folder is just re-pointed to the
    // drive: nothing is read, nothing even asked of the cache.
    WaveIndex moved = here;
    const ScanStats again =
        scanRoot(moved, "w1", after, ScanPass::Coverage, {}, options);
    QVERIFY2(again.read == 0 && again.cached == 0,
             qPrintable(QStringLiteral("%1: %2 read, %3 from the cache")
                            .arg(QLatin1String(k.name))
                            .arg(again.read)
                            .arg(again.cached)));
  }
}

void TstStudyScan::cacheIsNotFooledByALookAlike() {
  // Two files with the same name, size and time, holding different days.
  QTemporaryDir cacheDir, a, b;
  ScanCache cache(cacheDir.path());
  ScanOptions options;
  options.cache = &cache;
  write(a.filePath("day.mseed"), days("AAA", utc(2021, 1, 1), 1));
  write(b.filePath("day.mseed"), days("AAA", utc(2021, 7, 1), 1));
  const QDateTime when = utc(2022, 2, 2, 2);
  for (const QString &path :
       {a.filePath("day.mseed"), b.filePath("day.mseed")}) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(when, QFileDevice::FileModificationTime));
  }
  QCOMPARE(QFileInfo(a.filePath("day.mseed")).size(),
           QFileInfo(b.filePath("day.mseed")).size());

  WaveIndex one, two;
  scanBoth(one, "w1", a.path(), options);
  const ScanStats q =
      scanRoot(two, "w1", b.path(), ScanPass::Identity, {}, options);
  QCOMPARE(q.cached, 0); // its first record is not the one the result is from
  const ScanStats c =
      scanRoot(two, "w1", b.path(), ScanPass::Coverage, {}, options);
  QCOMPARE(c.read, 1);
  QCOMPARE(two.daysCovered("AAA", utc(2021, 7, 1), utc(2021, 7, 2)), 1);
  QCOMPARE(two.daysCovered("AAA", utc(2021, 1, 1), utc(2021, 1, 2)), 0);
  // A true copy elsewhere is recognised (a time two seconds off included,
  // as a file system with coarse times would keep it).
  QTemporaryDir c2;
  QVERIFY(QFile::copy(a.filePath("day.mseed"), c2.filePath("day.mseed")));
  {
    QFile f(c2.filePath("day.mseed"));
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(when.addSecs(1), QFileDevice::FileModificationTime));
  }
  WaveIndex three;
  QCOMPARE(
      scanRoot(three, "w1", c2.path(), ScanPass::Identity, {}, options).cached,
      1);
  QCOMPARE(three.daysCovered("AAA", utc(2021, 1, 1), utc(2021, 1, 2)), 1);
}

void TstStudyScan::cacheTellsTwinsApart() {
  // The worst look-alikes: two files in different folders with the same
  // name, size, time and first record, whose later records differ. Neither
  // may ever be given the other's coverage, and neither's result may push
  // the other's out of the cache.
  QTemporaryDir cacheDir, a, b;
  ScanCache cache(cacheDir.filePath("deep/er/cache"));
  ScanOptions options;
  options.workers = 4;
  options.cache = &cache;
  const QByteArray first = days("AAA", utc(2021, 1, 1), 1);
  const QByteArray third = first + days("AAA", utc(2021, 1, 3), 1);
  const QByteArray fifth = first + days("AAA", utc(2021, 1, 5), 1);
  QCOMPARE(third.size(), fifth.size());
  const QDateTime when = utc(2022, 2, 2, 2);
  const auto put = [&when](const QString &path, const QByteArray &bytes) {
    write(path, bytes);
    QFile f(path);
    return f.open(QIODevice::ReadWrite) &&
           f.setFileTime(when, QFileDevice::FileModificationTime);
  };
  QVERIFY(put(a.filePath("day.mseed"), third));
  QVERIFY(put(b.filePath("day.mseed"), fifth));
  QVERIFY(ScanCache::fingerprint(a.filePath("day.mseed")) !=
          ScanCache::fingerprint(b.filePath("day.mseed")));
  QVERIFY(ScanCache::fingerprint(a.filePath("nothing.mseed")).isEmpty());
  const auto has = [](const WaveIndex &index, int day) {
    return index.daysCovered("AAA", utc(2021, 1, day), utc(2021, 1, day + 1));
  };

  WaveIndex one, two;
  ScanStats st = scanRoot(one, "w1", a.path(), ScanPass::Identity, {}, options);
  st = scanRoot(one, "w1", a.path(), ScanPass::Coverage, {}, options);
  QCOMPARE(st.read, 1);
  QCOMPARE(st.unfiled, 0);
  QCOMPARE(cache.usage().files, 1);
  // The twin: same key by name, other content. It is read, not served.
  st = scanRoot(two, "w1", b.path(), ScanPass::Identity, {}, options);
  QCOMPARE(st.cached, 0);
  st = scanRoot(two, "w1", b.path(), ScanPass::Coverage, {}, options);
  QCOMPARE(st.read, 1);
  QCOMPARE(st.unfiled, 0);
  QCOMPARE(has(one, 3), 1);
  QCOMPARE(has(one, 5), 0);
  QCOMPARE(has(two, 3), 0);
  QCOMPARE(has(two, 5), 1);
  QCOMPARE(cache.usage().files, 2);

  // Each is still in the cache as itself: at its place, and as a copy
  // somewhere else.
  QTemporaryDir c, d;
  QVERIFY(put(c.filePath("day.mseed"), third));
  QVERIFY(put(d.filePath("day.mseed"), fifth));
  for (const auto &[folder, day] :
       {std::pair{a.path(), 3}, std::pair{b.path(), 5}, std::pair{c.path(), 3},
        std::pair{d.path(), 5}}) {
    WaveIndex index;
    st = scanRoot(index, "w1", folder, ScanPass::Identity, {}, options);
    QCOMPARE(st.cached, 1);
    QCOMPARE(has(index, day), 1);
    QCOMPARE(has(index, day == 3 ? 5 : 3), 0);
  }

  // A file rewritten in place under the same size and time is not given
  // what stood there before.
  QVERIFY(put(a.filePath("day.mseed"), fifth));
  WaveIndex rewritten;
  scanBoth(rewritten, "w1", a.path(), options);
  QCOMPARE(has(rewritten, 5), 1);
  QCOMPARE(has(rewritten, 3), 0);
  // Nor is it when no other file happens to hold the new content.
  const QByteArray seventh = first + days("AAA", utc(2021, 1, 7), 1);
  QVERIFY(put(b.filePath("day.mseed"), seventh));
  WaveIndex again;
  st = scanRoot(again, "w1", b.path(), ScanPass::Identity, {}, options);
  QCOMPARE(st.cached, 0);
  scanRoot(again, "w1", b.path(), ScanPass::Coverage, {}, options);
  QCOMPARE(has(again, 7), 1);
  QCOMPARE(has(again, 5), 0);
}

void TstStudyScan::cacheThatCannotBeWritten() {
  // The cache folder cannot be made (its parent is a file): the scan is
  // whole all the same, and says how many results it could not file.
  QTemporaryDir dir;
  write(dir.filePath("file"), "not a folder\n");
  ScanCache cache(dir.filePath("file/cache"));
  ScanOptions options;
  options.workers = 3;
  options.cache = &cache;
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("scan cache: .*")));
  QTest::ignoreMessage(QtWarningMsg,
                       QRegularExpression(QStringLiteral("scan cache: .*")));
  QTemporaryDir data;
  manyFiles(data.path(), 2);
  WaveIndex index, reference;
  scanRoot(index, "w1", data.path(), ScanPass::Identity, {}, options);
  const ScanStats st =
      scanRoot(index, "w1", data.path(), ScanPass::Coverage, {}, options);
  QCOMPARE(st.read, 2);
  QCOMPARE(st.unfiled, 2);
  QCOMPARE(cache.writeFailures(), 2);
  scanBoth(reference, "w1", data.path());
  QCOMPARE(dump(index), dump(reference));
  QCOMPARE(cache.usage().files, 0);
}

void TstStudyScan::firstResultsOfManyWorkersAreAllFiled() {
  // Many workers file their first results at the same moment into a cache
  // folder none of whose levels exists yet. Every one must be there
  // afterwards (on Windows some were lost while the folders were made).
  QTemporaryDir data;
  manyFiles(data.path(), 24);
  WaveIndex quick;
  scanRoot(quick, "w1", data.path(), ScanPass::Identity);
  for (int round = 0; round < 25; ++round) {
    QTemporaryDir dir;
    ScanCache cache(dir.filePath(QStringLiteral("a/b/c/cache-%1").arg(round)));
    ScanOptions options;
    options.workers = 12;
    options.cache = &cache;
    WaveIndex index = quick;
    const ScanStats st =
        scanRoot(index, "w1", data.path(), ScanPass::Coverage, {}, options);
    QCOMPARE(st.read, 24);
    QCOMPARE(st.unfiled, 0);
    QCOMPARE(cache.usage().files, 24);
  }
}

void TstStudyScan::twoScansShareTheCache() {
  // Two applications scanning the same folder at once, each for its own
  // study, with one cache between them.
  QTemporaryDir data, cacheDir;
  manyFiles(data.path(), 40);
  WaveIndex reference;
  scanBoth(reference, "w1", data.path());

  WaveIndex one, two;
  const auto scan = [&](WaveIndex *index) {
    ScanCache cache(cacheDir.path());
    ScanOptions options;
    options.workers = 3;
    options.cache = &cache;
    scanBoth(*index, "w1", data.path(), options);
  };
  std::thread a(scan, &one), b(scan, &two);
  a.join();
  b.join();
  QCOMPARE(dump(one), dump(reference));
  QCOMPARE(dump(two), dump(reference));

  // Every result in the cache is whole.
  int shards = 0;
  QDirIterator it(cacheDir.path(), {QStringLiteral("*.json")}, QDir::Files,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    QFile f(it.next());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    QVERIFY2(o.value("file").isObject() && o.value("first").isObject() &&
                 o.value("print").toString().size() == 40,
             qPrintable(f.fileName()));
    ++shards;
  }
  QCOMPARE(shards, 80); // by path and by name
  // Nothing but results is left behind (no temporary files).
  QDirIterator all(cacheDir.path(), QDir::Files, QDirIterator::Subdirectories);
  int files = 0;
  while (all.hasNext()) {
    all.next();
    ++files;
  }
  QCOMPARE(files, 80);
  ScanCache cache(cacheDir.path());
  QCOMPARE(cache.usage().files, 40);
  ScanOptions options;
  options.cache = &cache;
  WaveIndex third;
  QCOMPARE(scanRoot(third, "w1", data.path(), ScanPass::Identity, {}, options)
               .cached,
           40);
  QCOMPARE(dump(third), dump(reference));
}

void TstStudyScan::waveScanUsesTheCache() {
  // The application's own scan, as jobs, for two studies on one folder.
  QTemporaryDir dir;
  ScanCache::setSharedDir(dir.filePath("cache"));
  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  settings.setValue(QLatin1String(kScanWorkersSetting), 3);
  QCOMPARE(WaveScan::workers(&settings), 3);
  JobModel jobs;
  StudyStore store(&settings);
  AppContext ctx;
  ctx.jobs = &jobs;
  ctx.settings = &settings;
  ctx.study = &store;
  WaveScan scan(ctx);

  const auto scanned = [&](const QString &name) {
    if (!store.create(dir.filePath(name), name) ||
        !store.update([this](Study &s) { s.addWaveRoot(root_); }))
      return false;
    scan.start();
    if (!scan.running())
      return false;
    const QString studyDir = store.study().dir;
    return QTest::qWaitFor(
        [&] {
          return !scan.running() && !WaveIndex::load(studyDir).isEmpty() &&
                 WaveIndex::load(studyDir).pendingCoverage() == 0;
        },
        30000);
  };
  QVERIFY(scanned("first"));
  QVERIFY(scan.status().isEmpty());
  const WaveIndex first = WaveIndex::load(store.study().dir);
  QCOMPARE(first.stations().size(), 4);
  QCOMPARE(ScanCache(ScanCache::sharedDir()).usage().files, 5);
  const int jobsBefore = jobs.rowCount();
  QCOMPARE(jobsBefore, 2); // the quick pass, then the exact one

  QVERIFY(scanned("second"));
  const WaveIndex second = WaveIndex::load(store.study().dir);
  QCOMPARE(contents(second), contents(first));
  // Its quick pass took the five from the cache; its exact pass read none.
  QCOMPARE(jobs.rowCount(), 4);
  const QString quickLog =
      jobs.jobAt(jobsBefore)->log().join(QLatin1Char('\n'));
  const QString exactLog =
      jobs.jobAt(jobsBefore + 1)->log().join(QLatin1Char('\n'));
  QVERIFY2(quickLog.contains("5 from the scan cache"), qPrintable(quickLog));
  QVERIFY2(exactLog.contains("0 read now"), qPrintable(exactLog));
  QCOMPARE(jobs.jobAt(jobsBefore + 1)->state(), Job::State::Done);
}

void TstStudyScan::twoWaveScansAtOnce() {
  // Two studies scanned at the same time by the application's own scan,
  // both filing into one cache.
  QTemporaryDir dir, data;
  manyFiles(data.path(), 30);
  ScanCache::setSharedDir(dir.filePath("cache"));
  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  JobModel jobs;
  StudyStore storeA(&settings), storeB(&settings);
  AppContext ctxA, ctxB;
  ctxA.jobs = ctxB.jobs = &jobs;
  ctxA.settings = ctxB.settings = &settings;
  ctxA.study = &storeA;
  ctxB.study = &storeB;
  WaveScan scanA(ctxA), scanB(ctxB);
  for (StudyStore *store : {&storeA, &storeB}) {
    QVERIFY(store->create(dir.filePath(store == &storeA ? "a" : "b"),
                          QStringLiteral("x")));
    QVERIFY(store->update([&data](Study &s) { s.addWaveRoot(data.path()); }));
  }
  scanA.start();
  scanB.start();
  QVERIFY(scanA.running() && scanB.running());
  const auto finished = [](const WaveScan &scan, const StudyStore &store) {
    const WaveIndex index = WaveIndex::load(store.study().dir);
    return !scan.running() && index.fileCount() == 30 &&
           index.pendingCoverage() == 0;
  };
  QVERIFY(QTest::qWaitFor(
      [&] { return finished(scanA, storeA) && finished(scanB, storeB); },
      60000));
  WaveIndex reference;
  scanBoth(reference, "w1", data.path());
  QCOMPARE(contents(WaveIndex::load(storeA.study().dir)), contents(reference));
  QCOMPARE(contents(WaveIndex::load(storeB.study().dir)), contents(reference));
  for (int row = 0; row < jobs.rowCount(); ++row)
    QCOMPARE(jobs.jobAt(row)->state(), Job::State::Done);
  // The cache holds each file once, whole, and serves a third study.
  ScanCache cache(ScanCache::sharedDir());
  QCOMPARE(cache.usage().files, 30);
  ScanOptions options;
  options.cache = &cache;
  WaveIndex third;
  QCOMPARE(scanRoot(third, "w1", data.path(), ScanPass::Identity, {}, options)
               .cached,
           30);
  QCOMPARE(contents(third), contents(reference));
}

void TstStudyScan::casesPageTakesUpACutShortScan() {
  QTemporaryDir dir;
  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  settings.setValue(Theme::kSettingMode, QStringLiteral("light"));
  Theme theme(&settings);
  theme.apply();
  JobModel jobs;
  ToolRegistry tools(&settings);
  StudyStore store(&settings);
  AppContext ctx;
  ctx.jobs = &jobs;
  ctx.tools = &tools;
  ctx.settings = &settings;
  ctx.theme = &theme;
  ctx.study = &store;
  QVERIFY(store.create(dir.filePath("study"), QStringLiteral("Resume")));
  QVERIFY(store.update([this](Study &s) { s.addWaveRoot(root_); }));
  const QString studyDir = store.study().dir;
  const auto settled = [&] {
    for (int row = 0; row < jobs.rowCount(); ++row)
      if (jobs.jobAt(row)->state() == Job::State::Running ||
          jobs.jobAt(row)->state() == Job::State::Queued)
        return false;
    const WaveIndex index = WaveIndex::load(studyDir);
    return !index.isEmpty() && index.pendingCoverage() == 0;
  };
  // What the jobs said, for a failure message.
  const auto told = [&] {
    QString text = QStringLiteral("%1 job(s); index: %2 file(s), %3 pending")
                       .arg(jobs.rowCount())
                       .arg(WaveIndex::load(studyDir).fileCount())
                       .arg(WaveIndex::load(studyDir).pendingCoverage());
    for (int row = 0; row < jobs.rowCount(); ++row)
      text += QStringLiteral("\n[%1] %2\n%3")
                  .arg(row)
                  .arg(stateName(jobs.jobAt(row)->state()),
                       jobs.jobAt(row)->log().join(QLatin1Char('\n')));
    return text;
  };
  {
    // The page scans a folder it has no index of, by itself.
    CasesPage page(ctx);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    QVERIFY2(QTest::qWaitFor(settled, 30000), qPrintable(told()));
    QCOMPARE(jobs.rowCount(), 2);
  }
  // The application was closed while the exact pass ran: the index has the
  // quick pass only. Nothing in the folders has changed since.
  WaveIndex quick;
  scanRoot(quick, store.study().waveRoots.first().id, root_,
           ScanPass::Identity);
  QVERIFY(quick.save(studyDir));
  QCOMPARE(WaveIndex::load(studyDir).pendingCoverage(), 5);
  {
    CasesPage page(ctx);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    QVERIFY(QTest::qWaitFor(settled, 30000));
    QCOMPARE(jobs.rowCount(), 4); // taken up again, once
    // Showing the page again starts nothing more.
    page.hide();
    page.show();
    QTest::qWait(100);
    QCOMPARE(jobs.rowCount(), 4);
  }
  // With everything read, a new session scans nothing.
  {
    CasesPage page(ctx);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));
    QTest::qWait(100);
    QCOMPARE(jobs.rowCount(), 4);
  }
}

// A folder taken off the study while a scan runs does not come back into the
// index with what the scan holds in memory, and the scan goes on with the
// folders that are left. Whenever the removal lands (before the job reads,
// while it reads, after it saved), the end is the same.
void TstStudyScan::removingAFolderDuringAScan() {
  QTemporaryDir dir, dataA, dataB;
  manyFiles(dataA.path(), 12);
  manyFiles(dataB.path(), 12);
  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  JobModel jobs;
  StudyStore store(&settings);
  AppContext ctx;
  ctx.jobs = &jobs;
  ctx.settings = &settings;
  ctx.study = &store;
  WaveScan scan(ctx);
  for (int round = 0; round < 6; ++round) {
    QVERIFY(store.create(dir.filePath(QStringLiteral("s%1").arg(round)),
                         QStringLiteral("s")));
    QString idA, idB;
    QVERIFY(store.update([&](Study &s) {
      idA = s.addWaveRoot(dataA.path());
      idB = s.addWaveRoot(dataB.path());
    }));
    const QString studyDir = store.study().dir;
    scan.start();
    QVERIFY(scan.running());
    // Let the scan get some way in, differently each round.
    QTest::qWait(round * 4);
    QVERIFY(store.update([&](Study &s) {
      s.waveRoots.removeIf([&](const WaveRoot &r) { return r.id == idB; });
    }));
    // When the scan had already ended, the page that follows the study
    // starts one for the changed list (the Cases page does, by the stamp of
    // the folders); a scan that was running is taken up again by itself.
    if (!scan.running())
      scan.start();
    QVERIFY(QTest::qWaitFor(
        [&] {
          return !scan.running() &&
                 WaveIndex::load(studyDir).pendingCoverage() == 0 &&
                 WaveIndex::load(studyDir).fileCount() == 12;
        },
        30000));
    const WaveIndex index = WaveIndex::load(studyDir);
    QVERIFY2(!index.roots.contains(idB), "the removed folder is back");
    QCOMPARE(index.roots.value(idA).size(), 12);
    QVERIFY(!scan.stoppedByUser());
    // A folder added now gets a new id, not the one that left.
    QTemporaryDir dataC;
    QString idC;
    QVERIFY(store.update([&](Study &s) { idC = s.addWaveRoot(dataC.path()); }));
    QVERIFY(idC != idB);
    QVERIFY(QTest::qWaitFor([&] { return !scan.running(); }, 30000));
  }
}

// For an edit of the folder list the scan is stopped and waited for; that is
// not a stop by the user, and it can be started again at once.
void TstStudyScan::stopAndWaitForAnEdit() {
  QTemporaryDir dir, data;
  manyFiles(data.path(), 30);
  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  JobModel jobs;
  StudyStore store(&settings);
  AppContext ctx;
  ctx.jobs = &jobs;
  ctx.settings = &settings;
  ctx.study = &store;
  WaveScan scan(ctx);
  QVERIFY(scan.stopAndWait()); // nothing running: at once
  QVERIFY(store.create(dir.filePath("study"), "s"));
  QVERIFY(store.update([&](Study &s) { s.addWaveRoot(data.path()); }));
  const QString studyDir = store.study().dir;
  scan.start();
  QVERIFY(scan.running());
  QVERIFY(scan.stopAndWait());
  QVERIFY(!scan.running());
  QVERIFY(!scan.stoppedByUser());
  QVERIFY(scan.status().isEmpty());
  // It did not go on to the coverage pass behind our back.
  QTest::qWait(50);
  QVERIFY(!scan.running());
  scan.start();
  QVERIFY(QTest::qWaitFor(
      [&] {
        return !scan.running() &&
               WaveIndex::load(studyDir).fileCount() == 30 &&
               WaveIndex::load(studyDir).pendingCoverage() == 0;
      },
      30000));
}

// A scan that cannot save its index at the end (here: the study folder does
// not take a new file) says so and does not lose what it read: the next
// scan starts from it, reads nothing again, and saves it.
void TstStudyScan::anIndexThatCannotBeSavedIsNotLost() {
#ifdef Q_OS_WIN
  QSKIP("a folder cannot be made to refuse new files this way on Windows");
#else
  QTemporaryDir dir, data;
  manyFiles(data.path(), 8);
  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  JobModel jobs;
  StudyStore store(&settings);
  AppContext ctx;
  ctx.jobs = &jobs;
  ctx.settings = &settings;
  ctx.study = &store;
  WaveScan scan(ctx);
  QVERIFY(store.create(dir.filePath("study"), "s"));
  QVERIFY(store.update([&](Study &s) { s.addWaveRoot(data.path()); }));
  const QString studyDir = store.study().dir;
  const auto lock = [&](bool on) {
    return QFile::setPermissions(
        studyDir, on ? QFileDevice::ReadOwner | QFileDevice::ExeOwner
                     : QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                           QFileDevice::ExeOwner);
  };
  QVERIFY(lock(true));
  const auto unlock = qScopeGuard([&] { lock(false); });
  {
    QFile probe(studyDir + "/probe");
    if (probe.open(QIODevice::WriteOnly)) {
      probe.remove();
      QSKIP("this user may write anywhere (root): nothing to refuse the save");
    }
  }
  scan.start();
  QVERIFY(QTest::qWaitFor([&] { return !scan.running(); }, 60000));
  QVERIFY(!QFileInfo::exists(studyDir + "/" + WaveIndex::kFileName));
  QCOMPARE(jobs.jobAt(jobs.rowCount() - 1)->state(), Job::State::Failed);
  QString log = jobs.jobAt(jobs.rowCount() - 1)->log().join(QLatin1Char('\n'));
  QVERIFY2(log.contains("could not be saved") && log.contains("kept in memory"),
           qPrintable(log));
  QVERIFY(!scan.status().isEmpty());

  // The folder takes files again: the next scan saves what the first read.
  QVERIFY(lock(false));
  const int before = jobs.rowCount();
  scan.start();
  QVERIFY(QTest::qWaitFor(
      [&] {
        return !scan.running() &&
               WaveIndex::load(studyDir).fileCount() == 8 &&
               WaveIndex::load(studyDir).pendingCoverage() == 0;
      },
      60000));
  log = jobs.jobAt(before)->log().join(QLatin1Char('\n'));
  QVERIFY2(log.contains("0 read now"), qPrintable(log));
#endif
}

// The dialog: a folder that holds nothing now (a drive that is not
// connected) is said to be so and keeps what the study knew of it; "Scan
// again" forgets it only when the user says so. Removing a folder works
// while a scan runs.
void TstStudyScan::dialogAndAnEmptyFolder() {
  QTemporaryDir dir, data, other;
  manyFiles(data.path(), 6);
  manyFiles(other.path(), 6);
  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  JobModel jobs;
  StudyStore store(&settings);
  AppContext ctx;
  ctx.jobs = &jobs;
  ctx.settings = &settings;
  ctx.study = &store;
  WaveScan scan(ctx);
  QVERIFY(store.create(dir.filePath("study"), "s"));
  QString id, idOther;
  QVERIFY(store.update([&](Study &s) {
    id = s.addWaveRoot(data.path());
    idOther = s.addWaveRoot(other.path());
  }));
  const QString studyDir = store.study().dir;
  const auto settled = [&](int files) {
    return QTest::qWaitFor(
        [&] {
          return !scan.running() &&
                 WaveIndex::load(studyDir).fileCount() == files &&
                 WaveIndex::load(studyDir).pendingCoverage() == 0;
        },
        30000);
  };
  scan.start();
  QVERIFY(settled(12));
  QVERIFY(scan.unavailable().isEmpty());

  // The drive goes: the folder is there, with nothing in it.
  for (const QString &sub : QDir(data.path()).entryList(
           QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
    const QString path = data.filePath(sub);
    QVERIFY(QFileInfo(path).isDir() ? QDir(path).removeRecursively()
                                    : QFile::remove(path));
  }
  QVERIFY(QDir(data.path()).isEmpty());

  WaveformsDialog dialog(ctx, &scan);
  auto *folders = dialog.findChild<QListWidget *>("waveFolders");
  QVERIFY(folders);
  QPushButton *rescan = nullptr, *remove = nullptr;
  for (QPushButton *b : dialog.findChildren<QPushButton *>()) {
    if (b->text() == WaveformsDialog::tr("Scan again"))
      rescan = b;
    if (b->text() == WaveformsDialog::tr("Remove"))
      remove = b;
  }
  QVERIFY(rescan && remove);
  QCOMPARE(scan.unavailable(), QStringList{QDir(data.path()).path()});
  QVERIFY2(folders->item(0)->text().contains("holds nothing now"),
           qPrintable(folders->item(0)->text()));
  QVERIFY(!folders->item(1)->text().contains("holds nothing now"));

  // Scan again, and say "keep": nothing is forgotten, and the status says
  // which folder is away.
  QStringList asked;
  dialog.confirmForget = [&asked](const QString &q) {
    asked << q;
    return false;
  };
  rescan->click();
  QVERIFY(settled(12));
  QCOMPARE(asked.size(), 1);
  QVERIFY(asked.first().contains(QDir(data.path()).path()));
  QVERIFY(WaveIndex::load(studyDir).roots.value(id).size() == 6);
  QVERIFY2(scan.status().contains(QDir(data.path()).path()),
           qPrintable(scan.status()));
  const QString log =
      jobs.jobAt(jobs.rowCount() - 1)->log().join(QLatin1Char('\n'));
  QVERIFY2(log.contains("lists nothing now"), qPrintable(log));

  // Scan again, and say "forget": the folder's files leave the index.
  dialog.confirmForget = [&asked](const QString &q) {
    asked << q;
    return true;
  };
  rescan->click();
  QVERIFY(settled(6));
  QCOMPARE(asked.size(), 2);
  QVERIFY(WaveIndex::load(studyDir).roots.value(id).isEmpty());
  QVERIFY(scan.unavailable().isEmpty());
  // Nothing away any more: no question.
  rescan->click();
  QVERIFY(settled(6));
  QCOMPARE(asked.size(), 2);

  // Remove the other folder while a scan runs: the button works, the scan
  // is stopped for it, and the folder is gone from the study and the index.
  manyFiles(other.path(), 9); // something new to read
  scan.start();
  QVERIFY(scan.running());
  folders = dialog.findChild<QListWidget *>("waveFolders");
  folders->setCurrentRow(1);
  QVERIFY(remove->isEnabled());
  remove->click();
  QVERIFY(QTest::qWaitFor([&] { return !scan.running(); }, 30000));
  QCOMPARE(store.study().waveRoots.size(), 1);
  QCOMPARE(store.study().waveRoots.first().id, id);
  QVERIFY(!WaveIndex::load(studyDir).roots.contains(idOther));
}

void TstStudyScan::settingsPage() {
  QTemporaryDir dir;
  ScanCache::setSharedDir(dir.filePath("cache"));
  ScanCache cache(ScanCache::sharedDir());
  WaveIndex index;
  ScanOptions options;
  options.cache = &cache;
  scanBoth(index, "w1", root_, options);
  QCOMPARE(cache.usage().files, 5);

  QSettings settings(dir.filePath("s.ini"), QSettings::IniFormat);
  Theme theme(&settings);
  I18n i18n(&settings);
  ToolRegistry tools(&settings);
  JobModel jobs;
  AppContext ctx;
  ctx.jobs = &jobs;
  ctx.tools = &tools;
  ctx.settings = &settings;
  ctx.theme = &theme;
  SettingsModule module(&i18n, &theme);
  std::unique_ptr<QWidget> page(module.createWidget(ctx, nullptr));
  page->show();
  QVERIFY(QTest::qWaitForWindowExposed(page.get()));
  auto *workers = page->findChild<QSpinBox *>(QStringLiteral("scanWorkers"));
  auto *usage = page->findChild<QLabel *>(QStringLiteral("scanCacheUsage"));
  auto *clear =
      page->findChild<QPushButton *>(QStringLiteral("clearScanCache"));
  QVERIFY(workers && usage && clear);

  // Automatic unless set; the setting is what the scan uses.
  QCOMPARE(workers->value(), 0);
  QVERIFY(workers->text().contains(QString::number(defaultScanWorkers())));
  QCOMPARE(WaveScan::workers(&settings), defaultScanWorkers());
  QVERIFY(defaultScanWorkers() >= 1 && defaultScanWorkers() <= 6);
  workers->setValue(2);
  QCOMPARE(settings.value(QLatin1String(kScanWorkersSetting)).toInt(), 2);
  QCOMPARE(WaveScan::workers(&settings), 2);
  workers->setValue(0);
  QCOMPARE(WaveScan::workers(&settings), defaultScanWorkers());
  QVERIFY(!workers->toolTip().isEmpty() && !clear->toolTip().isEmpty());

  // The cache is shown and can be emptied; the study's index is not touched.
  QVERIFY2(usage->text().contains(QStringLiteral("5 waveform file")),
           qPrintable(usage->text()));
  QVERIFY(clear->isEnabled());
  clear->click();
  QCOMPARE(cache.usage().files, 0);
  QVERIFY2(usage->text().contains(QStringLiteral("0 waveform file")),
           qPrintable(usage->text()));
  QVERIFY(!clear->isEnabled());
  QCOMPARE(index.pendingCoverage(), 0);
}

// Not a test: timings of the passes on a folder, for a person to read. Runs
// only when asked for:
//   SKG_SCAN_BENCH_DATA  the folder to scan (only ever read)
//   SKG_SCAN_BENCH_OUT   a scratch folder for the index and the cache
//   SKG_SCAN_BENCH_MAKE  "6x150": first fill an empty DATA with that many
//                        archives of that many megabytes, shaped like the
//                        TDVMS ones (one deflated member each)
//   SKG_SCAN_BENCH_WORKERS  workers of the parallel run (default: automatic)
void TstStudyScan::scanBenchmark() {
  const QString data = qEnvironmentVariable("SKG_SCAN_BENCH_DATA");
  const QString out = qEnvironmentVariable("SKG_SCAN_BENCH_OUT");
  if (data.isEmpty() || out.isEmpty())
    QSKIP("set SKG_SCAN_BENCH_DATA and SKG_SCAN_BENCH_OUT to time a folder");
  const QString make = qEnvironmentVariable("SKG_SCAN_BENCH_MAKE");
  if (!make.isEmpty() &&
      QDir(data).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty()) {
    const int count = make.section(QLatin1Char('x'), 0, 0).toInt();
    const int megabytes = make.section(QLatin1Char('x'), 1, 1).toInt();
    QVERIFY(count > 0 && megabytes > 0);
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < count; ++i) {
      const QByteArray sta = QByteArray("B") + QByteArray::number(i % 3);
      const QDateTime first = utc(2021, 1, 1).addDays(i * 30);
      // Samples of seven random bits a byte: a zip of them is about 0.9 of
      // their size and is inflated symbol by symbol, as real waveforms are.
      QByteArray bytes = noisyRecords(sta, first, megabytes, quint32(i + 1));
      for (qsizetype r = 0; r + 512 <= bytes.size(); r += 512)
        for (int b = 64; b < 512; ++b)
          bytes[r + b] = char(bytes[r + b] & 0x7f);
      const qint64 seconds = qint64(megabytes) * 2048 * 10;
      zipWith(QStringLiteral("%1/%2/%2_%3.zip")
                  .arg(data, QString::fromLatin1(sta),
                       first.toString(QStringLiteral("yyyy-MM-dd"))),
              {{QStringLiteral("TU_%1_%2_000000_%3_000000_HH.mseed")
                    .arg(QString::fromLatin1(sta),
                         first.toString(QStringLiteral("ddMMyyyy")),
                         first.addSecs(seconds).addDays(1).toString(
                             QStringLiteral("ddMMyyyy"))),
                bytes}});
    }
    qInfo("made %d archives of %d MB in %.1f s", count, megabytes,
          double(t.elapsed()) / 1000.0);
  }
  QVERIFY(QDir().mkpath(out));
  const int many = qEnvironmentVariableIntValue("SKG_SCAN_BENCH_WORKERS") > 0
                       ? qEnvironmentVariableIntValue("SKG_SCAN_BENCH_WORKERS")
                       : defaultScanWorkers();
  QElapsedTimer t;
  const auto seconds = [&t] { return double(t.elapsed()) / 1000.0; };

  t.start();
  WaveIndex quick;
  const ScanStats q = scanRoot(quick, "w1", data, ScanPass::Identity);
  const double quickS = seconds();
  QVERIFY(quick.save(out));
  qInfo("quick pass: %d files in %.3f s; %d station(s), %d file(s) with an "
        "estimate, %d without, %d skipped",
        q.listed, quickS, int(quick.stations().size()), quick.estimatedFiles(),
        quick.pendingCoverage() - quick.estimatedFiles(), q.skipped);
  for (const WaveStation &w : quick.stations())
    qInfo("  %s: about %d day(s) estimated, %d file(s)", qPrintable(w.code),
          w.days, w.files);

  const auto exact = [&](int workers, ScanCache *cache, const char *label) {
    WaveIndex index = quick;
    ScanOptions options;
    options.workers = workers;
    options.cache = cache;
    t.restart();
    const ScanStats s =
        scanRoot(index, "w1", data, ScanPass::Coverage, {}, options);
    const double took = seconds();
    const double mb = double(s.bytes) / 1048576.0;
    qInfo("exact pass, %s, %d worker(s): %d file(s), %.0f MB on disk in "
          "%.2f s = %.0f MB/s",
          label, workers, s.read, mb, took, took > 0 ? mb / took : 0.0);
    return index;
  };
  // The first read may come from the disk; the later ones from memory, if
  // the machine has enough of it: the difference is the disk.
  const WaveIndex first = exact(1, nullptr, "first read");
  const WaveIndex serial = exact(1, nullptr, "read again");
  ScanCache cache(QDir(out).filePath("cache"));
  cache.clear();
  const WaveIndex parallel = exact(many, &cache, "read again");
  QCOMPARE(dump(parallel), dump(serial));
  QCOMPARE(dump(first), dump(serial));
  QVERIFY(parallel.save(out));
  for (const WaveStation &w : parallel.stations())
    qInfo("  %s: %d day(s) exactly", qPrintable(w.code), w.days);

  t.restart();
  WaveIndex again;
  ScanOptions options;
  options.cache = &cache;
  const ScanStats c =
      scanRoot(again, "w1", data, ScanPass::Identity, {}, options);
  qInfo("a second study, from the cache: %d of %d file(s) in %.3f s, %d left "
        "to read",
        c.cached, c.listed, seconds(), again.pendingCoverage());
  QCOMPARE(contents(again), contents(serial));
}

QTEST_MAIN(TstStudyScan)
#include "tst_study_scan.moc"
