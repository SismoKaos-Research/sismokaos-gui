#include "fx/config.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <cmath>
#include <stdexcept>

namespace skg::fx {

namespace {

// The first key present, Rust name first, then the Python tool's alias.
QJsonValue pick(const QJsonObject &o,
                std::initializer_list<const char *> keys) {
  for (const char *k : keys)
    if (o.contains(QLatin1String(k)))
      return o.value(QLatin1String(k));
  return {};
}

double num(const QJsonObject &o, std::initializer_list<const char *> keys,
           double fallback) {
  const QJsonValue v = pick(o, keys);
  return v.isDouble() ? v.toDouble() : fallback;
}

int integer(const QJsonObject &o, std::initializer_list<const char *> keys,
            int fallback) {
  const QJsonValue v = pick(o, keys);
  return v.isDouble() ? v.toInt() : fallback;
}

QString text(const QJsonObject &o, std::initializer_list<const char *> keys,
             const QString &fallback) {
  const QJsonValue v = pick(o, keys);
  return v.isString() ? v.toString() : fallback;
}

} // namespace

QJsonObject CalibrationConfig::toJson() const {
  QJsonObject o;
  o.insert("catalog", catalog);
  o.insert("station_coords", stationCoords);
  o.insert("station_lat",
           stationLat ? QJsonValue(*stationLat) : QJsonValue::Null);
  o.insert("station_lon",
           stationLon ? QJsonValue(*stationLon) : QJsonValue::Null);
  o.insert("min_mag", minMag);
  o.insert("max_dist_km", maxDistKm);
  o.insert("pre_sec", preSec);
  o.insert("min_post_sec", minPostSec);
  o.insert("coda_factor", codaFactor);
  o.insert("max_windows", maxWindows);
  o.insert("loud_factor", loudFactor);
  o.insert("seed", double(seed));
  o.insert("max_tau", maxTau);
  o.insert("max_dim", maxDim);
  return o;
}

CalibrationConfig CalibrationConfig::fromJson(const QJsonObject &o) {
  CalibrationConfig c;
  c.catalog = text(o, {"catalog"}, c.catalog);
  c.stationCoords = text(o, {"station_coords"}, c.stationCoords);
  if (o.value("station_lat").isDouble())
    c.stationLat = o.value("station_lat").toDouble();
  if (o.value("station_lon").isDouble())
    c.stationLon = o.value("station_lon").toDouble();
  c.minMag = num(o, {"min_mag"}, c.minMag);
  c.maxDistKm = num(o, {"max_dist_km"}, c.maxDistKm);
  c.preSec = num(o, {"pre_sec"}, c.preSec);
  c.minPostSec = num(o, {"min_post_sec"}, c.minPostSec);
  c.codaFactor = num(o, {"coda_factor"}, c.codaFactor);
  c.maxWindows = integer(o, {"max_windows"}, c.maxWindows);
  c.loudFactor = num(o, {"loud_factor"}, c.loudFactor);
  c.seed = quint64(num(o, {"seed"}, double(c.seed)));
  c.maxTau = integer(o, {"max_tau"}, c.maxTau);
  c.maxDim = integer(o, {"max_dim"}, c.maxDim);
  return c;
}

QString Band::label() const {
  return name.isEmpty() ? QStringLiteral("%1-%2Hz").arg(freqmin).arg(freqmax)
                        : name;
}

QList<Config> Config::bandRuns() const {
  if (bands.isEmpty()) {
    if (!bandFilter.isEmpty())
      throw std::invalid_argument(
          "a band was selected, but the config defines no bands");
    return {*this};
  }
  QStringList labels;
  for (const Band &b : bands)
    labels << b.label();
  for (const QString &f : bandFilter)
    if (!labels.contains(f))
      throw std::invalid_argument(
          QStringLiteral("no band named '%1'; the config defines %2")
              .arg(f, labels.join(", "))
              .toStdString());
  QList<Config> out;
  for (const Band &b : bands) {
    const QString label = b.label();
    if (!bandFilter.isEmpty() && !bandFilter.contains(label))
      continue;
    Config c = *this;
    c.bands.clear();
    c.freqmin = b.freqmin;
    c.freqmax = b.freqmax;
    c.fs = b.fs;
    c.winSec = b.winSec;
    c.stepSec = b.stepSec;
    c.staSec = b.staSec;
    c.ltaSec = b.ltaSec;
    c.spectralSplitHz = b.spectralSplitHz ? *b.spectralSplitHz
                                          : std::sqrt(b.freqmin * b.freqmax);
    c.outputRoot = outputRoot.isEmpty() ? label : outputRoot + '/' + label;
    c.band = label;
    out << c;
  }
  return out;
}

QStringList Config::validate() const {
  QStringList problems;
  if (fs <= 0) {
    problems << QStringLiteral("fs must be positive, got %1").arg(fs);
    return problems;
  }
  if (freqmin <= 0 || freqmax <= freqmin)
    problems << QStringLiteral(
                    "need 0 < freqmin < freqmax, got freqmin=%1 freqmax=%2")
                    .arg(freqmin)
                    .arg(freqmax);
  // The bandpass is the anti-alias filter: decimation follows it directly.
  const double nyquist = fs / 2;
  if (freqmax >= nyquist)
    problems << QStringLiteral("freqmax %1 Hz is at or above the output "
                               "Nyquist (%2 Hz for fs=%3). "
                               "The bandpass doubles as the anti-alias filter, "
                               "so everything above "
                               "%2 Hz would alias back into the band "
                               "undetectably. Lower freqmax or "
                               "raise fs.")
                    .arg(freqmax)
                    .arg(nyquist)
                    .arg(fs);
  if (winSec <= 0 || stepSec <= 0)
    problems << QStringLiteral("win_sec and step_sec must both be positive");
  QStringList labels;
  for (const Band &b : bands)
    labels << b.label();
  QStringList sorted = labels;
  sorted.sort();
  for (int i = 1; i < sorted.size(); ++i)
    if (sorted[i] == sorted[i - 1]) {
      problems << QStringLiteral("band names must be unique, got %1")
                      .arg(labels.join(", "));
      break;
    }
  if (!calibrationFile.isEmpty() && bands.size() > 1 && bandFilter.size() != 1)
    problems << QStringLiteral(
        "a calibration file holds one band's parameters; pick that band");
  const auto [lo, hi] = chaosRosRise;
  if (!(0.0 <= lo && lo < hi && hi <= 1.0))
    problems
        << QStringLiteral(
               "chaos_ros_rise must satisfy 0 <= lo < hi <= 1, got [%1, %2]")
               .arg(lo)
               .arg(hi);
  if (chaosRosHorizon <= 0)
    problems << QStringLiteral("chaos_ros_horizon must be positive, got %1")
                    .arg(chaosRosHorizon);
  if (chaosTau < 1 || chaosDim < 1 || chaosEvolve < 1)
    problems << QStringLiteral(
        "chaos_tau, chaos_dim and chaos_evolve must be at least 1");
  return problems;
}

QStringList Config::warnings() const {
  QStringList out;
  if (fs > 0 && freqmax < fs / 2 && freqmax > fs / 2 * 0.9)
    out << QStringLiteral("freqmax %1 Hz sits within 10%% of the output "
                          "Nyquist (%2 Hz); a "
                          "4th-order bandpass has not fully rolled off by "
                          "then, some aliasing "
                          "is likely")
               .arg(freqmax)
               .arg(fs / 2);
  if (!chaosOnly && (nsta() == 0 || nlta() >= winSize()))
    out << QStringLiteral(
               "STA/LTA needs 0 < sta_sec*fs and lta_sec*fs < win_size; got %1 "
               "and "
               "%2 samples against %3, so the STA_LTA features will be NaN")
               .arg(nsta())
               .arg(nlta())
               .arg(winSize());
  return out;
}

QString Config::checkDecimation(double nativeFs) const {
  if (nativeFs <= 0)
    return {};
  const int factor = std::max(1, int(nativeFs / fs));
  const double actual = nativeFs / factor;
  if (std::abs(actual - fs) > 1e-9)
    return QStringLiteral("requested fs=%1 Hz is not an exact divisor of the "
                          "native %2 Hz; "
                          "decimating by %3 gives %4 Hz instead, and window "
                          "lengths derived "
                          "from the requested rate will not match the data. "
                          "Use an fs that "
                          "divides %2 exactly.")
        .arg(fs)
        .arg(nativeFs)
        .arg(factor)
        .arg(actual, 0, 'f', 4);
  return {};
}

QJsonObject Config::toJson() const {
  QJsonObject o;
  o.insert("station", station);
  o.insert("fs", fs);
  o.insert("win_sec", winSec);
  o.insert("step_sec", stepSec);
  o.insert("sta_sec", staSec);
  o.insert("lta_sec", ltaSec);
  o.insert("freqmin", freqmin);
  o.insert("freqmax", freqmax);
  o.insert("chaos_tau", chaosTau);
  o.insert("chaos_dim", chaosDim);
  o.insert("chaos_evolve", chaosEvolve);
  o.insert("chaos_min_samples", chaosMinSamples);
  o.insert("chaos_mean_period", chaosMeanPeriod);
  o.insert("chaos_ros_rise", QJsonArray{chaosRosRise[0], chaosRosRise[1]});
  o.insert("chaos_ros_horizon", chaosRosHorizon);
  o.insert("chaos_sampent_m", chaosSampentM);
  o.insert("chaos_sampent_r", chaosSampentR);
  o.insert("chaos_sampent_tol",
           chaosSampentTol ? QJsonValue(QJsonArray{(*chaosSampentTol)[0],
                                                   (*chaosSampentTol)[1],
                                                   (*chaosSampentTol)[2]})
                           : QJsonValue(QJsonValue::Null));
  o.insert("chaos_calibration", calibration.toJson());
  o.insert("chaos_only", chaosOnly);
  o.insert("spectral_split_hz", spectralSplitHz);
  if (!bands.isEmpty()) {
    QJsonArray arr;
    for (const Band &b : bands) {
      QJsonObject bo;
      if (!b.name.isEmpty())
        bo.insert("name", b.name);
      bo.insert("freqmin", b.freqmin);
      bo.insert("freqmax", b.freqmax);
      bo.insert("fs", b.fs);
      bo.insert("win_sec", b.winSec);
      bo.insert("step_sec", b.stepSec);
      bo.insert("sta_sec", b.staSec);
      bo.insert("lta_sec", b.ltaSec);
      if (b.spectralSplitHz)
        bo.insert("spectral_split_hz", *b.spectralSplitHz);
      arr.append(bo);
    }
    o.insert("bands", arr);
  }
  if (!band.isEmpty())
    o.insert("band", band);
  return o;
}

Config Config::fromJson(const QJsonObject &o) {
  Config c;
  c.station = text(o, {"station", "STATION"}, c.station);
  c.fs = num(o, {"fs", "Fs"}, c.fs);
  c.winSec = integer(o, {"win_sec", "WIN_SEC"}, c.winSec);
  c.stepSec = integer(o, {"step_sec", "STEP_SEC"}, c.stepSec);
  c.staSec = num(o, {"sta_sec", "STA_SEC"}, c.staSec);
  c.ltaSec = integer(o, {"lta_sec", "LTA_SEC"}, c.ltaSec);
  c.freqmin = num(o, {"freqmin", "FREQMIN"}, c.freqmin);
  c.freqmax = num(o, {"freqmax", "FREQMAX"}, c.freqmax);
  c.chaosTau = integer(o, {"chaos_tau", "CHAOS_TAU"}, c.chaosTau);
  c.chaosDim = integer(o, {"chaos_dim", "CHAOS_DIM"}, c.chaosDim);
  c.chaosEvolve = integer(o, {"chaos_evolve", "CHAOS_EVOLVE"}, c.chaosEvolve);
  c.chaosMinSamples =
      integer(o, {"chaos_min_samples", "CHAOS_MIN_SAMPLES"}, c.chaosMinSamples);
  c.chaosMeanPeriod =
      num(o, {"chaos_mean_period", "CHAOS_MEAN_PERIOD"}, c.chaosMeanPeriod);
  const QJsonArray rise = o.value("chaos_ros_rise").toArray();
  if (rise.size() == 2)
    c.chaosRosRise = {rise[0].toDouble(), rise[1].toDouble()};
  c.chaosRosHorizon = num(o, {"chaos_ros_horizon"}, c.chaosRosHorizon);
  c.chaosSampentM =
      integer(o, {"chaos_sampent_m", "CHAOS_SAMPENT_M"}, c.chaosSampentM);
  c.chaosSampentR =
      num(o, {"chaos_sampent_r", "CHAOS_SAMPENT_R"}, c.chaosSampentR);
  const QJsonArray tol = o.value("chaos_sampent_tol").toArray();
  if (tol.size() == 3)
    c.chaosSampentTol = std::array<double, 3>{
        tol[0].toDouble(), tol[1].toDouble(), tol[2].toDouble()};
  if (o.value("chaos_calibration").isObject())
    c.calibration =
        CalibrationConfig::fromJson(o.value("chaos_calibration").toObject());
  c.chaosOnly = o.value("chaos_only").toBool(false);
  c.spectralSplitHz = num(o, {"spectral_split_hz"}, c.spectralSplitHz);
  for (const QJsonValue &v : o.value("bands").toArray()) {
    const QJsonObject bo = v.toObject();
    Band b;
    b.name = bo.value("name").toString();
    b.freqmin = bo.value("freqmin").toDouble();
    b.freqmax = bo.value("freqmax").toDouble();
    b.fs = bo.value("fs").toDouble();
    b.winSec = bo.value("win_sec").toInt();
    b.stepSec = bo.value("step_sec").toInt();
    b.staSec = bo.value("sta_sec").toDouble();
    b.ltaSec = bo.value("lta_sec").toInt();
    if (bo.value("spectral_split_hz").isDouble())
      b.spectralSplitHz = bo.value("spectral_split_hz").toDouble();
    c.bands << b;
  }
  c.band = o.value("band").toString();
  return c;
}

Config Config::load(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return Config();
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
  return doc.isObject() ? fromJson(doc.object()) : Config();
}

bool Config::save(const QString &path) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
  return f.commit();
}

} // namespace skg::fx
