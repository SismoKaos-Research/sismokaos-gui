#include "analysis/regionrun.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QTimeZone>

#include <stdexcept>
#include <vector>

#include "analysis/plot.h"

namespace skg::analysis {

namespace {
std::runtime_error fail(const QString &message) {
  return std::runtime_error(message.toStdString());
}

QString sha256(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  QCryptographicHash h(QCryptographicHash::Sha256);
  h.addData(&f);
  return QString::fromLatin1(h.result().toHex());
}

void writeFile(const QString &path, const QByteArray &bytes) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() ||
      !f.commit())
    throw fail(QStringLiteral("cannot write %1").arg(path));
}

QString methodName(RegionMethod m) {
  return m == RegionMethod::Som ? QStringLiteral("SOM")
                                : QStringLiteral("SOM-MeanShift");
}

QByteArray summaryCsv(const std::vector<Regions> &results) {
  QByteArray out = "method,region,center_lat,center_lon,lat_min,lat_max,"
                   "lon_min,lon_max,events\n";
  for (const Regions &r : results)
    for (const RegionSummary &s : r.regions)
      out += QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9\n")
                 .arg(methodName(r.method))
                 .arg(s.id)
                 .arg(s.center[0], 0, 'f', 4)
                 .arg(s.center[1], 0, 'f', 4)
                 .arg(s.latMin, 0, 'f', 4)
                 .arg(s.latMax, 0, 'f', 4)
                 .arg(s.lonMin, 0, 'f', 4)
                 .arg(s.lonMax, 0, 'f', 4)
                 .arg(s.count)
                 .toUtf8();
  return out;
}
} // namespace

QString regionMapTitle(const QString &kind, const QString &areaName,
                       const QString &lang) {
  const bool tr = lang == QLatin1String("tr");
  auto pick = [&](const char *t, const char *e) {
    return tr ? QString::fromUtf8(t) : QString::fromUtf8(e);
  };
  const QString area = areaName.trimmed();
  if (kind == QLatin1String("plain"))
    return (area.isEmpty() ? QString() : area + QLatin1Char('\n')) +
           pick("Sismik Etkinlik", "Seismic Activity");
  if (kind == QLatin1String("geographic-sources"))
    return (area.isEmpty() ? QString() : area + QLatin1Char('\n')) +
           pick("Sismik Etkinlik  |  Fay Kaynakları ve İdari Sınırlar",
                "Seismic Activity  |  Fault Sources & Administrative "
                "Boundaries");
  if (kind == QLatin1String("geographic"))
    return (area.isEmpty() ? QString() : area + QLatin1Char('\n')) +
           pick("Sismik Etkinlik  |  Diri Fay Hatları ve İdari Sınırlar",
                "Seismic Activity  |  Active Fault Lines & Administrative "
                "Boundaries");
  const QString head =
      (area.isEmpty() ? QString() : area + QStringLiteral("  |  ")) +
      pick("Sismik Alt Bölgelerin Dağılımı",
           "Distribution of Seismic Sub-regions");
  return head + QLatin1Char('\n') +
         (kind == QLatin1String("som")
              ? pick("SOM Modeli", "SOM Model")
              : pick("SOM–Mean Shift Modeli", "SOM–Mean Shift Model"));
}

RegionMapSpec regionMapSpec(const RegionRunOptions &o, const PointTable &table,
                            const Regions *regions, bool geographic) {
  RegionMapSpec spec;
  spec.lang = o.lang;
  spec.points = &table.points;
  spec.regions = regions;
  spec.cities = o.cities;
  if (regions)
    spec.marker = o.marker;
  if (geographic) {
    spec.countries = o.countries.get();
    spec.provinces = o.provinces.get();
    spec.faults = o.faults.get();
    if (spec.faults) {
      spec.faultsLabel = o.faultsLabel;
      spec.credit = o.faultsCredit;
    }
  }
  spec.title = regionMapTitle(regions      ? methodPrefix(regions->method)
                              : geographic
                                  ? (spec.faultsLabel.isEmpty()
                                         ? QStringLiteral("geographic")
                                         : QStringLiteral("geographic-sources"))
                                           : QStringLiteral("plain"),
                              o.areaName, o.lang);
  return spec;
}

