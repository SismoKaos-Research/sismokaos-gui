// The Runs page: which recipe has been run on which case. A matrix of
// recipes (columns) by case and station (rows); the recipes are made and
// edited here, and the missing and stale cells are queued as jobs.
//
// A cell is run, stopped or taken out of the queue with one button, or from
// the menu a right click opens on a cell, a row's name or a column's heading
// (run this case, run this recipe, duplicate a recipe). Several cells can be
// selected (Ctrl or Shift and a click) and run or stopped together. Under the
// matrix, "Activity" lists what ran, when, for how long and how it ended,
// read from the run folders, so it is there after a restart too.
//
// The cells are read from disk when something that can change them happens
// (the study is edited, a job ends, the page is shown) and kept; while jobs
// run, only their progress is redrawn.
#pragma once

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <atomic>
#include <functional>
#include <memory>

#include "core/module.h"
#include "study/runs.h"
#include "study/study.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSettings;
class QStackedWidget;
class QTableWidget;
class QTabWidget;
class QTimer;
class QToolButton;

namespace skg {

class Job;
class Theme;

// One cell as the matrix draws it.
struct MatrixCell {
  study::CellState state = study::CellState::Missing;
  QString why;
  bool running = false;
  double fraction = -1;
  QString doing; // running: the phase the run is in, as its job says it
  qint64 elapsedMs = -1; // running: for how long
  // Waiting for a free slot, or for the cell it stands on: place in the
  // queue (1 is next), and what it waits for when that is another cell.
  bool queued = false;
  int place = 0;
  QString waitsFor;
  // Something to know about the run (Cell::warnings), one sentence a line.
  QStringList warnings;
};

// Mouse: a click selects a cell (or a column, on its heading), a double
// click opens it; Ctrl and a click adds a cell to the selection or takes it
// out, Shift and a click selects the block from the last cell clicked; a
// right click asks for the menu of what is under it. Keyboard, when the
// matrix has the focus: the arrow keys move the selection, Enter or Space
// activates the cell, Ctrl+A selects every cell, the Menu key asks for the
// selected cell's menu.
class RunMatrix : public QWidget {
  Q_OBJECT
public:
  explicit RunMatrix(Theme *theme, QWidget *parent = nullptr);

  void setContents(const QStringList &rowLabels,
                   const QList<QPair<QString, QString>> &columns, // name, tool
                   const QList<QList<MatrixCell>> &cells);
  // The tooltip of a cell: state, queue place, reason, warnings.
  static QString cellTip(const MatrixCell &c);
  int selectedRow() const { return row_; }
  int selectedColumn() const { return column_; }
  void select(int row, int column);
  // Every selected cell (the one select() chose and those added to it), as
  // row and column.
  QList<QPair<int, int>> selectedCells() const;
  // Adds the cell to the selection, or takes it out when it is in it.
  void toggle(int row, int column);
  // What a point of the widget is on: a cell (both set), a row's name
  // (column -1), a column's heading (row -1), or nothing (false).
  bool hit(const QPointF &pos, int *row, int *column) const;
  // The full text of a row's name or a column's heading, for the tooltip.
  QString labelTip(int row, int column) const;
  QSize minimumSizeHint() const override;
  QSize sizeHint() const override { return minimumSizeHint(); }

  // The glyph and word for a state, e.g. "✓  Done" (translated).
  static QString stateText(const MatrixCell &c);

signals:
  void selectionChanged();
  void activated(); // Enter or Space on the selected cell
  void opened();    // a double click on a cell
  // A right click (or the Menu key): on a cell, a row's name (column -1) or
  // a column's heading (row -1).
  void menuRequested(int row, int column, const QPoint &globalPos);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *e) override;
  void mouseDoubleClickEvent(QMouseEvent *e) override;
  void contextMenuEvent(QContextMenuEvent *e) override;
  bool event(QEvent *e) override; // the cell's tooltip
  void keyPressEvent(QKeyEvent *e) override;
  void focusInEvent(QFocusEvent *e) override;
  void focusOutEvent(QFocusEvent *e) override;

private:
  QRectF cellRect(int row, int column) const;

