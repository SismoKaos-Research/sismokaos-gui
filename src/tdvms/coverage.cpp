#include "tdvms/coverage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimeZone>
#include <QtEndian>

#include <algorithm>
#include <stdexcept>

#include "miniz.h"
#include "tdvms/fetch.h"
#include <cmath>
#include <utility>

namespace skg::tdvms {

// --- reading records ------------------------------------------------------

double sampleRate(int factor, int mult) {
  if (factor > 0) {
    if (mult > 0)
      return double(factor) * mult;
    if (mult < 0)
      return double(factor) / -mult;
    return factor;
  }
  if (factor < 0) {
    if (mult > 0)
      return double(-mult) / factor; // negative / negative
    if (mult < 0)
      return 1.0 / (double(factor) * mult);
    return -1.0 / factor;
  }
  return 0.0;
}

namespace {

double epochOf(int year, int doy, int hour, int minute, int sec, int frac) {
  const QDate jan1(year, 1, 1);
  const qint64 base =
      QDateTime(jan1, QTime(0, 0), QTimeZone::utc()).toSecsSinceEpoch();
  return double(base) + (doy - 1) * 86400.0 + hour * 3600.0 + minute * 60.0 +
         sec + frac * 1e-4;
}

template <typename T> T readAt(const QByteArray &d, int off, bool big) {
  const uchar *p = reinterpret_cast<const uchar *>(d.constData()) + off;
  return big ? qFromBigEndian<T>(p) : qFromLittleEndian<T>(p);
}

// Blockette 1000 gives the record length; every TDVMS record carries one.
std::optional<int> recordLength(const QByteArray &d, int off,
                                int firstBlockette, int count, bool big) {
  int b = firstBlockette;
  for (int i = 0; i < count; ++i) {
    if (b == 0 || off + b + 7 > d.size())
      break;
    const quint16 kind = readAt<quint16>(d, off + b, big);
    const quint16 next = readAt<quint16>(d, off + b + 2, big);
    if (kind == 1000)
      return 1 << static_cast<uchar>(d[off + b + 6]);
    b = next;
  }
  return std::nullopt;
}

void addSpan(ScanResult &out, const QString &cid, double rate, double start,
             double end) {
  ChannelSpans &entry = out[cid];
  entry.rate = rate;
  if (!entry.spans.isEmpty()) {
    Span &last = entry.spans.last();
    if (last.first <= start && start <= last.second + 1.5 / rate) {
      if (end > last.second)
        last.second = end;
      return;
    }
  }
  entry.spans.append({start, end});
}

} // namespace

void MseedScanner::feed(const QByteArray &bytes) {
  QByteArray data = pending_.isEmpty() ? bytes : pending_ + bytes;
  int off = 0;
  const int n = data.size();
  while (n - off >= 48) {
    // Fixed header: 6 seq, 1 quality, 1 reserved, 5 sta, 2 loc, 3 cha, 2 net,
    // then BTIME (year u16, doy u16, h, m, s, pad, frac u16), nsamp u16,
    // factor i16, mult i16, 4 flag bytes, time corr i32, data off u16,
    // blockette off u16.
    bool big = true;
    quint16 year = readAt<quint16>(data, off + 20, true);
    if (!(year >= 1900 && year <= 2100)) {
      big = false;
      year = readAt<quint16>(data, off + 20, false);
    }
    const char quality = data[off + 6];
    if (!(quality == 'D' || quality == 'R' || quality == 'Q' ||
          quality == 'M') ||
        !(year >= 1900 && year <= 2100))
      throw std::runtime_error(
          QStringLiteral("not a miniSEED 2 record at byte %1")
              .arg(off)
              .toStdString());
    const quint16 doy = readAt<quint16>(data, off + 22, big);
    const int hour = static_cast<uchar>(data[off + 24]);
    const int minute = static_cast<uchar>(data[off + 25]);
    const int sec = static_cast<uchar>(data[off + 26]);
    const quint16 frac = readAt<quint16>(data, off + 28, big);
    const quint16 nsamp = readAt<quint16>(data, off + 30, big);
    const qint16 factor = readAt<qint16>(data, off + 32, big);
    const qint16 mult = readAt<qint16>(data, off + 34, big);
    const uchar activity = static_cast<uchar>(data[off + 36]);
    const uchar nblk = static_cast<uchar>(data[off + 39]);
    const qint32 corr = readAt<qint32>(data, off + 40, big);
    const quint16 blk = readAt<quint16>(data, off + 46, big);
    const auto reclen = recordLength(data, off, blk, nblk, big);
    if (!reclen) {
      if (n - off < 64)
        break; // the blockette may be in the next read
      throw std::runtime_error(
          "a record without blockette 1000; length unknown");
    }
    if (n - off < *reclen)
      break;
    const double rate = sampleRate(factor, mult);
    if (nsamp && rate > 0) {
      double start = epochOf(year, doy, hour, minute, sec, frac);
      if (!(activity & 0x02)) // time correction not yet applied
        start += corr * 1e-4;
      const double end = start + nsamp / rate;
      const QString cid =
          QStringLiteral("%1.%2.%3.%4")
              .arg(QString::fromLatin1(data.mid(off + 18, 2)).trimmed(),
                   QString::fromLatin1(data.mid(off + 8, 5)).trimmed(),
                   QString::fromLatin1(data.mid(off + 13, 2)).trimmed(),
                   QString::fromLatin1(data.mid(off + 15, 3)).trimmed());
      addSpan(spans_, cid, rate, start, end);
    }
    off += *reclen;
  }
  pending_ = data.mid(off);
}

ScanResult MseedScanner::finish() {
  ScanResult out;
  for (auto it = spans_.begin(); it != spans_.end(); ++it)
    out.insert(it.key(), {it->rate, mergeSpans(it->spans, 1.5 / it->rate)});
  pending_.clear();
  spans_.clear();
  return out;
}

ScanResult scanBytes(const QByteArray &bytes) {
  MseedScanner s;
  s.feed(bytes);
  return s.finish();
}

ScanResult scanArchive(const QString &zipPath) {
  ScanResult out;
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, zipPath.toLocal8Bit().constData(), 0))
    throw std::runtime_error(("not a zip: " + zipPath).toStdString());
  const mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; ++i) {
    char name[1024];
    mz_zip_reader_get_filename(&zip, i, name, sizeof name);
    if (!QString::fromUtf8(name).endsWith(QLatin1String(".mseed"),
                                          Qt::CaseInsensitive))
      continue;
    MseedScanner scanner;
    std::exception_ptr failure;
    auto cb = [](void *opaque, mz_uint64, const void *buf,
                 size_t len) -> size_t {
      auto *pair =
          static_cast<std::pair<MseedScanner *, std::exception_ptr *> *>(
              opaque);
      try {
        pair->first->feed(QByteArray(static_cast<const char *>(buf), int(len)));
      } catch (...) {
        *pair->second = std::current_exception();
        return 0; // stops the extraction
      }
      return len;
    };
    std::pair<MseedScanner *, std::exception_ptr *> opaque{&scanner, &failure};
    mz_zip_reader_extract_to_callback(&zip, i, cb, &opaque, 0);
    if (failure) {
      mz_zip_reader_end(&zip);
      std::rethrow_exception(failure);
    }
    const ScanResult got = scanner.finish();
    for (auto it = got.begin(); it != got.end(); ++it) {
      ChannelSpans &acc = out[it.key()];
      acc.rate = it->rate;
      acc.spans = mergeSpans(acc.spans + it->spans, 1.5 / it->rate);
    }
  }
  mz_zip_reader_end(&zip);
  return out;
}

