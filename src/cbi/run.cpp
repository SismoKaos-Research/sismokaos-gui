#include "cbi/run.h"

#include <QDir>
#include <QFileInfo>

#include <stdexcept>

#include "cbi/plots.h"

namespace skg::cbi {

QString runPrefix(const QString &catalogPath, const CatalogSelection &sel,
                  const IndicatorConfig &cfg) {
  return QStringLiteral("%1_%2_%3_%4_Dikdörtgen")
      .arg(QFileInfo(catalogPath).completeBaseName(), sel.tag(),
           cfg.window.shortName(), cfg.step.shortName());
}

QString runOutputDir(const RunOptions &o) {
  return QDir(o.outRoot).filePath(
      QStringLiteral("Deprem_Analiz_Grafikleri_") +
      runPrefix(o.catalogPath, o.selection, o.config));
}

RunResult runIndicators(const RunOptions &o, const RunHooks &hooks) {
  auto log = [&](const QString &s) {
    if (hooks.log)
      hooks.log(s);
  };
  auto stop = [&] { return hooks.shouldStop && hooks.shouldStop(); };
  auto progress = [&](int done, int total, const QString &what) {
    if (hooks.progress)
      hooks.progress(done, total, what);
  };

  RunResult r;
  QStringList selLog;
  r.catalog = o.selection.apply(loadCatalog(o.catalogPath), &selLog);
  for (const QString &l : selLog)
    log(l);
  if (r.catalog.empty())
    throw std::runtime_error("no event matches the selection");
  log(QStringLiteral("Selection: %1").arg(o.selection.tag()));
  for (const QString &l : describeCatalog(r.catalog, o.lang))
    log(l);

  r.table = computeIndicatorTable(r.catalog, o.config);
  if (r.table.empty())
    throw std::runtime_error("no time window fits the catalog's span");
  std::vector<double> targets;
  for (const analysis::Event &e : r.catalog.events)
    if (e.mag >= o.labelMag)
      targets.push_back(e.epoch);
  labelPreEventWindows(r.table, targets, o.labelDays);

  const QString prefix = runPrefix(o.catalogPath, o.selection, o.config);
  r.outDir = runOutputDir(o);
  if (!QDir().mkpath(r.outDir))
    throw std::runtime_error(("cannot create " + r.outDir).toStdString());
  r.csvPath =
      QDir(r.outDir).filePath(prefix + QStringLiteral("_pencereler.csv"));
  if (!writeTableCsv(r.table, r.csvPath))
    throw std::runtime_error(("cannot write " + r.csvPath).toStdString());
  log(QStringLiteral("[OK] %1 windows -> %2")
          .arg(r.table.rows())
          .arg(r.csvPath));
  if (!o.plots)
    return r;

  PlotContext ctx;
  ctx.lang = o.lang;
  ctx.windowLabel = o.config.window.label(o.lang);
  ctx.stepLabel = o.config.step.label(o.lang);
  ctx.dpi = o.dpi;
  const QDir out(r.outDir);
  auto save = [&](const QImage &img, const QString &suffix) {
    const QString path = out.filePath(prefix + QLatin1Char('_') + suffix +
                                      QStringLiteral(".png"));
    if (!img.save(path))
      throw std::runtime_error(("cannot write " + path).toStdString());
    r.figures << path;
  };
  const auto &inds = indicators();
  const int perIndicator =
      1 + (o.maCombined ? 1 : int(o.maWindows.size())) + (o.bollinger ? 1 : 0);
  const int total = 1 + int(inds.size()) * perIndicator;
  int done = 0;
  progress(done, total, QStringLiteral("yillik_olay_sayisi"));
  save(annualCountsFigure(r.catalog, ctx),
       QStringLiteral("yillik_olay_sayisi"));
  ++done;

  IndicatorTable smoothed = r.table;
  for (int w : o.maWindows)
    addMovingAverage(smoothed, w);
  IndicatorTable banded = r.table;
  if (o.bollinger)
    addBollingerBands(banded, o.bbWindow, o.bbK);

  for (const IndicatorInfo &ind : inds) {
    if (stop()) {
      r.stopped = true;
      log(QStringLiteral("Stopped."));
      return r;
    }
    progress(done, total, ind.column);
    save(indicatorFigure(r.table, ind, ctx), ind.column);
    ++done;
    if (o.maCombined) {
      save(combinedMovingAverageFigure(smoothed, ind, o.maWindows, ctx),
           ind.column + QStringLiteral("_MA_Kombine"));
      ++done;
    } else {
      for (int w : o.maWindows) {
        save(movingAverageFigure(smoothed, ind, w, ctx),
             ind.column + QStringLiteral("_MA%1").arg(w));
        ++done;
      }
    }
    if (o.bollinger) {
      save(bollingerFigure(banded, ind, o.bbWindow, o.bbK, ctx),
           ind.column + QStringLiteral("_BB%1").arg(o.bbWindow));
      ++done;
    }
  }
  progress(total, total, QString());
  log(QStringLiteral("[OK] Figures -> %1").arg(r.outDir));
  return r;
}

} // namespace skg::cbi
