#include "analysis/label.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "analysis/geo.h"
#include "fx/csv.h"

namespace skg::analysis {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

int fieldIndex(const fx::NpyTable &t, const QString &name) {
  for (int i = 0; i < t.fields.size(); ++i)
    if (t.fields[i].name == name)
      return i;
  return -1;
}

bool isStringField(const fx::NpyField &f) { return f.dtype.contains('U'); }

void removeColumn(fx::NpyTable &t, int i) {
  t.fields.removeAt(i);
  t.numeric.removeAt(i);
  t.strings.removeAt(i);
}

void addStringColumn(fx::NpyTable &t, const QString &name, QStringList values) {
  if (const int i = fieldIndex(t, name); i >= 0)
    removeColumn(t, i);
  int width = 1;
  for (const QString &v : values)
    width = std::max<int>(width, int(v.size()));
  t.fields.append({name, QStringLiteral("<U%1").arg(width)});
  t.numeric.append(std::vector<double>());
  t.strings.append(std::move(values));
}

void addNumericColumn(fx::NpyTable &t, const QString &name,
                      std::vector<double> values) {
  if (const int i = fieldIndex(t, name); i >= 0)
    removeColumn(t, i);
  t.fields.append({name, QStringLiteral("<f8")});
  t.numeric.append(std::move(values));
  t.strings.append(QStringList());
}

QString hms(double epoch) {
  return QDateTime::fromMSecsSinceEpoch(qint64(std::llround(epoch * 1000.0)),
                                        QTimeZone::utc())
      .toString(QStringLiteral("HH:mm:ss"));
}

QString fmt(double v, int decimals) {
  return std::isnan(v) ? QString() : QString::number(v, 'f', decimals);
}
QString fmtG(double v) {
  return std::isnan(v) ? QString() : QString::number(v, 'g', 10);
}
} // namespace

std::optional<WindowSpan> windowSpan(const fx::NpyTable &t, int row,
                                     double winSec) {
  const int idCol = fieldIndex(t, QStringLiteral("Pencere_ID")),
            tCol = fieldIndex(t, QStringLiteral("Zaman_Dk"));
  if (tCol < 0 || row < 0 || row >= t.rows)
    return std::nullopt;
  const double minutes = t.value(row, tCol).toDouble();
  if (std::isnan(minutes))
    return std::nullopt;
  // SCAP ids carry the hour: YYYY_MM_DD_HH_wNN, Zaman_Dk minutes into it.
  if (idCol >= 0) {
    const QString id = t.value(row, idCol).toString();
    // Built from its parts as UTC: parsed as local time and relabelled, an
    // hour inside a daylight-saving gap of this machine's zone would move.
    const QStringList parts = id.left(13).split(QLatin1Char('_'));
    bool okY = false, okM = false, okD = false, okH = false;
    const int y = parts.value(0).toInt(&okY), mo = parts.value(1).toInt(&okM),
              d = parts.value(2).toInt(&okD), h = parts.value(3).toInt(&okH);
    const QDateTime base =
        parts.size() == 4 && okY && okM && okD && okH && h >= 0 && h < 24
            ? QDateTime(QDate(y, mo, d), QTime(h, 0), QTimeZone::UTC)
            : QDateTime();
    if (base.isValid() && minutes < 1e6) {
      const double b = double(base.toMSecsSinceEpoch()) / 1000.0;
      const double end = b + std::round(minutes * 60.0);
      return WindowSpan{std::max(b, end - 200.0), end};
    }
  }
  // fx: Zaman_Dk is the window end in epoch minutes.
  const double end = minutes * 60.0;
  return WindowSpan{end - winSec, end};
}

