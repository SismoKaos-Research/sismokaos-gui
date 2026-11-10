#include "sfc/run.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPainter>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "sfc/charts.h"
#include "sfc/experiments.h"
#include "sfc/metrics.h"
#include "sfc/models.h"
#include "sfc/split.h"

namespace skg::sfc {

namespace {

// Collects the lines of one step's report and writes them to a file.
class Report {
public:
  Report(const QString &path, const LogFn &log) : path_(path), log_(log) {}
  ~Report() {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    QSaveFile f(path_);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      f.write(lines_.join('\n').toUtf8() + '\n');
      f.commit();
    }
  }
  void operator()(const QString &line) {
    lines_ << line;
    if (log_)
      log_(line);
  }
  LogFn fn() {
    return [this](const QString &l) { (*this)(l); };
  }

private:
  QString path_;
  LogFn log_;
  QStringList lines_;
};

QStringList classLabels(const Settings &s) {
  QStringList out;
  const QMap<int, QString> names = s.classNames();
  for (auto it = names.begin(); it != names.end(); ++it)
    out << QString::number(it.key());
  return out;
}

QStringList classNameList(const Settings &s) {
  QStringList out;
  const QMap<int, QString> names = s.classNames();
  for (auto it = names.begin(); it != names.end(); ++it)
    out << it.value();
  return out;
}

int classCount(const Settings &s) { return s.classNames().size(); }

QString fmt(double v, int d = 4) {
  return std::isnan(v) ? QStringLiteral("nan") : QString::number(v, 'f', d);
}

void savePng(const QImage &img, const QString &path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  img.save(path, "PNG");
}

QString metricsLine(const QString &split, const Metrics &m,
                    const QString &note = {}) {
  return QStringLiteral(
             "%1 | acc %2 | precision %3 | recall %4 | f1 %5 | roc auc %6%7")
      .arg(split, -5)
      .arg(m.accuracy, 0, 'f', 3)
      .arg(m.precisionMacro, 0, 'f', 3)
      .arg(m.recallMacro, 0, 'f', 3)
      .arg(m.f1Macro, 0, 'f', 3)
      .arg(m.rocAucMacro, 0, 'f', 3)
      .arg(note);
}

double dayNumber(const QDate &d) {
  return double(
             QDateTime(d, QTime(0, 0), QTimeZone::utc()).toSecsSinceEpoch()) /
         86400.0;
}

} // namespace

const QStringList &allSteps() {
  static const QStringList steps{
      "data_understanding", "split",  "split_visualization",
      "baseline",           "ann",    "trees",
      "error_timeline",     "compare"};
  return steps;
}

// --- data understanding
// ----------------------------------------------------------

