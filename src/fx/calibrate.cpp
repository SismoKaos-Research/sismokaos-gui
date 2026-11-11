#include "fx/calibrate.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "fx/catalog.h"
#include "fx/dsp.h"
#include "fx/features.h"
#include <utility>

namespace skg::fx {

namespace {
constexpr int kMinQuietWindows = 10;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

QJsonArray arr2(std::array<double, 2> a) { return {a[0], a[1]}; }
QJsonArray arr3(std::array<double, 3> a) { return {a[0], a[1], a[2]}; }
std::array<double, 2> read2(const QJsonArray &a) {
  return {a.at(0).toDouble(), a.at(1).toDouble()};
}
std::array<double, 3> read3(const QJsonArray &a) {
  return {a.at(0).toDouble(), a.at(1).toDouble(), a.at(2).toDouble()};
}

// SplitMix64, seeded, so a rerun picks the same windows.
struct Rng {
  quint64 s;
  size_t below(size_t n) {
    s += 0x9E3779B97F4A7C15ull;
    quint64 z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return size_t((z ^ (z >> 31)) % quint64(n));
  }
};

double stdDev(const std::vector<double> &x) {
  const double n = double(x.size());
  double mean = 0;
  for (double v : x)
    mean += v;
  mean /= n;
  double s = 0;
  for (double v : x)
    s += (v - mean) * (v - mean);
  return std::sqrt(s / (n - 1.0));
}

std::vector<double> zscorePop(const std::vector<double> &x) {
  const double n = double(x.size());
  double mean = 0;
  for (double v : x)
    mean += v;
  mean /= n;
  double s = 0;
  for (double v : x)
    s += (v - mean) * (v - mean);
  const double sd = std::sqrt(s / n);
  std::vector<double> out(x.size());
  for (size_t i = 0; i < x.size(); ++i)
    out[i] = sd > 0 ? (x[i] - mean) / sd : x[i];
  return out;
}

double median(std::vector<double> v) { return percentile(std::move(v), 50.0); }
std::array<double, 2> iqr(const std::vector<double> &v) {
  return {percentile(v, 25.0), percentile(v, 75.0)};
}
} // namespace

QString CalibrationError::code(Reason reason) {
  switch (reason) {
  case Reason::NoData:
    return QStringLiteral("no_data");
  case Reason::LittleData:
    return QStringLiteral("little_data");
  case Reason::GappyData:
    return QStringLiteral("gappy_data");
  case Reason::Events:
    return QStringLiteral("events");
  }
  return {};
}

double percentile(std::vector<double> v, double q) {
  if (v.empty())
    return kNaN;
  std::sort(v.begin(), v.end());
  const double pos = q / 100.0 * double(v.size() - 1);
  const size_t lo = size_t(std::floor(pos)), hi = size_t(std::ceil(pos));
  return v[lo] + (v[hi] - v[lo]) * (pos - double(lo));
}

double meanFrequency(const std::vector<double> &x, double fs) {
  const size_t n = x.size();
  if (n < 4)
    return kNaN;
  double mean = 0;
  for (double v : x)
    mean += v;
  mean /= double(n);
  std::vector<std::complex<double>> buf(n);
  for (size_t i = 0; i < n; ++i) {
    const double w =
        0.5 - 0.5 * std::cos(2 * std::numbers::pi * double(i) / double(n - 1));
    buf[i] = std::complex<double>((x[i] - mean) * w, 0.0);
  }
  fft(buf);
  double num = 0, den = 0;
  for (size_t k = 1; k <= n / 2; ++k) {
    const double p = std::norm(buf[k]);
    num += double(k) * fs / double(n) * p;
    den += p;
  }
  return den > 0 ? num / den : kNaN;
}

QJsonObject Calibration::toJson() const {
  QJsonObject o;
  o.insert("station", station);
  o.insert("fs", fs);
  o.insert("tau", tau);
  o.insert("dim", dim);
  o.insert("mean_period", meanPeriod);
  o.insert("sampent_r", sampentR);
  o.insert("sampent_tol", arr3(sampentTol));
  o.insert("spread", QJsonObject{{"tau_iqr", arr2(tauIqr)},
                                 {"dim_iqr", arr2(dimIqr)},
                                 {"mean_period_iqr", arr2(meanPeriodIqr)},
                                 {"quiet_std", arr3(quietStd)}});
  o.insert("windows",
           QJsonObject{{"total", windowsTotal},
                       {"quiet", windowsQuiet},
                       {"sampled", windowsSampled},
                       {"loud", QJsonArray{windowsLoud[0], windowsLoud[1],
                                           windowsLoud[2]}},
                       {"measured", measured},
                       {"with_tau", withTau}});
  o.insert("station_lat", stationLat);
  o.insert("station_lon", stationLon);
  o.insert("exclusion_intervals", exclusionIntervals);
  o.insert("settings", settings.toJson());
  return o;
}

Calibration Calibration::fromJson(const QJsonObject &o) {
  if (!o.contains("tau") || !o.contains("fs"))
    throw std::runtime_error("not a calibration file");
  Calibration c;
  c.station = o.value("station").toString();
  c.fs = o.value("fs").toDouble();
  c.tau = o.value("tau").toInt();
  c.dim = o.value("dim").toInt();
  c.meanPeriod = o.value("mean_period").toDouble();
  c.sampentR = o.value("sampent_r").toDouble();
  c.sampentTol = read3(o.value("sampent_tol").toArray());
  const QJsonObject spread = o.value("spread").toObject();
  c.tauIqr = read2(spread.value("tau_iqr").toArray());
  c.dimIqr = read2(spread.value("dim_iqr").toArray());
  c.meanPeriodIqr = read2(spread.value("mean_period_iqr").toArray());
  c.quietStd = read3(spread.value("quiet_std").toArray());
  const QJsonObject w = o.value("windows").toObject();
  c.windowsTotal = w.value("total").toInt();
  c.windowsQuiet = w.value("quiet").toInt();
  c.windowsSampled = w.value("sampled").toInt();
  const QJsonArray loud = w.value("loud").toArray();
  c.windowsLoud = {loud.at(0).toInt(), loud.at(1).toInt(), loud.at(2).toInt()};
  c.measured = w.value("measured").toInt();
  c.withTau = w.value("with_tau").toInt();
  c.stationLat = o.value("station_lat").toDouble();
  c.stationLon = o.value("station_lon").toDouble();
  c.exclusionIntervals = o.value("exclusion_intervals").toInt();
  c.settings = CalibrationConfig::fromJson(o.value("settings").toObject());
  return c;
}

Calibration Calibration::load(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(("cannot read " + path).toStdString());
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  if (!doc.isObject())
    throw std::runtime_error((path + " is not JSON").toStdString());
  return fromJson(doc.object());
}

bool Calibration::save(const QString &path) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
  return f.commit();
}

