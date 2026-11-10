// Paths with letters outside ASCII, as a Turkish user's folders have them
// ("C:\Users\Oğuz\Veri\İstasyonlar"). Qt's own file classes cope everywhere;
// the C libraries the application hands paths to (miniz for zip archives,
// libsvm for its model files) need the path in the form core/nativepath.h
// gives them. Everything here runs inside a folder named with such letters,
// on every platform the tests run on: on Windows this is the proof that a
// study, an archive, a feature table, a model and a catalog under such a
// path can be written and read.
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <cstdio>
#include <cstdlib>

#include "analysis/events.h"
#include "core/nativepath.h"
#include "fx/mseed.h"
#include "fx/npy.h"
#include "fx/preprocess.h"
#include "miniz.h"
#include "sfc/models.h"
#include "sfc/tuning.h"
#include "study/scancache.h"
#include "study/study.h"
#include "study/waveindex.h"
#include "tdvms/coverage.h"
#include "tdvms/fetch.h"

using namespace skg;
using namespace skg::study;

namespace {

QDateTime utc(int y, int m, int d, int h = 0) {
  return QDateTime(QDate(y, m, d), QTime(h, 0), QTimeZone::UTC);
}

// One 512-byte miniSEED 2 data record: fixed header plus blockette 1000 with
// the record length given as a power of two.
QByteArray record(const QDateTime &start, const QByteArray &sta,
                  const QByteArray &cha = "HHZ", int exponent = 9) {
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
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1); // flags, nblk=1
  s << qint32(0) << quint16(64) << quint16(48); // corr, data off, blk off
  s << quint16(1000) << quint16(0) << quint8(11) << quint8(1)
    << quint8(exponent) << quint8(0);
  return r.leftJustified(512, '\0');
}

QByteArray day(const QByteArray &sta, const QDateTime &first) {
  QByteArray out;
  for (const char *cha : {"HHE", "HHN", "HHZ"})
    for (int half = 0; half < 2; ++half)
      out += record(first.addSecs(half * 43200), sta, cha);
  return out;
}

bool writeFile(const QString &path, const QByteArray &bytes) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

bool zipWith(const QString &path, const QMap<QString, QByteArray> &members) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile::remove(path);
  mz_zip_archive zip{};
  if (!mz_zip_writer_init_file(&zip, minizPath(path).constData(), 0))
    return false;
  bool ok = true;
  for (auto it = members.begin(); it != members.end(); ++it)
    ok = ok && mz_zip_writer_add_mem(&zip, it.key().toUtf8().constData(),
                                     it.value().constData(),
                                     size_t(it.value().size()),
                                     MZ_DEFAULT_COMPRESSION);
  ok = ok && mz_zip_writer_finalize_archive(&zip);
  mz_zip_writer_end(&zip);
  return ok;
}

} // namespace

class TstPaths : public QObject {
  Q_OBJECT
private slots:
  void initTestCase();
  void whatMinizIsGiven();
  void aLongWindowsPathTakesItsExtendedForm();
  void zipArchivesAreScanned();
  void zipArchivesAreUnpackedForFeatures();
  void tdvmsReadsItsArchives();
  void studyIndexAndCache();
  void featureTableAndCatalog();
  void aCLibraryCanOpenTheFile();
  void svmModelThroughATemporaryFile();
  void recordLengthMustBeSane();

private:
  QTemporaryDir tmp_;
  QString dir_; // <tmp>/İstasyon ğüşöç ı
};

void TstPaths::initTestCase() {
  QVERIFY(tmp_.isValid());
  // Dotted and dotless i, soft g, s and c with cedilla, o and u with
  // diaeresis, and a space: none of ğ, İ, ı, ş is in the Western European
  // code page.
  dir_ = tmp_.filePath(QString::fromUtf8("İstasyon ğüşöç ı"));
  QVERIFY(QDir().mkpath(dir_));
  QVERIFY(QFileInfo(dir_).isDir());
}

void TstPaths::whatMinizIsGiven() {
  const QString path = QString::fromUtf8("C:/Veri/İstasyonlar/ğ.zip");
  // Windows: UTF-8 (miniz decodes it and calls the wide-character API),
  // with backslashes.
  QCOMPARE(minizPathFor(path, true),
           QString::fromUtf8("C:\\Veri\\İstasyonlar\\ğ.zip").toUtf8());
  // Elsewhere: the file system's encoding, which Qt knows.
  QCOMPARE(minizPathFor(path, false), QFile::encodeName(path));
  // The helper for this platform opens what Qt wrote.
  const QString real = dir_ + QStringLiteral("/plain.bin");
  QVERIFY(writeFile(real, "abc"));
  FILE *f = nullptr;
#ifdef Q_OS_WIN
  f = _wfopen(reinterpret_cast<const wchar_t *>(
                  QString::fromUtf8(minizPath(real)).utf16()),
              L"rb");
#else
  f = std::fopen(minizPath(real).constData(), "rb");
#endif
  QVERIFY(f != nullptr);
  std::fclose(f);
}

