// The waveform index: what a folder of miniSEED holds is read from the
// record headers (plain files and zips, any layout), kept by relative path so
// the folder can move, and used to pick the files that overlap a case's
// span. Fixtures are tiny hand-made records: 1 Hz, 12 hours each.
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include "miniz.h"
#include "study/runs.h"
#include "study/study.h"
#include "study/waveindex.h"

using namespace skg::study;

// The built-in station list is a resource of the tdvms library.
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

void write(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(bytes);
}

void zipWith(const QString &path, const QMap<QString, QByteArray> &members) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile::remove(path);
  mz_zip_archive zip{};
  QVERIFY(mz_zip_writer_init_file(&zip, path.toLocal8Bit().constData(), 0));
  for (auto it = members.begin(); it != members.end(); ++it)
    QVERIFY(mz_zip_writer_add_mem(&zip, it.key().toUtf8().constData(),
                                  it.value().constData(),
                                  size_t(it.value().size()),
                                  MZ_DEFAULT_COMPRESSION));
  QVERIFY(mz_zip_writer_finalize_archive(&zip));
  mz_zip_writer_end(&zip);
}

const WaveStation *find(const QList<WaveStation> &all, const QString &code) {
  for (const WaveStation &w : all)
    if (w.code == code)
      return &w;
  return nullptr;
}

} // namespace

class TstStudyWaveIndex : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void identityPassFindsStations();
  void coveragePassCountsDays();
  void unchangedFilesAreNotReadAgain();
  void indexSurvivesAMovedFolder();
  void filesForTheSpan();
  void namesAreTheFallback();
  void studyKeepsFoldersAndStations();
  void stationXml();
  void stampTellsWhenAScanIsDue();
  void scanSpeed();

private:
  QTemporaryDir tmp_;
  QString root_;
};

void TstStudyWaveIndex::initTestCase() {
  initResources();
  root_ = tmp_.filePath("data");
  // A zip filed like a TDVMS request: named by the day the request starts,
  // holding three weeks that begin later. Station AAA, 2021-01-10..12.
  zipWith(root_ + "/AAA/AAA_2020-11-01.zip",
          {{"TU_AAA_x_HH.mseed", days("AAA", utc(2021, 1, 10), 3)}});
  // Named for a day inside the span, but its data is from 2019.
  zipWith(root_ + "/AAA/AAA_2021-01-11.zip",
          {{"TU_AAA_y_HH.mseed", days("AAA", utc(2019, 6, 1), 1)}});
  // A plain file of BBB with a gap: day 1, then day 3; and only half of day 5.
  write(root_ + "/BBB/b.mseed",
        days("BBB", utc(2021, 1, 1), 1) + days("BBB", utc(2021, 1, 3), 1) +
            record(utc(2021, 1, 5), "BBB"));
  // Both stations in one file, in a folder named for neither.
  write(root_ + "/odd/mixed.dat",
        days("AAA", utc(2021, 2, 1), 1) + days("BBB", utc(2021, 2, 1), 1));
  // Not miniSEED, and folders that are not entered.
  write(root_ + "/BBB/notes.txt", "station log\n");
  write(root_ + "/_campaigns/ledger.mseed", days("ZZZ", utc(2021, 1, 1), 1));
  write(root_ + "/.cache/x.mseed", days("ZZZ", utc(2021, 1, 1), 1));
}