void stepDataUnderstanding(const QString &runDir, const Settings &s,
                           const Dataset &d, const LogFn &log) {
  Report rep(runDir + QStringLiteral("/data_understanding_report.txt"), log);
  const QString out = runDir + QStringLiteral("/data_understanding");
  QDir().mkpath(out);
  const int k = classCount(s);
  const QStringList labels = classLabels(s), names = classNameList(s);
  rep(QStringLiteral("%1 windows | %2 days | %3 features")
          .arg(d.rows())
          .arg(d.days().size())
          .arg(d.nFeatures()));
  // 1) class distribution
  std::vector<QSet<QDate>> days(static_cast<size_t>(k));
  std::vector<double> windows(static_cast<size_t>(k), 0.0),
      dayCount(static_cast<size_t>(k), 0.0);
  for (int r = 0; r < d.rows(); ++r) {
    const int c = d.cls[static_cast<size_t>(r)];
    if (c < 0 || c >= k)
      continue;
    days[static_cast<size_t>(c)].insert(d.date[static_cast<size_t>(r)]);
    ++windows[static_cast<size_t>(c)];
  }
  rep(QString());
  rep(QStringLiteral("1) CLASS DISTRIBUTION"));
  rep(QStringLiteral("%1 days  windows  windows_per_day").arg("class", -22));
  for (int c = 0; c < k; ++c) {
    dayCount[static_cast<size_t>(c)] = days[static_cast<size_t>(c)].size();
    rep(QStringLiteral("%1 %2  %3  %4")
            .arg(QStringLiteral("%1 (%2)").arg(c).arg(names[c]), -22)
            .arg(dayCount[static_cast<size_t>(c)], 4)
            .arg(windows[static_cast<size_t>(c)], 7)
            .arg(dayCount[static_cast<size_t>(c)]
                     ? windows[static_cast<size_t>(c)] /
                           dayCount[static_cast<size_t>(c)]
                     : 0.0,
                 0, 'f', 1));
  }
  savePng(classDistributionFigure(labels, dayCount, windows),
          out + QStringLiteral("/class_distribution.png"));
  // 2) missing values (the prepared dataset has none; counted anyway)
  rep(QString());
  rep(QStringLiteral("2) MISSING VALUES (NaN)"));
  int anyNan = 0;
  for (int f = 0; f < d.nFeatures(); ++f) {
    int n = 0;
    for (int r = 0; r < d.rows(); ++r)
      n += std::isnan(d.at(r, f));
    rep(QStringLiteral("%1 %2  %3%")
            .arg(d.featureNames[f], -16)
            .arg(n, 5)
            .arg(d.rows() ? 100.0 * n / d.rows() : 0, 0, 'f', 2));
  }
  for (int r = 0; r < d.rows(); ++r)
    for (int f = 0; f < d.nFeatures(); ++f)
      if (std::isnan(d.at(r, f))) {
        ++anyNan;
        break;
      }
  rep(QStringLiteral("Windows with at least one NaN feature: %1").arg(anyNan));
  // 3) mean and std per class
  rep(QString());
  rep(QStringLiteral("3) FEATURE MEAN PER CLASS"));
  std::vector<std::vector<std::vector<double>>> perClass(
      static_cast<size_t>(d.nFeatures()),
      std::vector<std::vector<double>>(static_cast<size_t>(k)));
  for (int r = 0; r < d.rows(); ++r)
    for (int f = 0; f < d.nFeatures(); ++f)
      if (d.cls[static_cast<size_t>(r)] >= 0 &&
          d.cls[static_cast<size_t>(r)] < k && !std::isnan(d.at(r, f)))
        perClass[static_cast<size_t>(f)][static_cast<size_t>(d.cls[size_t(r)])]
            .push_back(d.at(r, f));
  auto meanStd = [](const std::vector<double> &v) {
    if (v.empty())
      return std::make_pair(std::nan(""), std::nan(""));
    double m = 0;
    for (double x : v)
      m += x;
    m /= double(v.size());
    double var = 0;
    for (double x : v)
      var += (x - m) * (x - m);
    return std::make_pair(
        m, v.size() > 1 ? std::sqrt(var / double(v.size() - 1)) : 0.0);
  };
  QString header = QStringLiteral("%1").arg("feature", -16);
  for (int c = 0; c < k; ++c)
    header += QStringLiteral("%1").arg(c, 12);
  rep(header);
  for (int f = 0; f < d.nFeatures(); ++f) {
    QString line = QStringLiteral("%1").arg(d.featureNames[f], -16);
    for (int c = 0; c < k; ++c)
      line += QStringLiteral("%1").arg(
          fmt(meanStd(perClass[static_cast<size_t>(f)][static_cast<size_t>(c)])
                  .first),
          12);
    rep(line);
  }
  rep(QString());
  rep(QStringLiteral("   FEATURE STD PER CLASS"));
  rep(header);
  for (int f = 0; f < d.nFeatures(); ++f) {
    QString line = QStringLiteral("%1").arg(d.featureNames[f], -16);
    for (int c = 0; c < k; ++c)
      line += QStringLiteral("%1").arg(
          fmt(meanStd(perClass[static_cast<size_t>(f)][static_cast<size_t>(c)])
                  .second),
          12);
    rep(line);
  }
  savePng(
      boxplotsFigure(
          d.featureNames, labels, perClass,
          QStringLiteral("Feature distribution per class (outliers hidden)")),
      out + QStringLiteral("/boxplots_per_class.png"));
  std::vector<std::vector<double>> all(static_cast<size_t>(d.nFeatures()));
  for (int f = 0; f < d.nFeatures(); ++f)
    for (int r = 0; r < d.rows(); ++r)
      all[static_cast<size_t>(f)].push_back(d.at(r, f));
  savePng(histogramsFigure(d.featureNames, all,
                           QStringLiteral("Feature histograms (x axis limited "
                                          "to the 0.5-99.5 percentile range)")),
          out + QStringLiteral("/histograms.png"));
  // 6) correlation
  const int p = d.nFeatures();
  std::vector<std::vector<double>> corr(
      static_cast<size_t>(p), std::vector<double>(static_cast<size_t>(p), 1.0));
  std::vector<double> mean(static_cast<size_t>(p), 0.0),
      sd(static_cast<size_t>(p), 0.0);
  for (int f = 0; f < p; ++f) {
    const auto ms = meanStd(all[static_cast<size_t>(f)]);
    mean[static_cast<size_t>(f)] = ms.first;
    sd[static_cast<size_t>(f)] = ms.second;
  }
  for (int i = 0; i < p; ++i)
    for (int j = i + 1; j < p; ++j) {
      double c = 0;
      for (int r = 0; r < d.rows(); ++r)
        c += (d.at(r, i) - mean[static_cast<size_t>(i)]) *
             (d.at(r, j) - mean[static_cast<size_t>(j)]);
      c = d.rows() > 1 && sd[static_cast<size_t>(i)] > 0 &&
                  sd[static_cast<size_t>(j)] > 0
              ? c / ((d.rows() - 1) * sd[static_cast<size_t>(i)] *
                     sd[static_cast<size_t>(j)])
              : 0.0;
      corr[static_cast<size_t>(i)][static_cast<size_t>(j)] =
          corr[static_cast<size_t>(j)][static_cast<size_t>(i)] = c;
    }
  savePng(correlationFigure(d.featureNames, corr),
          out + QStringLiteral("/correlation_matrix.png"));
  rep(QString());
  rep(QStringLiteral("6) FEATURE PAIRS WITH |CORRELATION| >= 0.9"));
  bool any = false;
  for (int i = 0; i < p; ++i)
    for (int j = i + 1; j < p; ++j)
      if (std::abs(corr[static_cast<size_t>(i)][static_cast<size_t>(j)]) >=
          0.9) {
        rep(QStringLiteral("%1  %2  %3")
                .arg(d.featureNames[i], d.featureNames[j])
                .arg(corr[static_cast<size_t>(i)][static_cast<size_t>(j)], 0,
                     'f', 3));
        any = true;
      }
  if (!any)
    rep(QStringLiteral("none"));
  rep(QString());
  rep(QStringLiteral("Figures saved to: %1").arg(out));
}

// --- split
// ---------------------------------------------------------------------

void stepSplit(const QString &runDir, const Settings &s, const Dataset &d,
               const LogFn &log) {
  Report rep(runDir + QStringLiteral("/split_report.txt"), log);
  QDir().mkpath(runDir);
  const SplitData data = prepareData(d, s);
  for (const QString &l : splitSummary(d, data.split, s))
    rep(l);
  for (const char *name : {"train", "val", "test"}) {
    const Matrix &m = data.part(name);
    rep(QStringLiteral("%1: X (%2, %3)  y (%2,)")
            .arg(name, -5)
            .arg(m.rows)
            .arg(m.cols));
  }
  // split_days.csv: Date,Label,Class,Split, one row per day
  QMap<QDate, QString> rows;
  for (int r = 0; r < d.rows(); ++r)
    rows.insert(d.date[static_cast<size_t>(r)],
                QStringLiteral("%1,%2,%3,%4")
                    .arg(d.date[static_cast<size_t>(r)].toString(Qt::ISODate))
                    .arg(d.label[static_cast<size_t>(r)])
                    .arg(d.cls[static_cast<size_t>(r)])
                    .arg(data.split[static_cast<size_t>(r)]));
  QSaveFile f(runDir + QStringLiteral("/split_days.csv"));
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write("Date,Label,Class,Split\n");
    for (const QString &l : rows)
      f.write(l.toUtf8() + '\n');
    f.commit();
  }
  rep(QString());
  rep(QStringLiteral("Split saved to: %1/split_days.csv").arg(runDir));
}

// --- split visualization
// ---------------------------------------------------------