void TstPaths::aLongWindowsPathTakesItsExtendedForm() {
  QCOMPARE(windowsExtendedPath(QStringLiteral("C:\\a\\b")),
           QStringLiteral("\\\\?\\C:\\a\\b"));
  QCOMPARE(windowsExtendedPath(QStringLiteral("\\\\server\\share\\a")),
           QStringLiteral("\\\\?\\UNC\\server\\share\\a"));
  QCOMPARE(windowsExtendedPath(QStringLiteral("\\\\?\\C:\\a")),
           QStringLiteral("\\\\?\\C:\\a"));
  QCOMPARE(windowsExtendedPath(QStringLiteral("relative\\a")),
           QStringLiteral("relative\\a"));
  // Short paths are left as they are; from the limit on, the prefix.
  QString longPath = QStringLiteral("C:/data");
  while (longPath.size() < 300)
    longPath += QStringLiteral("/a-folder-with-a-long-name");
  longPath += QStringLiteral("/x.zip");
  const QByteArray given = minizPathFor(longPath, true);
  QVERIFY(given.startsWith("\\\\?\\C:\\data\\"));
  QVERIFY(!given.contains('/'));
  QVERIFY(!minizPathFor(QStringLiteral("C:/data/x.zip"), true).startsWith("\\\\"));
}

// The waveform scan reads a zip under the folder, quick pass and exact pass.
void TstPaths::zipArchivesAreScanned() {
  const QString root = dir_ + QStringLiteral("/veri");
  QVERIFY(zipWith(root + QString::fromUtf8("/ĞĞĞ/arşiv_2021-01-10.zip"),
                  {{"TU_AAA_10012021_000000_11012021_000000_HH.mseed",
                    day("AAA", utc(2021, 1, 10))}}));
  QVERIFY(writeFile(root + QString::fromUtf8("/düz/b.mseed"),
                    day("BBB", utc(2021, 1, 10))));
  WaveIndex index;
  ScanStats st = scanRoot(index, "w1", root, ScanPass::Identity);
  QCOMPARE(st.listed, 2);
  QVERIFY2(index.skipped().isEmpty(),
           qPrintable(index.skipped().isEmpty()
                          ? QString()
                          : index.skipped().first().rel + ": " +
                                index.skipped().first().why));
  QVERIFY(index.hasStation("AAA"));
  QVERIFY(index.hasStation("BBB"));
  st = scanRoot(index, "w1", root, ScanPass::Coverage);
  QCOMPARE(st.read, 2);
  QCOMPARE(index.daysCovered("AAA", utc(2021, 1, 1), utc(2021, 2, 1)), 1);
  QCOMPARE(index.daysCovered("BBB", utc(2021, 1, 1), utc(2021, 2, 1)), 1);
}

// Features unpacks the archives into its staging folder, which is inside the
// study and so under the same kind of path, and indexes the members.
void TstPaths::zipArchivesAreUnpackedForFeatures() {
  const QString zip = dir_ + QString::fromUtf8("/özellik/giriş.zip");
  QVERIFY(zipWith(zip, {{"TU_AAA_x_HH.mseed", day("AAA", utc(2021, 3, 1))}}));
  const QString staging = dir_ + QString::fromUtf8("/çalışma");
  QVERIFY(QDir().mkpath(staging));
  QStringList files;
  {
    const fx::Inputs in = fx::expandInputs({zip}, staging);
    QCOMPARE(in.files.size(), 1);
    QVERIFY2(QFileInfo::exists(in.files.first()),
             qPrintable(in.files.first()));
    QVERIFY(in.files.first().startsWith(staging));
    // The unpacked member reads as miniSEED: six records.
    QCOMPARE(fx::indexFile(in.files.first(), 0).size(), 6);
    files = in.files;
  }
  // The staging folder goes with the Inputs.
  QVERIFY(!QFileInfo::exists(files.first()));
}

