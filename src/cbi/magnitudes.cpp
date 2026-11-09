#include "cbi/magnitudes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "cbi/decluster.h"

// The embedded data lives in a static library: register it on first use.
static void initCbiResources() { Q_INIT_RESOURCE(cbi); }

namespace skg::cbi {

namespace {
QDateTime isoUtc(const QString &s) {
  QDateTime dt = QDateTime::fromString(s.trimmed(), Qt::ISODate);
  if (!dt.isValid())
    throw std::runtime_error(("bad date " + s).toStdString());
  dt.setTimeZone(QTimeZone::utc());
  return dt;
}

QString isoText(const QDateTime &dt) {
  return dt.toString(QStringLiteral("yyyy-MM-dd"));
}

// numpy.interp on increasing xp.
double interp(double x, const std::vector<double> &xp,
              const std::vector<double> &fp) {
  if (xp.empty())
    return std::nan("");
  if (x <= xp.front())
    return fp.front();
  if (x >= xp.back())
    return fp.back();
  const auto it = std::upper_bound(xp.begin(), xp.end(), x);
  const size_t j = static_cast<size_t>(it - xp.begin());
  const double x0 = xp[j - 1], x1 = xp[j];
  if (x1 == x0)
    return fp[j];
  return fp[j - 1] + (fp[j] - fp[j - 1]) * (x - x0) / (x1 - x0);
}

std::vector<double> toVector(const QJsonArray &a) {
  std::vector<double> v;
  for (const QJsonValue &x : a)
    v.push_back(x.toDouble());
  return v;
}

QJsonArray toArray(const std::vector<double> &v) {
  QJsonArray a;
  for (double x : v)
    a.append(x);
  return a;
}
} // namespace

double PeriodMap::convert(double m) const {
  if (mFrom.empty())
    return m;
  if (m < mFrom.front())
    return m + (mTo.front() - mFrom.front());
  if (m > mFrom.back())
    return m + (mTo.back() - mFrom.back());
  return interp(m, mFrom, mTo);
}

bool PeriodMap::contains(double epoch) const {
  if (epoch >= toEpoch(end))
    return false;
  return !start || epoch >= toEpoch(*start);
}

void MagnitudeConversion::apply(Catalog &catalog) const {
  catalog.originalMagnitude = catalog.magnitudes();
  for (analysis::Event &e : catalog.events) {
    double m = e.mag;
    for (const PeriodMap &p : periods)
      if (p.contains(e.epoch)) {
        m = p.convert(m);
        break;
      }
    // Re-bin so the b-value bin correction stays valid.
    e.mag = roundToStep(m, magBin);
  }
}

QJsonObject MagnitudeConversion::toJson() const {
  QJsonObject o;
  QJsonArray ps;
  for (const PeriodMap &p : periods) {
    QJsonObject po;
    po.insert(QStringLiteral("start"),
              p.start ? QJsonValue(isoText(*p.start)) : QJsonValue());
    po.insert(QStringLiteral("end"), isoText(p.end));
    po.insert(QStringLiteral("m_from"), toArray(p.mFrom));
    po.insert(QStringLiteral("m_to"), toArray(p.mTo));
    ps.append(po);
  }
  o.insert(QStringLiteral("periods"), ps);
  o.insert(QStringLiteral("reference"),
           QJsonArray{isoText(referenceStart), isoText(referenceEnd)});
  o.insert(QStringLiteral("mag_bin"), magBin);
  o.insert(QStringLiteral("note"), note);
  return o;
}

MagnitudeConversion MagnitudeConversion::fromJson(const QJsonObject &o) {
  if (!o.contains(QStringLiteral("periods")) ||
      !o.contains(QStringLiteral("reference")))
    throw std::runtime_error("not a magnitude conversion: periods and "
                             "reference are required");
  MagnitudeConversion c;
  for (const QJsonValue &v : o.value(QStringLiteral("periods")).toArray()) {
    const QJsonObject po = v.toObject();
    PeriodMap p;
    const QJsonValue s = po.value(QStringLiteral("start"));
    if (!s.isNull() && !s.isUndefined())
      p.start = isoUtc(s.toString());
    p.end = isoUtc(po.value(QStringLiteral("end")).toString());
    p.mFrom = toVector(po.value(QStringLiteral("m_from")).toArray());
    p.mTo = toVector(po.value(QStringLiteral("m_to")).toArray());
    if (p.mFrom.size() != p.mTo.size())
      throw std::runtime_error("m_from and m_to differ in length");
    c.periods.push_back(p);
  }
  const QJsonArray ref = o.value(QStringLiteral("reference")).toArray();
  if (ref.size() != 2)
    throw std::runtime_error("reference must hold two dates");
  c.referenceStart = isoUtc(ref.at(0).toString());
  c.referenceEnd = isoUtc(ref.at(1).toString());
  c.magBin = o.value(QStringLiteral("mag_bin")).toDouble(0.1);
  c.note = o.value(QStringLiteral("note")).toString();
  return c;
}

MagnitudeConversion MagnitudeConversion::load(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(("cannot read " + path).toStdString());
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject())
    throw std::runtime_error((path + ": " + err.errorString()).toStdString());
  return fromJson(doc.object());
}

