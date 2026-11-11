#include "cbi/table.h"

#include <QFile>
#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "analysis/geo.h"
#include "cbi/decluster.h"
#include "cbi/magnitudes.h"
#include "cbi/zones.h"
#include "fx/csv.h"

namespace skg::cbi {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
} // namespace

QString riskNear() { return QStringLiteral("3 aydan az"); }
QString riskFar() { return QStringLiteral("3 aydan fazla"); }

int IndicatorTable::columnIndex(const QString &name) const {
  return int(columns.indexOf(name));
}

const std::vector<double> *IndicatorTable::column(const QString &name) const {
  const int i = columnIndex(name);
  return i < 0 ? nullptr : &values[static_cast<size_t>(i)];
}

std::vector<double> *IndicatorTable::column(const QString &name) {
  const int i = columnIndex(name);
  return i < 0 ? nullptr : &values[static_cast<size_t>(i)];
}

void IndicatorTable::addColumn(const QString &name, std::vector<double> v) {
  const int i = columnIndex(name);
  if (i >= 0) {
    values[static_cast<size_t>(i)] = std::move(v);
    return;
  }
  columns << name;
  values.push_back(std::move(v));
}

QString CatalogSelection::tag() const {
  QString t;
  switch (zone) {
  case Zone::None:
    t = QStringLiteral("all");
    break;
  case Zone::Eafz:
    t = QStringLiteral("eafz%1km").arg(QString::number(bufferKm, 'g'));
    break;
  case Zone::GeoJson:
    t = QFileInfo(zoneFile).completeBaseName() +
        QString::number(bufferKm, 'g') + QStringLiteral("km");
    break;
  case Zone::Circle:
    t = QStringLiteral("r%1km").arg(QString::number(radiusKm, 'g'));
    break;
  }
  if (homogenize)
    t += QStringLiteral("_hom");
  if (decluster)
    t += QStringLiteral("_dc");
  return t;
}

namespace {
Catalog withinCircle(const Catalog &c, double lat, double lon, double km) {
  std::vector<bool> keep;
  keep.reserve(c.size());
  for (const analysis::Event &e : c.events)
    keep.push_back(analysis::haversineKm(lat, lon, e.lat, e.lon) <= km);
  return c.filtered(keep);
}
} // namespace

Catalog CatalogSelection::apply(const Catalog &input, QStringList *log) const {
  Catalog catalog = input;
  auto say = [&](const QString &s) {
    if (log)
      *log << s;
  };
  if (homogenize) {
    const MagnitudeConversion conv =
        conversionFile.isEmpty() ? MagnitudeConversion::afadDefault()
                                 : MagnitudeConversion::load(conversionFile);
    conv.apply(catalog);
    say(QStringLiteral("Homogenised magnitudes (%1 periods)")
            .arg(conv.periods.size()));
  }
  std::optional<FaultZone> fz;
  if (zone == Zone::Eafz)
    fz = eafz(strands);
  else if (zone == Zone::GeoJson)
    fz = loadFaultZone(zoneFile, strands);
  if (decluster) {
    if (fz)
      catalog = selectNearFault(catalog, *fz, bufferKm + kDeclusterPadKm);
    else if (zone == Zone::Circle)
      catalog = withinCircle(catalog, centerLat, centerLon,
                             radiusKm + kDeclusterPadKm);
    const size_t before = catalog.size();
    catalog = catalog.filtered(gardnerKnopoff(catalog));
    say(QStringLiteral("Declustered: %1 of %2 events independent")
            .arg(catalog.size())
            .arg(before));
  }
  if (fz) {
    catalog = selectNearFault(catalog, *fz, bufferKm);
    say(QStringLiteral("Within %1 km of %2 (%3): %4 events")
            .arg(QString::number(bufferKm, 'g'), fz->name,
                 fz->strands.join(QStringLiteral(", ")))
            .arg(catalog.size()));
  } else if (zone == Zone::Circle) {
    catalog = withinCircle(catalog, centerLat, centerLon, radiusKm);
    say(QStringLiteral("Within %1 km of %2, %3: %4 events")
            .arg(QString::number(radiusKm, 'g'),
                 QString::number(centerLat, 'f', 3),
                 QString::number(centerLon, 'f', 3))
            .arg(catalog.size()));
  }
  if (start) {
    const double t0 = toEpoch(*start);
    std::vector<bool> keep;
    for (const analysis::Event &e : catalog.events)
      keep.push_back(e.epoch >= t0);
    catalog = catalog.filtered(keep);
    say(QStringLiteral("From %1: %2 events")
            .arg(start->toString(QStringLiteral("yyyy-MM-dd")))
            .arg(catalog.size()));
  }
  return catalog;
}

