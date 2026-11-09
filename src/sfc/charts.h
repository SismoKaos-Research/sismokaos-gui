// The figures sfc's steps save, drawn with QPainter: bar charts, heatmaps,
// box plots, histograms, scatter panels, lines. A small chart toolkit and
// the specific figures built on it, all returning images at 150 dpi.
#pragma once

#include <QColor>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <vector>

class QPainter;

namespace skg::sfc {

// One set of axes in a rectangle: data coordinates to pixels, ticks, labels.
class Axes {
public:
  Axes(QPainter &p, const QRectF &rect, double xMin, double xMax, double yMin,
       double yMax);
  QPointF px(double x, double y) const;
  double sx(double x) const;
  double sy(double y) const;
  const QRectF &plot() const { return plot_; }
  double unit() const { return unit_; } // pixels per "point"
  void frame();
  void xTicks(const std::vector<double> &at, const QStringList &labels,
              bool rotate = false);
  void yTicks(const std::vector<double> &at, const QStringList &labels);
  void autoYTicks(int n = 6);
  void autoXTicks(int n = 6);
  void labels(const QString &x, const QString &y);
  void title(const QString &t);
  void grid(bool x, bool y);
  void legend(const QList<QPair<QString, QColor>> &items,
              Qt::Alignment where = Qt::AlignRight | Qt::AlignTop);

private:
  QPainter &p_;
  QRectF outer_, plot_;
  double xMin_, xMax_, yMin_, yMax_, unit_;
  std::vector<double> yTicks_, xTicks_;
};

// Colours: scikit-learn's class palette (tab10) and sfc's split colours.
QColor classColor(int cls);
QColor splitColor(const QString &split); // train blue, val orange, test green
QColor blues(double t);                  // 0..1, matplotlib Blues
QColor coolwarm(double t);               // 0..1, matplotlib coolwarm
QString tickLabel(double v);

// --- figures (sizes in inches at 150 dpi, as sfc)
// -------------------------------------

// Bars per class: days and windows, two panels (10 x 4 in).
QImage classDistributionFigure(const QStringList &classes,
                               const std::vector<double> &days,
                               const std::vector<double> &windows);
// Box plots of every feature per class, N_COLS = 4 (outliers hidden).
QImage
boxplotsFigure(const QStringList &features, const QStringList &classes,
               const std::vector<std::vector<std::vector<double>>> &values,
               const QString &title);
// Histograms of every feature, 50 bins between the 0.5 and 99.5 percentiles.
QImage histogramsFigure(const QStringList &features,
                        const std::vector<std::vector<double>> &values,
                        const QString &title);
// Correlation heatmap with annotations and a colour bar (8 x 7 in).
QImage correlationFigure(const QStringList &features,
                         const std::vector<std::vector<double>> &corr);
// Stacked bars of windows and days per class and split (11 x 4.5 in).
QImage splitCountsFigure(const QStringList &classes, const QStringList &splits,
                         const std::vector<std::vector<double>> &windows,
                         const std::vector<std::vector<double>> &days);
// Step histograms of every feature per split, density, 40 bins.
QImage featureDistributionsFigure(
    const QStringList &features, const QStringList &splits,
    const std::vector<std::vector<std::vector<double>>> &values,
    const QString &title);
// Two panels: coloured by class, coloured by split (13 x 5.8 in).
struct ScatterData {
  std::vector<double> x, y;
  std::vector<int> cls;
  std::vector<QString> split;
};
QImage scatterPanelsFigure(
    const ScatterData &d, const QStringList &axisNames,
    const QStringList &classNames, const QString &title,
    const std::vector<std::pair<double, double>> *limits = nullptr);
// Confusion matrix (5 x 4.5 in) and ROC curves (5.5 x 5 in).
QImage confusionFigure(const std::vector<std::vector<int>> &cm,
                       const QStringList &labels, const QString &title);
struct RocSeries {
  QString label;
  std::vector<double> fpr, tpr;
};
QImage rocFigure(const std::vector<RocSeries> &curves, const QString &title);
// Train and validation loss per epoch (6 x 4 in).
QImage lossFigure(const std::vector<double> &train,
                  const std::vector<double> &val, const QString &title);
// Grouped bars of train/val/test F1 per model (max(6, 1.3 n) x 4.5 in).
QImage comparisonFigure(const QStringList &models,
                        const std::vector<double> &trainF1,
                        const std::vector<double> &valF1,
                        const std::vector<double> &testF1);
// Daily error rate per split over time, with the event day (11 x 4 in).
struct TimelinePoint {
  double epochDay; // days since 1970
  double rate;
  QString split;
};
QImage errorTimelineFigure(const std::vector<TimelinePoint> &points,
                           double eventDay, bool showEvent,
                           const QString &title);

// Helpers shared with the run: percentiles and PCA (first two components).
double percentileOf(std::vector<double> v, double q);
struct Pca {
  std::vector<double> mean;
  std::vector<std::vector<double>> components; // k x p
  std::vector<double> explained;               // variance ratio
};
Pca pcaFit(const std::vector<double> &X, int rows, int cols, int k);
std::vector<double> pcaTransform(const Pca &pca, const std::vector<double> &X,
                                 int rows, int cols);

} // namespace skg::sfc
