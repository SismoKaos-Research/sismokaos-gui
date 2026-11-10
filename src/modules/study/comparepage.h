// The Compare page: one recipe across every case. On top the scores of a
// classification recipe with the baseline beside each; below, a series of
// an indicators or features recipe with every case on one axis of days
// before its event, and the values each case fitted for itself.
//
// The runs are read off the GUI thread (study/compare.h keeps what it has
// read until a run changes), so showing the page never waits on the disk.
#pragma once

#include <QSet>
#include <QWidget>

#include "core/module.h"
#include "study/compare.h"

class QComboBox;
class QLabel;
class QPainter;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QToolButton;

namespace skg {

class Theme;

// Mouse: a click on a heading sorts, a double click on a row opens its run.
// Keyboard, when the table has the focus: Left and Right choose the sort
// column, Space turns the order, Up and Down choose a row, Enter opens it.
class ScoreTable : public QWidget {
  Q_OBJECT
public:
  explicit ScoreTable(Theme *theme, QWidget *parent = nullptr);
  void setRows(const QList<study::ScoreRow> &rows);
  // In the order shown.
  const QList<study::ScoreRow> &rows() const { return rows_; }
  int sortColumn() const { return sortColumn_; }
  int currentRow() const { return current_; }
  QSize minimumSizeHint() const override;
  QSize sizeHint() const override { return minimumSizeHint(); }

signals:
  void rowActivated(const study::ScoreRow &row);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;
  void focusInEvent(QFocusEvent *e) override;
  void focusOutEvent(QFocusEvent *e) override;

private:
  void sortRows();
  void sortBy(int column);
  int rowAt(const QPointF &pos) const;
  QList<double> columnX() const;

  Theme *theme_;
  QList<study::ScoreRow> rows_;
  QStringList order_; // case ids as given, for the colour of a case
  int sortColumn_ = -1;
  bool descending_ = true;
  int current_ = -1; // the row the keys act on
};

class SeriesPlot : public QWidget {
  Q_OBJECT
public:
  explicit SeriesPlot(Theme *theme, QWidget *parent = nullptr);
  // At most eight series are drawn (the eight colours); `notDrawn` is how
  // many more there are. `order`: the case ids of the study, which fix each
  // case's colour.
  void setSeries(const QList<study::Series> &series, const QString &column,
                 double spanDays, const QStringList &order, int notDrawn = 0);
  // Draws the plot into `rect`. `forFile`: in the light scheme whatever the
  // theme, as a figure written to a file is.
  void paintPlot(QPainter &p, const QRectF &rect, bool forFile = false) const;
  bool isEmpty() const { return series_.isEmpty(); }
  int seriesCount() const { return int(series_.size()); }
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

  // Whether a read of the runs is under way (the page shows the previous
  // result until it is back).
  bool loading() const { return loading_; }
  // The exports, as the buttons do them after asking for a file. The figure
  // is a PNG, or an SVG when the name ends in .svg; always in the light
  // scheme. Each returns false and sets `error` when the file is not
  // written.
  bool writeTable(const QString &path, QString *error = nullptr) const;
  bool writeFigure(const QString &path, QString *error = nullptr) const;
  // <study>/exports, made when missing: where the exports go by default.
  QString exportDir() const;

protected:
  void changeEvent(QEvent *e) override;
  void showEvent(QShowEvent *e) override;

private:
  void retranslate();
  void refresh(); // the recipe lists, then load()
  void load();    // reads the runs on a worker thread, then apply()
  void apply(const study::CompareData &d);
  void showSeries(); // the chosen series of data_ into the plot
  void exportTable();
  void exportFigure();
  static QString seriesKey(const study::Series &s);

  AppContext ctx_;
  bool loading_ = false;
  int generation_ = 0;
  study::CompareData data_;
  // Which series are drawn when there are more than eight, per recipe.
  QString chosenFor_;
  QSet<QString> chosen_;

  QStackedWidget *stack_;
  QLabel *noStudy_;
  QPushButton *newStudy_, *openStudy_;
  QLabel *recipeLabel_, *headline_, *seriesLabel_, *columnLabel_, *aligned_;
  QComboBox *recipe_, *seriesRecipe_, *column_;
  QPushButton *exportTable_, *exportFigure_;
  ScoreTable *scores_;
  QLabel *scoresHint_;
  QPushButton *scoresGo_; // to the page where what is missing is done
  QToolButton *pick_;     // which series, when there are more than eight
  SeriesPlot *plot_;
  QLabel *seriesNote_; // series that could not be read; the last export
  QLabel *fittedTitle_, *fittedNote_;
  QTableWidget *fitted_;
};

} // namespace skg