void TstPaths::tdvmsReadsItsArchives() {
  const QString zip = dir_ + QString::fromUtf8("/indirme/AAA_2021-04-01.zip");
  QVERIFY(zipWith(zip, {{"TU_AAA_01042021_000000_02042021_000000_HH.mseed",
                         day("AAA", utc(2021, 4, 1))}}));
  const tdvms::Inspection seen = tdvms::inspectArchive(zip);
  QCOMPARE(int(seen.problem), int(tdvms::Inspection::Problem::None));
  QCOMPARE(seen.members.size(), 1);
  QVERIFY(seen.hasWaveform());
  QVERIFY(!tdvms::readMember(zip, seen.members.first(), 512).isEmpty());
  const tdvms::ScanResult got = tdvms::scanArchive(zip);
  QCOMPARE(got.size(), 3); // three channels
}

// A study folder, its index and the scan cache, all under the folder.
void TstPaths::studyIndexAndCache() {
  const QString studyDir = dir_ + QString::fromUtf8("/çalışma dosyası");
  const QString data = dir_ + QString::fromUtf8("/arşiv");
  QVERIFY(zipWith(data + QStringLiteral("/CCC/c.zip"),
                  {{"TU_CCC_x_HH.mseed", day("CCC", utc(2021, 5, 1))}}));
  Study s;
  s.dir = studyDir;
  s.name = QString::fromUtf8("Doğanyol ve Şırnak");
  const QString id = s.addWaveRoot(data);
  s.save();
  const Study back = Study::load(studyDir);
  QCOMPARE(back.name, s.name);
  QCOMPARE(back.waveRoots.size(), 1);
  QVERIFY(sameFolder(back.waveRootPath(id), data));

  ScanCache cache(dir_ + QString::fromUtf8("/önbellek"));
  ScanOptions options;
  options.cache = &cache;
  WaveIndex index;
  ScanStats st =
      scanRoot(index, id, back.waveRootPath(id), ScanPass::Coverage, {}, options);
  QCOMPARE(st.read, 1);
  QCOMPARE(st.unfiled, 0);
  QVERIFY(index.save(studyDir));
  QCOMPARE(WaveIndex::load(studyDir).fileCount(), 1);
  QCOMPARE(cache.usage().files, 1);
  // A second study gets it from the cache.
  WaveIndex second;
  st = scanRoot(second, "w1", data, ScanPass::Coverage, {}, options);
  QCOMPARE(st.cached, 1);
  QCOMPARE(st.read, 0);
}

void TstPaths::featureTableAndCatalog() {
  // A feature table (.npy) written and read back.
  fx::NpyTable t;
  t.fields = {{"time", "<f8"}, {"label", "<U8"}};
  t.rows = 2;
  t.numeric.append(std::vector<double>{1.5, 2.5});
  t.numeric.append(std::vector<double>());
  t.strings.append(QStringList());
  t.strings.append(QStringList{QStringLiteral("a"), QString::fromUtf8("ğ")});
  const QString npy = dir_ + QString::fromUtf8("/öznitelikler.npy");
  QVERIFY(fx::writeNpy(npy, t));
  const fx::NpyTable back = fx::readNpy(npy);
  QCOMPARE(back.rows, 2);
  QCOMPARE(back.numeric[0], t.numeric[0]);
  QCOMPARE(back.strings[1], t.strings[1]);

  // A catalog under the folder, with Turkish place names in it.
  const QString csv = dir_ + QString::fromUtf8("/katalog/deprem kataloğu.csv");
  QVERIFY(writeFile(
      csv,
      QString::fromUtf8(
          "Date,Latitude,Longitude,Depth,Type,Magnitude,Location,EventID\n"
          "24/01/2020 17:55:14,38.3593,39.0630,8.06,Mw,6.8,\"Sivrice "
          "(Elazığ)\",1\n"
          "06/02/2023 01:17:34,37.288,37.043,8.6,Mw,7.7,\"Pazarcık "
          "(Kahramanmaraş)\",2\n")
          .toUtf8()));
  const auto events = analysis::loadEvents(csv);
  QCOMPARE(int(events.size()), 2);
  QCOMPARE(events[0].mag, 6.8);
  QCOMPARE(events[0].place, QString::fromUtf8("Sivrice (Elazığ)"));
  QCOMPARE(events[1].place, QString::fromUtf8("Pazarcık (Kahramanmaraş)"));
}

