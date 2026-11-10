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

#include <atomic>
#include <thread>
#include <vector>

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
  void longFilesAreNotDroppedBeforeTheExactPass();
  void earliestMemberStartCounts();
  void emptyFolderKeepsItsEntries();
  void coarseFileTimesStillMatch();
  void indexSavesWhileItIsRead();
  void namesAreTheFallback();
  void studyKeepsFoldersAndStations();
  void oneFolderInSeveralSpellings();
  void rootIdsAreNeverReused();
  void relocateIgnoresCaseWhereTheSystemDoes();
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
  // Before the coverage is read, and with no period in the names, nothing
  // says how far a file reaches: every file of the station that starts
  // before the span ends is taken, however long before, and marked as taken
  // on a guess. The file from 2019 used to be dropped here (more than five
  // weeks before the span), which lost the data of long files.
  WaveIndex first;
  scanRoot(first, id, root_, ScanPass::Identity);
  const WaveChoice early =
      chooseWaveFiles(s, first, a, utc(2021, 1, 20), utc(2021, 1, 25));
  QCOMPARE(early.files, (QStringList{root_ + "/AAA/AAA_2020-11-01.zip",
                                     root_ + "/AAA/AAA_2021-01-11.zip"}));
  QCOMPARE(early.uncertain, early.files);
  // A file that starts after the span is not taken.
  QVERIFY(!early.files.contains(root_ + "/odd/mixed.dat"));
}

// A plain file of a whole year, read by the quick pass only: a span late in
// the year must get it, and it must be marked as an estimate.
void TstStudyWaveIndex::longFilesAreNotDroppedBeforeTheExactPass() {
  QTemporaryDir studyDir, data;
  write(data.filePath("LLL/year.mseed"),
        record(utc(2021, 1, 1), "LLL") + record(utc(2021, 11, 20), "LLL"));
  Study s;
  s.dir = studyDir.path();
  const QString id = s.addWaveRoot(data.path());
  WaveIndex index;
  scanRoot(index, id, data.path(), ScanPass::Identity);
  StationRef l;
  l.code = "LLL";
  const WaveChoice c =
      chooseWaveFiles(s, index, l, utc(2021, 11, 1), utc(2021, 12, 1));
  QCOMPARE(c.files, QStringList{data.filePath("LLL/year.mseed")});
  QCOMPARE(c.uncertain, c.files);
  QVERIFY(c.how.contains("estimate"));
  // Read through, the same span takes it for certain.
  scanRoot(index, id, data.path(), ScanPass::Coverage);
  const WaveChoice exact =
      chooseWaveFiles(s, index, l, utc(2021, 11, 1), utc(2021, 12, 1));
  QCOMPARE(exact.files, c.files);
  QVERIFY(exact.uncertain.isEmpty());
}

// The members of an archive are not in the order of time: the earliest start
// is the file's.
void TstStudyWaveIndex::earliestMemberStartCounts() {
  QTemporaryDir data;
  // The directory lists "a_late" before "b_early".
  zipWith(data.filePath("MMM/m.zip"),
          {{"a_late.mseed", record(utc(2021, 6, 1), "MMM")},
           {"b_early.mseed", record(utc(2021, 3, 1), "MMM")}});
  WaveIndex index;
  scanRoot(index, "w1", data.path(), ScanPass::Identity);
  const WaveFile f = index.roots["w1"]["MMM/m.zip"];
  QCOMPARE(f.channels.size(), 1);
  QCOMPARE(f.channels.first().firstStart,
           double(utc(2021, 3, 1).toSecsSinceEpoch()));
  const auto refs = index.filesFor("MMM", utc(2021, 4, 1), utc(2021, 5, 1));
  QCOMPARE(refs.size(), 1); // starts before the span ends
}

