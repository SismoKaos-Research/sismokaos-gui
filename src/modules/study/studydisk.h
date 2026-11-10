// "Study on disk": what the open study's results take, per case, tool and
// recipe, and what is left over (working folders of runs that crashed, what
// failed runs left, unpacked archives, results of removed cases and recipes),
// with a way to remove the leftovers. A finished result of the study as it
// is, and anything of a run that is queued or going on, is never removed.
#pragma once

#include <QDialog>
#include <QList>

#include <functional>

#include "core/module.h"
#include "study/runinfo.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace skg {

class StudyDiskDialog : public QDialog {
  Q_OBJECT
public:
  explicit StudyDiskDialog(AppContext &ctx, QWidget *parent = nullptr);

  // The question before anything is removed (a message box by default), so
  // that a test can answer.
  std::function<bool(const QString &title, const QString &text)> confirm;
  // True while the folders are being measured (off the GUI thread).
  bool loading() const { return loading_; }
  QList<study::DiskItem> items() const { return items_; }
  // What "Clean up leftovers…" would remove now.
  QList<study::DiskItem> leftovers() const;
  // Measures again.
  void refresh();
  // Asks, removes the leftovers, measures again. Returns the bytes freed.
  qint64 cleanUp();

private:
  void fill();

  AppContext ctx_;
  bool loading_ = false;
  int generation_ = 0;
  QList<study::DiskItem> items_;
  QLabel *summary_;
  QLabel *note_;
  QTableWidget *table_;
  QPushButton *clean_;
};

// Opens the dialog for the open study (for the Runs page, and for a File
// menu entry). Does nothing when no study is open.
void showStudyOnDisk(AppContext &ctx, QWidget *parent);

} // namespace skg