// A library that calls fopen() (libsvm) is given a path it can open.
void TstPaths::aCLibraryCanOpenTheFile() {
  const QString path = dir_ + QString::fromUtf8("/model dosyası.txt");
  QVERIFY(writeFile(path, "svm"));
  bool ok = false;
  const QByteArray given = cLibraryPath(path, &ok);
  if (!ok)
    QSKIP("this volume keeps no short (8.3) names and the code page lacks "
          "the letters: fopen() cannot name the file here at all");
  FILE *f = std::fopen(given.constData(), "rb");
  QVERIFY2(f != nullptr, given.constData());
  char buf[4] = {};
  QCOMPARE(std::fread(buf, 1, 3, f), size_t(3));
  std::fclose(f);
  QCOMPARE(QByteArray(buf), QByteArray("svm"));
}

// libsvm writes and reads its models through a temporary file; with the
// temporary folder under the Turkish path (as it is for a Windows user
// named Oğuz) the model still makes the round trip.
void TstPaths::svmModelThroughATemporaryFile() {
  const QString temp = dir_ + QString::fromUtf8("/geçici");
  QVERIFY(QDir().mkpath(temp));
  {
    bool ok = false;
    QVERIFY(writeFile(temp + QStringLiteral("/probe"), "x"));
    cLibraryPath(temp + QStringLiteral("/probe"), &ok);
    if (!ok)
      QSKIP("this volume keeps no short (8.3) names and the code page lacks "
            "the letters: fopen() cannot name a file here at all");
  }
#ifdef Q_OS_WIN
  const QByteArray oldTmp = qgetenv("TMP"), oldTemp = qgetenv("TEMP");
  const QString native = QDir::toNativeSeparators(temp);
  _wputenv_s(L"TMP", reinterpret_cast<const wchar_t *>(native.utf16()));
  _wputenv_s(L"TEMP", reinterpret_cast<const wchar_t *>(native.utf16()));
#else
  const QByteArray oldTmp = qgetenv("TMPDIR");
  qputenv("TMPDIR", QFile::encodeName(temp));
#endif
  const auto restore = qScopeGuard([&] {
#ifdef Q_OS_WIN
    qputenv("TMP", oldTmp);
    qputenv("TEMP", oldTemp);
#else
    if (oldTmp.isNull())
      qunsetenv("TMPDIR");
    else
      qputenv("TMPDIR", oldTmp);
#endif
  });
  QVERIFY2(sameFolder(QDir::tempPath(), temp), qPrintable(QDir::tempPath()));

  sfc::Matrix m;
  m.cols = 2;
  for (int i = 0; i < 40; ++i) {
    const int y = i % 2;
    m.X.push_back(y ? 2.0 + 0.01 * i : -2.0 - 0.01 * i);
    m.X.push_back(y ? 1.0 : -1.0);
    m.y.push_back(y);
    m.index.push_back(i);
  }
  m.rows = 40;
  auto model =
      sfc::makeModel(QStringLiteral("svm_linear"), {{"C", 1}}, sfc::Settings());
  model->nClasses = 2;
  model->fit(m, {});
  const std::vector<int> before = model->predict(m);
  QCOMPARE(before, m.y);
  const QByteArray bytes = model->serialize();
  QVERIFY2(!bytes.isEmpty(), "libsvm could not write its model file");
  auto again =
      sfc::makeModel(QStringLiteral("svm_linear"), {{"C", 1}}, sfc::Settings());
  again->nClasses = 2;
  again->deserialize(bytes);
  QCOMPARE(again->predict(m), before);
}

// A record whose length field is damaged is refused, not followed off the
// end of the buffer: 2^8 .. 2^20 bytes are lengths, nothing else is.
void TstPaths::recordLengthMustBeSane() {
  QVERIFY(fx::parseHeader(record(utc(2021, 1, 1), "AAA", "HHZ", 9)).valid);
  QCOMPARE(fx::parseHeader(record(utc(2021, 1, 1), "AAA", "HHZ", 12))
               .recordLength,
           4096);
  for (int exponent : {0, 7, 21, 31, 32, 63, 255})
    QVERIFY2(!fx::parseHeader(record(utc(2021, 1, 1), "AAA", "HHZ", exponent))
                  .valid,
             qPrintable(QString::number(exponent)));
  // A file that starts with such a record is named as unreadable.
  const QString bad = dir_ + QStringLiteral("/bad.mseed");
  QVERIFY(writeFile(bad, record(utc(2021, 1, 1), "AAA", "HHZ", 31)));
  bool threw = false;
  try {
    fx::indexFile(bad, 0);
  } catch (const std::runtime_error &e) {
    threw = QByteArray(e.what()).contains("not miniSEED");
  }
  QVERIFY(threw);
}

QTEST_GUILESS_MAIN(TstPaths)
#include "tst_paths.moc"
