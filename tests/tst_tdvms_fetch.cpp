// What arrived, and the four ways it can look like data and not be; and
// coverage read from miniSEED record headers. Mirrors test_fetch.py and
// test_coverage.py.
#include <QtTest>

#include <QRandomGenerator>

#include "miniz.h"
#include "tdvms/coverage.h"
#include "tdvms/fetch.h"

using namespace skg::tdvms;

namespace {
const QString MEMBER = "TU_ELBA_09092025_000000_30092025_000000_HH.mseed";
const QByteArray NOTICE =
    "Secilen istasyon icerisinde gecerli zaman araliginda veri bulunamamistir.";

void zipWith(const QString &path, const QMap<QString, QByteArray> &members) {
  QFile::remove(path);
  mz_zip_archive zip{};
  QVERIFY(mz_zip_writer_init_file(&zip, path.toLocal8Bit().constData(), 0));
  for (auto it = members.begin(); it != members.end(); ++it)
    QVERIFY(mz_zip_writer_add_mem(&zip, it.key().toUtf8().constData(),
                                  it.value().constData(), it.value().size(),
                                  MZ_DEFAULT_COMPRESSION));
  QVERIFY(mz_zip_writer_finalize_archive(&zip));
  mz_zip_writer_end(&zip);
}

Row chunk(const QString &station, const QString &start, const QString &state,
          const QString &end = "2025-09-30T00:00:00") {
  Row r = Ledger::makeRow(station, start, end);
  r.insert("state", state);
  return r;
}

const QDateTime T0(QDate(2025, 1, 1), QTime(0, 0), QTimeZone::utc());
const double T0S = T0.toSecsSinceEpoch();

// One 512-byte miniSEED 2 data record: fixed header plus blockette 1000.
QByteArray record(const QDateTime &start, int nsamp = 100, int rate = 100,
                  const QByteArray &cha = "HHZ", const QByteArray &sta = "ELBA",
                  const QByteArray &net = "TU", bool big = true) {
  QByteArray r;
  QDataStream s(&r, QIODevice::WriteOnly);
  s.setByteOrder(big ? QDataStream::BigEndian : QDataStream::LittleEndian);
  s.writeRawData("000001", 6);
  s.writeRawData("D", 1);
  s.writeRawData(" ", 1);
  s.writeRawData(sta.leftJustified(5, ' ').constData(), 5);
  s.writeRawData("  ", 2);
  s.writeRawData(cha.constData(), 3);
  s.writeRawData(net.constData(), 2);
  s << quint16(start.date().year()) << quint16(start.date().dayOfYear())
    << quint8(start.time().hour()) << quint8(start.time().minute())
    << quint8(start.time().second()) << quint8(0)
    << quint16(start.time().msec() * 10);
  s << quint16(nsamp) << qint16(rate) << qint16(1);
  s << quint8(0) << quint8(0) << quint8(0) << quint8(1); // flags, nblk=1
  s << qint32(0) << quint16(64) << quint16(48); // corr, data off, blk off
  s << quint16(1000) << quint16(0) << quint8(11) << quint8(big ? 1 : 0)
    << quint8(9) << quint8(0);
  return r.leftJustified(512, '\0');
}

QByteArray stream(const QList<int> &seconds, const QByteArray &cha = "HHZ",
                  bool big = true) {
  QByteArray out;
  for (int s : seconds)
    out += record(T0.addSecs(s), 100, 100, cha, "ELBA", "TU", big);
  return out;
}

QMap<QString, QList<QPair<double, double>>> relative(const ScanResult &r) {
  QMap<QString, QList<QPair<double, double>>> out;
  for (auto it = r.begin(); it != r.end(); ++it) {
    QList<QPair<double, double>> l;
    for (const Span &s : it->spans)
      l.append({s.first - T0S, s.second - T0S});
    out.insert(it.key(), l);
  }
  return out;
}
} // namespace

class TstTdvmsFetch : public QObject {
  Q_OBJECT
  QTemporaryDir tmp_;
  QString path(const QString &n) const { return tmp_.filePath(n); }

private slots:
  void initTestCase() { QVERIFY(tmp_.isValid()); }

