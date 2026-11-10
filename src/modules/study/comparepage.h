// The Compare page: one recipe across every case. On top the scores of a
// classification recipe with the baseline beside each; below, a series of
// an indicators or features recipe with every case on one axis of days
// before its event, and the values each case fitted for itself.
//
// The runs are read off the GUI thread (study/compare.h keeps what it has
// read until a run changes), so showing the page never waits on the disk.
#pragma once

#include <QList>
#include <QRectF>
#include <QSet>
#include <QStringList>
#include <QWidget>

#include <optional>

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
class GeoMap;

// Mouse: a click on a heading sorts, a double click on a row opens its run;
// pointing at a row says its values in full (names that are cut short, the
// scores with what they are, the warnings its run was made under).
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
  // What pointing at a row says.
  static QString rowTip(const study::ScoreRow &row);
  QSize minimumSizeHint() const override;
  QSize sizeHint() const override { return minimumSizeHint(); }

signals:
  void rowActivated(const study::ScoreRow &row);

protected:
  bool event(QEvent *e) override; // a row's warnings on hover
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

// Pointing at the plot puts a guide on the nearest day that has a value and
// says each drawn series' value there; with the focus on the plot, Left and
// Right move the guide a day (a point) at a time, Escape takes it away. A
// click on a name in the legend hides that series or shows it again.
class SeriesPlot : public QWidget {
  Q_OBJECT
public:
  explicit SeriesPlot(Theme *theme, QWidget *parent = nullptr);
  // What the guide reads: the day (0 is the event, negative before it) and,
  // for each series that is drawn, its name and its value there (NaN where
  // the series has none on that day).
  struct Readout {
    double day = 0;
    QStringList names;
    QList<double> values;
  };
  bool hasGuide() const { return guide_.has_value(); }
  Readout readout() const;
  // Puts the guide on the day with a value nearest to `day`; false when no
  // drawn series has a value.
  bool setGuide(double day);
  void clearGuide();
  // The series hidden through the legend, as "case id|station".
  QStringList hiddenSeries() const;
  void setHiddenSeries(const QStringList &keys);
  // Hides a series, or shows it again; false when there is no such series.
  bool toggleSeries(const QString &key);
  // Where the legend has the series' name (after a paint), for a click.
  QRectF legendRect(int index) const { return legend_.value(index); }
  static QString seriesKey(const study::Series &s);
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

signals:
  void hiddenChanged();

protected:
  void paintEvent(QPaintEvent *) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void leaveEvent(QEvent *e) override;
  void keyPressEvent(QKeyEvent *e) override;

private:
  QList<double> days() const; // every day a drawn series has a value on
  void paintGuide(QPainter &p) const;

  Theme *theme_;
  QList<study::Series> series_;
  QString column_;
  double span_ = 365;
  QStringList order_;
  int hidden_ = 0; // series beyond the eight colours
  QSet<QString> off_;           // hidden through the legend
  std::optional<double> guide_; // the day the guide stands on
  bool byKeys_ = false;         // put there by the keys: it stays on leave
  // Of the last paint on the screen: where things are.
  mutable QRectF plotRect_;
  mutable double lo_ = 0, hi_ = 1;
  mutable QList<QRectF> legend_;
  mutable QList<QColor> colors_;
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
  // The score table as shown, tab-separated, for pasting into a sheet.
  QString scoresTsv() const;
  // The file the last export wrote (the note under the plot links to it).
  QString lastExport() const { return lastExport_; }
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
  void updateWhere(); // the cases on the map of the "Where" panel
  void exportTable();
  void exportFigure();
  void copyTable();
  void noteExport(const QString &path);
  // The page's choices (recipes, column, hidden series) are kept per study.
  QString choiceKey(const char *name) const;
  void saveChoices();
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
  QLabel *recipeLabel_, *headline_, *seriesLabel_, *columnLabel_, *aligned_;
  QComboBox *recipe_, *seriesRecipe_, *column_;
  QPushButton *exportTable_, *exportFigure_, *copyTable_, *again_;
  QLabel *busy_; // "reading the results…" while a read is under way
  QString lastExport_;
  ScoreTable *scores_;
  QLabel *scoresHint_;
  QPushButton *scoresGo_; // to the page where what is missing is done
  QToolButton *pick_;     // which series, when there are more than eight
  SeriesPlot *plot_;
  QLabel *seriesNote_; // series that could not be read; the last export
  QLabel *fittedTitle_, *fittedNote_;
  QTableWidget *fitted_;
  // "Where": the cases on a map, in their series colours; it folds away.
  QToolButton *whereToggle_;
  QLabel *whereHint_;
  GeoMap *whereMap_;
};

} // namespace skg