// A folder that is there but lists nothing: the mount point of a drive that
// is not connected. What the index knows of it is kept, and said; only when
// asked are the entries dropped. Files gone from a folder that still holds
// others are dropped as before.
void TstStudyWaveIndex::emptyFolderKeepsItsEntries() {
  QTemporaryDir studyDir, data;
  write(data.filePath("AAA/a1.mseed"), days("AAA", utc(2021, 1, 1), 1));
  write(data.filePath("AAA/a2.mseed"), days("AAA", utc(2021, 1, 2), 1));
  write(data.filePath("BBB/b1.mseed"), days("BBB", utc(2021, 1, 1), 1));
  Study s;
  s.dir = studyDir.path();
  const QString id = s.addWaveRoot(data.path());
  WaveIndex index;
  scanRoot(index, id, data.path(), ScanPass::Coverage);
  QCOMPARE(index.fileCount(), 3);
  QVERIFY(unavailableWaveRoots(s, index).isEmpty());

  // One file of a folder that still holds another: it is gone.
  QVERIFY(QFile::remove(data.filePath("AAA/a2.mseed")));
  ScanStats st = scanRoot(index, id, data.path(), ScanPass::Coverage);
  QCOMPARE(st.dropped, 1);
  QVERIFY(st.unavailable.isEmpty());
  QCOMPARE(index.fileCount(), 2);

  // A sub-folder that lists nothing: its entry stays.
  QVERIFY(QFile::remove(data.filePath("BBB/b1.mseed")));
  st = scanRoot(index, id, data.path(), ScanPass::Coverage);
  QCOMPARE(st.dropped, 0);
  QCOMPARE(st.unavailable, QStringList{data.filePath("BBB")});
  QVERIFY(index.hasStation("BBB"));

  // The whole folder empty, as when the drive is not mounted.
  QVERIFY(QDir(data.filePath("AAA")).removeRecursively());
  QVERIFY(QDir(data.filePath("BBB")).removeRecursively());
  QVERIFY(QDir(data.path()).isEmpty());
  st = scanRoot(index, id, data.path(), ScanPass::Coverage);
  QCOMPARE(st.dropped, 0);
  QCOMPARE(st.unavailable, QStringList{QDir(data.path()).path()});
  QCOMPARE(index.fileCount(), 2);
  QVERIFY(index.hasStation("AAA") && index.hasStation("BBB"));
  QCOMPARE(unavailableWaveRoots(s, index), QStringList{QDir(data.path()).path()});
  // The files Features would read are still named (they are just not there
  // to open now), so a run says "cannot open", not "no data in the span".
  StationRef a;
  a.code = "AAA";
  QCOMPARE(waveFilesFor(s, index, a, utc(2021, 1, 1), utc(2021, 1, 2)).size(),
           1);

  // The drive is back: nothing is read again, nothing was lost.
  write(data.filePath("AAA/a1.mseed"), days("AAA", utc(2021, 1, 1), 1));
  // (a new file on disk has a new time, so this one is read; the point is
  // that the entries survived the empty folder)
  st = scanRoot(index, id, data.path(), ScanPass::Coverage);
  QVERIFY(unavailableWaveRoots(s, index).isEmpty());
  QVERIFY(index.hasStation("AAA"));

  // Asked to forget: an empty folder's entries go like any other.
  QVERIFY(QDir(data.filePath("AAA")).removeRecursively());
  ScanOptions forget;
  forget.forgetEmptyFolders = true;
  st = scanRoot(index, id, data.path(), ScanPass::Coverage, {}, forget);
  QVERIFY(st.unavailable.isEmpty());
  QCOMPARE(index.fileCount(), 0);
}