bool MagnitudeConversion::save(const QString &path) const {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
  return true;
}

MagnitudeConversion MagnitudeConversion::afadDefault() {
  initCbiResources();
  return load(QStringLiteral(":/cbi/afad_magnitude_map.json"));
}

double maxCurvatureMc(const std::vector<double> &magnitudes, double magBin,
                      double correction) {
  if (magnitudes.empty())
    return std::nan("");
  std::map<double, int> counts; // sorted: ties go to the lowest bin
  for (double m : magnitudes)
    counts[roundToStep(m, magBin)]++;
  double best = counts.begin()->first;
  int bestCount = 0;
  for (const auto &[bin, count] : counts)
    if (count > bestCount) {
      best = bin;
      bestCount = count;
    }
  return best + correction;
}

namespace {
struct Rates {
  std::vector<double> rate;
  std::vector<int> count;
};

Rates exceedance(std::vector<double> mags, double years,
                 const std::vector<double> &grid) {
  std::sort(mags.begin(), mags.end());
  Rates r;
  for (double g : grid) {
    const auto it = std::lower_bound(mags.begin(), mags.end(), g - 1e-9);
    const int c = int(mags.end() - it);
    r.count.push_back(c);
    r.rate.push_back(years > 0 ? c / years : std::nan(""));
  }
  return r;
}

std::vector<double> magsIn(const Catalog &c, std::pair<double, double> w) {
  std::vector<double> m;
  for (const analysis::Event &e : c.events)
    if (e.epoch >= w.first && e.epoch < w.second)
      m.push_back(e.mag);
  return m;
}

// pandas: (end - start).days / 365.25, whole days.
double yearsOf(std::pair<double, double> w) {
  return std::floor((w.second - w.first) / 86400.0) / 365.25;
}
} // namespace

std::pair<std::vector<double>, std::vector<double>>
fitRateMatching(const Catalog &catalog, std::pair<double, double> period,
                std::pair<double, double> reference, int minCount,
                double magBin) {
  const std::vector<double> mp = magsIn(catalog, period),
                            mr = magsIn(catalog, reference);
  std::vector<double> grid;
  for (double g = 0.0; g <= 9.0 + magBin / 2; g += magBin)
    grid.push_back(roundDecimals(g, 2));
  const Rates rp = exceedance(mp, yearsOf(period), grid);
  const Rates rr = exceedance(mr, yearsOf(reference), grid);
  if (mp.empty() || mr.empty())
    return {};
  const double mcP = maxCurvatureMc(mp, magBin),
               mcR = maxCurvatureMc(mr, magBin);

  // Reference log-rate is decreasing in m; reversed it is increasing.
  std::vector<double> logR, mRef;
  for (size_t i = grid.size(); i-- > 0;)
    if (grid[i] >= mcR && rr.count[i] >= minCount) {
      logR.push_back(std::log10(rr.rate[i]));
      mRef.push_back(grid[i]);
    }
  if (logR.empty())
    return {};
  const double lrMin = *std::min_element(logR.begin(), logR.end());
  const double lrMax = *std::max_element(logR.begin(), logR.end());
  std::vector<double> from, to;
  for (size_t i = 0; i < grid.size(); ++i) {
    if (!(grid[i] >= mcP && rp.count[i] >= minCount))
      continue;
    const double lr = std::log10(rp.rate[i]);
    if (lr >= lrMin && lr <= lrMax) {
      from.push_back(grid[i]);
      to.push_back(interp(lr, logR, mRef));
    }
  }
  double running = -1e300;
  for (double &t : to) {
    running = std::max(running, t);
    t = roundDecimals(running, 3);
  }
  return {from, to};
}

