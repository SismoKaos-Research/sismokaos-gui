#include "fx/catalog.h"
#include "fx/csv.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "fx/mseed.h"
#include "text/text.h"
#include "miniz.h"
#include <utility>

namespace skg::fx {

namespace {
// Where the deflate data of the gzip member starting at `pos` begins. Throws
// on a header that is not gzip or runs past the end of the file.
qsizetype gzipDataStart(const QByteArray &gz, qsizetype pos) {
  const auto need = [&gz](qsizetype upTo) {
    if (upTo > gz.size())
      throw std::runtime_error("gzip: truncated header");
  };
  need(pos + 10);
  if (quint8(gz[pos]) != 0x1f || quint8(gz[pos + 1]) != 0x8b)
    throw std::runtime_error("not a gzip file");
  if (quint8(gz[pos + 2]) != 8)
    throw std::runtime_error("gzip: unsupported compression method");
  const quint8 flags = quint8(gz[pos + 3]);
  pos += 10;
  if (flags & 0x04) { // FEXTRA
    need(pos + 2);
    const int xlen = quint8(gz[pos]) | (quint8(gz[pos + 1]) << 8);
    pos += 2 + xlen;
  }
  for (const quint8 bit : {quint8(0x08), quint8(0x10)}) // FNAME, FCOMMENT
    if (flags & bit) {
      while (pos < gz.size() && gz[pos] != '\0')
        ++pos;
      ++pos;
    }
  if (flags & 0x02) // FHCRC
    pos += 2;
  need(pos + 1);
  return pos;
}
} // namespace

QByteArray gunzip(const QByteArray &gz) {
  // Member after member (a .gz may hold several, as `cat a.gz b.gz`, pigz
  // and bgzip write them), each inflated to its own end. The length in a
  // member's trailer is not used: it is modulo 2^32 and describes only that
  // member, and a stream that stops short of its end is an error, not a
  // shorter catalog.
  constexpr qsizetype kMaxOut = qsizetype(1) << 31; // 2 GiB of text
  const char *const kTruncated =
      "gzip: the file ends before its data does (truncated)";
  QByteArray out;
  qsizetype pos = 0;
  bool any = false;
  while (pos < gz.size()) {
    if (any) {
      // Zero padding after the last member is allowed.
      qsizetype z = pos;
      while (z < gz.size() && gz[z] == '\0')
        ++z;
      if (z == gz.size())
        break;
    }
    pos = gzipDataStart(gz, pos);
    mz_stream stream{};
    if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK)
      throw std::runtime_error("gzip: cannot start inflating");
    const qsizetype given = std::min<qsizetype>(
        gz.size() - pos, qsizetype(std::numeric_limits<int>::max()));
    stream.next_in =
        reinterpret_cast<const unsigned char *>(gz.constData()) + pos;
    stream.avail_in = static_cast<unsigned int>(given);
    QByteArray chunk(1 << 20, Qt::Uninitialized);
    int status = MZ_OK;
    while (status == MZ_OK) {
      stream.next_out = reinterpret_cast<unsigned char *>(chunk.data());
      stream.avail_out = static_cast<unsigned int>(chunk.size());
      status = mz_inflate(&stream, MZ_NO_FLUSH);
      const qsizetype got = chunk.size() - qsizetype(stream.avail_out);
      if (out.size() + got > kMaxOut) {
        mz_inflateEnd(&stream);
        throw std::runtime_error("gzip: more than 2 GiB when inflated");
      }
      out.append(chunk.constData(), got);
      if (status == MZ_OK && got == 0 && stream.avail_in == 0)
        break; // the input is used up before the stream ends
    }
    const qsizetype used = given - qsizetype(stream.avail_in);
    mz_inflateEnd(&stream);
    if (status != MZ_STREAM_END)
      throw std::runtime_error(status == MZ_OK || status == MZ_BUF_ERROR
                                   ? kTruncated
                                   : "gzip: corrupt deflate stream");
    pos += used;
    if (pos + 8 > gz.size())
      throw std::runtime_error(kTruncated);
    pos += 8; // this member's CRC32 and length
    any = true;
  }
  if (!any)
    throw std::runtime_error("not a gzip file");
  return out;
}

QByteArray readMaybeGz(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(("cannot open " + path).toStdString());
  const QByteArray raw = f.readAll();
  return path.endsWith(QLatin1String(".gz"), Qt::CaseInsensitive) ? gunzip(raw)
                                                                  : raw;
}

// RFC 4180-ish split of one line; quotes may wrap a field.
QStringList splitCsv(const QString &line) {
  QStringList out;
  QString cur;
  bool quoted = false;
  for (int i = 0; i < line.size(); ++i) {
    const QChar c = line[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          cur += '"';
          ++i;
        } else
          quoted = false;
      } else
        cur += c;
    } else if (c == '"')
      quoted = true;
    else if (c == ',') {
      out << cur;
      cur.clear();
    } else
      cur += c;
  }
  out << cur;
  return out;
}