// A file copied to a drive that keeps coarser times (FAT two seconds, HFS+
// one, exFAT a hundredth) is still the file the index knows; a file whose
// time really changed is not.
void TstStudyWaveIndex::coarseFileTimesStillMatch() {
  QVERIFY(sameFileTime(1700000000.123, 1700000000.123));
  QVERIFY(sameFileTime(1700000000.123, 1700000000.12));  // exFAT
  QVERIFY(sameFileTime(1700000000.123, 1700000000.0));   // HFS+
  QVERIFY(sameFileTime(1700000001.900, 1700000002.0));   // FAT, rounded up
  QVERIFY(sameFileTime(1700000001.900, 1700000000.0));   // FAT, cut off
  QVERIFY(sameFileTime(1700000002.0, 1700000001.900));   // and back again
  QVERIFY(!sameFileTime(1700000000.123, 1700000000.523)); // rewritten
  QVERIFY(!sameFileTime(1700000000.123, 1700000003.0));
  QVERIFY(!sameFileTime(1700000000.0, 1700000010.0));

  // The index of a folder survives the move to such a drive: the files come
  // back with rounded times and are not read again.
  QTemporaryDir fine, coarse;
  write(fine.filePath("AAA/a.mseed"), days("AAA", utc(2021, 1, 1), 1));
  write(fine.filePath("BBB/b.mseed"), days("BBB", utc(2021, 1, 1), 1));
  const QDateTime odd = QDateTime::fromMSecsSinceEpoch(1700000001900LL);
  for (const char *rel : {"AAA/a.mseed", "BBB/b.mseed"}) {
    QFile f(fine.filePath(rel));
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.setFileTime(odd, QFileDevice::FileModificationTime));
  }
  WaveIndex index;
  ScanStats st = scanRoot(index, "w1", fine.path(), ScanPass::Coverage);
  QCOMPARE(st.read, 2);
  for (const char *rel : {"AAA/a.mseed", "BBB/b.mseed"}) {
    QDir().mkpath(QFileInfo(coarse.filePath(rel)).absolutePath());
    QVERIFY(QFile::copy(fine.filePath(rel), coarse.filePath(rel)));
    QFile f(coarse.filePath(rel));
    QVERIFY(f.open(QIODevice::ReadWrite));
    // What FAT would keep: the next even second.
    QVERIFY(f.setFileTime(QDateTime::fromMSecsSinceEpoch(1700000002000LL),
                          QFileDevice::FileModificationTime));
  }
  st = scanRoot(index, "w1", coarse.path(), ScanPass::Coverage);
  QCOMPARE(st.read, 0);
  QCOMPARE(st.cached, 0);
  QCOMPARE(index.fileCount(), 2);
}