AfadPeriods afadPeriods() {
  AfadPeriods p;
  const QDateTime van = isoUtc(QStringLiteral("2011-10-23")),
                  ml = isoUtc(QStringLiteral("2013-11-20"));
  p.periods = {{std::nullopt, van}, {van, ml}};
  p.calibration = {{isoUtc(QStringLiteral("2007-01-01")), van}, {van, ml}};
  p.reference = {isoUtc(QStringLiteral("2014-01-01")),
                 isoUtc(QStringLiteral("2020-01-01"))};
  return p;
}

MagnitudeConversion calibrateAfadConversion(const Catalog &national,
                                            int minCount, QStringList *log) {
  const AfadPeriods ap = afadPeriods();
  const double from = toEpoch(isoUtc(QStringLiteral("2006-01-01")));
  const double until = toEpoch(isoUtc(QStringLiteral("2020-06-01")));
  std::vector<bool> keep;
  for (const analysis::Event &e : national.events)
    keep.push_back(e.epoch >= from && e.epoch < until);
  Catalog cat = national.filtered(keep);
  if (log)
    *log << QStringLiteral("Declustering %1 events (Gardner-Knopoff)...")
                .arg(cat.size());
  const Catalog independent = cat.filtered(gardnerKnopoff(cat));
  if (log)
    *log << QStringLiteral("%1 independent events").arg(independent.size());

  MagnitudeConversion c;
  c.referenceStart = ap.reference.first;
  c.referenceEnd = ap.reference.second;
  const std::pair<double, double> ref{toEpoch(ap.reference.first),
                                      toEpoch(ap.reference.second)};
  for (size_t i = 0; i < ap.periods.size(); ++i) {
    const auto &cal = ap.calibration[i];
    const std::pair<double, double> w{toEpoch(cal.first), toEpoch(cal.second)};
    auto [mFrom, mTo] = fitRateMatching(independent, w, ref, minCount);
    if (mFrom.empty())
      throw std::runtime_error(QStringLiteral("no overlap of rates for %1..%2")
                                   .arg(isoText(cal.first), isoText(cal.second))
                                   .toStdString());
    PeriodMap p;
    p.start = ap.periods[i].first;
    p.end = ap.periods[i].second;
    p.mFrom = mFrom;
    p.mTo = mTo;
    c.periods.push_back(p);
    if (log) {
      *log << QStringLiteral("%1..%2 -> reference %3..%4")
                  .arg(isoText(cal.first), isoText(cal.second),
                       isoText(ap.reference.first),
                       isoText(ap.reference.second));
      for (size_t k = 0; k < mFrom.size(); ++k)
        *log << QStringLiteral("  %1 -> %2")
                    .arg(QString::number(mFrom[k], 'f', 1),
                         QString::number(mTo[k], 'f', 3));
    }
  }
  c.note = QStringLiteral(
      "Rate-matching conversion of AFAD magnitudes before 2013-11-20 onto "
      "the 2014-2019 ML/Mw practice; declustered (Gardner-Knopoff), fitted "
      "in sismokaos-gui.");
  return c;
}

} // namespace skg::cbi