  void windowComesFromTheArchive() {
    const MemberWindow w = windowFromMember(MEMBER);
    QVERIFY(w.valid);
    QCOMPARE(w.station, QString("ELBA"));
    QCOMPARE(w.start.toString("yyyy-MM-dd"), QString("2025-09-09"));
    QCOMPARE(w.end.toString("yyyy-MM-dd"), QString("2025-09-30"));
    for (const auto &[name, station] : QList<QPair<QString, QString>>{
             {"KO_SVRC_25092019_000000_16102019_000000_BH.mseed", "SVRC"},
             {"TB_MARM_23102024_000000_13112024_000000_HH.mseed", "MARM"},
             {"TU_ELZG_25092019_000000_16102019_000000_HH.mseed", "ELZG"},
             {"TU_ELBA_09092025_000000_30092025_000000.txt", "ELBA"}}) {
      const MemberWindow m = windowFromMember(name);
      QVERIFY(m.valid);
      QCOMPARE(m.station, station);
      QCOMPARE(m.start.daysTo(m.end), qint64(21));
    }
    QVERIFY(!windowFromMember("something_else.mseed").valid);
  }

  void emptyArchiveIsTransient() {
    zipWith(path("e.zip"), {});
    const Inspection i = inspectArchive(path("e.zip"));
    QCOMPARE(i.problem, Inspection::Problem::EmptyArchive);
    QVERIFY(i.members.isEmpty());
    QCOMPARE(i.size, qint64(22));
  }

  void corruptFileIsNotAnArchive() {
    QFile f(path("bad.zip"));
    f.open(QIODevice::WriteOnly);
    f.write("this is not a zip");
    f.close();
    QCOMPARE(inspectArchive(path("bad.zip")).problem,
             Inspection::Problem::Expired);
  }

  void noDataNoticeIsAValidArchiveWithoutWaveform() {
    zipWith(path("n.zip"),
            {{"TU_ELBA_09092025_000000_30092025_000000.txt", NOTICE}});
    const Inspection i = inspectArchive(path("n.zip"));
    QCOMPARE(i.problem, Inspection::Problem::None);
    QVERIFY(!i.hasWaveform());
    QCOMPARE(readMember(path("n.zip"), i.members.first(), 200), NOTICE);
  }

  void realArchiveFiledUnderItsOwnStation() {
    zipWith(path("ok.zip"), {{MEMBER, QByteArray(100, 'x')}});
    const Inspection i = inspectArchive(path("ok.zip"));
    QVERIFY(i.hasWaveform());
    const Row row = chunk("ELBA", "2025-09-09T00:00:00", "pending");
    const FiledArchive f =
        fileArchive(path("ok.zip"), row, path("raw"), i.members.size(), i.size);
    QCOMPARE(f.kind, FiledArchive::Kind::Fetched);
    QCOMPARE(f.path, path("raw/ELBA/ELBA_2025-09-09.zip"));
    QVERIFY(QFileInfo::exists(f.path));
    QCOMPARE(archivePath(path("raw"), row), f.path);
  }

  void existingArchiveNeverSilentlyOverwritten() {
    QDir().mkpath(path("raw2/ELBA"));
    QFile keeper(path("raw2/ELBA/ELBA_2025-09-09.zip"));
    keeper.open(QIODevice::WriteOnly);
    keeper.write("the good 825 MB");
    keeper.close();
    zipWith(path("ok2.zip"), {{MEMBER, QByteArray(100, 'x')}});
    const Row row = chunk("ELBA", "2025-09-09T00:00:00", "pending");
    const FiledArchive f =
        fileArchive(path("ok2.zip"), row, path("raw2"), 1, 100);
    QCOMPARE(f.kind, FiledArchive::Kind::Duplicate);
    keeper.open(QIODevice::ReadOnly);
    QCOMPARE(keeper.readAll(), QByteArray("the good 825 MB"));
    keeper.close();
    QCOMPARE(QDir(path("raw2/ELBA")).entryList({"*.dup*.zip"}).size(), 1);
    zipWith(path("ok3.zip"), {{MEMBER, QByteArray(100, 'x')}});
    QCOMPARE(fileArchive(path("ok3.zip"), row, path("raw2"), 1, 100, true).kind,
             FiledArchive::Kind::Fetched);
  }