int column(const QStringList &headers,
           std::initializer_list<const char *> names) {
  for (int i = 0; i < headers.size(); ++i) {
    QString h = headers[i].trimmed();
    if (h.startsWith(QChar(0xFEFF)))
      h.remove(0, 1);
    for (const char *n : names)
      if (h.compare(QLatin1String(n), Qt::CaseInsensitive) == 0)
        return i;
  }
  return -1;
}

std::optional<double> number(const QStringList &row, int idx) {
  if (idx < 0 || idx >= row.size())
    return std::nullopt;
  bool ok = false;
  const double v = row[idx].trimmed().replace(',', '.').toDouble(&ok);
  return ok ? std::optional<double>(v) : std::nullopt;
}

QStringList lines(const QByteArray &text) {
  // UTF-8, or the Turkish code page when the bytes are not (text/text.h).
  QStringList out = text::decode(text).split(QLatin1Char('\n'));
  for (QString &l : out)
    if (l.endsWith('\r'))
      l.chop(1);
  return out;
}

std::optional<double> parseAfadTime(const QString &s) {
  const QString t = s.trimmed();
  const int sp = t.indexOf(' ');
  if (sp < 0)
    return std::nullopt;
  const QStringList d = t.left(sp).split('/');
  const QStringList c = t.mid(sp + 1).trimmed().split(':');
  if (d.size() != 3 || c.size() < 3)
    return std::nullopt;
  bool ok1, ok2, ok3, ok4, ok5, ok6;
  const int day = d[0].toInt(&ok1), month = d[1].toInt(&ok2),
            year = d[2].toInt(&ok3);
  const int h = c[0].toInt(&ok4), mi = c[1].toInt(&ok5);
  const double sec = c[2].toDouble(&ok6);
  if (!(ok1 && ok2 && ok3 && ok4 && ok5 && ok6) || month < 1 || month > 12 ||
      day < 1 || day > 31)
    return std::nullopt;
  return double(daysFromCivil(year, month, day) * 86400 + h * 3600 + mi * 60) +
         sec;
}

std::optional<double> parseIsoUtc(const QString &text) {
  static const QRegularExpression re(QStringLiteral(
      "^(\\d{4})-(\\d{1,2})-(\\d{1,2})"
      "(?:[T ]+(\\d{1,2}):(\\d{2})(?::(\\d{2})(?:[.,](\\d+))?)?"
      "\\s*(Z|z|[+-]\\d{2}(?::?\\d{2})?)?)?$"));
  const QRegularExpressionMatch m = re.match(text.trimmed());
  if (!m.hasMatch())
    return std::nullopt;
  const int y = m.captured(1).toInt(), mo = m.captured(2).toInt(),
            d = m.captured(3).toInt();
  if (!QDate::isValid(y, mo, d))
    return std::nullopt;
  const int h = m.captured(4).toInt(), mi = m.captured(5).toInt(),
            sec = m.captured(6).toInt();
  if (h > 24 || mi > 59 || sec > 60 || (h == 24 && (mi != 0 || sec != 0)))
    return std::nullopt;
  double epoch =
      double(daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec);
  if (!m.captured(7).isEmpty())
    epoch += (QStringLiteral("0.") + m.captured(7)).toDouble();
  const QString zone = m.captured(8);
  if (zone.size() > 1) { // an offset: the text's time is UTC plus it
    QString digits = zone.mid(1);
    digits.remove(QLatin1Char(':'));
    const int off = digits.left(2).toInt() * 3600 + digits.mid(2).toInt() * 60;
    epoch -= zone[0] == QLatin1Char('-') ? -off : off;
  }
  return epoch;
}

QDateTime isoUtcDateTime(const QString &text) {
  const std::optional<double> epoch = parseIsoUtc(text);
  if (!epoch)
    return {};
  return QDateTime::fromMSecsSinceEpoch(qint64(std::llround(*epoch * 1000.0)),
                                        QTimeZone::UTC);
}