// --- interval arithmetic ------------------------------------------------

Spans mergeSpans(Spans spans, double tol) {
  std::sort(spans.begin(), spans.end());
  Spans out;
  for (const Span &s : spans) {
    if (!out.isEmpty() && s.first <= out.last().second + tol)
      out.last().second = std::max(out.last().second, s.second);
    else
      out.append(s);
  }
  return out;
}

Spans clipSpans(const Spans &spans, double lo, double hi) {
  Spans out;
  for (const Span &s : spans)
    if (s.second > lo && s.first < hi)
      out.append({std::max(s.first, lo), std::min(s.second, hi)});
  return out;
}

Spans subtractSpans(const Spans &wanted, const Spans &have) {
  Spans out;
  int j = 0;
  for (const Span &w : wanted) {
    double cur = w.first;
    while (j < have.size() && have[j].second <= cur)
      ++j;
    int k = j;
    while (k < have.size() && have[k].first < w.second) {
      if (have[k].first > cur)
        out.append({cur, have[k].first});
      cur = std::max(cur, have[k].second);
      ++k;
    }
    if (cur < w.second)
      out.append({cur, w.second});
  }
  return out;
}

double spanLength(const Spans &spans) {
  double total = 0;
  for (const Span &s : spans)
    total += s.second - s.first;
  return total;
}

// --- the report -----------------------------------------------------------

QString ChannelCoverage::label() const {
  const QStringList parts = cid.split('.');
  if (parts.size() < 4)
    return cid;
  return parts[2].isEmpty() ? parts[3] : parts[2] + '.' + parts[3];
}

Spans StationCoverage::requested() const {
  Spans all;
  for (const WindowState &w : windows)
    all.append({w.start, w.end});
  return mergeSpans(all);
}