// The index file: a save that meets a busy file is tried again, a reader
// never keeps it open, and many readers and a writer at once lose nothing.
void TstStudyWaveIndex::indexSavesWhileItIsRead() {
  QTemporaryDir studyDir;
  WaveIndex index;
  scanRoot(index, "w1", root_, ScanPass::Identity);
  QVERIFY(index.save(studyDir.path()));
  std::atomic<bool> stop{false};
  std::atomic<int> bad{0}, reads{0};
  std::vector<std::thread> readers;
  for (int i = 0; i < 4; ++i)
    readers.emplace_back([&] {
      while (!stop.load()) {
        const WaveIndex got = WaveIndex::load(studyDir.path());
        // Never a half-written file: all of it or (never) nothing.
        if (got.fileCount() != index.fileCount())
          ++bad;
        ++reads;
        // As the application reads it: now and then, not without a pause
        // (a file that is open the whole time cannot be replaced at all on
        // Windows, whatever the writer does).
        QThread::msleep(3);
      }
    });
  int failed = 0;
  for (int i = 0; i < 200; ++i) {
    QString why;
    if (!index.save(studyDir.path(), &why))
      ++failed;
  }
  stop.store(true);
  for (std::thread &t : readers)
    t.join();
  QCOMPARE(failed, 0);
  QCOMPARE(bad.load(), 0);
  QVERIFY(reads.load() > 0);
  // A folder that cannot take the file says so instead of pretending.
  QString why;
  QVERIFY(!index.save(studyDir.filePath("no/such/folder"), &why, 2));
  QVERIFY(!why.isEmpty());
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
  // The file of 2019 is followed by one that starts on 2020-12-20, less
  // than five weeks before the span: nothing says the 2019 file ends before
  // the span, so it is kept (it used to be dropped for starting too early).
  QCOMPARE(files, (QStringList{data.filePath("CCC/CCC_2019-01-01.zip"),
                               data.filePath("CCC/CCC_2020-12-20.zip"),
                               data.filePath("CCC/CCC_2021-01-05.zip"),
                               data.filePath("CCC/undated.mseed")}));
  QVERIFY2(how.contains("not indexed"), qPrintable(how));
  // With a file in between that starts long before the span, the 2019 one
  // is bounded by it and left out; the one in between is bounded by the
  // file of 2020-12-20 and kept.
  write(data.filePath("CCC/CCC_2020-06-01.zip"), "x");
  QCOMPARE(waveFilesFor(s, WaveIndex(), c, utc(2021, 1, 1), utc(2021, 2, 1)),
           (QStringList{data.filePath("CCC/CCC_2020-06-01.zip"),
                        data.filePath("CCC/CCC_2020-12-20.zip"),
                        data.filePath("CCC/CCC_2021-01-05.zip"),
                        data.filePath("CCC/undated.mseed")}));
  // A name that carries its period is taken by the period: a year-long file
  // that covers the span, and not a day file from long before.
  write(data.filePath("CCC/TU_CCC_01072020_000000_01072021_000000_HH.mseed"),
        "x");
  write(data.filePath("CCC/TU_CCC_01032020_000000_02032020_000000_HH.mseed"),
        "x");
  const QStringList withPeriods =
      waveFilesFor(s, WaveIndex(), c, utc(2021, 1, 1), utc(2021, 2, 1));
  QVERIFY(withPeriods.contains(
      data.filePath("CCC/TU_CCC_01072020_000000_01072021_000000_HH.mseed")));
  QVERIFY(!withPeriods.contains(
      data.filePath("CCC/TU_CCC_01032020_000000_02032020_000000_HH.mseed")));
  QFile::remove(data.filePath("CCC/CCC_2020-06-01.zip"));
  QFile::remove(
      data.filePath("CCC/TU_CCC_01072020_000000_01072021_000000_HH.mseed"));
  QFile::remove(
      data.filePath("CCC/TU_CCC_01032020_000000_02032020_000000_HH.mseed"));
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

// The same folder in another spelling is the same folder: a trailing slash,
// "..", a symbolic link, and other letter case where the volume ignores it.
void TstStudyWaveIndex::oneFolderInSeveralSpellings() {
  QTemporaryDir dir;
  const QString data = dir.filePath("Data");
  QVERIFY(QDir().mkpath(data + "/AAA"));
  QVERIFY(QDir().mkpath(dir.filePath("other")));
  QVERIFY(sameFolder(data, data + "/"));
  QVERIFY(sameFolder(data, dir.filePath("other/../Data")));
  QVERIFY(!sameFolder(data, dir.filePath("other")));
  QVERIFY(!sameFolder(data, data + "/AAA"));
  QVERIFY(folderInside(data + "/AAA", data));
  QVERIFY(folderInside(data, data));
  QVERIFY(!folderInside(data, data + "/AAA"));
  QVERIFY(!folderInside(dir.filePath("Data2"), data)); // not by prefix of text
  // Folders that do not exist are compared by their text.
  QVERIFY(sameFolder("/no/such/place", "/no/such/place/"));
  QVERIFY(folderInside("/no/such/place/sub", "/no/such/place"));
  QVERIFY(!folderInside("/no/such/placement", "/no/such/place"));

  // Letter case: the same folder exactly when the volume says so.
  const bool ignoresCase = QFileInfo::exists(dir.filePath("data"));
  QCOMPARE(sameFolder(data, dir.filePath("data")), ignoresCase);
  QCOMPARE(sameFolder(data, dir.filePath("DATA")), ignoresCase);
  QCOMPARE(folderInside(dir.filePath("DATA/AAA"), data), ignoresCase);

  Study s;
  s.dir = dir.filePath("study");
  QVERIFY(s.addWaveformRoot(data));
  QVERIFY(!s.addWaveformRoot(data + "/"));
  QVERIFY(!s.addWaveformRoot(dir.filePath("other/../Data")));
  QCOMPARE(s.addWaveformRoot(dir.filePath("DATA")), !ignoresCase);
  if (ignoresCase)
    QCOMPARE(s.waveRoots.size(), 1);
  // Adding it again in other spellings, or a folder inside it: covered.
  Study t;
  t.dir = dir.filePath("study");
  QCOMPARE(t.addWaveRoots({data}).added.size(), 1);
  Study::RootsAdded r =
      t.addWaveRoots({data + "/", dir.filePath("other/../Data/AAA")});
  QVERIFY(r.added.isEmpty());
  QCOMPARE(r.covered.size(), 2);
  QCOMPARE(t.waveRoots.size(), 1);
  if (ignoresCase) {
    r = t.addWaveRoots({dir.filePath("DATA"), dir.filePath("data/aaa")});
    QVERIFY(r.added.isEmpty());
    QCOMPARE(t.waveRoots.size(), 1);
  }
#ifndef Q_OS_WIN
  // Through a symbolic link.
  const QString link = dir.filePath("link");
  QVERIFY(QFile::link(data, link));
  QVERIFY(sameFolder(link, data));
  QVERIFY(folderInside(link + "/AAA", data));
  r = t.addWaveRoots({link, link + "/AAA"});
  QVERIFY(r.added.isEmpty());
  QCOMPARE(r.covered.size(), 2);
  QCOMPARE(t.waveRoots.size(), 1);
  // A parent given through the link takes the place of what it holds.
  Study u;
  u.dir = dir.filePath("study");
  QCOMPARE(u.addWaveRoots({data + "/AAA"}).added.size(), 1);
  r = u.addWaveRoots({link});
  QCOMPARE(r.added.size(), 1);
  QCOMPARE(r.replaced.size(), 1);
  QCOMPARE(u.waveRoots.size(), 1);
#endif
}

// The id of a folder that left the study is not given to a later one: the
// index, and a scan that is still running, hold its files under that id.
void TstStudyWaveIndex::rootIdsAreNeverReused() {
  QTemporaryDir dir;
  Study s;
  s.dir = dir.path();
  QCOMPARE(s.addWaveRoot("/data/a"), QStringLiteral("w1"));
  QCOMPARE(s.addWaveRoot("/data/b"), QStringLiteral("w2"));
  s.waveRoots.removeLast(); // w2 goes
  QCOMPARE(s.addWaveRoot("/data/c"), QStringLiteral("w3"));
  s.waveRoots.clear();
  QCOMPARE(s.addWaveRoots({"/data/d"}).added.size(), 1);
  QCOMPARE(s.waveRoots.first().id, QStringLiteral("w4"));
  // It is kept in the study file, also with no folder left.
  QVERIFY(s.toToml().contains("waveforms_last_id = 4"));
  s.waveRoots.clear();
  Study back = Study::fromToml(s.toToml(), dir.path());
  QVERIFY(back.waveRoots.isEmpty());
  QCOMPARE(back.addWaveRoot("/data/e"), QStringLiteral("w5"));
  // A file from before the key: the highest id in use counts.
  Study old = Study::fromToml("schema = 1\nname = \"o\"\n\n[[waveforms]]\n"
                              "id = \"w7\"\npath = \"/data/x\"\n",
                              dir.path());
  QCOMPARE(old.addWaveRoot("/data/y"), QStringLiteral("w8"));
  // A study with no folder ever writes no such line.
  QVERIFY(!Study().toToml().contains("waveforms_last_id"));

  // The stale entries of a removed folder never become a new folder's: the
  // survey's case was remove w2, add another, and find it named w2 with the
  // old files under it.
  QTemporaryDir data1, data2, data3;
  write(data1.filePath("AAA/a.mseed"), days("AAA", utc(2021, 1, 1), 1));
  write(data2.filePath("BBB/b.mseed"), days("BBB", utc(2021, 1, 1), 1));
  write(data3.filePath("CCC/c.mseed"), days("CCC", utc(2021, 1, 1), 1));
  Study t;
  t.dir = dir.path();
  const QString one = t.addWaveRoot(data1.path());
  const QString two = t.addWaveRoot(data2.path());
  WaveIndex index;
  scanRoot(index, one, data1.path(), ScanPass::Identity);
  scanRoot(index, two, data2.path(), ScanPass::Identity);
  t.waveRoots.removeLast();
  const QString three = t.addWaveRoot(data3.path());
  QVERIFY(three != two);
  StationRef b;
  b.code = "BBB";
  // What is left under the old id resolves to no folder, so no file.
  QVERIFY(waveFilesFor(t, index, b, utc(2021, 1, 1), utc(2021, 1, 2)).isEmpty());
}

// "Moved…": the old place is gone, so only its text can be compared, and
// without regard to letter case where file names have none.
void TstStudyWaveIndex::relocateIgnoresCaseWhereTheSystemDoes() {
  QTemporaryDir dir;
  QVERIFY(QDir().mkpath(dir.filePath("new/stations")));
  Study s;
  s.dir = dir.filePath("study");
  s.waveRoots << WaveRoot{"w1", dir.filePath("OldDisk/stations")};
  const int changed =
      s.relocate(dir.filePath("olddisk/stations"), dir.filePath("new/stations"));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
  QCOMPARE(changed, 1);
  QVERIFY(sameFolder(s.waveRootPath("w1"), dir.filePath("new/stations")));
#else
  QCOMPARE(changed, 0);
#endif
  // The spelling as stored always works.
  Study t;
  t.dir = dir.filePath("study");
  t.waveRoots << WaveRoot{"w1", dir.filePath("OldDisk/stations")};
  QCOMPARE(t.relocate(dir.filePath("OldDisk/stations"),
                      dir.filePath("new/stations")),
           1);
  QCOMPARE(t.waveRoots.first().id, QStringLiteral("w1"));
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
