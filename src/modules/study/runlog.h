// The log of one run (run.log), in a window that keeps up with a run that is
// still writing it: new lines are added at the end, and the view follows
// them until the reader scrolls up to look at something.
#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTimer;

namespace skg {

class RunLogDialog : public QDialog {
  Q_OBJECT
public:
  // `path` may not exist yet (a run that has not written its first line).
  explicit RunLogDialog(const QString &path, QWidget *parent = nullptr);

  QString path() const { return path_; }
  // Whether the view moves to the end when lines arrive.
  bool following() const;
  // Reads what the file has gained since the last look; a file that became
  // shorter (a new run of the cell) is read again from its start.
  void refresh();
  // Selects the next place `text` occurs in (wrapping round); false when
  // there is none.
  bool findNext(const QString &text);
  QString text() const;

private:
  QString path_;
  qint64 read_ = 0; // bytes of the file already shown
  QLabel *where_;
  QPlainTextEdit *view_;
  QCheckBox *follow_;
  QLineEdit *search_;
  QLabel *status_;
  QTimer *timer_;
};

} // namespace skg