Span StationCoverage::span() const {
  const Spans req = requested();
  return req.isEmpty() ? Span{0, 0}
                       : Span{req.first().first, req.last().second};
}

QString StationCoverage::stateAt(double t) const {
  for (const WindowState &w : windows)
    if (w.start <= t && t < w.end)
      return w.state;
  return {};
}

QString gapReason(const QString &state) {
  if (state == QLatin1String("fetched"))
    return QStringLiteral("data gap");
  if (state == QLatin1String("nodata"))
    return QStringLiteral("no data at source");
  if (state == QLatin1String("pending") || kInFlight.contains(state))
    return QStringLiteral("not downloaded yet");
  return QStringLiteral("unavailable");
}

namespace {

double tsOf(const QString &iso) {
  QDateTime t = parseStamp(iso);
  return t.isValid() ? t.toMSecsSinceEpoch() / 1000.0 : 0.0;
}

QList<Gap> gapsOf(const QList<WindowState> &windows, const Spans &have) {
  QList<Gap> out;
  for (const WindowState &w : windows) {
    const QString why = gapReason(w.state);
    for (const Span &g :
         subtractSpans({{w.start, w.end}}, clipSpans(have, w.start, w.end))) {
      if (!out.isEmpty() && out.last().why == why &&
          std::abs(out.last().end - g.first) < 1e-6)
        out.last().end = g.second;
      else
        out.append({g.first, g.second, why});
    }
  }
  return out;
}

QJsonObject toJson(const ScanResult &scan) {
  QJsonObject channels;
  for (auto it = scan.begin(); it != scan.end(); ++it) {
    QJsonArray spans;
    for (const Span &s : it->spans)
      spans.append(QJsonArray{s.first, s.second});
    channels.insert(it.key(),
                    QJsonObject{{"rate", it->rate}, {"spans", spans}});
  }
  return channels;
}

ScanResult fromJson(const QJsonObject &channels) {
  ScanResult out;
  for (auto it = channels.begin(); it != channels.end(); ++it) {
    const QJsonObject c = it->toObject();
    ChannelSpans cs;
    cs.rate = c.value("rate").toDouble();
    for (const QJsonValue &v : c.value("spans").toArray()) {
      const QJsonArray a = v.toArray();
      cs.spans.append({a.at(0).toDouble(), a.at(1).toDouble()});
    }
    out.insert(it.key(), cs);
  }
  return out;
}

} // namespace

QList<StationCoverage> coverageReport(const Ledger &ledger, const QString &raw,
                                      const QStringList &stations, bool refresh,
                                      const Progress &progress) {
  QList<Row> rows;
  for (const Row &r : ledger.rows())
    if (stations.isEmpty() || stations.contains(str(r, "station")))
      rows << r;
  const QDir rawDir(raw);
  const QString cachePath = rawDir.filePath(QStringLiteral(".coverage.json"));
  QJsonObject cache;
  if (!refresh) {
    QFile f(cachePath);
    if (f.open(QIODevice::ReadOnly))
      cache = QJsonDocument::fromJson(f.readAll()).object();
  }
  struct Fetched {
    Row row;
    QString path;
  };
  QList<Fetched> fetched;
  for (const Row &r : rows)
    if (str(r, "state") == QLatin1String("fetched")) {
      const QString p = archivePath(raw, r);
      if (QFileInfo::exists(p))
        fetched.append({r, p});
    }
  QMap<QString, ScanResult> scanned;
  for (int i = 0; i < fetched.size(); ++i) {
    const QFileInfo fi(fetched[i].path);
    const QString key = rawDir.relativeFilePath(fetched[i].path);
    const QJsonObject hit = cache.value(key).toObject();
    const double mtime = fi.lastModified().toMSecsSinceEpoch() / 1000.0;
    if (!hit.isEmpty() && hit.value("size").toDouble() == double(fi.size()) &&
        hit.value("mtime").toDouble() == mtime) {
      scanned.insert(key, fromJson(hit.value("channels").toObject()));
      continue;
    }
    if (progress)
      progress(i, fetched.size(), fi.fileName());
    ScanResult got;
    try {
      got = scanArchive(fetched[i].path);
    } catch (const std::exception &) {
      got = {};
    }
    scanned.insert(key, got);
    cache.insert(key, QJsonObject{{"size", double(fi.size())},
                                  {"mtime", mtime},
                                  {"channels", toJson(got)}});
  }
  if (progress)
    progress(fetched.size(), fetched.size(), QString());
  if (rawDir.exists()) {
    QSaveFile f(cachePath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      f.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
      f.commit();
    }
  }

  QStringList codes;
  for (const Row &r : rows)
    if (!codes.contains(str(r, "station")))
      codes << str(r, "station");
  codes.sort();
  QList<StationCoverage> out;
  for (const QString &code : codes) {
    StationCoverage st;
    st.code = code;
    QList<Row> mine;
    for (const Row &r : rows)
      if (str(r, "station") == code) {
        mine << r;
        st.windows.append(
            {tsOf(str(r, "start")), tsOf(str(r, "end")), str(r, "state")});
      }
    std::sort(st.windows.begin(), st.windows.end(),
              [](const WindowState &a, const WindowState &b) {
                return std::tie(a.start, a.end, a.state) <
                       std::tie(b.start, b.end, b.state);
              });
    const Spans requested = st.requested();
    QMap<QString, ChannelSpans> perChannel;
    for (const Row &r : mine) {
      if (str(r, "state") != QLatin1String("fetched"))
        continue;
      const ScanResult sc =
          scanned.value(rawDir.relativeFilePath(archivePath(raw, r)));
      for (auto it = sc.begin(); it != sc.end(); ++it) {
        ChannelSpans &acc = perChannel[it.key()];
        acc.rate = it->rate;
        acc.spans += it->spans;
      }
    }
    const Span whole = st.span();
    for (auto it = perChannel.begin(); it != perChannel.end(); ++it) {
      const double tol = 1.5 / it->rate;
      const Spans have = mergeSpans(
          clipSpans(mergeSpans(it->spans, tol), whole.first, whole.second),
          tol);
      ChannelCoverage ch;
      ch.cid = it.key();
      ch.rate = it->rate;
      ch.spans = have;
      ch.gaps = gapsOf(st.windows, have);
      ch.requested = spanLength(requested);
      double gapSeconds = 0;
      for (const Gap &g : ch.gaps)
        gapSeconds += g.seconds();
      ch.covered = ch.requested - gapSeconds;
      st.channels.append(ch);
    }
    out.append(st);
  }
  return out;
}