void Calibration::apply(Config &cfg) const {
  cfg.chaosTau = tau;
  cfg.chaosDim = dim;
  cfg.chaosMeanPeriod = meanPeriod;
  cfg.chaosSampentR = sampentR;
  cfg.chaosSampentTol = sampentTol;
}

QStringList Calibration::describe() const {
  return {QStringLiteral(" Quiet windows: %1 of %2 (%3 sampled; dropped as "
                         "loud E/N/Z: %4/%5/%6)")
              .arg(windowsQuiet)
              .arg(windowsTotal)
              .arg(windowsSampled)
              .arg(windowsLoud[0])
              .arg(windowsLoud[1])
              .arg(windowsLoud[2]),
          QStringLiteral(" tau         : %1 samples (IQR %2-%3)")
              .arg(tau)
              .arg(tauIqr[0], 0, 'f', 1)
              .arg(tauIqr[1], 0, 'f', 1),
          QStringLiteral(" dim         : %1 (IQR %2-%3)")
              .arg(dim)
              .arg(dimIqr[0], 0, 'f', 1)
              .arg(dimIqr[1], 0, 'f', 1),
          QStringLiteral(" mean period : %1 s (IQR %2-%3)")
              .arg(meanPeriod, 0, 'f', 3)
              .arg(meanPeriodIqr[0], 0, 'f', 3)
              .arg(meanPeriodIqr[1], 0, 'f', 3),
          QStringLiteral(" SampEn tol  : E %1, N %2, Z %3 (r = %4)")
              .arg(sampentTol[0], 0, 'e', 4)
              .arg(sampentTol[1], 0, 'e', 4)
              .arg(sampentTol[2], 0, 'e', 4)
              .arg(sampentR)};
}