RegionRunResult runRegions(const RegionRunOptions &o,
                           const RegionRunHooks &hooks) {
  auto log = [&](const QString &m) {
    if (hooks.log)
      hooks.log(m);
  };
  auto stop = [&] { return hooks.shouldStop && hooks.shouldStop(); };
  if (!o.som && !o.somMeanShift)
    throw fail(QStringLiteral("choose at least one method"));
  if (o.outDir.trimmed().isEmpty())
    throw fail(QStringLiteral("no output folder"));
  // The catalog, each method, each map, the record.
  const int methodCount = (o.som ? 1 : 0) + (o.somMeanShift ? 1 : 0);
  const int total = 1 + methodCount + (o.maps ? 2 + methodCount : 0) + 1;
  int done = 0;
  auto step = [&](const QString &what) {
    if (hooks.progress)
      hooks.progress(done++, total, what);
  };

  RegionRunResult result;
  step(QStringLiteral("catalog"));
  auto table = std::make_shared<PointTable>(loadPointTable(o.catalogPath));
  result.table = table;
  if (table->points.empty())
    throw fail(QStringLiteral("%1 holds no event with coordinates")
                   .arg(o.catalogPath));
  log(QStringLiteral("[OK] %L1 events with coordinates [%2, %3]")
          .arg(table->size())
          .arg(table->header.value(table->latCol),
               table->header.value(table->lonCol)));
  if (!QDir().mkpath(o.outDir))
    throw fail(QStringLiteral("cannot create %1").arg(o.outDir));
  const QDir out(o.outDir);
  result.outDir = out.absolutePath();

  auto saveMap = [&](const QString &stem, const Regions *regions,
                     bool geographic) {
    const RegionMapSpec spec = regionMapSpec(o, *table, regions, geographic);
    const DrawFn draw = [&spec](QPainter &p, const QRectF &r) {
      drawRegionMap(p, r, spec);
    };
    const QString png = out.filePath(stem + QStringLiteral(".png"));
    QImage image = renderImage(regionMapSize(o.dpi), draw);
    const int dpm = static_cast<int>(o.dpi / 0.0254 + 0.5);
    image.setDotsPerMeterX(dpm);
    image.setDotsPerMeterY(dpm);
    if (!image.save(png))
      throw fail(QStringLiteral("cannot write %1").arg(png));
    result.files << png;
    log(QStringLiteral("Map saved (%1 dpi) -> %2").arg(o.dpi).arg(png));
    if (o.svg) {
      const QString svg = out.filePath(stem + QStringLiteral(".svg"));
      if (!renderSvg(svg, regionMapSize(100), draw))
        throw fail(QStringLiteral("cannot write %1").arg(svg));
      result.files << svg;
    }
  };

  if (o.maps && !stop()) {
    step(QStringLiteral("map 1"));
    saveMap(QStringLiteral("map_01_study_area_plain"), nullptr, false);
  }
  if (o.maps && !stop()) {
    step(QStringLiteral("map 2"));
    saveMap(QStringLiteral("map_02_study_area_geographic"), nullptr, true);
  }
  std::vector<RegionMethod> methods;
  if (o.som)
    methods.push_back(RegionMethod::Som);
  if (o.somMeanShift)
    methods.push_back(RegionMethod::SomMeanShift);
  result.results.reserve(methods.size());
  for (RegionMethod m : methods) {
    if (stop())
      break;
    step(methodName(m));
    RegionOptions ro;
    ro.method = m;
    ro.regions = o.regions;
    ro.gridRows = o.gridRows;
    ro.gridCols = o.gridCols;
    ro.iterations = o.iterations;
    ro.quantile = o.quantile;
    ro.seed = o.seed;
    log(m == RegionMethod::Som
            ? QStringLiteral("SOM: 1 x %1 map, %2 iterations, seed %3")
                  .arg(o.regions)
                  .arg(o.iterations)
                  .arg(o.seed)
            : QStringLiteral("SOM + Mean Shift: %1 x %2 map, %3 iterations, "
                             "quantile %4, seed %5")
                  .arg(o.gridRows)
                  .arg(o.gridCols)
                  .arg(o.iterations)
                  .arg(o.quantile)
                  .arg(o.seed));
    result.results.push_back(regionalise(table->points, ro));
    const Regions &r = result.results.back();
    if (m == RegionMethod::SomMeanShift)
      log(QStringLiteral("[OK] Mean Shift -> %1 regions (bandwidth %2)")
              .arg(r.regions.size())
              .arg(r.bandwidth, 0, 'f', 4));
    for (const RegionSummary &s : r.regions)
      log(QStringLiteral("  Region %1: centre %2°N %3°E, latitude %4 – %5, "
                         "longitude %6 – %7, %L8 events")
              .arg(s.id)
              .arg(s.center[0], 0, 'f', 4)
              .arg(s.center[1], 0, 'f', 4)
              .arg(s.latMin, 0, 'f', 4)
              .arg(s.latMax, 0, 'f', 4)
              .arg(s.lonMin, 0, 'f', 4)
              .arg(s.lonMax, 0, 'f', 4)
              .arg(s.count));
    result.files << saveRegionCsvs(*table, r, result.outDir);
    if (o.maps && !stop()) {
      step(methodName(m) + QStringLiteral(" map"));
      saveMap(m == RegionMethod::Som ? QStringLiteral("map_03_som")
                                     : QStringLiteral("map_04_som_meanshift"),
              &r, true);
    }
  }
  result.stopped = stop();
  if (result.results.empty() && result.files.isEmpty())
    throw fail(QStringLiteral("the run was stopped before it wrote anything"));

  step(QStringLiteral("record"));
  if (!result.results.empty()) {
    const QString summary = out.filePath(QStringLiteral("regions_summary.csv"));
    writeFile(summary, summaryCsv(result.results));
    result.files << summary;
  }
  // What was run, on what, with which version.
  QJsonObject rec;
  rec[QStringLiteral("tool")] = QStringLiteral("sismokaos-gui regions");
  rec[QStringLiteral("version")] = QCoreApplication::applicationVersion();
  rec[QStringLiteral("qt")] = QString::fromLatin1(qVersion());
  rec[QStringLiteral("argv")] =
      QJsonArray::fromStringList(QCoreApplication::arguments());
  rec[QStringLiteral("written_utc")] =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  rec[QStringLiteral("method_source")] = QStringLiteral(
      "port of SismoKaos-Research/earthquake-clustering-eafz (commit "
      "d267c9c); own SOM and Mean Shift, not MiniSom/scikit-learn");
  QJsonObject in;
  in[QStringLiteral("catalog")] = QFileInfo(o.catalogPath).absoluteFilePath();
  in[QStringLiteral("catalog_sha256")] = sha256(o.catalogPath);
  in[QStringLiteral("events")] = table->size();
  in[QStringLiteral("latitude_column")] = table->header.value(table->latCol);
  in[QStringLiteral("longitude_column")] = table->header.value(table->lonCol);
  rec[QStringLiteral("input")] = in;
  QJsonObject par;
  par[QStringLiteral("som")] = o.som;
  par[QStringLiteral("som_meanshift")] = o.somMeanShift;
  par[QStringLiteral("regions")] = o.regions;
  par[QStringLiteral("grid_rows")] = o.gridRows;
  par[QStringLiteral("grid_cols")] = o.gridCols;
  par[QStringLiteral("iterations")] = o.iterations;
  par[QStringLiteral("quantile")] = o.quantile;
  par[QStringLiteral("seed")] = QString::number(o.seed);
  par[QStringLiteral("sigma_linear")] = 0.5;
  par[QStringLiteral("sigma_grid")] = 1.0;
  par[QStringLiteral("learning_rate")] = 0.5;
  par[QStringLiteral("area_name")] = o.areaName;
  par[QStringLiteral("language")] = o.lang;
  par[QStringLiteral("dpi")] = o.dpi;
  par[QStringLiteral("countries")] = o.countriesSource;
  par[QStringLiteral("provinces")] = o.provincesSource;
  par[QStringLiteral("faults")] = o.faultsSource;
  par[QStringLiteral("cities")] = o.citiesSource;
  if (o.marker) {
    QJsonObject mk;
    mk[QStringLiteral("lat")] = o.marker->lat;
    mk[QStringLiteral("lon")] = o.marker->lon;
    mk[QStringLiteral("label")] = o.marker->label;
    par[QStringLiteral("marker")] = mk;
  }
  rec[QStringLiteral("parameters")] = par;
  QJsonArray res;
  for (const Regions &r : result.results) {
    QJsonObject ro;
    ro[QStringLiteral("method")] = methodName(r.method);
    ro[QStringLiteral("regions")] = static_cast<int>(r.regions.size());
    if (r.method == RegionMethod::SomMeanShift)
      ro[QStringLiteral("bandwidth")] = r.bandwidth;
    res << ro;
  }
  rec[QStringLiteral("results")] = res;
  rec[QStringLiteral("stopped")] = result.stopped;
  QJsonArray files;
  for (const QString &f : result.files)
    files << QFileInfo(f).fileName();
  rec[QStringLiteral("files")] = files;
  const QString record = out.filePath(QStringLiteral("regions_run.json"));
  writeFile(record, QJsonDocument(rec).toJson(QJsonDocument::Indented));
  result.files << record;
  log(QStringLiteral("Output folder: %1").arg(result.outDir));
  return result;
}

} // namespace skg::analysis