void TstStudyWaveIndex::identityPassFindsStations() {
  WaveIndex index;
  const ScanStats st = scanRoot(index, "w1", root_, ScanPass::Identity);
  QCOMPARE(st.listed, 5); // the two zips, b.mseed, mixed.dat, notes.txt
  QCOMPARE(st.read, 5);
  QCOMPARE(st.skipped, 1);
  QVERIFY(index.hasStation("AAA"));
  QVERIFY(index.hasStation("aaa")); // codes are compared without case
  QVERIFY(index.hasStation("BBB"));
  QVERIFY(!index.hasStation("ZZZ")); // _campaigns and .cache are not entered
  const QList<SkippedFile> skipped = index.skipped();
  QCOMPARE(skipped.size(), 1);
  QCOMPARE(skipped[0].rel, QStringLiteral("BBB/notes.txt"));
  QCOMPARE(skipped[0].why, QStringLiteral("not miniSEED"));
  const QList<WaveStation> stations = index.stations();
  QCOMPARE(stations.size(), 2);
  const WaveStation *a = find(stations, "AAA");
  QVERIFY(a);
  QCOMPARE(a->network, QStringLiteral("TU"));
  QVERIFY(!a->coverageKnown);
  QCOMPARE(a->rates, QList<double>{1.0});
  // The first pass sees the first record of a plain file only, so the
  // second station of mixed.dat is not known yet: AAA has three files.
  QCOMPARE(a->files, 3);
  QCOMPARE(index.pendingCoverage(), 4);
  // Before the coverage is read a file counts for the day it starts on.
  bool approx = false;
  QCOMPARE(index.daysCovered("AAA", utc(2021, 1, 1), utc(2021, 2, 1), &approx),
           1);
  QVERIFY(approx);
}

void TstStudyWaveIndex::coveragePassCountsDays() {
  WaveIndex index;
  scanRoot(index, "w1", root_, ScanPass::Identity);
  const ScanStats st = scanRoot(index, "w1", root_, ScanPass::Coverage);
  QCOMPARE(st.read, 4); // the skipped file is not tried again
  QCOMPARE(index.pendingCoverage(), 0);
  const QList<WaveStation> stations = index.stations();
  const WaveStation *a = find(stations, "AAA");
  const WaveStation *b = find(stations, "BBB");
  QVERIFY(a && b);
  QVERIFY(a->coverageKnown && b->coverageKnown);
  QCOMPARE(a->channels, (QStringList{"HHE", "HHN", "HHZ"}));
  QCOMPARE(a->days, 3 + 1 + 1); // Jan 10-12, the 2019 day, Feb 1
  QCOMPARE(b->days, 1 + 1 + 1 + 1); // Jan 1, 3, half of 5, Feb 1
  QCOMPARE(b->files, 2);
  // Both are in mixed.dat, which sits in a folder named for neither.
  QVERIFY(a->sharedFiles && b->sharedFiles);
  QVERIFY(a->otherFolders.contains("odd"));
  QVERIFY(!a->otherFolders.contains("AAA"));
  QVERIFY(!a->mixedRates);
  bool approx = true;
  QCOMPARE(index.daysCovered("AAA", utc(2021, 1, 1), utc(2021, 2, 1), &approx),
           3);
  QVERIFY(!approx);
  // The gap on Jan 2 and Jan 4 shows; a half day still counts as a day.
  QCOMPARE(index.daysCovered("BBB", utc(2021, 1, 1), utc(2021, 1, 6)), 3);
  QCOMPARE(index.daysCovered("BBB", utc(2021, 1, 2), utc(2021, 1, 3)), 0);
  QCOMPARE(index.daysCovered("CCC", utc(2021, 1, 1), utc(2021, 2, 1)), 0);
}

void TstStudyWaveIndex::unchangedFilesAreNotReadAgain() {
  QTemporaryDir studyDir;
  WaveIndex index;
  scanRoot(index, "w1", root_, ScanPass::Coverage);
  QVERIFY(index.save(studyDir.path()));
  WaveIndex again = WaveIndex::load(studyDir.path());
  QCOMPARE(again.fileCount(), 5);
  QCOMPARE(again.stations().size(), 2);
  const ScanStats st = scanRoot(again, "w1", root_, ScanPass::Coverage);
  QCOMPARE(st.read, 0);
  // A file that changed is read again; one that went away is dropped.
  QTemporaryDir copy;
  const QString root = copy.filePath("r");
  write(root + "/AAA/a.mseed", days("AAA", utc(2021, 3, 1), 1));
  write(root + "/AAA/b.mseed", days("AAA", utc(2021, 3, 2), 1));
  WaveIndex small;
  QCOMPARE(scanRoot(small, "w1", root, ScanPass::Coverage).read, 2);
  write(root + "/AAA/a.mseed", days("AAA", utc(2021, 3, 1), 2));
  QVERIFY(QFile::remove(root + "/AAA/b.mseed"));
  const ScanStats changed = scanRoot(small, "w1", root, ScanPass::Coverage);
  QCOMPARE(changed.read, 1);
  QCOMPARE(changed.dropped, 1);
  QCOMPARE(small.stations().first().days, 2);
  // A folder that is not there is an error, not an empty result.
  QVERIFY_EXCEPTION_THROWN(
      scanRoot(small, "w1", root + "/nowhere", ScanPass::Identity),
      std::runtime_error);
}