  // --- coverage ---------------------------------------------------------
  void backToBackRecordsAreOneSpan() {
    const auto got = relative(scanBytes(stream({0, 1, 2, 4, 5})));
    QCOMPARE(got.size(), 1);
    QCOMPARE(got["TU.ELBA..HHZ"],
             (QList<QPair<double, double>>{{0, 3}, {4, 6}}));
  }

  void channelsKeptApart() {
    const auto got =
        relative(scanBytes(stream({0, 1}, "HHZ") + stream({0}, "HHN")));
    QCOMPARE(got["TU.ELBA..HHZ"], (QList<QPair<double, double>>{{0, 2}}));
    QCOMPARE(got["TU.ELBA..HHN"], (QList<QPair<double, double>>{{0, 1}}));
  }

  void recordSplitAcrossReadsAndLittleEndian() {
    MseedScanner s;
    const QByteArray all = stream({0, 1, 2});
    for (int off = 0; off < all.size(); off += 700)
      s.feed(all.mid(off, 700));
    QCOMPARE(relative(s.finish())["TU.ELBA..HHZ"],
             (QList<QPair<double, double>>{{0, 3}}));
    QCOMPARE(relative(scanBytes(stream({0, 1}, "HHZ", false)))["TU.ELBA..HHZ"],
             (QList<QPair<double, double>>{{0, 2}}));
  }

  // --- corrupt and cut-short input ---------------------------------------
  void recordLengthOutsideWhatMiniseedHasIsRefused() {
    // Byte 54 is the record-length exponent of blockette 1000. 1 << 31 is
    // negative and 1 << 0 is one byte: neither may steer the scan.
    for (int exponent : {0, 1, 7, 21, 30, 31, 32, 200, 255}) {
      QByteArray bad = stream({0, 1, 2});
      bad[54] = char(exponent);
      QVERIFY_THROWS_EXCEPTION(std::runtime_error, scanBytes(bad));
      // Also when it is not the first record that is corrupt.
      QByteArray later = stream({0, 1, 2});
      later[512 + 54] = char(exponent);
      QVERIFY_THROWS_EXCEPTION(std::runtime_error, scanBytes(later));
    }
    // The smallest and a large legal length are read.
    QByteArray small = record(T0).left(256) + record(T0.addSecs(1)).left(256);
    small[54] = char(8);
    small[256 + 54] = char(8);
    QCOMPARE(relative(scanBytes(small))["TU.ELBA..HHZ"],
             (QList<QPair<double, double>>{{0, 2}}));
    QByteArray large = record(T0).leftJustified(1 << 12, '\0');
    large[54] = char(12);
    QCOMPARE(relative(scanBytes(large + large))["TU.ELBA..HHZ"].size(), 1);
  }