IndicatorTable computeIndicatorTable(const Catalog &catalog,
                                     const IndicatorConfig &cfg) {
  IndicatorTable t;
  const QStringList cols = windowIndicatorColumns();
  t.columns = cols;
  t.values.assign(static_cast<size_t>(cols.size()), {});
  if (catalog.empty())
    return t;
  const std::vector<double> epochs = catalog.epochs();
  const std::vector<double> mags = catalog.magnitudes();
  const auto bounds =
      windowBounds(toDateTime(epochs.front()), toDateTime(epochs.back()),
                   cfg.window, cfg.step);
  for (const auto &[start, end] : bounds) {
    const auto lo =
        std::lower_bound(epochs.begin(), epochs.end(), toEpoch(start));
    const auto hi =
        std::lower_bound(epochs.begin(), epochs.end(), toEpoch(end));
    const size_t a = static_cast<size_t>(lo - epochs.begin()),
                 b = static_cast<size_t>(hi - epochs.begin());
    const std::vector<double> wm(mags.begin() + long(a),
                                 mags.begin() + long(b));
    const std::vector<double> wt(epochs.begin() + long(a),
                                 epochs.begin() + long(b));
    const WindowIndicators w =
        computeWindowIndicators(wm, wt, cfg.mc, cfg.halfGaussSigma, cfg.magBin);
    t.start.push_back(start);
    t.end.push_back(end);
    t.xLabel << start.toString(QStringLiteral("yyyy-MM")) +
                    QStringLiteral(" - ") +
                    end.toString(QStringLiteral("yyyy-MM"));
    for (int c = 0; c < cols.size(); ++c)
      t.values[static_cast<size_t>(c)].push_back(w.value(cols[c]));
  }
  markFirstAppearance(t, catalog);
  return t;
}

void markFirstAppearance(IndicatorTable &table, const Catalog &catalog) {
  const size_t n = static_cast<size_t>(table.rows());
  std::vector<double> band5(n, 0.0), band6(n, 0.0);
  for (const analysis::Event &e : catalog.events) {
    if (e.mag < kLargeEventMag)
      continue;
    for (size_t i = 0; i < n; ++i) {
      if (e.epoch >= toEpoch(table.start[i]) &&
          e.epoch < toEpoch(table.end[i])) {
        std::vector<double> &band = e.mag < kMajorEventMag ? band5 : band6;
        band[i] = std::max(band[i], e.mag);
        break;
      }
    }
  }
  table.addColumn(QStringLiteral("Mmax_obs_first_5_to_6"), band5);
  table.addColumn(QStringLiteral("Mmax_obs_first_6_plus"), band6);
}

void labelPreEventWindows(IndicatorTable &table,
                          const std::vector<double> &targetEpochs, int days) {
  table.risk.clear();
  for (int i = 0; i < table.rows(); ++i) {
    const double end = toEpoch(table.end[static_cast<size_t>(i)]);
    bool near = false;
    for (double t : targetEpochs) {
      const double lead = std::floor((t - end) / 86400.0);
      near = near || (lead >= 0 && lead <= days);
    }
    table.risk << (near ? riskNear() : riskFar());
  }
}

namespace {
// pandas rolling(window, min_periods=1): statistics of the non-NaN values
// among the last `window` rows.
void rolling(const std::vector<double> &v, int window,
             std::vector<double> &mean, std::vector<double> &sampleStd) {
  const size_t n = v.size();
  mean.assign(n, kNaN);
  sampleStd.assign(n, kNaN);
  for (size_t i = 0; i < n; ++i) {
    const size_t lo = i + 1 >= static_cast<size_t>(window)
                          ? i + 1 - static_cast<size_t>(window)
                          : 0;
    double s = 0;
    int c = 0;
    for (size_t j = lo; j <= i; ++j)
      if (std::isfinite(v[j])) {
        s += v[j];
        ++c;
      }
    if (c == 0)
      continue;
    const double m = s / c;
    mean[i] = m;
    if (c > 1) {
      double ss = 0;
      for (size_t j = lo; j <= i; ++j)
        if (std::isfinite(v[j]))
          ss += (v[j] - m) * (v[j] - m);
      sampleStd[i] = std::sqrt(ss / (c - 1));
    }
  }
}
} // namespace

void addMovingAverage(IndicatorTable &table, int window,
                      const QStringList &columns) {
  for (const QString &col : columns) {
    const std::vector<double> *v = table.column(col);
    if (!v)
      continue;
    std::vector<double> mean, sd;
    rolling(*v, window, mean, sd);
    table.addColumn(col + QStringLiteral("_MA%1").arg(window), mean);
  }
}

void addBollingerBands(IndicatorTable &table, int window, double k,
                       const QStringList &columns) {
  for (const QString &col : columns) {
    const std::vector<double> *v = table.column(col);
    if (!v)
      continue;
    std::vector<double> mean, sd;
    rolling(*v, window, mean, sd);
    std::vector<double> upper(sd.size()), lower(sd.size()), width(sd.size());
    for (size_t i = 0; i < sd.size(); ++i) {
      const double s = std::isfinite(sd[i]) ? sd[i] : 0.0;
      upper[i] = mean[i] + k * s;
      lower[i] = mean[i] - k * s;
      width[i] = 2 * k * s;
    }
    table.addColumn(col + QStringLiteral("_BB_MA"), mean);
    table.addColumn(col + QStringLiteral("_BB_Upper"), upper);
    table.addColumn(col + QStringLiteral("_BB_Lower"), lower);
    table.addColumn(col + QStringLiteral("_BB_Width"), width);
  }
}