namespace {
constexpr int kMaxPoints = 4000;

std::vector<int> sampleRows(int n, int seed) {
  Rng rng{quint64(seed)};
  return rng.choice(n, std::min(kMaxPoints, n), false);
}

void scatterOf(const Dataset &d, const SplitData &data,
               const QStringList &features, const QString &dir,
               const Settings &s, const LogFn &log) {
  QList<int> cols;
  for (const QString &f : features) {
    const int c = d.column(f);
    if (c < 0) {
      if (log)
        log(QStringLiteral("[WARNING] %1 not in the feature table, skipping %2")
                .arg(f, features.join(", ")));
      return;
    }
    cols << c;
  }
  const std::vector<int> rows = sampleRows(d.rows(), s.randomSeed);
  std::vector<std::pair<double, double>> limits;
  for (int c : cols) {
    std::vector<double> v;
    for (int r = 0; r < d.rows(); ++r)
      v.push_back(d.at(r, c));
    limits.push_back({percentileOf(v, 0.005), percentileOf(v, 0.995)});
  }
  const QStringList names = classNameList(s);
  const QString stem = QStringLiteral("scatter_") + features.join('_');
  if (cols.size() == 2) {
    ScatterData sd;
    for (int r : rows) {
      sd.x.push_back(d.at(r, cols[0]));
      sd.y.push_back(d.at(r, cols[1]));
      sd.cls.push_back(d.cls[static_cast<size_t>(r)]);
      sd.split.push_back(data.split[static_cast<size_t>(r)]);
    }
    savePng(scatterPanelsFigure(
                sd, features, names,
                features.join(" vs ") +
                    " (axes limited to the 0.5-99.5 percentile range)",
                &limits),
            dir + '/' + stem + ".png");
    return;
  }
  // Three features: the three pairwise projections side by side, class colours.
  QImage img(int(13 * 150), int(4.6 * 150),
             QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  {
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const int pairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
    for (int k = 0; k < 3; ++k) {
      const int a = pairs[k][0], b = pairs[k][1];
      Axes ax(p,
              QRectF(k * img.width() / 3.0, 40, img.width() / 3.0,
                     img.height() - 40),
              limits[static_cast<size_t>(a)].first,
              limits[static_cast<size_t>(a)].second,
              limits[static_cast<size_t>(b)].first,
              limits[static_cast<size_t>(b)].second);
      ax.autoXTicks(5);
      ax.autoYTicks(5);
      ax.grid(true, true);
      p.setClipRect(ax.plot());
      for (int r : rows) {
        QColor c = classColor(d.cls[static_cast<size_t>(r)]);
        c.setAlphaF(0.5);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(ax.px(d.at(r, cols[a]), d.at(r, cols[b])), 1.6, 1.6);
      }
      p.setClipping(false);
      ax.frame();
      ax.labels(features[a], features[b]);
      if (k == 0) {
        QList<QPair<QString, QColor>> legend;
        for (int c = 0; c < names.size(); ++c)
          legend << qMakePair(
              QStringLiteral("Class %1 (%2)").arg(c).arg(names[c]),
              classColor(c));
        ax.legend(legend);
      }
    }
    QFont bf = p.font();
    bf.setBold(true);
    p.setFont(bf);
    p.setPen(Qt::black);
    p.drawText(QRectF(0, 0, img.width(), 40), Qt::AlignCenter,
               features.join(" vs ") + " (pairwise projections, axes limited "
                                       "to the 0.5-99.5 percentile range)");
  }
  savePng(img, dir + '/' + stem + ".png");
}
} // namespace

void stepSplitVisualization(const QString &runDir, const Settings &s,
                            const Dataset &d, const LogFn &log,
                            const StopFn &stop) {
  Report rep(runDir + QStringLiteral("/split_visualization_report.txt"), log);
  const QString out = runDir + QStringLiteral("/split_visualization");
  QDir().mkpath(out);
  const SplitData data = prepareData(d, s);
  const int k = classCount(s);
  const QStringList labels = classLabels(s), splits{"train", "val", "test"};
  // split counts
  std::vector<std::vector<double>> windows(
      3, std::vector<double>(static_cast<size_t>(k), 0.0)),
      days(3, std::vector<double>(static_cast<size_t>(k), 0.0));
  std::vector<std::vector<QSet<QDate>>> daySets(
      3, std::vector<QSet<QDate>>(static_cast<size_t>(k)));
  for (int r = 0; r < d.rows(); ++r) {
    const int sp = splits.indexOf(data.split[static_cast<size_t>(r)]),
              c = d.cls[static_cast<size_t>(r)];
    if (sp < 0 || c < 0 || c >= k)
      continue;
    ++windows[static_cast<size_t>(sp)][static_cast<size_t>(c)];
    daySets[static_cast<size_t>(sp)][static_cast<size_t>(c)].insert(
        d.date[static_cast<size_t>(r)]);
  }
  for (int sp = 0; sp < 3; ++sp)
    for (int c = 0; c < k; ++c)
      days[static_cast<size_t>(sp)][static_cast<size_t>(c)] =
          daySets[static_cast<size_t>(sp)][static_cast<size_t>(c)].size();
  rep(QStringLiteral("Windows per class and split (%):"));
  for (int c = 0; c < k; ++c) {
    const double total = windows[0][static_cast<size_t>(c)] +
                         windows[1][static_cast<size_t>(c)] +
                         windows[2][static_cast<size_t>(c)];
    rep(QStringLiteral("class %1: train %2  val %3  test %4")
            .arg(c)
            .arg(total ? 100 * windows[0][static_cast<size_t>(c)] / total : 0,
                 0, 'f', 1)
            .arg(total ? 100 * windows[1][static_cast<size_t>(c)] / total : 0,
                 0, 'f', 1)
            .arg(total ? 100 * windows[2][static_cast<size_t>(c)] / total : 0,
                 0, 'f', 1));
  }
  savePng(splitCountsFigure(labels, splits, windows, days),
          out + QStringLiteral("/split_counts.png"));
  // feature distributions per split
  std::vector<std::vector<std::vector<double>>> perSplit(
      static_cast<size_t>(d.nFeatures()), std::vector<std::vector<double>>(3));
  for (int r = 0; r < d.rows(); ++r) {
    const int sp = splits.indexOf(data.split[static_cast<size_t>(r)]);
    if (sp < 0)
      continue;
    for (int f = 0; f < d.nFeatures(); ++f)
      perSplit[static_cast<size_t>(f)][static_cast<size_t>(sp)].push_back(
          d.at(r, f));
  }
  rep(QString());
  rep(QStringLiteral("Feature median in each split:"));
  for (int f = 0; f < d.nFeatures(); ++f)
    rep(QStringLiteral("%1 %2 %3 %4")
            .arg(d.featureNames[f], -16)
            .arg(fmt(percentileOf(perSplit[static_cast<size_t>(f)][0], 0.5)),
                 10)
            .arg(fmt(percentileOf(perSplit[static_cast<size_t>(f)][1], 0.5)),
                 10)
            .arg(fmt(percentileOf(perSplit[static_cast<size_t>(f)][2], 0.5)),
                 10));
  savePng(featureDistributionsFigure(
              d.featureNames, splits, perSplit,
              QStringLiteral("Feature distribution in each split (x axis "
                             "limited to the 0.5-99.5 percentile range)")),
          out + QStringLiteral("/feature_distributions.png"));
  // PCA of the scaled training data, applied to every split
  if (data.train.rows > 2 && d.nFeatures() >= 2) {
    const Pca pca = pcaFit(data.train.X, data.train.rows, data.train.cols, 2);
    ScatterData sd;
    const QStringList names = classNameList(s);
    for (const char *name : {"train", "val", "test"}) {
      const Matrix &m = data.part(name);
      const std::vector<double> t = pcaTransform(pca, m.X, m.rows, m.cols);
      for (int r = 0; r < m.rows; ++r) {
        sd.x.push_back(t[static_cast<size_t>(r) * 2]);
        sd.y.push_back(t[static_cast<size_t>(r) * 2 + 1]);
        sd.cls.push_back(m.y[static_cast<size_t>(r)]);
        sd.split.push_back(name);
      }
    }
    if (int(sd.x.size()) > kMaxPoints) {
      const std::vector<int> rows = sampleRows(int(sd.x.size()), s.randomSeed);
      ScatterData sub;
      for (int r : rows) {
        sub.x.push_back(sd.x[static_cast<size_t>(r)]);
        sub.y.push_back(sd.y[static_cast<size_t>(r)]);
        sub.cls.push_back(sd.cls[static_cast<size_t>(r)]);
        sub.split.push_back(sd.split[static_cast<size_t>(r)]);
      }
      sd = sub;
    }
    savePng(
        scatterPanelsFigure(
            sd,
            {QStringLiteral("PC1 (%1%)").arg(100 * pca.explained[0], 0, 'f', 1),
             QStringLiteral("PC2 (%1%)")
                 .arg(100 * pca.explained[1], 0, 'f', 1)},
            names, QStringLiteral("PCA of the scaled features")),
        out + QStringLiteral("/pca_scatter_2d.png"));
    rep(QString());
    rep(QStringLiteral("PCA: PC1 %1%, PC2 %2% of the variance")
            .arg(100 * pca.explained[0], 0, 'f', 1)
            .arg(100 * pca.explained[1], 0, 'f', 1));
  }
  // feature scatter plots
  auto combos = [](const QStringList &features, int size,
                   const QList<QStringList> &extra) {
    QList<QStringList> out;
    if (size == 2) {
      for (int i = 0; i < features.size(); ++i)
        for (int j = i + 1; j < features.size(); ++j)
          out << QStringList{features[i], features[j]};
    } else
      for (int i = 0; i < features.size(); ++i)
        for (int j = i + 1; j < features.size(); ++j)
          for (int l = j + 1; l < features.size(); ++l)
            out << QStringList{features[i], features[j], features[l]};
    for (const QStringList &e : extra) {
      bool have = false;
      for (const QStringList &c : out)
        if (QSet<QString>(c.begin(), c.end()) ==
            QSet<QString>(e.begin(), e.end()))
          have = true;
      if (!have)
        out << e;
    }
    return out;
  };
  for (int size : {2, 3}) {
    const QList<QStringList> list =
        size == 2 ? combos(s.scatterCombinationFeatures, 2, s.scatterPairs)
                  : combos(s.scatterTripleFeatures, 3, s.scatterTriples);
    const QString dir = out + QStringLiteral("/scatter_%1d").arg(size);
    QDir().mkpath(dir);
    rep(QString());
    rep(QStringLiteral("Drawing %1 %2-D feature scatter plots -> %3")
            .arg(list.size())
            .arg(size)
            .arg(dir));
    for (const QStringList &c : list) {
      if (stop && stop())
        return;
      scatterOf(d, data, c, dir, s, rep.fn());
    }
  }
  rep(QString());
  rep(QStringLiteral("Figures saved to: %1").arg(out));
}

// --- models
// ---------------------------------------------------------------------

namespace {
void saveModelFigures(const Model &model, const Matrix &test,
                      const QString &name, const QString &dir,
                      const Settings &s, const LogFn &log) {
  const QStringList labels = classLabels(s);
  const int k = labels.size();
  const std::vector<int> pred = model.predict(test);
  const auto cm = confusionMatrix(test.y, pred, k);
  if (log) {
    log(QStringLiteral("Confusion matrix (rows: true, columns: predicted):"));
    for (const auto &row : cm) {
      QStringList cells;
      for (int v : row)
        cells << QString::number(v);
      log("  [" + cells.join("  ") + "]");
    }
  }
  writeConfusionCsv(dir + '/' + name + QStringLiteral("_confusion_matrix.csv"),
                    cm, labels);
  savePng(confusionFigure(cm, labels, name + QStringLiteral(" - test set")),
          dir + '/' + name + QStringLiteral("_confusion_matrix.png"));
  const std::vector<double> proba = model.proba(test);
  std::vector<RocSeries> curves;
  for (int c = 0; c < k; ++c) {
    std::vector<char> pos(static_cast<size_t>(test.rows));
    std::vector<double> sc(static_cast<size_t>(test.rows));
    size_t nPos = 0;
    for (int r = 0; r < test.rows; ++r) {
      pos[static_cast<size_t>(r)] = test.y[static_cast<size_t>(r)] == c;
      nPos += pos[static_cast<size_t>(r)];
      sc[static_cast<size_t>(r)] =
          proba[static_cast<size_t>(r) * static_cast<size_t>(k) +
                static_cast<size_t>(c)];
    }
    if (nPos == 0 || nPos == static_cast<size_t>(test.rows))
      continue;
    const RocCurve rc = rocCurve(pos, sc);
    curves.push_back(
        {QStringLiteral("Class %1 (AUC %2)").arg(c).arg(rc.auc, 0, 'f', 2),
         rc.fpr, rc.tpr});
  }
  savePng(rocFigure(curves, name + QStringLiteral(" - test set ROC")),
          dir + '/' + name + QStringLiteral("_roc_curves.png"));
}
} // namespace

void stepModels(const QString &projectDir, const Settings &s, const Dataset &d,
                const QString &family, const RunHooks &hooks) {
  const QString dir = runDir(projectDir, s);
  QDir().mkpath(dir);
  Report rep(dir + '/' + family + QStringLiteral("_report.txt"), hooks.log);
  const SplitData data = prepareData(d, s);
  for (const QString &l : splitSummary(d, data.split, s))
    rep(l);
  rep(QString());
  rep(QStringLiteral("Features (%1): %2")
          .arg(d.nFeatures())
          .arg(d.featureNames.join(", ")));
  const int k = classCount(s);
  Validation v(data, d, s, k);
  if (v.cv())
    rep(QStringLiteral("[CV] %1-fold cross-validation over the train + "
                       "validation days (a day is never split between folds)")
            .arg(*s.cvFolds));
  const QString experiments =
      modelsDir(projectDir, s) + QStringLiteral("/experiments.csv");
  struct Candidate {
    QString name;
    Grid grid;
  };
  QList<Candidate> candidates;
  if (family == QLatin1String("baseline")) {
    for (const QString &name : s.modelsToRun) {
      if (name == QLatin1String("logistic"))
        candidates << Candidate{name, s.logisticGrid};
      else if (s.svmGrids.contains(name))
        candidates << Candidate{name, s.svmGrids.value(name)};
      else
        rep(QStringLiteral("[WARNING] unknown model %1, skipped").arg(name));
    }
  } else if (family == QLatin1String("trees")) {
    for (const QString &name : s.treeModelsToRun) {
      if (s.treeGrids.contains(name))
        candidates << Candidate{name, s.treeGrids.value(name)};
      else
        rep(QStringLiteral("[WARNING] no grid for %1, skipped").arg(name));
    }
  } else if (family == QLatin1String("ann"))
    candidates << Candidate{QStringLiteral("ann"), s.annGrid};
  const int total = candidates.size();
  for (int i = 0; i < total; ++i) {
    const Candidate &c = candidates[i];
    if (hooks.shouldStop && hooks.shouldStop())
      return;
    if (hooks.progress)
      hooks.progress(i, total, c.name);
    rep(QString());
    rep(QStringLiteral("=== Tuning %1 ===").arg(c.name));
    std::unique_ptr<Model> model;
    SplitMetrics results;
    QVariantMap logged;
    if (c.name == QLatin1String("ann")) {
      TuneResult best = v.tuneAnn(rep.fn(), hooks.shouldStop);
      if (best.params.isEmpty())
        return;
      rep(QStringLiteral("Best parameters: %1 (best epoch %2)")
              .arg(paramsText(best.params))
              .arg(best.bestEpoch));
      model = v.refitAnn(best);
      results = v.evaluate(*model, v.cv() ? &best.valMetrics : nullptr);
      logged = v.loggedParams(best.params, best.selectStd);
      logged.insert("best_epoch", best.bestEpoch);
      savePng(lossFigure(best.trainLoss, best.valLoss,
                         QStringLiteral("ann - loss") +
                             (v.cv() ? " (first fold)" : "")),
              dir + QStringLiteral("/ann_loss_curve.png"));
    } else {
      auto makeFit = [&](const QVariantMap &p) {
        return fitFor(c.name, p, s, k);
      };
      TuneResult best =
          v.tune(c.name, makeFit, c.grid, rep.fn(), hooks.shouldStop);
      if (best.params.isEmpty() && c.grid.isEmpty())
        best.params = QVariantMap();
      if (hooks.shouldStop && hooks.shouldStop())
        return;
      rep(QStringLiteral("Best parameters: %1").arg(paramsText(best.params)));
      model = v.refit(makeFit(best.params), best);
      results = v.evaluate(*model, v.cv() ? &best.valMetrics : nullptr);
      logged = v.loggedParams(best.params, best.selectStd);
    }
    rep(QString());
    rep(QStringLiteral("--- %1 ---").arg(c.name));
    rep(metricsLine("train", results.train));
    rep(metricsLine("val", results.val,
                    v.cv() ? QStringLiteral(" (cross-validation mean)")
                           : QString()));
    rep(metricsLine("test", results.test));
    if (s.saveModels)
      model->saveTo(dir + '/' + c.name + QStringLiteral("_model.json"));
    saveModelFigures(*model, v.test(), c.name, dir, s, rep.fn());
    logExperiment(experiments, s.runName, c.name, logged, d.featureNames,
                  results.train, results.val, results.test);
  }
  if (hooks.progress)
    hooks.progress(total, total, QString());
  rep(QString());
  rep(QStringLiteral("Figures saved to: %1").arg(dir));
  rep(QStringLiteral("All experiments: %1").arg(experiments));
}

// --- error timeline
// ----------------------------------------------------------------

void stepErrorTimeline(const QString &projectDir, const Settings &s,
                       const Dataset &d, const LogFn &log) {
  const QString dir = runDir(projectDir, s);
  Report rep(dir + QStringLiteral("/error_timeline_report.txt"), log);
  const QString out = dir + QStringLiteral("/error_timeline");
  QDir().mkpath(out);
  const std::vector<Trial> trials =
      loadTrials(modelsDir(projectDir, s) + QStringLiteral("/experiments.csv"));
  const Trial *best = nullptr;
  for (const Trial &t : trials)
    if (t.run == s.runName && (!best || t.valF1 > best->valF1))
      best = &t;
  if (!best) {
    rep(QStringLiteral("[ERROR] No trained model found for run '%1'. Run the "
                       "model steps first.")
            .arg(s.runName));
    return;
  }
  const QString modelPath =
      dir + '/' + best->model + QStringLiteral("_model.json");
  if (!QFileInfo::exists(modelPath)) {
    rep(QStringLiteral("[ERROR] %1 not found. Train with save_models = true.")
            .arg(modelPath));
    return;
  }
  rep(QStringLiteral("Best model of '%1': %2 (validation F1 %3)")
          .arg(s.runName, best->model)
          .arg(best->valF1, 0, 'f', 3));
  std::unique_ptr<Model> model = loadModel(modelPath, s);
  const SplitData data = prepareData(d, s);
  const int k = classCount(s);
  // per day, class and split: share of wrong predictions
  struct Key {
    int cls;
    QDate date;
    QString split;
    bool operator<(const Key &o) const {
      if (cls != o.cls)
        return cls < o.cls;
      if (date != o.date)
        return date < o.date;
      return split < o.split;
    }
  };
  QMap<Key, QPair<int, int>> daily; // wrong, total
  QMap<QPair<int, QString>, QPair<int, int>> perClassSplit;
  for (const char *name : {"train", "val", "test"}) {
    const Matrix &m = data.part(name);
    const std::vector<int> pred = model->predict(m);
    for (int r = 0; r < m.rows; ++r) {
      const bool wrong =
          pred[static_cast<size_t>(r)] != m.y[static_cast<size_t>(r)];
      auto &e =
          daily[Key{m.y[static_cast<size_t>(r)],
                    d.date[static_cast<size_t>(m.index[size_t(r)])], name}];
      e.first += wrong;
      ++e.second;
      auto &f =
          perClassSplit[qMakePair(m.y[static_cast<size_t>(r)], QString(name))];
      f.first += wrong;
      ++f.second;
    }
  }
  rep(QString());
  rep(QStringLiteral(
      "Share of wrong predictions (windows) per class and split:"));
  rep(QStringLiteral("class  train    val   test"));
  for (int c = 0; c < k; ++c) {
    QString line = QStringLiteral("%1").arg(c, 5);
    for (const char *name : {"train", "val", "test"}) {
      const auto e = perClassSplit.value(qMakePair(c, QString(name)));
      line += QStringLiteral("%1").arg(
          e.second ? QString::number(double(e.first) / e.second, 'f', 3)
                   : QStringLiteral("nan"),
          7);
    }
    rep(line);
  }
  QSaveFile csv(out + QStringLiteral("/daily_error_rates.csv"));
  if (csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    csv.write("Class,Date,Split,Error_rate\n");
    for (auto it = daily.begin(); it != daily.end(); ++it)
      csv.write(
          QStringLiteral("%1,%2,%3,%4\n")
              .arg(it.key().cls)
              .arg(it.key().date.toString(Qt::ISODate), it.key().split)
              .arg(double(it.value().first) / it.value().second, 0, 'g', 10)
              .toUtf8());
    csv.commit();
  }
  const QMap<int, QString> names = s.classNames();
  const double eventDay = dayNumber(s.eqDate);
  for (int c = 0; c < k; ++c) {
    std::vector<TimelinePoint> pts;
    double maxDay = -1e18;
    for (auto it = daily.begin(); it != daily.end(); ++it)
      if (it.key().cls == c) {
        const double day = dayNumber(it.key().date);
        pts.push_back({day, double(it.value().first) / it.value().second,
                       it.key().split});
        maxDay = std::max(maxDay, day);
      }
    savePng(errorTimelineFigure(pts, eventDay, maxDay >= eventDay - 60,
                                QStringLiteral("%1: class %2 (%3)")
                                    .arg(best->model)
                                    .arg(c)
                                    .arg(names.value(c))),
            out + QStringLiteral("/error_timeline_class%1.png").arg(c));
  }
  rep(QString());
  rep(QStringLiteral("Figures saved to: %1").arg(out));
}

// --- compare
// ---------------------------------------------------------------------

void stepCompare(const QString &projectDir, const Settings &s,
                 const LogFn &log) {
  const QString dir = modelsDir(projectDir, s);
  Report rep(dir + QStringLiteral("/model_comparison_report.txt"), log);
  const QString path = dir + QStringLiteral("/experiments.csv");
  if (!QFileInfo::exists(path)) {
    rep(QStringLiteral("[ERROR] %1 not found. Run the model steps first.")
            .arg(path));
    return;
  }
  std::vector<Trial> trials = loadTrials(path, false);
  if (s.compareRuns)
    trials.erase(std::remove_if(trials.begin(), trials.end(),
                                [&](const Trial &t) {
                                  return !s.compareRuns->contains(t.run);
                                }),
                 trials.end());
  if (trials.empty()) {
    rep(QStringLiteral("[ERROR] No experiment found for compare_runs."));
    return;
  }
  std::stable_sort(
      trials.begin(), trials.end(),
      [](const Trial &a, const Trial &b) { return a.valF1 > b.valF1; });
  rep(QStringLiteral("All trials (%1), ranked by validation F1:")
          .arg(trials.size()));
  rep(QString());
  const QString head = QStringLiteral("%1 %2 %3 %4 %5 %6 %7  %8")
                           .arg("run", -14)
                           .arg("model", -18)
                           .arg("train_f1", 8)
                           .arg("val_f1", 8)
                           .arg("test_f1", 8)
                           .arg("test_acc", 8)
                           .arg("test_auc", 8)
                           .arg("params");
  rep(head);
  auto line = [](const Trial &t) {
    return QStringLiteral("%1 %2 %3 %4 %5 %6 %7  %8")
        .arg(t.run, -14)
        .arg(t.model, -18)
        .arg(t.trainF1, 8, 'f', 4)
        .arg(t.valF1, 8, 'f', 4)
        .arg(t.testF1, 8, 'f', 4)
        .arg(t.testAcc, 8, 'f', 4)
        .arg(t.testAuc, 8, 'f', 4)
        .arg(t.params.left(60));
  };
  for (const Trial &t : trials)
    rep(line(t));
  const std::vector<Trial> best = bestPerModel(trials);
  rep(QString());
  rep(QStringLiteral("Best trial of each model:"));
  rep(QString());
  rep(head);
  for (const Trial &t : best)
    rep(line(t));
  rep(QString());
  rep(QStringLiteral("Train F1 - validation F1 (large value = overfitting):"));
  QStringList models;
  std::vector<double> tr, va, te;
  for (const Trial &t : best) {
    rep(QStringLiteral("%1 %2")
            .arg(t.model, -18)
            .arg(t.trainF1 - t.valF1, 0, 'f', 4));
    models << t.model;
    tr.push_back(t.trainF1);
    va.push_back(t.valF1);
    te.push_back(t.testF1);
  }
  writeComparisonCsv(dir + QStringLiteral("/model_comparison.csv"), best);
  savePng(comparisonFigure(models, tr, va, te),
          dir + QStringLiteral("/model_comparison.png"));
  rep(QString());
  rep(QStringLiteral("Saved: %1/model_comparison.csv").arg(dir));
  rep(QStringLiteral("       %1/model_comparison.png").arg(dir));
}

// --- the run
// ---------------------------------------------------------------------

std::vector<StepOutcome> runSteps(const QString &projectDir, const Settings &s,
                                  const Dataset &d, const QStringList &wanted,
                                  const RunHooks &hooks) {
  QStringList steps;
  for (const QString &st : allSteps())
    if (wanted.contains(st))
      steps << st;
  std::vector<StepOutcome> outcomes;
  const QString dir = runDir(projectDir, s);
  QDir().mkpath(dir);
  s.save(dir + QStringLiteral("/settings.toml"));
  if (hooks.log) {
    hooks.log(QStringLiteral("Experiment: %1").arg(s.runName));
    hooks.log(QStringLiteral("Steps: %1").arg(steps.join(", ")));
  }
  for (int i = 0; i < steps.size(); ++i) {
    const QString &step = steps[i];
    if (hooks.shouldStop && hooks.shouldStop()) {
      outcomes.push_back(
          {step, QStringLiteral("SKIPPED"), 0, QStringLiteral("stopped")});
      continue;
    }
    if (hooks.log)
      hooks.log(
          QStringLiteral("\n%1\nSTEP: %2\n%1").arg(QString(60, '='), step));
    // One scale for the whole run: every step has an equal share of it, and
    // a step that trains several models moves through its share model by
    // model. (Reported in thousandths, so the callers' done/total stays.)
    constexpr int kScale = 1000;
    const int stepCount = int(steps.size());
    const QString stepLabel =
        QStringLiteral("%1/%2 %3").arg(i + 1).arg(stepCount).arg(step);
    if (hooks.progress)
      hooks.progress(i * kScale, stepCount * kScale, stepLabel);
    RunHooks inStep = hooks;
    inStep.progress = [&hooks, i, stepCount, stepLabel](int done, int total,
                                                        const QString &what) {
      if (!hooks.progress || total <= 0)
        return;
      const int within = int(double(std::clamp(done, 0, total)) / total * kScale);
      hooks.progress(i * kScale + std::min(within, kScale - 1),
                     stepCount * kScale,
                     what.isEmpty() ? stepLabel
                                    : stepLabel + QStringLiteral(" · ") + what);
    };
    QElapsedTimer timer;
    timer.start();
    StepOutcome o{step, QStringLiteral("OK"), 0, {}};
    try {
      if (step == "data_understanding")
        stepDataUnderstanding(dir, s, d, hooks.log);
      else if (step == "split")
        stepSplit(dir, s, d, hooks.log);
      else if (step == "split_visualization")
        stepSplitVisualization(dir, s, d, hooks.log, hooks.shouldStop);
      else if (step == "baseline" || step == "ann" || step == "trees")
        stepModels(projectDir, s, d, step, inStep);
      else if (step == "error_timeline")
        stepErrorTimeline(projectDir, s, d, hooks.log);
      else if (step == "compare")
        stepCompare(projectDir, s, hooks.log);
    } catch (const std::exception &e) {
      o.status = QStringLiteral("FAILED");
      o.error = QString::fromUtf8(e.what());
      if (hooks.log)
        hooks.log(QStringLiteral("[ERROR] %1").arg(o.error));
    }
    o.seconds = timer.elapsed() / 1000.0;
    outcomes.push_back(o);
    if (o.status == "FAILED" && s.stopOnError)
      break;
  }
  if (hooks.progress && !steps.isEmpty() &&
      !(hooks.shouldStop && hooks.shouldStop()))
    hooks.progress(int(steps.size()), int(steps.size()), QString());
  if (hooks.log) {
    hooks.log(QStringLiteral("\n%1\nSUMMARY (%2)\n%1")
                  .arg(QString(60, '='), s.runName));
    for (const StepOutcome &o : outcomes)
      hooks.log(QStringLiteral("%1 %2 %3 min")
                    .arg(o.step, -22)
                    .arg(o.status, -7)
                    .arg(o.seconds / 60.0, 6, 'f', 1));
  }
  // the whole log also as run_all_report.txt is the GUI job's own log
  return outcomes;
}

// --- HTML report
// ---------------------------------------------------------------------

namespace {
QString esc(const QString &s) { return s.toHtmlEscaped(); }
QString metric(double v) {
  return std::isnan(v) ? QString() : QString::number(v, 'f', 3);
}
QString image(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QStringLiteral("<img loading=\"lazy\" alt=\"%1\" "
                        "src=\"data:image/png;base64,%2\">")
      .arg(esc(QFileInfo(path).fileName()),
           QString::fromLatin1(f.readAll().toBase64()));
}
const char *kStyle = R"(
:root { --bg: #ffffff; --fg: #1d2330; --muted: #5b6475; --line: #d9dee7; --head: #f2f4f8; --accent: #1f5fbf; }
@media (prefers-color-scheme: dark) {
  :root { --bg: #14171d; --fg: #e3e7ee; --muted: #9aa3b2; --line: #2c323d; --head: #1c2129; --accent: #6ea8ff; }
}
body { background: var(--bg); color: var(--fg); font: 15px/1.5 system-ui, sans-serif; margin: 0 auto;
       max-width: 1200px; padding: 24px 16px 64px; }
h1 { font-size: 24px; margin: 0 0 4px; } h2 { font-size: 20px; margin: 40px 0 8px; border-top: 1px solid var(--line);
     padding-top: 24px; } h3 { font-size: 16px; margin: 24px 0 8px; }
.muted { color: var(--muted); } a { color: var(--accent); }
.scroll { overflow-x: auto; }
table { border-collapse: collapse; margin: 8px 0 16px; font-variant-numeric: tabular-nums; }
th, td { border: 1px solid var(--line); padding: 4px 10px; text-align: right; white-space: nowrap; }
th { background: var(--head); } td.text, th.text { text-align: left; white-space: normal; }
tr.best td { font-weight: 600; }
.cm td { min-width: 48px; text-align: center; }
.figures { display: grid; grid-template-columns: repeat(auto-fill, minmax(340px, 1fr)); gap: 16px; }
figure { margin: 0; } figure img { width: 100%; height: auto; border: 1px solid var(--line); background: #fff; }
figcaption { font-size: 13px; color: var(--muted); word-break: break-all; }
)";
} // namespace

QString writeHtmlReport(const QString &projectDir, const Settings &s,
                        const QStringList &wanted) {
  const QString dir = modelsDir(projectDir, s);
  const QStringList known = runNames(dir);
  const QStringList runs = wanted.isEmpty() ? known : wanted;
  const QVariantMap project = s.asMap();
  const std::vector<RunSummary> summaries = summarize(dir, project);
  const std::vector<Trial> trials =
      loadTrials(dir + QStringLiteral("/experiments.csv"));
  QStringList page;
  const QString title = s.station + " / " + s.earthquakeName;
  page << "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
       << "<meta name=\"viewport\" content=\"width=device-width, "
          "initial-scale=1\">"
       << QStringLiteral(
              "<title>%1 experiments</title><style>%2</style></head><body>")
              .arg(esc(title), QString::fromUtf8(kStyle))
       << QStringLiteral("<h1>%1: %2 experiment%3</h1>")
              .arg(esc(title))
              .arg(runs.size())
              .arg(runs.size() != 1 ? "s" : "")
       << QStringLiteral(
              "<p class=\"muted\">Project %1 | event %2 | written %3</p>")
              .arg(esc(projectDir), s.eqDate.toString(Qt::ISODate),
                   QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"));
  QStringList rows;
  for (const QString &run : runs) {
    const auto it =
        std::find_if(summaries.begin(), summaries.end(),
                     [&](const RunSummary &r) { return r.run == run; });
    if (it == summaries.end())
      continue;
    rows << QStringLiteral(
                "<tr><td class=\"text\"><a "
                "href=\"#%1\">%1</a></td><td>%2</td><td class=\"text\">%3</td>"
                "<td>%4</td><td>%5</td><td>%6</td><td>%7</td><td "
                "class=\"text\">%8</td></tr>")
                .arg(esc(run))
                .arg(it->models)
                .arg(esc(it->bestModel), metric(it->valF1), metric(it->testF1),
                     metric(it->testAcc), metric(it->testAuc),
                     esc(it->changes ? it->changes->join(", ") : QString()));
  }
  page << "<div class=\"scroll\"><table><tr><th "
          "class=\"text\">run</th><th>models</th><th class=\"text\">best "
          "model</th>"
          "<th>val F1</th><th>test F1</th><th>test acc</th><th>test AUC</th>"
          "<th class=\"text\">settings different from the project</th></tr>"
       << rows.join("") << "</table></div>";
  const QString comparison = dir + QStringLiteral("/model_comparison.png");
  if (QFileInfo::exists(comparison) && runs.size() > 1)
    page << QStringLiteral(
                "<figure>%1<figcaption>model_comparison.png (best trial of "
                "each model, all runs)</figcaption></figure>")
                .arg(image(comparison));
  for (const QString &run : runs) {
    const QString folder = dir + '/' + run;
    page << QStringLiteral("<h2 id=\"%1\">%1</h2><p class=\"muted\">%2</p>")
                .arg(esc(run), esc(folder));
    const auto it =
        std::find_if(summaries.begin(), summaries.end(),
                     [&](const RunSummary &r) { return r.run == run; });
    QString changes = QStringLiteral("unknown (no settings.toml)");
    if (it != summaries.end() && it->changes)
      changes = it->changes->isEmpty() ? QStringLiteral("none")
                                       : esc(it->changes->join(", "));
    page << QStringLiteral(
                "<p><b>Settings different from the project:</b> %1</p>")
                .arg(changes);
    std::vector<const Trial *> mine;
    for (const Trial &t : trials)
      if (t.run == run)
        mine.push_back(&t);
    std::sort(mine.begin(), mine.end(), [](const Trial *a, const Trial *b) {
      return a->valF1 > b->valF1;
    });
    if (mine.empty())
      page << "<p>No trained models.</p>";
    else {
      page << "<div class=\"scroll\"><table><tr><th "
              "class=\"text\">model</th><th>train F1</th><th>val F1</th>"
              "<th>test F1</th><th>test acc</th><th>test AUC</th><th "
              "class=\"text\">parameters</th></tr>";
      for (size_t i = 0; i < mine.size(); ++i)
        page << QStringLiteral("<tr%1><td "
                               "class=\"text\">%2</td><td>%3</td><td>%4</"
                               "td><td>%5</td><td>%6</td><td>%7</td>"
                               "<td class=\"text\">%8</td></tr>")
                    .arg(i == 0 ? " class=\"best\"" : "", esc(mine[i]->model),
                         metric(mine[i]->trainF1), metric(mine[i]->valF1),
                         metric(mine[i]->testF1), metric(mine[i]->testAcc),
                         metric(mine[i]->testAuc), esc(mine[i]->params));
      page << "</table></div>";
      QStringList matrices;
      for (const Trial *t : mine) {
        QStringList labels;
        const auto cm = readConfusionCsv(
            folder + '/' + t->model + QStringLiteral("_confusion_matrix.csv"),
            &labels);
        if (!cm)
          continue;
        QString tbl = "<table class=\"cm\"><tr><th></th>";
        for (const QString &l : labels)
          tbl += QStringLiteral("<th>pred %1</th>").arg(esc(l));
        tbl += "</tr>";
        for (size_t i = 0; i < cm->size(); ++i) {
          int total = 0;
          for (int v : (*cm)[i])
            total += v;
          tbl += QStringLiteral("<tr><th>true %1</th>")
                     .arg(esc(labels.value(int(i))));
          for (size_t j = 0; j < (*cm)[i].size(); ++j) {
            const double share = total ? double((*cm)[i][j]) / total : 0;
            tbl += QStringLiteral("<td style=\"background: rgba(31, 95, 191, "
                                  "%1);%2\">%3</td>")
                       .arg(0.08 + 0.75 * share, 0, 'f', 2)
                       .arg(i == j ? " font-weight: 700;" : "")
                       .arg((*cm)[i][j]);
          }
          tbl += "</tr>";
        }
        tbl += "</table>";
        matrices
            << QStringLiteral("<div><b>%1</b>%2</div>").arg(esc(t->model), tbl);
      }
      if (!matrices.isEmpty())
        page << "<h3>Confusion matrices (test days)</h3><div class=\"figures\">"
             << matrices.join("") << "</div>";
    }
    // figures by sub-folder
    QMap<QString, QStringList> groups;
    QDirIterator it2(folder, {"*.png"}, QDir::Files,
                     QDirIterator::Subdirectories);
    while (it2.hasNext()) {
      const QString f = it2.next();
      const QString rel = QDir(folder).relativeFilePath(f);
      groups[rel.contains('/') ? rel.section('/', 0, 0)
                               : QStringLiteral("models")]
          << f;
    }
    for (auto g = groups.begin(); g != groups.end(); ++g) {
      QStringList files = g.value();
      std::sort(files.begin(), files.end());
      page << QStringLiteral("<h3>%1 (%2)</h3><div class=\"figures\">")
                  .arg(esc(QString(g.key()).replace('_', ' ')))
                  .arg(files.size());
      for (const QString &f : files)
        page << QStringLiteral("<figure>%1<figcaption>%2</figcaption></figure>")
                    .arg(image(f), esc(QDir(folder).relativeFilePath(f)));
      page << "</div>";
    }
  }
  page << "</body></html>";
  const QString name =
      runs == known
          ? QStringLiteral("report.html")
          : QStringLiteral("report_%1.html").arg(runs.join('_').left(80));
  const QString out = dir + '/' + name;
  QDir().mkpath(dir);
  QSaveFile f(out);
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    f.write(page.join('\n').toUtf8());
    f.commit();
  }
  return out;
}

} // namespace skg::sfc
