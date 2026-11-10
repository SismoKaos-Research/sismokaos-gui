#include "fx/catalog.h"
#include "fx/csv.h"

#include <QFile>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fx/mseed.h"
#include "text/text.h"
#include "miniz.h"
#include <utility>

namespace skg::fx {

QByteArray gunzip(const QByteArray &gz) {
  if (gz.size() < 18 || quint8(gz[0]) != 0x1f || quint8(gz[1]) != 0x8b)
    throw std::runtime_error("not a gzip file");
  if (quint8(gz[2]) != 8)
    throw std::runtime_error("gzip: unsupported compression method");
  const quint8 flags = quint8(gz[3]);
  int pos = 10;
  if (flags & 0x04) { // FEXTRA
    const int xlen = quint8(gz[pos]) | (quint8(gz[pos + 1]) << 8);
    pos += 2 + xlen;
  }
  if (flags & 0x08) { // FNAME
    while (pos < gz.size() && gz[pos] != '\0')
      ++pos;
    ++pos;
  }
  if (flags & 0x10) { // FCOMMENT
    while (pos < gz.size() && gz[pos] != '\0')
      ++pos;
    ++pos;
  }
  if (flags & 0x02) // FHCRC
    pos += 2;
  if (pos >= gz.size() - 8)
    throw std::runtime_error("gzip: truncated header");
  // The trailer's ISIZE gives the uncompressed length modulo 2^32.
  const uchar *tail =
      reinterpret_cast<const uchar *>(gz.constData()) + gz.size() - 4;
  const quint32 isize =
      tail[0] | (tail[1] << 8) | (tail[2] << 16) | (quint32(tail[3]) << 24);
  QByteArray out;
  out.resize(qsizetype(isize) + 1);
  tinfl_decompressor inflator;
  tinfl_init(&inflator);
  size_t inLen = size_t(gz.size() - 8 - pos), outLen = size_t(out.size());
  const tinfl_status status = tinfl_decompress(
      &inflator, reinterpret_cast<const mz_uint8 *>(gz.constData() + pos),
      &inLen, reinterpret_cast<mz_uint8 *>(out.data()),
      reinterpret_cast<mz_uint8 *>(out.data()), &outLen,
      TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  if (status < 0)
    throw std::runtime_error("gzip: corrupt deflate stream");
  out.resize(qsizetype(outLen));
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

std::vector<CatalogEvent> loadCatalog(const QString &path) {
  const QStringList rows = lines(readMaybeGz(path));
  if (rows.isEmpty())
    throw std::runtime_error((path + " is empty").toStdString());
  const QStringList headers = splitCsv(rows.first());
  const int t = column(headers, {"Date"}), mag = column(headers, {"Magnitude"}),
            lat = column(headers, {"Latitude"}),
            lon = column(headers, {"Longitude"});
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
    const auto epoch = parseAfadTime(r[t]);
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