void TstStudyWaveIndex::indexSurvivesAMovedFolder() {
  // The data moves to another drive: same files, another path.
  QTemporaryDir studyDir, drives;
  const QString before = drives.filePath("laptop/data");
  const QString after = drives.filePath("portable/data");
  write(before + "/AAA/a.mseed", days("AAA", utc(2021, 3, 1), 2));
  zipWith(before + "/BBB/b.zip", {{"m.mseed", days("BBB", utc(2021, 3, 1), 1)}});
  Study s;
  s.dir = studyDir.path();
  const QString id = s.addWaveRoot(before);
  WaveIndex index;
  QCOMPARE(scanRoot(index, id, s.waveRootPath(id), ScanPass::Coverage).read, 2);
  QVERIFY(index.save(s.dir));
  QVERIFY(QDir().mkpath(drives.filePath("portable")));
  QVERIFY(QDir().rename(before, after));
  s.waveRoots[0].path = s.stored(after);
  WaveIndex loaded = WaveIndex::load(s.dir);
  // Nothing is read again, and the files are found at the new place.
  QCOMPARE(scanRoot(loaded, id, s.waveRootPath(id), ScanPass::Coverage).read,
           0);
  QCOMPARE(loaded.stations().size(), 2);
  QVERIFY(loaded.save(s.dir));
  StationRef st;
  st.code = "AAA";
  const QStringList files =
      waveFilesFor(s, loaded, st, utc(2021, 3, 1), utc(2021, 3, 2));
  QCOMPARE(files, QStringList{after + "/AAA/a.mseed"});
  QVERIFY(hasWaveforms(s, st));
}

void TstStudyWaveIndex::filesForTheSpan() {
  QTemporaryDir studyDir;
  Study s;
  s.dir = studyDir.path();
  const QString id = s.addWaveRoot(root_);
  QCOMPARE(s.addWaveRoot(root_), id); // the same folder is not listed twice
  WaveIndex index;
  scanRoot(index, id, root_, ScanPass::Coverage);
  StationRef a;
  a.code = "AAA";
  // The zip named 2020-11-01 holds the span's days; the one named
  // 2021-01-11 does not. The headers decide, not the names.
  QString how;
  QStringList files =
      waveFilesFor(s, index, a, utc(2021, 1, 11), utc(2021, 1, 12), &how);
  QCOMPARE(files, QStringList{root_ + "/AAA/AAA_2020-11-01.zip"});
  QVERIFY2(how.contains("index"), qPrintable(how));
  // A span over February takes the shared file from the oddly named folder.
  files = waveFilesFor(s, index, a, utc(2021, 1, 25), utc(2021, 2, 5));
  QCOMPARE(files, QStringList{root_ + "/odd/mixed.dat"});
  // No data in the span: nothing, although the station is known.
  QVERIFY(waveFilesFor(s, index, a, utc(2022, 1, 1), utc(2022, 2, 1)).isEmpty());
  // Before the coverage is read, a file is taken when it starts in the span
  // or up to five weeks before it.
  WaveIndex first;
  scanRoot(first, id, root_, ScanPass::Identity);
  files = waveFilesFor(s, first, a, utc(2021, 1, 20), utc(2021, 1, 25));
  QCOMPARE(files, QStringList{root_ + "/AAA/AAA_2020-11-01.zip"});
}

