// Studies that move: several waveform folders, paths stored so that a study
// and its data can move together, and finding inputs again after a move.
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include "study/store.h"
#include "study/study.h"

using namespace skg;
using namespace skg::study;

namespace {

bool touch(const QString &path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write("x") == 1;
}

} // namespace

class TstStudyPortable : public QObject {
  Q_OBJECT
private slots:
  void waveformRootsRoundTrip();
  void oldFilesWithOnlyDataRootStillRead();
  void aStationIsLookedForInEveryRoot();
  void pathsBesideTheStudyAreRelative();
  void aStudyAndItsDataMoveTogether();
  void locateRewritesEverythingUnderTheMovedFolder();
  void reopenLastSaysWhy();
};

void TstStudyPortable::waveformRootsRoundTrip() {
  QTemporaryDir dir;
  Study s;
  s.dir = dir.path();
  QVERIFY(s.waveformRoots().isEmpty());
  QVERIFY(s.addWaveformRoot(dir.filePath("raw")));
  // Stored relative, as the first [[waveforms]] folder.
  QCOMPARE(s.waveRoots.size(), 1);
  QCOMPARE(s.waveRoots[0].path, QStringLiteral("raw"));
  QVERIFY(s.addWaveformRoot(QStringLiteral("/elsewhere/fdsn_raw")));
  QVERIFY(!s.addWaveformRoot(dir.filePath("raw"))); // already listed
  QVERIFY(!s.addWaveformRoot(QStringLiteral("/elsewhere/fdsn_raw/")));
  const QStringList want = {dir.filePath("raw"),
                            QStringLiteral("/elsewhere/fdsn_raw")};
  QCOMPARE(s.waveformRoots(), want);
  const Study back = Study::fromToml(s.toToml(), dir.path());
  QCOMPARE(back.waveformRoots(), want);
  QCOMPARE(back.waveRoots.size(), 2);
  QCOMPARE(back.waveRoots[0].path, QStringLiteral("raw"));
  // The ids, which key the index of what the folders hold, are kept.
  QCOMPARE(back.waveRoots[0].id, s.waveRoots[0].id);
  QCOMPARE(back.waveRoots[1].id, s.waveRoots[1].id);
  QVERIFY(back.waveRoots[0].id != back.waveRoots[1].id);
  // The file names them as [[waveforms]], not in the older keys.
  QVERIFY(s.toToml().contains(QStringLiteral("[[waveforms]]")));
  QVERIFY(!s.toToml().contains(QStringLiteral("waveform_roots")));
  QVERIFY(!s.toToml().contains(QStringLiteral("data_root")));
}

void TstStudyPortable::oldFilesWithOnlyDataRootStillRead() {
  QTemporaryDir dir;
  const Study s = Study::fromToml(
      QStringLiteral("schema = 1\nname = \"x\"\ndata_root = \"mseed\"\n"),
      dir.path());
  QCOMPARE(s.waveformRoots(), QStringList{dir.filePath("mseed")});
  // waveform_roots alone (no data_root) also gives a first root.
  const Study t = Study::fromToml(
      QStringLiteral("schema = 1\nwaveform_roots = [\"a\", \"b\"]\n"),
      dir.path());
  QCOMPARE(t.waveRoots.size(), 2);
  QCOMPARE(t.waveRoots[0].path, QStringLiteral("a"));
  QCOMPARE(t.waveformRoots(),
           (QStringList{dir.filePath("a"), dir.filePath("b")}));
  // Both old keys together: data_root first, and it is not listed twice.
  const Study u = Study::fromToml(
      QStringLiteral("schema = 1\ndata_root = \"a\"\n"
                     "waveform_roots = [\"a\", \"b\"]\n"),
      dir.path());
  QCOMPARE(u.waveformRoots(),
           (QStringList{dir.filePath("a"), dir.filePath("b")}));
}

void TstStudyPortable::aStationIsLookedForInEveryRoot() {
  QTemporaryDir dir;
  QVERIFY(QDir().mkpath(dir.filePath("tdvms/AAA")));
  QVERIFY(QDir().mkpath(dir.filePath("fdsn/BBB")));
  Study s;
  s.dir = dir.path();
  s.addWaveformRoot(dir.filePath("tdvms"));
  s.addWaveformRoot(dir.filePath("fdsn"));
  StationRef a, b, c;
  a.code = QStringLiteral("AAA");
  b.code = QStringLiteral("BBB");
  c.code = QStringLiteral("CCC");
  QCOMPARE(s.dataDirFor(a), dir.filePath("tdvms/AAA"));
  QCOMPARE(s.dataDirFor(b), dir.filePath("fdsn/BBB"));
  // Found nowhere: the place it would be in the first root.
  QCOMPARE(s.dataDirFor(c), dir.filePath("tdvms/CCC"));
}

void TstStudyPortable::pathsBesideTheStudyAreRelative() {
  QTemporaryDir disk;
  Study s;
  s.dir = disk.filePath("studies/eafz");
  QCOMPARE(s.stored(disk.filePath("data/stations")),
           QStringLiteral("../../data/stations"));
  QCOMPARE(s.resolved(QStringLiteral("../../data/stations")),
           disk.filePath("data/stations"));
  // Nothing in common but the root: absolute.
  QCOMPARE(s.stored(QStringLiteral("/elsewhere/c.csv")),
           QStringLiteral("/elsewhere/c.csv"));
  // The built-in list is not a path.
  QCOMPARE(s.stored(QStringLiteral(":/tdvms/stations.csv")),
           QStringLiteral(":/tdvms/stations.csv"));
}

