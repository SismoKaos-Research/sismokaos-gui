// The Runs page: which recipe has been run on which case. A matrix of
// recipes (columns) by case and station (rows); the recipes are made and
// edited here, and the missing and stale cells are queued as jobs.
#pragma once

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include "core/module.h"
#include "study/runs.h"
#include "study/study.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
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
};

class RunMatrix : public QWidget {
  Q_OBJECT
public:
  explicit RunMatrix(Theme *theme, QWidget *parent = nullptr);

  void setContents(const QStringList &rowLabels,
                   const QList<QPair<QString, QString>> &columns, // name, tool
                   const QList<QList<MatrixCell>> &cells);
  int selectedRow() const { return row_; }
  int selectedColumn() const { return column_; }
  void select(int row, int column);
  QSize minimumSizeHint() const override;
  QSize sizeHint() const override { return minimumSizeHint(); }

  // The glyph and word for a state, e.g. "✓  Done" (translated).
  static QString stateText(const MatrixCell &c);

signals:
  void selectionChanged();

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *e) override;

private:
  QRectF cellRect(int row, int column) const;

  Theme *theme_;
  QStringList rows_;
  QList<QPair<QString, QString>> columns_;
  QList<QList<MatrixCell>> cells_;
  int row_ = -1, column_ = -1;
};

// Name and settings of one recipe. Values are typed as text and keep the
// type the setting had.
class RecipeDialog : public QDialog {
  Q_OBJECT
public:
  RecipeDialog(const study::Study &s, const study::Recipe &r,
               QWidget *parent = nullptr);
  // The edited recipe; the revision is one higher when a setting changed.
  study::Recipe recipe() const { return recipe_; }
  void accept() override;

private:
  void loadFile();

  study::Recipe recipe_;
  QLineEdit *name_;
  QTableWidget *table_;
};

class RunsPage : public QWidget {
  Q_OBJECT
public:
  RunsPage(AppContext &ctx, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *e) override;

private:
  struct Row {
    QString caseId, station;
  };
  void retranslate();
  void refresh();
  void showSelection();
  void newRecipe(const QString &tool);
  void editRecipe();
  void removeRecipe();
  void runAll();
  void runSelected();
  void pump();
  bool start(const Row &row, const study::Recipe &r);
  static QString key(const QString &caseId, const QString &station,
                     const QString &recipeId);
  QList<QPair<Row, QString>> runnable() const; // row, recipe id

  AppContext ctx_;
  QList<Row> rows_;
  QHash<QString, QPointer<Job>> running_;
  QSet<QString> attempted_;
  bool runAll_ = false;

  QStackedWidget *stack_;
  QLabel *noStudy_;
  QPushButton *run_, *runOne_, *edit_, *remove_, *openFolder_;
  QToolButton *new_;
  QLabel *hint_;
  RunMatrix *matrix_;
  QLabel *cellInfo_;
  QLabel *recipeTitle_;
  QTableWidget *recipeTable_;
  QTimer *tick_;
};

} // namespace skg