  Theme *theme_;
  QStringList rows_;
  QList<QPair<QString, QString>> columns_;
  QList<QList<MatrixCell>> cells_;
  int row_ = -1, column_ = -1;
  QSet<qint64> more_; // further selected cells, row * 65536 + column
};

// Name and settings of one recipe. Values are typed as text and keep the
// type the setting had; the features recipe a classification builds on is
// chosen from a list.
class RecipeDialog : public QDialog {
  Q_OBJECT
public:
  RecipeDialog(const study::Study &s, const study::Recipe &r,
               QWidget *parent = nullptr);
  // The edited recipe; the revision is one higher when a setting changed.
  study::Recipe recipe() const { return recipe_; }
  void accept() override;

  // Puts `text` (a file's contents) into the text setting `key`. A features
  // configuration that names a calibration catalog of its own keeps it only
  // when `keepOwnCatalog`; otherwise the name is cleared and the study's
  // catalog is used. Returns false when the recipe has no such text setting.
  bool loadText(const QString &key, const QString &text, bool keepOwnCatalog);
  // The catalog the configuration in the dialog names for itself, or empty.
  QString ownCatalog() const;
  // Where the file dialog's last folder is remembered (may stay null).
  QSettings *settings = nullptr;

private:
  void loadFile();
  void showOverride();
  int rowOf(const QString &key) const;

  study::Recipe recipe_;
  QLineEdit *name_;
  QTableWidget *table_;
  QComboBox *featuresBox_ = nullptr; // classification: the features recipe
  QComboBox *channelsBox_ = nullptr; // features: the sensor to read
  QLabel *override_ = nullptr;       // features: the catalog override, if any
  QPushButton *clearOverride_ = nullptr;
};

class RunsPage : public QWidget {
  Q_OBJECT
public:
  RunsPage(AppContext &ctx, QWidget *parent = nullptr);