void TstStudyWaveIndex::namesAreTheFallback() {
  QCOMPARE(dateInFileName("AAA_2020-11-01.zip"), QDate(2020, 11, 1));
  QCOMPARE(dateInFileName("TU_ELBA_09092025_000000_10092025_000000_HH.mseed"),
           QDate(2025, 9, 9));
  QVERIFY(!dateInFileName("waveforms.mseed").isValid());
  // A station the index does not know, with a folder of its own: the dates
  // in the file names choose, with five weeks of slack before the span.
  QTemporaryDir studyDir, data;
  write(data.filePath("CCC/CCC_2019-01-01.zip"), "x");
  write(data.filePath("CCC/CCC_2020-12-20.zip"), "x");
  write(data.filePath("CCC/CCC_2021-01-05.zip"), "x");
  write(data.filePath("CCC/CCC_2021-03-01.zip"), "x");
  write(data.filePath("CCC/undated.mseed"), "x");
  Study s;
  s.dir = studyDir.path();
  s.addWaveRoot(data.path());
  StationRef c;
  c.code = "CCC";
  QString how;
  const QStringList files =
      waveFilesFor(s, WaveIndex(), c, utc(2021, 1, 1), utc(2021, 2, 1), &how);
  QCOMPARE(files, (QStringList{data.filePath("CCC/CCC_2020-12-20.zip"),
                               data.filePath("CCC/CCC_2021-01-05.zip"),
                               data.filePath("CCC/undated.mseed")}));
  QVERIFY2(how.contains("not indexed"), qPrintable(how));
  QVERIFY(hasWaveforms(s, c));
  // A folder chosen for the station by hand wins over the index.
  StationRef own;
  own.code = "AAA";
  own.dataDir = data.filePath("CCC");
  WaveIndex index;
  scanRoot(index, "w1", root_, ScanPass::Identity);
  QVERIFY(waveFilesFor(s, index, own, utc(2021, 1, 1), utc(2021, 2, 1))
              .contains(data.filePath("CCC/CCC_2021-01-05.zip")));
  StationRef none;
  none.code = "NOPE";
  QVERIFY(!hasWaveforms(s, none));
  QVERIFY(waveFilesFor(s, WaveIndex(), none, utc(2021, 1, 1), utc(2021, 2, 1))
              .isEmpty());
}

void TstStudyWaveIndex::studyKeepsFoldersAndStations() {
  QTemporaryDir dir;
  Study s;
  s.dir = dir.path();
  s.name = "x";
  QCOMPARE(s.addWaveRoot(dir.filePath("in/data")), QStringLiteral("w1"));
  QCOMPARE(s.addWaveRoot("/elsewhere/data"), QStringLiteral("w2"));
  QCOMPARE(s.waveRoots[0].path, QStringLiteral("in/data")); // kept relative
  s.extraStations << ExtraStation{"XYZ", "KO", 38.5, 39.25, "typed"};
  const Study b = Study::fromToml(s.toToml(), dir.path());
  QCOMPARE(b.waveRoots.size(), 2);
  QCOMPARE(b.waveRootPath("w1"), dir.filePath("in/data"));
  QCOMPARE(b.waveRootPath("w2"), QStringLiteral("/elsewhere/data"));
  QVERIFY(b.waveRootPath("w9").isEmpty());
  QCOMPARE(b.extraStations.size(), 1);
  QCOMPARE(b.extraStations[0].lat, 38.5);
  QCOMPARE(b.extraStations[0].source, QStringLiteral("typed"));
  // The extra station is offered beside the built-in list.
  const auto all = b.stations();
  QVERIFY(std::any_of(all.begin(), all.end(), [](const auto &st) {
    return st.code == "XYZ" && st.lon == 39.25;
  }));
  QVERIFY(all.size() > 1);
  // A study file from before [[waveforms]].
  const Study old = Study::fromToml(
      "schema = 1\nname = \"o\"\ndata_root = \"mseed\"\n", dir.path());
  QCOMPARE(old.waveRoots.size(), 1);
  QCOMPARE(old.waveRootPath("w1"), dir.filePath("mseed"));
  // The station's folder: the first waveform folder that has one.
  QDir().mkpath(dir.filePath("second/STA1"));
  Study two;
  two.dir = dir.path();
  two.addWaveRoot(dir.filePath("first"));
  two.addWaveRoot(dir.filePath("second"));
  StationRef st;
  st.code = "STA1";
  QCOMPARE(two.dataDirFor(st), dir.filePath("second/STA1"));
}

