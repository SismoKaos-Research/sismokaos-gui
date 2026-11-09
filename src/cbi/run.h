// One indicators run as the fork's `sismokaos indicators` command does it:
// load and select the catalog, compute the window table, label it, write
// the CSV, then the figures (indicators, moving averages, Bollinger bands,
// annual counts) into Deprem_Analiz_Grafikleri_<prefix>/ under the output
// root, with the originals' file names.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

#include "cbi/catalog.h"
#include "cbi/table.h"

namespace skg::cbi {

struct RunOptions {
  QString catalogPath;
  CatalogSelection selection;
  IndicatorConfig config;
  std::vector<int> maWindows{7, 15, 23};
  bool maCombined = false; // every MA on one figure per indicator
  bool bollinger = false;
  int bbWindow = 20;
  double bbK = 2.0;
  double labelMag = 6.0; // Risk_Durumu marks windows ending shortly before
  int labelDays = 90;    // M >= labelMag, within this lead
  QString outRoot;
  bool plots = true;
  int dpi = 100;
  QString lang = QStringLiteral("tr");
};

struct RunHooks {
  std::function<void(const QString &)> log;
  std::function<bool()> shouldStop;
  std::function<void(int done, int total, const QString &what)> progress;
};

struct RunResult {
  QString outDir, csvPath;
  QStringList figures; // paths, in the order written
  IndicatorTable table;
  Catalog catalog; // after the selection
  bool stopped = false;
};

// "<catalog>_<tag>_<12ay>_<1ay>_Dikdörtgen"
QString runPrefix(const QString &catalogPath, const CatalogSelection &sel,
                  const IndicatorConfig &cfg);
QString runOutputDir(const RunOptions &o);

// Throws std::runtime_error when the catalog cannot be read, the selection
// leaves nothing, or no window fits.
RunResult runIndicators(const RunOptions &o, const RunHooks &hooks = {});

} // namespace skg::cbi
