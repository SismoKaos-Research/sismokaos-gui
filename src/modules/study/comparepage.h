// The Compare page: one recipe across every case. On top the scores of a
// classification recipe with the baseline beside each; below, a series of
// an indicators or features recipe with every case on one axis of days
// before its event, and the values each case fitted for itself.
#pragma once

#include <QWidget>

#include "core/module.h"
#include "study/compare.h"

class QComboBox;
class QLabel;
class QPainter;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace skg {

class Theme;

class ScoreTable : public QWidget {
  Q_OBJECT
public:
  explicit ScoreTable(Theme *theme, QWidget *parent = nullptr);
  void setRows(const QList<study::ScoreRow> &rows);
  // In the order shown.
  const QList<study::ScoreRow> &rows() const { return rows_; }
  QSize minimumSizeHint() const override;
  QSize sizeHint() const override { return minimumSizeHint(); }

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *e) override;

private:
  void sortRows();
  QList<double> columnX() const;

  Theme *theme_;
  QList<study::ScoreRow> rows_;
  QStringList order_; // case ids as given, for the colour of a case
  int sortColumn_ = -1;
  bool descending_ = true;
};

class SeriesPlot : public QWidget {
  Q_OBJECT
public:
  explicit SeriesPlot(Theme *theme, QWidget *parent = nullptr);
  // `order`: the case ids of the study, which fix each case's colour.
  void setSeries(const QList<study::Series> &series, const QString &column,
                 double spanDays, const QStringList &order);
  // Draws the plot into `rect` (also used for the PNG and SVG exports).
  void paintPlot(QPainter &p, const QRectF &rect) const;
  bool isEmpty() const { return series_.isEmpty(); }
  QSize minimumSizeHint() const override { return {480, 280}; }

protected:
  void paintEvent(QPaintEvent *) override;

private:
  Theme *theme_;
  QList<study::Series> series_;
  QString column_;
  double span_ = 365;
  QStringList order_;
  int hidden_ = 0; // series beyond the eight colours
};

class ComparePage : public QWidget {
  Q_OBJECT
public:
  ComparePage(AppContext &ctx, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *e) override;
  void showEvent(QShowEvent *e) override;

private:
  void retranslate();
  void refresh();
  void refreshScores();
  void refreshSeries();
  void exportTable();
  void exportFigure();

  AppContext ctx_;
  bool updating_ = false;
  QStackedWidget *stack_;
  QLabel *noStudy_;
  QLabel *recipeLabel_, *headline_, *seriesLabel_, *columnLabel_, *aligned_;
  QComboBox *recipe_, *seriesRecipe_, *column_;
  QPushButton *exportTable_, *exportFigure_;
  ScoreTable *scores_;
  QLabel *scoresHint_;
  SeriesPlot *plot_;
  QLabel *fittedTitle_, *fittedNote_;
  QTableWidget *fitted_;
};

} // namespace skg
