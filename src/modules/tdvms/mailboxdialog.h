// Mailbox credentials and the run defaults (slots, notifications), saved to
// the same mailbox.env / settings.env the Python tool uses.
#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;

namespace skg {

class JobModel;

class MailboxDialog : public QDialog {
  Q_OBJECT
public:
  explicit MailboxDialog(JobModel *jobs, QWidget *parent = nullptr);

private:
  void check();
  void save();

  JobModel *jobs_;
  QLineEdit *host_;
  QLineEdit *user_;
  QLineEdit *password_;
  QSpinBox *slots_;
  QLineEdit *notify_;
  QLabel *source_;
  QPushButton *check_;
};

} // namespace skg