  void aBrokenBlocketteChainIsRefused() {
    // The first blockette points back at itself, and is not a 1000.
    QByteArray loop = stream({0});
    loop[48] = char(0);
    loop[49] = char(100); // kind 100
    loop[50] = char(0);
    loop[51] = char(48); // next: itself
    loop[39] = char(200); // "200 blockettes follow"
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, scanBytes(loop));
    // A first blockette inside the fixed header.
    QByteArray inside = stream({0});
    inside[46] = char(0);
    inside[47] = char(8);
    QVERIFY_THROWS_EXCEPTION(std::runtime_error, scanBytes(inside));
  }

  void aFileCutAnywhereIsReadAsFarAsItGoes() {
    const QByteArray all = stream({0, 1, 2});
    for (int cut = 0; cut <= all.size(); ++cut) {
      const auto got = relative(scanBytes(all.left(cut)));
      const int whole = cut / 512;
      if (whole == 0)
        QVERIFY2(got.isEmpty(), qPrintable(QString::number(cut)));
      else
        QCOMPARE(got["TU.ELBA..HHZ"],
                 (QList<QPair<double, double>>{{0, double(whole)}}));
    }
  }

  void aRecordSplitAtEveryOffsetIsTheSameRecord() {
    const QByteArray all = stream({0, 1});
    const auto whole = relative(scanBytes(all));
    for (int split = 1; split < all.size(); ++split) {
      MseedScanner s;
      s.feed(all.left(split));
      s.feed(all.mid(split));
      QCOMPARE(relative(s.finish()), whole);
    }
    // In three pieces, the middle one a single byte.
    for (int split = 1; split + 1 < all.size(); split += 5) {
      MseedScanner s;
      s.feed(all.left(split));
      s.feed(all.mid(split, 1));
      s.feed(all.mid(split + 1));
      QCOMPARE(relative(s.finish()), whole);
    }
  }

  void aBlocketteBeyondTheBytesInHandIsWaitedFor() {
    // Blockette 1000 need not sit right after the header. Here it is at
    // byte 128; a read that ends before it must not call the record
    // length-less.
    QByteArray rec = record(T0);
    const QByteArray blockette = rec.mid(48, 8);
    for (int i = 48; i < 56; ++i)
      rec[i] = '\0';
    rec.replace(128, 8, blockette);
    rec[46] = char(0);
    rec[47] = char(128);
    QByteArray two = rec;
    QByteArray second = record(T0.addSecs(1));
    two += second;
    const auto whole = relative(scanBytes(two));
    QCOMPARE(whole["TU.ELBA..HHZ"],
             (QList<QPair<double, double>>{{0, 2}}));
    for (int split = 1; split < two.size(); ++split) {
      MseedScanner s;
      s.feed(two.left(split));
      s.feed(two.mid(split));
      QCOMPARE(relative(s.finish()), whole);
    }
  }

  void randomBytesNeverReadOutOfBounds() {
    // Nothing to compare: the scan must end, with a result or a refusal.
    // (Run under a sanitizer, an out-of-bounds read would show here.)
    QRandomGenerator rng(20261111);
    int refused = 0, read = 0;
    const QByteArray good = stream({0, 1, 2, 3});
    for (int round = 0; round < 600; ++round) {
      QByteArray data;
      if (round % 3 == 0) {
        data.resize(int(rng.bounded(1, 5000)));
        for (char &c : data)
          c = char(rng.bounded(256));
      } else {
        // A real stream with a few bytes changed, most of them in headers.
        data = good;
        for (int k = 0, n = int(rng.bounded(1, 6)); k < n; ++k) {
          const int rec = int(rng.bounded(4));
          const int at = rec * 512 + int(rng.bounded(round % 3 == 1 ? 64 : 512));
          data[at] = char(rng.bounded(256));
        }
        data.truncate(int(rng.bounded(1, int(good.size()) + 1)));
      }
      MseedScanner s;
      try {
        for (int off = 0; off < data.size();) {
          const int step = int(rng.bounded(1, 900));
          s.feed(data.mid(off, step));
          off += step;
        }
        s.finish();
        ++read;
      } catch (const std::runtime_error &) {
        ++refused;
      }
    }
    QCOMPARE(read + refused, 600);
    QVERIFY(refused > 0);
    QVERIFY(read > 0);
  }

  // --- a transfer that broke is not an archive ---------------------------
  void aBrokenTransferIsATransferErrorNotAnExpiredLink() {
    // The server said 200 and the connection dropped half way: the bytes
    // are a cut-short zip. That is a transfer to try again, with the link
    // and the request slot kept, not an expired link to ask for anew.
    class Half : public Http {
    public:
      HttpResult get(const QUrl &, int) override { return {}; }
      HttpResult postJson(const QUrl &, const QByteArray &, int,
                          const QMap<QString, QString> &) override {
        return {};
      }
      HttpResult postBody(const QUrl &, const QByteArray &, int,
                          const QMap<QString, QString> &) override {
        return {};
      }
      HttpResult getToDevice(const QUrl &, QIODevice *sink, int,
                             const std::function<bool()> &) override {
        sink->write(QByteArray(4096, 'P'));
        HttpResult r;
        r.status = 200;
        r.contentType = "application/zip";
        r.error = "Connection closed";
        return r;
      }
    } http;
    const QString out = path("half");
    const DownloadOutcome dl = download(http, "http://x.invalid/a.zip", out);
    QVERIFY(!dl.ok());
    QVERIFY(!dl.expired);
    QVERIFY(!dl.diskFull);
    QCOMPARE(dl.error, QString("Connection closed"));
    QVERIFY(QDir(out).entryList(QDir::Files).isEmpty());
    // The status alone no longer makes an answer complete.
    HttpResult r;
    r.status = 200;
    QVERIFY(r.ok());
    r.error = "Connection closed";
    QVERIFY(!r.ok());
    QVERIFY(!r.connectionError()); // there was an answer; it broke off
  }

  void notMiniseedIsRefused() {
    QVERIFY_THROWS_EXCEPTION(
        std::runtime_error,
        scanBytes(
            QByteArray("<html>expired link</html>").leftJustified(512, ' ')));
  }

  void sampleRateRules() {
    QCOMPARE(sampleRate(100, 1), 100.0);
    QCOMPARE(sampleRate(-10, 1), 0.1);
    QCOMPARE(sampleRate(1, -10), 0.1);
  }

  void intervalArithmetic() {
    QCOMPARE(subtractSpans({{0, 10}}, {{2, 3}, {5, 6}}),
             (Spans{{0, 2}, {3, 5}, {6, 10}}));
    QCOMPARE(subtractSpans({{0, 10}}, {}), (Spans{{0, 10}}));
    QCOMPARE(mergeSpans({{5, 6}, {0, 2}, {1, 3}}), (Spans{{0, 3}, {5, 6}}));
    QCOMPARE(spanLength({{0, 3}, {5, 6}}), 4.0);
  }

  void everyGapSaysWhy() {
    const double DAY = 86400;
    auto iso = [](int d) {
      return T0.addDays(d).toString("yyyy-MM-ddTHH:mm:ss");
    };
    QFile f(path("elba_ledger.jsonl"));
    f.open(QIODevice::WriteOnly);
    for (const Row &r : {chunk("ELBA", iso(0), "fetched", iso(1)),
                         chunk("ELBA", iso(1), "nodata", iso(2)),
                         chunk("ELBA", iso(2), "pending", iso(3))})
      f.write(QJsonDocument(r).toJson(QJsonDocument::Compact) + "\n");
    f.close();
    QDir().mkpath(path("elba_raw/ELBA"));
    QList<int> half;
    for (int s = 0; s < int(DAY) / 2; ++s)
      half << s;
    zipWith(
        path("elba_raw/ELBA/ELBA_2025-01-01.zip"),
        {{"TU_ELBA_01012025_000000_02012025_000000_HH.mseed", stream(half)}});
    Ledger led(path("elba_ledger.jsonl"));
    int progressCalls = 0;
    const auto report =
        coverageReport(led, path("elba_raw"), {}, false,
                       [&](int, int, const QString &) { ++progressCalls; });
    QVERIFY(progressCalls >= 2);
    QCOMPARE(report.size(), 1);
    const StationCoverage &st = report.first();
    QCOMPARE(st.channels.size(), 1);
    const ChannelCoverage &ch = st.channels.first();
    QCOMPARE(ch.covered, DAY / 2);
    QCOMPARE(ch.requested, 3 * DAY);
    QCOMPARE(ch.gaps.size(), 3);
    QCOMPARE(ch.gaps[0].start - T0S, DAY / 2);
    QCOMPARE(ch.gaps[0].why, QString("data gap"));
    QCOMPARE(ch.gaps[1].why, QString("no data at source"));
    QCOMPARE(ch.gaps[2].why, QString("not downloaded yet"));
    QCOMPARE(ch.gaps[2].end - T0S, 3 * DAY);
    QCOMPARE(ch.label(), QString("HHZ"));

    // Cached: a second report reads nothing (progress reports no archive).
    QVERIFY(QFileInfo::exists(path("elba_raw/.coverage.json")));
    int scannedAgain = 0;
    const auto again =
        coverageReport(led, path("elba_raw"), {}, false,
                       [&](int done, int total, const QString &name) {
                         if (done < total && !name.isEmpty())
                           ++scannedAgain;
                       });
    QCOMPARE(scannedAgain, 0);
    QCOMPARE(again.first().channels.first().covered, DAY / 2);

    QStringList kinds;
    for (const TimelineCell &c : timeline(st, &ch, 6))
      kinds << c.kind;
    QCOMPARE(kinds, (QStringList{"full", "gap", "nodata", "nodata", "pending",
                                 "pending"}));
    QCOMPARE(formatSeconds(90), QString("90 s"));
    QCOMPARE(formatSeconds(3600), QString("60 min"));
    QCOMPARE(formatTime(T0S), QString("2025-01-01 00:00:00"));
  }
};

QTEST_GUILESS_MAIN(TstTdvmsFetch)
#include "tst_tdvms_fetch.moc"
#include <stdexcept>