LabelSummary labelWindows(fx::NpyTable &table, const std::vector<Event> &events,
                          const LabelOptions &opt) {
  std::vector<Event> sorted = events;
  std::sort(sorted.begin(), sorted.end(),
            [](const Event &a, const Event &b) { return a.epoch < b.epoch; });
  const bool enhanced = opt.stationLat && opt.stationLon;
  QStringList spans, mags, lats, lons, dists, classes, dirs;
  std::vector<double> counts, labels;
  LabelSummary sum;
  sum.windows = table.rows;
  for (int r = 0; r < table.rows; ++r) {
    const auto span = windowSpan(table, r, opt.winSec);
    if (!span) {
      spans << QString();
      mags << QString();
      lats << QString();
      lons << QString();
      dists << QString();
      classes << QString();
      dirs << QString();
      counts.push_back(0);
      labels.push_back(0);
      continue;
    }
    spans << hms(span->start) + QStringLiteral(" - ") + hms(span->end);
    auto lo =
        std::lower_bound(sorted.begin(), sorted.end(), span->start,
                         [](const Event &e, double t) { return e.epoch < t; });
    QStringList m, la, lo_, d, c, dir;
    int n = 0;
    for (auto it = lo; it != sorted.end() && it->epoch < span->end; ++it, ++n) {
      m << fmtG(it->mag);
      if (enhanced) {
        la << fmtG(it->lat);
        lo_ << fmtG(it->lon);
        const double km =
            haversineKm(*opt.stationLat, *opt.stationLon, it->lat, it->lon);
        d << fmt(km, 2);
        c << QString::number(distanceClass(km));
        dir << compassDirection(
            bearingDeg(*opt.stationLat, *opt.stationLon, it->lat, it->lon),
            opt.lang);
      }
    }
    const QString sep = QStringLiteral(", ");
    mags << m.join(sep);
    lats << la.join(sep);
    lons << lo_.join(sep);
    dists << d.join(sep);
    classes << c.join(sep);
    dirs << dir.join(sep);
    counts.push_back(n);
    labels.push_back(n > 0 ? 1 : 0);
    if (n > 0)
      ++sum.withEvents;
  }
  addStringColumn(table, QStringLiteral("Zaman_Araligi"), spans);
  addStringColumn(table, QStringLiteral("Magnitude"), mags);
  addStringColumn(table, QStringLiteral("Latitude"), lats);
  addStringColumn(table, QStringLiteral("Longitude"), lons);
  addStringColumn(table, QStringLiteral("Distance_km"), dists);
  addStringColumn(table, QStringLiteral("Mesafe_Derecesi"), classes);
  addStringColumn(table, QStringLiteral("Yön_Bilgisi"), dirs);
  addNumericColumn(table, QStringLiteral("Deprem_Sayisi"), counts);
  addNumericColumn(table, QStringLiteral("Etiket"), labels);
  sum.columnsAdded = {"Zaman_Araligi", "Magnitude",     "Latitude",
                      "Longitude",     "Distance_km",   "Mesafe_Derecesi",
                      "Yön_Bilgisi",   "Deprem_Sayisi", "Etiket"};
  return sum;
}

bool writeCsv(const QString &path, const fx::NpyTable &t) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write("\xEF\xBB\xBF");
  QStringList header;
  for (const fx::NpyField &fl : t.fields)
    header << fl.name;
  f.write(header.join(',').toUtf8() + '\n');
  for (int r = 0; r < t.rows; ++r) {
    QStringList cells;
    for (int c = 0; c < t.fields.size(); ++c) {
      if (isStringField(t.fields[c])) {
        QString s = t.strings[c].value(r);
        if (s.contains(',') || s.contains('"') || s.contains('\n'))
          s = '"' + s.replace('"', QStringLiteral("\"\"")) + '"';
        cells << s;
      } else {
        const double v = t.numeric[c][size_t(r)];
        cells << (std::isnan(v) ? QString() : QString::number(v, 'g', 15));
      }
    }
    f.write(cells.join(',').toUtf8() + '\n');
  }
  return f.commit();
}

fx::NpyTable readCsvTable(const QString &path) {
  const QStringList rows = fx::lines(fx::readMaybeGz(path));
  if (rows.isEmpty())
    throw std::runtime_error((path + " is empty").toStdString());
  QStringList header = fx::splitCsv(rows.first());
  if (!header.isEmpty() && header[0].startsWith(QChar(0xFEFF)))
    header[0].remove(0, 1);
  const int ncol = header.size();
  std::vector<QStringList> cols{size_t(ncol)};
  int n = 0;
  for (int i = 1; i < rows.size(); ++i) {
    if (rows[i].trimmed().isEmpty())
      continue;
    const QStringList r = fx::splitCsv(rows[i]);
    for (int c = 0; c < ncol; ++c)
      cols[size_t(c)] << r.value(c);
    ++n;
  }
  fx::NpyTable t;
  t.rows = n;
  for (int c = 0; c < ncol; ++c) {
    const QStringList &col = cols[size_t(c)];
    bool numeric = true;
    std::vector<double> vals;
    vals.reserve(size_t(n));
    for (const QString &s : col) {
      const QString v = s.trimmed();
      if (v.isEmpty()) {
        vals.push_back(kNaN);
        continue;
      }
      bool ok = false;
      const double d = v.toDouble(&ok);
      if (!ok) {
        numeric = false;
        break;
      }
      vals.push_back(d);
    }
    if (numeric && header[c] != QLatin1String("Pencere_ID")) {
      t.fields.append({header[c], QStringLiteral("<f8")});
      t.numeric.append(std::move(vals));
      t.strings.append(QStringList());
    } else {
      int width = 1;
      for (const QString &s : col)
        width = std::max<int>(width, int(s.size()));
      t.fields.append({header[c], QStringLiteral("<U%1").arg(width)});
      t.numeric.append(std::vector<double>());
      t.strings.append(col);
    }
  }
  return t;
}

fx::NpyTable loadTable(const QString &path) {
  if (path.endsWith(QLatin1String(".npy"), Qt::CaseInsensitive))
    return fx::readNpy(path);
  return readCsvTable(path);
}

} // namespace skg::analysis
