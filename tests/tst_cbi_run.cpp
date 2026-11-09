// A whole indicators run on the synthetic catalog: the output folder and
// file names of the originals, the CSV with the risk label, one PNG per
// indicator plus moving averages and Bollinger bands, the annual counts,
// and the stop hook.
#include <QtTest>

#include "cbi/plots.h"
#include "cbi/run.h"

using namespace skg::cbi;

class TstCbiRun : public QObject {
  Q_OBJECT
private slots:
  void wholeRun();
  void stopsBetweenFigures();
  void figuresRenderWithGaps();
};

void TstCbiRun::wholeRun() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  RunOptions o;
  o.catalogPath = QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv");
  o.selection.zone = CatalogSelection::Zone::None;
  o.config.mc = 1.3;
  o.config.window = Duration{6, QLatin1Char('M')};
  o.config.step = Duration{1, QLatin1Char('M')};
  o.maWindows = {3, 5};
  o.bollinger = true;
  o.bbWindow = 4;
  o.outRoot = dir.path();
  o.dpi = 40;
  QStringList log;
  RunHooks hooks;
  hooks.log = [&](const QString &s) { log << s; };
  int lastDone = -1, total = 0;
  hooks.progress = [&](int d, int t, const QString &) {
    lastDone = d;
    total = t;
  };
  const RunResult r = runIndicators(o, hooks);
  QVERIFY(!r.stopped);
  QCOMPARE(
      QFileInfo(r.outDir).fileName(),
      QStringLiteral(
          "Deprem_Analiz_Grafikleri_synthetic_catalog_all_6ay_1ay_Dikdörtgen"));
  QCOMPARE(QFileInfo(r.csvPath).fileName(),
           QStringLiteral(
               "synthetic_catalog_all_6ay_1ay_Dikdörtgen_pencereler.csv"));
  QVERIFY(QFileInfo::exists(r.csvPath));
  QCOMPARE(r.table.rows(),
           30); // starts 2018-06 .. 2020-11, 6-month windows monthly
  QCOMPARE(r.table.risk.size(), 30);
  QVERIFY(r.table.risk.contains(
      riskNear())); // the M6.1 and M6.4 follow some windows
  // 1 annual + per indicator (1 + 2 MA + 1 BB)
  QCOMPARE(r.figures.size(), 1 + 10 * 4);
  QCOMPARE(lastDone, total);
  for (const QString &f : r.figures) {
    QVERIFY2(QFileInfo::exists(f), qPrintable(f));
    QImage img(f);
    QVERIFY2(!img.isNull(), qPrintable(f));
    QVERIFY(img.width() >= 12 * 40);
  }
  QVERIFY(
      r.figures.first().endsWith(QStringLiteral("_yillik_olay_sayisi.png")));
  QVERIFY(r.figures.contains(QDir(r.outDir).filePath(QStringLiteral(
      "synthetic_catalog_all_6ay_1ay_Dikdörtgen_b_value_MA3.png"))));
  QVERIFY(r.figures.contains(QDir(r.outDir).filePath(
      QStringLiteral("synthetic_catalog_all_6ay_1ay_Dikdörtgen_eta_BB4.png"))));
  QVERIFY(log.join('\n').contains(QStringLiteral("30 windows")));
  const IndicatorTable back = readTableCsv(r.csvPath);
  QCOMPARE(back.rows(), 30);
  QVERIFY(back.column(QStringLiteral("Mmax_obs_first_6_plus")));
}

void TstCbiRun::stopsBetweenFigures() {
  QTemporaryDir dir;
  RunOptions o;
  o.catalogPath = QStringLiteral(SKG_TEST_DATA "/cbi/synthetic_catalog.csv");
  o.selection.zone = CatalogSelection::Zone::None;
  o.config.window = Duration{12, QLatin1Char('M')};
  o.config.step = Duration{3, QLatin1Char('M')};
  o.maWindows = {3};
  o.maCombined = true;
  o.outRoot = dir.path();
  o.dpi = 30;
  RunHooks hooks;
  int calls = 0;
  hooks.shouldStop = [&] { return ++calls > 2; };
  const RunResult r = runIndicators(o, hooks);
  QVERIFY(r.stopped);
  QVERIFY(QFileInfo::exists(r.csvPath));
  QCOMPARE(r.figures.size(),
           1 + 2 * 2); // annual + two indicators with their combined MA
}

void TstCbiRun::figuresRenderWithGaps() {
  IndicatorTable t;
  for (int i = 0; i < 6; ++i) {
    t.start.push_back(
        QDateTime(QDate(2020, 1 + i, 1), QTime(), QTimeZone::utc()));
    t.end.push_back(t.start.back().addMonths(1));
    t.xLabel << t.start.back().toString(QStringLiteral("yyyy-MM"));
  }
  const double nan = std::nan("");
  t.addColumn(QStringLiteral("b_value"), {1.0, nan, 1.2, 1.1, nan, nan});
  t.addColumn(QStringLiteral("Mmax_obs_first_5_to_6"), {0, 0, 5.4, 0, 0, 0});
  t.addColumn(QStringLiteral("Mmax_obs_first_6_plus"), {0, 0, 0, 6.1, 0, 0});
  addMovingAverage(t, 2, {QStringLiteral("b_value")});
  addBollingerBands(t, 3, 2.0, {QStringLiteral("b_value")});
  PlotContext ctx;
  ctx.dpi = 40;
  ctx.lang = QStringLiteral("en");
  const IndicatorInfo &b = *indicatorInfo(QStringLiteral("b_value"));
  QVERIFY(!indicatorFigure(t, b, ctx).isNull());
  QVERIFY(!movingAverageFigure(t, b, 2, ctx).isNull());
  QVERIFY(!combinedMovingAverageFigure(t, b, {2}, ctx).isNull());
  QVERIFY(!bollingerFigure(t, b, 3, 2.0, ctx).isNull());
  Catalog c;
  QVERIFY(!annualCountsFigure(c, ctx).isNull());
}

QTEST_MAIN(TstCbiRun) // the figures need a QGuiApplication for fonts
#include "tst_cbi_run.moc"
