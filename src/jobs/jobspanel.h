// The dock at the bottom of the window: the table of jobs on the left, the
// log of the selected job on the right, cancel and clear buttons.
#pragma once

#include <QWidget>

class QTableView;
class QPlainTextEdit;
class QPushButton;
class QLabel;

namespace skg {

class JobModel;
class Job;

class JobsPanel : public QWidget {
  Q_OBJECT
public:
  explicit JobsPanel(JobModel *model, QWidget *parent = nullptr);

  void showJob(Job *job);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void onSelectionChanged();
  void appendLine(const QString &line);
  void updateButtons();

  JobModel *model_;
  Job *current_ = nullptr;
  QTableView *table_;
  QPlainTextEdit *log_;
  QPushButton *cancel_;
  QPushButton *copy_;
  QPushButton *clear_;
  QLabel *summary_;
};

} // namespace skg
