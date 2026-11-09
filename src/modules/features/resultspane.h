// The outputs of feature-extraction runs in a directory: feature tables
// (.npy/.csv), calibration and run metadata (.json), preprocessed streams
// (.f32 + sidecar), listed and previewed.
#pragma once

#include <QWidget>

class QListWidget;
class QListWidgetItem;
class QTableView;
class QPlainTextEdit;
class QStackedWidget;
class QLabel;
class QPushButton;

namespace skg {

class NpyModel;

class ResultsPane : public QWidget {
  Q_OBJECT
public:
  explicit ResultsPane(QWidget *parent = nullptr);
  void setDirectory(const QString &dir);
  void refresh();

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void preview(QListWidgetItem *item);
  void showText(const QString &text);

  QString dir_;
  QListWidget *files_;
  QPushButton *refresh_, *open_;
  QLabel *summary_;
  QStackedWidget *stack_;
  QTableView *table_;
  QPlainTextEdit *text_;
  NpyModel *model_;
};

} // namespace skg