std::vector<CatalogEvent> loadCatalog(const QString &path) {
  const QStringList rows = lines(readMaybeGz(path));
  if (rows.isEmpty())
    throw std::runtime_error((path + " is empty").toStdString());
  const QStringList headers = splitCsv(rows.first());
  // The columns and times the other reader of a catalog accepts
  // (analysis::loadEvents): one file reads the same for every tool.
  const int t = column(headers, {"Date", "DateTime", "Tarih", "Zaman", "time",
                                 "origin_time"}),
            mag = column(headers, {"Magnitude", "Buyukluk", "Büyüklük", "Mag",
                                   "ML", "Mw", "mag"}),
            lat = column(headers, {"Latitude", "Enlem", "lat"}),
            lon = column(headers, {"Longitude", "Boylam", "lon"});
  if (t < 0 || mag < 0 || lat < 0 || lon < 0)
    throw std::runtime_error(
        QStringLiteral("%1 needs Date, Magnitude, Latitude and Longitude "
                       "columns; its columns are %2")
            .arg(path, headers.join(", "))
            .toStdString());
  std::vector<CatalogEvent> events;
  for (int i = 1; i < rows.size(); ++i) {
    if (rows[i].trimmed().isEmpty())
      continue;
    const QStringList r = splitCsv(rows[i]);
    if (t >= r.size())
      continue;
    auto epoch = parseAfadTime(r[t]);
    if (!epoch)
      epoch = parseIsoUtc(r[t]); // without a zone: UTC, never local time
    const auto m = number(r, mag), la = number(r, lat), lo = number(r, lon);
    if (epoch && m && la && lo)
      events.push_back({*epoch, *m, *la, *lo});
  }
  if (events.empty())
    throw std::runtime_error(
        (path + " holds no readable events").toStdString());
  return events;
}

std::optional<std::pair<double, double>> stationCoords(const QString &path,
                                                       const QString &station) {
  const QStringList rows = lines(readMaybeGz(path));
  if (rows.isEmpty())
    throw std::runtime_error((path + " is empty").toStdString());
  const QStringList headers = splitCsv(rows.first());
  const int code = column(headers, {"Code", "station", "sta"}),
            lat = column(headers, {"Latitude", "lat"}),
            lon = column(headers, {"Longitude", "lon"});
  if (code < 0 || lat < 0 || lon < 0)
    throw std::runtime_error(
        QStringLiteral(
            "%1 needs code, latitude and longitude columns; its columns are %2")
            .arg(path, headers.join(", "))
            .toStdString());
  std::vector<std::pair<double, double>> found;
  for (int i = 1; i < rows.size(); ++i) {
    const QStringList r = splitCsv(rows[i]);
    if (code >= r.size() ||
        r[code].trimmed().compare(station, Qt::CaseInsensitive) != 0)
      continue;
    const auto la = number(r, lat), lo = number(r, lon);
    if (!la || !lo)
      continue;
    const bool same =
        std::any_of(found.begin(), found.end(), [&](const auto &p) {
          return std::abs(p.first - *la) < 1e-3 &&
                 std::abs(p.second - *lo) < 1e-3;
        });
    if (!same)
      found.push_back({*la, *lo});
  }
  if (found.size() > 1)
    throw std::runtime_error(
        QStringLiteral("station %1 is listed at several places in %2; set the "
                       "station's latitude and "
                       "longitude in the calibration settings")
            .arg(station, path)
            .toStdString());
  if (found.empty())
    return std::nullopt;
  return found.front();
}

double haversineKm(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = lat1 * M_PI / 180, p2 = lat2 * M_PI / 180;
  const double dp = p2 - p1, dl = (lon2 - lon1) * M_PI / 180;
  const double a = std::pow(std::sin(dp / 2), 2) +
                   std::cos(p1) * std::cos(p2) * std::pow(std::sin(dl / 2), 2);
  return 2.0 * 6371.0 * std::asin(std::sqrt(a));
}

double postEventSec(double mag, const CalibrationConfig &opts) {
  const double coda = std::pow(10.0, (mag + 0.87) / 2.0);
  return std::max(opts.minPostSec, opts.codaFactor * coda);
}

Exclusions::Exclusions(const std::vector<CatalogEvent> &events,
                       std::pair<double, double> station,
                       const CalibrationConfig &opts) {
  std::vector<std::pair<double, double>> spans;
  for (const CatalogEvent &e : events) {
    if (e.mag < opts.minMag)
      continue;
    if (haversineKm(station.first, station.second, e.lat, e.lon) >
        opts.maxDistKm)
      continue;
    spans.push_back(
        {e.epoch - opts.preSec, e.epoch + postEventSec(e.mag, opts)});
  }
  std::sort(spans.begin(), spans.end());
  for (const auto &[s, e] : spans) {
    if (!ends_.empty() && s <= ends_.back())
      ends_.back() = std::max(ends_.back(), e);
    else {
      starts_.push_back(s);
      ends_.push_back(e);
    }
  }
}

bool Exclusions::overlaps(double a, double b) const {
  const auto it =
      std::lower_bound(starts_.begin(), starts_.end(), b); // first start >= b
  const size_t i = size_t(it - starts_.begin());
  return i > 0 && ends_[i - 1] > a;
}

int Exclusions::countWithin(double a, double b) const {
  const auto hi = std::upper_bound(starts_.begin(), starts_.end(), b);
  const auto lo = std::lower_bound(starts_.begin(), starts_.end(), a);
  return int(hi - lo);
}

} // namespace skg::fx