void TstStudyPortable::aStudyAndItsDataMoveTogether() {
  QTemporaryDir top;
  const QString disk1 = top.filePath("disk1"), disk2 = top.filePath("usb/x");
  QVERIFY(touch(disk1 + "/data/catalog.csv"));
  QVERIFY(QDir().mkpath(disk1 + "/data/stations/AAA"));
  QSettings settings(top.filePath("s.ini"), QSettings::IniFormat);
  {
    StudyStore store(&settings);
    QVERIFY(store.create(disk1 + "/studies/one", QStringLiteral("One")));
    QVERIFY(store.update([&](Study &s) {
      s.catalog = s.stored(disk1 + "/data/catalog.csv");
      s.addWaveformRoot(disk1 + "/data/stations");
    }));
    QVERIFY(store.study().missingInputs().isEmpty());
    QVERIFY(!QDir::isAbsolutePath(store.study().catalog));
  }
  // The whole disk goes elsewhere.
  QVERIFY(QDir().mkpath(top.filePath("usb")));
  QVERIFY(QDir().rename(disk1, disk2));
  StudyStore store(&settings);
  QVERIFY(store.open(disk2 + "/studies/one"));
  QVERIFY(store.study().missingInputs().isEmpty());
  QCOMPARE(store.study().catalogPath(), disk2 + "/data/catalog.csv");
  StationRef a;
  a.code = QStringLiteral("AAA");
  QCOMPARE(store.study().dataDirFor(a), disk2 + "/data/stations/AAA");
}

void TstStudyPortable::locateRewritesEverythingUnderTheMovedFolder() {
  // The study stays; its data, named by absolute paths, moves to a disk.
  QTemporaryDir studyTop, oldData, newData;
  QVERIFY(touch(oldData.filePath("lab/catalog.csv")));
  QVERIFY(QDir().mkpath(oldData.filePath("lab/stations/AAA")));
  QVERIFY(QDir().mkpath(oldData.filePath("lab/fdsn_raw/BBB")));
  Study s;
  s.dir = studyTop.filePath("s");
  s.name = QStringLiteral("S");
  s.catalog = oldData.filePath("lab/catalog.csv");
  QVERIFY(s.addWaveformRoot(oldData.filePath("lab/stations")));
  QVERIFY(s.addWaveformRoot(oldData.filePath("lab/fdsn_raw")));
  Case c = referenceCase(
      QStringLiteral("R"),
      QDateTime::fromString(QStringLiteral("2020-01-01T00:00:00Z"),
                            Qt::ISODate),
      38, 39);
  c.id = QStringLiteral("r");
  StationRef st;
  st.code = QStringLiteral("BBB");
  st.dataDir = oldData.filePath("lab/fdsn_raw/BBB");
  c.stations << st;
  s.cases << c;
  s.save();
  QVERIFY(s.missingInputs().isEmpty());

  QVERIFY(QDir().rename(oldData.filePath("lab"), newData.filePath("moved")));
  Study t = Study::load(s.dir);
  const QList<MissingInput> missing = t.missingInputs();
  QCOMPARE(missing.size(), 4);
  QCOMPARE(int(missing[0].kind), int(MissingInput::Kind::Catalog));
  QCOMPARE(missing[0].path, oldData.filePath("lab/catalog.csv"));
  // One answer (where the catalog is now) fixes the rest under that folder.
  QCOMPARE(t.relocate(missing[0].path, newData.filePath("moved/catalog.csv")),
           4);
  QVERIFY(t.missingInputs().isEmpty());
  QCOMPARE(t.catalogPath(), newData.filePath("moved/catalog.csv"));
  QCOMPARE(t.waveformRoots(), (QStringList{newData.filePath("moved/stations"),
                                           newData.filePath("moved/fdsn_raw")}));
  QCOMPARE(t.dataDirFor(t.cases[0].stations[0]),
           newData.filePath("moved/fdsn_raw/BBB"));
  // An unrelated answer changes nothing.
  Study u = Study::load(s.dir);
  QCOMPARE(u.relocate(QStringLiteral("/nowhere/x.csv"),
                      newData.filePath("moved/catalog.csv")),
           0);
}

void TstStudyPortable::reopenLastSaysWhy() {
  QTemporaryDir top;
  QSettings settings(top.filePath("s.ini"), QSettings::IniFormat);
  {
    StudyStore store(&settings);
    QString error;
    QVERIFY(store.reopenLast(&error)); // nothing was open: fine
    QVERIFY(error.isEmpty());
    QVERIFY(store.create(top.filePath("one"), QStringLiteral("One")));
  }
  {
    QFile f(top.filePath("one/study.toml"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("schema = = 1\n");
  }
  {
    StudyStore store(&settings);
    QString error;
    QVERIFY(!store.reopenLast(&error));
    QVERIFY(error.contains(QStringLiteral("line 1")));
    QVERIFY(!store.isOpen());
  }
  QVERIFY(QDir(top.filePath("one")).removeRecursively());
  StudyStore store(&settings);
  QString error;
  QVERIFY(!store.reopenLast(&error));
  QVERIFY(error.contains(top.filePath("one")));
}

QTEST_GUILESS_MAIN(TstStudyPortable)
#include "tst_study_portable.moc"