void TstStudyWaveIndex::stationXml() {
  QTemporaryDir dir;
  const QString path = dir.filePath("s.xml");
  write(path,
        "<?xml version=\"1.0\"?>\n<FDSNStationXML><Network code=\"KO\">"
        "<Station code=\"ABC\"><Latitude>38.5</Latitude>"
        "<Longitude>39.25</Longitude>"
        "<Channel code=\"HHZ\"><Latitude>1</Latitude><Longitude>2</Longitude>"
        "</Channel></Station>"
        "<Station code=\"DEF\"><Latitude unit=\"DEGREES\">40.0</Latitude>"
        "<Longitude>30.0</Longitude></Station>"
        "</Network></FDSNStationXML>");
  const QList<ExtraStation> got = readStationXml(path);
  QCOMPARE(got.size(), 2);
  QCOMPARE(got[0].code, QStringLiteral("ABC"));
  QCOMPARE(got[0].network, QStringLiteral("KO"));
  QCOMPARE(got[0].lat, 38.5); // the station's, not its channel's
  QCOMPARE(got[0].lon, 39.25);
  QCOMPARE(got[1].lat, 40.0);
  QCOMPARE(got[0].source, QStringLiteral("stationxml"));
  write(dir.filePath("bad.xml"), "<a><b></a>");
  QVERIFY_EXCEPTION_THROWN(readStationXml(dir.filePath("bad.xml")),
                           std::runtime_error);
}

void TstStudyWaveIndex::stampTellsWhenAScanIsDue() {
  QTemporaryDir data;
  write(data.filePath("AAA/a.mseed"), days("AAA", utc(2021, 3, 1), 1));
  write(data.filePath("_campaigns/x.txt"), "x");
  const QString before = folderStamp(data.path());
  QCOMPARE(folderStamp(data.path()), before); // looking changes nothing
  QTest::qWait(20);
  // A file in a folder that is not scanned does not count.
  write(data.filePath("_campaigns/y.txt"), "y");
  QCOMPARE(folderStamp(data.path()), before);
  // A new file in a station's folder, or a new folder, does.
  write(data.filePath("AAA/b.mseed"), days("AAA", utc(2021, 3, 2), 1));
  const QString second = folderStamp(data.path());
  QVERIFY(second != before);
  QTest::qWait(20);
  write(data.filePath("BBB/c.mseed"), days("BBB", utc(2021, 3, 2), 1));
  QVERIFY(folderStamp(data.path()) != second);
  QCOMPARE(folderStamp(data.filePath("nowhere")), QStringLiteral("missing"));
}

void TstStudyWaveIndex::scanSpeed() {
  // Not a claim about real archives: a figure for these fixtures only, so a
  // gross slowdown shows up. 200 plain files of 60 records each.
  QTemporaryDir data;
  for (int i = 0; i < 200; ++i)
    write(data.filePath(QStringLiteral("S%1/f%2.mseed").arg(i % 4).arg(i)),
          days(QByteArray("S") + QByteArray::number(i % 4),
               utc(2021, 1, 1).addDays(i), 10));
  WaveIndex index;
  QElapsedTimer t;
  t.start();
  scanRoot(index, "w1", data.path(), ScanPass::Identity);
  const qint64 first = t.restart();
  scanRoot(index, "w1", data.path(), ScanPass::Coverage);
  const qint64 second = t.elapsed();
  qInfo("200 files (6.1 MB): identity %lld ms, coverage %lld ms", first,
        second);
  QCOMPARE(index.stations().size(), 4);
  QVERIFY(first < 10000 && second < 20000);
}

QTEST_GUILESS_MAIN(TstStudyWaveIndex)
#include "tst_study_waveindex.moc"