QString pandasNumber(double v) {
  if (std::isnan(v))
    return QString();
  if (std::isinf(v))
    return v > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
  QString s;
  for (int prec = 1; prec <= 17; ++prec) {
    s = QString::number(v, 'g', prec);
    if (s.toDouble() == v)
      break;
  }
  if (s.contains(QLatin1Char('e'))) {
    const double a = std::abs(v);
    if (a >= 1e-4 && a < 1e16) {
      // Python's repr keeps positional notation in this range.
      const int exp = int(std::floor(std::log10(a)));
      int digits = 0;
      for (int prec = 1; prec <= 17; ++prec)
        if (QString::number(v, 'g', prec).toDouble() == v) {
          digits = prec;
          break;
        }
      s = QString::number(v, 'f', std::max(1, digits - 1 - exp));
      while (s.endsWith(QLatin1Char('0')) && !s.endsWith(QLatin1String(".0")))
        s.chop(1);
    }
    return s;
  }
  if (!s.contains(QLatin1Char('.')))
    s += QStringLiteral(".0");
  return s;
}

namespace {
QString timeText(const QDateTime &dt) {
  return dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

bool isIntegerColumn(const QString &name) {
  return name == QLatin1String("Event_Count");
}
} // namespace

bool writeTableCsv(const IndicatorTable &table, const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  QByteArray out = "\xEF\xBB\xBF";
  QStringList header{QStringLiteral("Window_Index"), QStringLiteral("x_label"),
                     QStringLiteral("Window_Start_Time"),
                     QStringLiteral("Window_End_Time")};
  header << table.columns;
  if (!table.risk.isEmpty())
    header << QStringLiteral("Risk_Durumu");
  out += header.join(QLatin1Char(',')).toUtf8() + '\n';
  for (int i = 0; i < table.rows(); ++i) {
    const size_t r = static_cast<size_t>(i);
    QStringList row{QString::number(i), table.xLabel.value(i),
                    timeText(table.start[r]), timeText(table.end[r])};
    for (int c = 0; c < table.columns.size(); ++c) {
      const double v = table.values[static_cast<size_t>(c)][r];
      if (isIntegerColumn(table.columns[c]) && std::isfinite(v))
        row << QString::number(qint64(std::llround(v)));
      else
        row << pandasNumber(v);
    }
    if (!table.risk.isEmpty())
      row << table.risk.value(i);
    out += row.join(QLatin1Char(',')).toUtf8() + '\n';
  }
  return f.write(out) == out.size();
}

IndicatorTable readTableCsv(const QString &path) {
  const QStringList rows = fx::lines(fx::readMaybeGz(path));
  if (rows.isEmpty())
    throw std::runtime_error((path + " is empty").toStdString());
  QStringList header = fx::splitCsv(rows.first());
  if (!header.isEmpty())
    header[0].remove(QChar(0xFEFF));
  const int ix = int(header.indexOf(QStringLiteral("x_label"))),
            is = int(header.indexOf(QStringLiteral("Window_Start_Time"))),
            ie = int(header.indexOf(QStringLiteral("Window_End_Time"))),
            ir = int(header.indexOf(QStringLiteral("Risk_Durumu")));
  if (is < 0 || ie < 0)
    throw std::runtime_error(
        (path + " is not an indicator table").toStdString());
  IndicatorTable t;
  std::vector<int> numeric;
  for (int c = 0; c < header.size(); ++c) {
    if (c == ix || c == is || c == ie || c == ir ||
        header[c] == QLatin1String("Window_Index"))
      continue;
    numeric.push_back(c);
    t.columns << header[c];
    t.values.push_back({});
  }
  auto parseTime = [](const QString &s) {
    // The table's times are UTC; read as such, not as local time relabelled
    // (which moves a time inside a daylight-saving gap of this machine).
    return fx::isoUtcDateTime(s);
  };
  for (int i = 1; i < rows.size(); ++i) {
    if (rows[i].trimmed().isEmpty())
      continue;
    const QStringList r = fx::splitCsv(rows[i]);
    t.start.push_back(parseTime(r.value(is)));
    t.end.push_back(parseTime(r.value(ie)));
    t.xLabel << (ix >= 0 ? r.value(ix) : QString());
    if (ir >= 0)
      t.risk << r.value(ir);
    for (size_t k = 0; k < numeric.size(); ++k) {
      const QString s = r.value(numeric[k]).trimmed();
      bool ok = false;
      const double v = s.toDouble(&ok);
      t.values[k].push_back(ok ? v : kNaN);
    }
  }
  return t;
}

} // namespace skg::cbi