QList<TimelineCell> timeline(const StationCoverage &station,
                             const ChannelCoverage *channel, int width) {
  const Spans wanted = station.requested();
  const double total = spanLength(wanted);
  QList<TimelineCell> cells;
  if (total <= 0 || width < 1)
    return cells;
  const Spans have = channel ? channel->spans : Spans{};
  const double step = total / width;
  int i = 0;
  double offset = 0.0; // requested time before wanted[i]
  for (int n = 0; n < width; ++n) {
    const double lo = n * step, hi = (n + 1) * step;
    double covered = 0;
    QMap<QString, double> whyLen;
    while (i < wanted.size()) {
      const double s = wanted[i].first, e = wanted[i].second;
      const double segLo = std::max(lo, offset),
                   segHi = std::min(hi, offset + (e - s));
      if (segHi > segLo) {
        const double a = s + (segLo - offset), b = s + (segHi - offset);
        covered += spanLength(clipSpans(have, a, b));
        whyLen[gapReason(station.stateAt((a + b) / 2))] += b - a;
      }
      if (offset + (e - s) <= hi) {
        offset += e - s;
        ++i;
      } else {
        break;
      }
    }
    const double f = covered / step;
    if (f >= 0.995)
      cells.append({QStringLiteral("█"), QStringLiteral("full"), f});
    else if (f > 0)
      cells.append({f >= 0.75  ? QStringLiteral("▆")
                    : f >= 0.5 ? QStringLiteral("▄")
                               : QStringLiteral("▂"),
                    QStringLiteral("partial"), f});
    else {
      QString why;
      double best = -1;
      for (auto it = whyLen.begin(); it != whyLen.end(); ++it)
        if (it.value() > best) {
          best = it.value();
          why = it.key();
        }
      if (why == QLatin1String("data gap"))
        cells.append({QStringLiteral("▁"), QStringLiteral("gap"), 0});
      else if (why == QLatin1String("no data at source"))
        cells.append({QStringLiteral("×"), QStringLiteral("nodata"), 0});
      else if (why == QLatin1String("not downloaded yet"))
        cells.append({QStringLiteral("·"), QStringLiteral("pending"), 0});
      else
        cells.append({QStringLiteral(" "), QStringLiteral("none"), 0});
    }
  }
  return cells;
}

QString formatSeconds(double s) {
  if (s < 120)
    return QStringLiteral("%1 s").arg(s, 0, 'f', 0);
  if (s < 7200)
    return QStringLiteral("%1 min").arg(s / 60, 0, 'f', 0);
  if (s < 172800)
    return QStringLiteral("%1 h").arg(s / 3600, 0, 'f', 1);
  return QStringLiteral("%1 d").arg(s / 86400, 0, 'f', 1);
}

QString formatTime(double t) {
  return QDateTime::fromMSecsSinceEpoch(qint64(t * 1000), QTimeZone::utc())
      .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace skg::tdvms