Calibration calibrate(const Config &cfg, const PreprocessOptions &opt,
                      const ProgressFn &progress) {
  const CalibrationConfig &opts = cfg.calibration;
  auto say = [&](const QString &m) {
    if (opt.log)
      opt.log(m);
  };
  std::pair<double, double> location;
  if (opts.stationLat && opts.stationLon)
    location = {*opts.stationLat, *opts.stationLon};
  else {
    if (opts.stationCoords.isEmpty())
      throw std::runtime_error(
          "no station list set, so the station's quiet periods cannot be "
          "found; set "
          "the station's latitude and longitude or a station list");
    const auto found = stationCoords(opts.stationCoords, cfg.station);
    if (!found)
      throw std::runtime_error(
          QStringLiteral("station %1 is not in %2, so its quiet periods cannot "
                         "be found; set its latitude "
                         "and longitude, or reuse another run's calibration")
              .arg(cfg.station, opts.stationCoords)
              .toStdString());
    location = *found;
  }
  if (opts.catalog.isEmpty())
    throw std::runtime_error("no earthquake catalog set for the calibration");
  const std::vector<CatalogEvent> events = loadCatalog(opts.catalog);
  const Exclusions exclusions(events, location, opts);
  say(QStringLiteral(" Catalog: %1 events, %2 exclusion intervals within %3 km "
                     "of %4 (M >= %5)")
          .arg(events.size())
          .arg(exclusions.size())
          .arg(opts.maxDistKm)
          .arg(cfg.station)
          .arg(opts.minMag));

  const int win = cfg.winSize();
  Calibration cal;
  cal.station = cfg.station;
  cal.settings = opts;
  cal.stationLat = location.first;
  cal.stationLon = location.second;
  cal.exclusionIntervals = exclusions.size();
  Rng rng{opts.seed};
  std::vector<std::array<std::vector<double>, 3>> reservoir;
  reservoir.reserve(size_t(opts.maxWindows));
  double fs = cfg.fs;
  double spanLo = std::numeric_limits<double>::infinity(), spanHi = -spanLo;
  std::array<std::vector<double>, 3> buf;
  std::array<std::vector<bool>, 3> interp;
  double bufEpoch = kNaN;
  double secondsRead = 0;
  std::array<double, 3> secondsGood{};
  int windowsGaps = 0, windowsEvents = 0;

  const auto readThrough = [&](const ChunkSink &sink) {
    try {
      return preprocessDirectory(cfg.dataDir, cfg, opt, sink);
    } catch (const NoDataInPeriod &e) {
      // The same thing to say, and to do about it, as when the files could
      // be read and held nothing usable: the station has no data to
      // calibrate on in the period.
      throw CalibrationError(CalibrationError::Reason::NoData,
                             QStringLiteral("%1, so there is nothing to "
                                            "calibrate on. Check the "
                                            "station's coverage, or get its "
                                            "waveforms for the period")
                                 .arg(QString::fromUtf8(e.what())));
    }
  };
  const bool readAll = readThrough([&](ChannelChunk &&chunk) {
    fs = chunk.fs;
    if (progress)
      progress(chunk.recordsDone, chunk.recordsTotal);
    // How much was read, and how much of it is real (not missing, not
    // filled in), for the message when it turns out to be too little.
    if (chunk.fs > 0) {
      secondsRead += double(chunk.e.size()) / chunk.fs;
      const std::vector<double> *comp[3] = {&chunk.e, &chunk.n, &chunk.z};
      const std::vector<bool> *fill[3] = {&chunk.eInterp, &chunk.nInterp,
                                          &chunk.zInterp};
      for (int c = 0; c < 3; ++c) {
        size_t good = 0;
        for (size_t i = 0; i < comp[c]->size(); ++i)
          good += std::isfinite((*comp[c])[i]) &&
                  !(i < fill[c]->size() && (*fill[c])[i]);
        secondsGood[c] += double(good) / chunk.fs;
      }
    }
    const double expected = bufEpoch + double(buf[0].size()) / fs;
    if (std::isnan(bufEpoch) || buf[0].empty() ||
        std::abs(chunk.startEpoch - expected) > 1.5 / fs) {
      for (auto &b : buf)
        b.clear();
      for (auto &m : interp)
        m.clear();
      bufEpoch = chunk.startEpoch;
    }
    buf[0].insert(buf[0].end(), chunk.e.begin(), chunk.e.end());
    buf[1].insert(buf[1].end(), chunk.n.begin(), chunk.n.end());
    buf[2].insert(buf[2].end(), chunk.z.begin(), chunk.z.end());
    interp[0].insert(interp[0].end(), chunk.eInterp.begin(),
                     chunk.eInterp.end());
    interp[1].insert(interp[1].end(), chunk.nInterp.begin(),
                     chunk.nInterp.end());
    interp[2].insert(interp[2].end(), chunk.zInterp.begin(),
                     chunk.zInterp.end());
    size_t cursor = 0;
    while (buf[0].size() - cursor >= size_t(win)) {
      const double t0 = bufEpoch + double(cursor) / fs,
                   t1 = t0 + double(win) / fs;
      const size_t from = cursor;
      cursor += size_t(win);
      ++cal.windowsTotal;
      spanLo = std::min(spanLo, t0);
      spanHi = std::max(spanHi, t1);
      bool clean = true;
      for (int c = 0; c < 3 && clean; ++c)
        for (size_t i = from; i < from + size_t(win) && clean; ++i)
          clean = std::isfinite(buf[c][i]) && !interp[c][i];
      if (!clean) {
        ++windowsGaps;
        continue;
      }
      if (exclusions.overlaps(t0, t1)) {
        ++windowsEvents;
        continue;
      }
      ++cal.windowsQuiet;
      auto window = [&] {
        std::array<std::vector<double>, 3> w;
        for (int c = 0; c < 3; ++c)
          w[c].assign(buf[c].begin() + from, buf[c].begin() + from + win);
        return w;
      };
      if (int(reservoir.size()) < opts.maxWindows)
        reservoir.push_back(window());
      else {
        const size_t k = rng.below(size_t(cal.windowsQuiet));
        if (k < size_t(opts.maxWindows))
          reservoir[k] = window();
      }
    }
    for (auto &b : buf)
      b.erase(b.begin(), b.begin() + cursor);
    for (auto &m : interp)
      m.erase(m.begin(), m.begin() + cursor);
    bufEpoch += double(cursor) / fs;
  });
  // Stopped part-way: what was read so far is no calibration, and too few
  // windows of it would be reported as too little data. Neither.
  if (!readAll)
    throw Stopped();

  cal.fs = fs;
  cal.windowsSampled = int(reservoir.size());
  if (std::isfinite(spanLo) &&
      exclusions.countWithin(spanLo - 86400, spanHi + 86400) == 0)
    say(QStringLiteral(" Warning: no catalog event near the station falls "
                       "within the data; check that the "
                       "catalog covers these dates"));
  if (int(reservoir.size()) < kMinQuietWindows) {
    // Say which of the three things it was: no data to speak of, data full
    // of gaps, or data the catalog's earthquakes rule out. The advice differs.
    const double winSeconds = fs > 0 ? double(win) / fs : 0.0;
    const auto hours = [](double seconds) {
      return QString::number(seconds / 3600.0, 'f', seconds < 36000 ? 1 : 0);
    };
    const QString read =
        QStringLiteral("%1 h of data were read for %2 from %3 file(s) (E %4 h, "
                       "N %5 h, Z %6 h)")
            .arg(hours(secondsRead), cfg.station)
            .arg(cfg.inputFiles.isEmpty() ? 1 : int(cfg.inputFiles.size()))
            .arg(hours(secondsGood[0]), hours(secondsGood[1]),
                 hours(secondsGood[2]));
    if (cal.windowsTotal < kMinQuietWindows) {
      const bool none = secondsRead <= 0.0;
      throw CalibrationError(
          none ? CalibrationError::Reason::NoData
               : CalibrationError::Reason::LittleData,
          none ? QStringLiteral(
                     "no waveform data could be read for %1 from the %2 "
                     "file(s) given: the station has no usable data in the "
                     "period, so there is nothing to calibrate on. Check the "
                     "station's coverage, or get its waveforms for the period")
                     .arg(cfg.station)
                     .arg(cfg.inputFiles.isEmpty() ? 1
                                                   : int(cfg.inputFiles.size()))
               : QStringLiteral(
                     "too little waveform data to calibrate on: %1, which "
                     "gives %2 window(s) of %3 s where at least %4 quiet ones "
                     "are needed. The period has too little data for this "
                     "station; get more of its waveforms, or reuse another "
                     "run's calibration")
                     .arg(read)
                     .arg(cal.windowsTotal)
                     .arg(winSeconds, 0, 'f', 0)
                     .arg(kMinQuietWindows));
    }
    if (windowsGaps >= windowsEvents)
      throw CalibrationError(
          CalibrationError::Reason::GappyData,
          QStringLiteral(
              "only %1 quiet windows found (need %2): of %3 windows, %4 hold "
              "gaps or interpolated samples and %5 fall near a catalog "
              "earthquake (%6). The data of the period is too broken up for "
              "this station; get more of its waveforms, or reuse another "
              "run's calibration")
              .arg(reservoir.size())
              .arg(kMinQuietWindows)
              .arg(cal.windowsTotal)
              .arg(windowsGaps)
              .arg(windowsEvents)
              .arg(read));
    throw CalibrationError(
        CalibrationError::Reason::Events,
        QStringLiteral(
            "only %1 quiet windows found (need %2): of %3 windows, %4 fall "
            "near a catalog earthquake (%5 exclusion intervals within %6 km, "
            "M >= %7) and %8 hold gaps. Raise min_mag or lower max_dist_km in "
            "the recipe's calibration settings, or reuse another run's "
            "calibration")
            .arg(reservoir.size())
            .arg(kMinQuietWindows)
            .arg(cal.windowsTotal)
            .arg(windowsEvents)
            .arg(exclusions.size())
            .arg(opts.maxDistKm)
            .arg(opts.minMag)
            .arg(windowsGaps));
  }

  // Loudness screening per component: one channel can glitch alone.
  std::vector<std::array<double, 3>> stds;
  for (const auto &w : reservoir)
    stds.push_back({stdDev(w[0]), stdDev(w[1]), stdDev(w[2])});
  std::vector<std::array<bool, 3>> keep(reservoir.size(), {true, true, true});
  for (int c = 0; c < 3; ++c) {
    std::vector<double> col;
    for (const auto &s : stds)
      col.push_back(s[c]);
    const double limit = opts.loudFactor * median(col);
    std::vector<double> kept;
    for (size_t i = 0; i < stds.size(); ++i) {
      keep[i][c] = stds[i][c] <= limit;
      if (keep[i][c])
        kept.push_back(stds[i][c]);
      else
        ++cal.windowsLoud[c];
    }
    cal.quietStd[c] = median(kept);
  }

  std::vector<double> taus, periods, dims;
  struct Seg {
    std::vector<double> z;
    const std::vector<double> *raw;
  };
  std::vector<Seg> segments;
  for (size_t i = 0; i < reservoir.size(); ++i)
    for (int c = 0; c < 3; ++c)
      if (keep[i][c])
        segments.push_back({zscorePop(reservoir[i][c]), &reservoir[i][c]});
  cal.measured = int(segments.size());
  for (const Seg &s : segments) {
    if (opt.shouldStop && opt.shouldStop())
      throw Stopped();
    if (const auto t = amiTau(s.z, opts.maxTau))
      taus.push_back(double(*t));
    const double mf = meanFrequency(*s.raw, fs);
    if (std::isfinite(mf) && mf > 0)
      periods.push_back(1.0 / mf);
  }
  cal.withTau = int(taus.size());
  if (int(taus.size()) < kMinQuietWindows)
    throw std::runtime_error(
        QStringLiteral("the average mutual information had a minimum below "
                       "max_tau = %1 in only %2 quiet "
                       "window-components; raise max_tau")
            .arg(opts.maxTau)
            .arg(taus.size())
            .toStdString());
  cal.tau = std::max(1, int(std::round(median(taus))));
  for (const Seg &s : segments) {
    if (opt.shouldStop && opt.shouldStop())
      throw Stopped();
    if (const auto d = fnnDim(s.z, cal.tau, opts.maxDim))
      dims.push_back(double(*d));
  }
  if (dims.empty())
    throw std::runtime_error(
        "no quiet window was long enough for False Nearest Neighbours");
  cal.dim = std::max(1, int(std::round(median(dims))));
  cal.meanPeriod = median(periods);
  cal.sampentR = cfg.chaosSampentR;
  for (int c = 0; c < 3; ++c)
    cal.sampentTol[c] = cfg.chaosSampentR * cal.quietStd[c];
  cal.tauIqr = iqr(taus);
  cal.dimIqr = iqr(dims);
  cal.meanPeriodIqr = iqr(periods);
  return cal;
}

} // namespace skg::fx