  // Every yes/no question of the page is asked through this (a message box
  // by default), so that a test can answer.
  std::function<bool(const QString &title, const QString &text)> confirm;
  // Whether a batch ("Run missing and stale") is going on.
  bool batchRunning() const { return runAll_; }
  // The cells waiting for a free slot, in order ("case|station|recipe").
  QStringList queued() const;
  // How many cells run at the same time (the setting, at least 1).
  int atOnce() const;
  // Free bytes on the disk that holds a folder (-1: unknown); the disk is
  // asked by default, a test answers instead.
  std::function<qint64(const QString &dir)> freeSpace;
  // The menu of a cell, of a row's name (column -1) or of a column's heading
  // (row -1), as a right click opens it. The caller owns it. Its actions
  // have object names (actRun, actStop, actLog, actFolder, actCopyPath,
  // actCases, actRecalibrate, actRunSelected, actStopSelected, actRunRow,
  // actRunCase, actRunRecipe, actEdit, actDuplicate, actRemove).
  QMenu *menuFor(int row, int column);
  // Queues the cells that can run (row, column), after the questions a
  // direct request asks: runs that are done already, temporary space.
  void runCells(const QList<QPair<int, int>> &cells, bool fresh = false);
  void runRow(int row);       // every recipe on this row
  void runCase(int row);      // every recipe on every row of this row's case
  void runColumn(int column); // this recipe on every row
  // Stops the cell's run, or takes it out of the queue; the other runs and
  // the queue go on.
  void stopCells(const QList<QPair<int, int>> &cells);
  // A copy of the selected column's recipe under a new name, revision 1.
  // `name` empty: asked for. Returns the new recipe's id, or empty.
  QString duplicateRecipe(const QString &name = QString());
  ~RunsPage() override;

protected:
  void changeEvent(QEvent *e) override;
  void showEvent(QShowEvent *e) override;

private:
  struct Row {
    QString caseId, station;
  };
  void retranslate();
  struct Want {
    Row row;
    QString recipeId;
  };
  // Queues what of `wants` can run. `bulk`: asked for as a group (a row, a
  // column, a selection), which is confirmed with its count.
  void request(const QList<Want> &wants, bool fresh, bool bulk);
  // Whether the disk has room for what the cells unpack, beside what is
  // queued; asks or refuses with the figures when it is short.
  bool spaceAllows(const QList<Want> &wants);
  void stopKeys(const QStringList &keys);
  void fillActivity();
  void syncLive(); // tells LiveRuns which run folders are queued or going on
  // Tells the shell how many cells wait here (status strip, quit question).
  void reportQueue(int waiting);
  // Why nothing can be run into this study now, or empty: its folder is not
  // locked for this window (read-only, or open in another window).
  QString lockProblem() const;
  QString dirOf(const QString &key) const;
  void showMenu(int row, int column, const QPoint &globalPos);
  void reload(); // reads every cell from disk, then render()
  // Redraws from what was read and the running jobs; `selection` also the
  // selected cell's line and the recipe's settings.
  void render(bool selection = true);
  void showSelection();
  void newRecipe(const QString &tool);
  void editRecipe();
  void removeRecipe();
  void runAll();
  // `fresh`: a features cell computes its calibration anew.
  void runSelected(bool fresh = false);
  void stopBatch();
  void fixSelected();
  void showLog();
  void openSelectedFolder();
  // Starts queued cells while slots are free; in a batch, first queues what
  // has become runnable.
  void pump();
  void enqueue(const Row &row, const study::Recipe &r, bool fresh = false);
  bool dequeue(const QString &key);
  bool start(const Row &row, const study::Recipe &r, bool fresh = false);
  static QString key(const QString &caseId, const QString &station,
                     const QString &recipeId);
  QString keyOf(int row, const study::Recipe &r) const;
  bool busy(const QString &key) const;
  const study::Cell *cellAt(int row, int column) const;
  QList<QPair<Row, QString>> runnable() const; // row, recipe id

  AppContext ctx_;
  QList<Row> rows_;
  QList<QList<study::Cell>> cells_; // rows_ by the study's recipes
  QHash<QString, QPointer<Job>> running_;
  // Set while a cell's worker is still in its run, also after the job was
  // cancelled: the same cell is not started twice.
  QHash<QString, std::shared_ptr<std::atomic<bool>>> working_;
  QSet<QString> attempted_;
  bool runAll_ = false;
  // Cells asked for, one by one or by a batch, that wait for a free slot.
  struct Queued {
    Row row;
    QString recipeId, key;
    bool fresh = false; // features: with a new calibration
  };
  QList<Queued> queue_;
  // Runs stopped one by one: their ending does not end the batch.
  QSet<QString> stoppingOne_;
  QSet<QString> live_; // the run folders entered in LiveRuns
  // The batch going on: when it began, what it has finished, and how long
  // those took together (for "about … left").
  QDateTime batchStarted_;
  int batchDone_ = 0, batchAsked_ = 0;
  qint64 batchMs_ = 0;
  QString spaceNote_; // a queued cell that waits for disk space, in words
  bool spaceWaits_ = false; // the note is about a cell still in the queue

  QStackedWidget *stack_;
  QLabel *noStudy_;
  QPushButton *run_, *stop_, *runOne_, *recal_, *edit_, *remove_;
  QPushButton *fix_, *showLog_, *openFolder_;
  QToolButton *new_;
  QLabel *hint_;
  QLabel *guide_;
  QPushButton *guideGo_;
  RunMatrix *matrix_;
  QLabel *cellInfo_;
  QLabel *recipeTitle_;
  QTableWidget *recipeTable_;
  QLineEdit *filter_;
  QComboBox *show_;
  QLabel *shown_;
  QPushButton *disk_;
  QTabWidget *tabs_;
  QTableWidget *activity_;
  QLabel *zone_;
  QTimer *tick_;
};

} // namespace skg
