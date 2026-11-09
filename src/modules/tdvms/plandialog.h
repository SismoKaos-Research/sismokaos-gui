// Plan a station's windows into a campaign: station from the catalog,
// dates, window length, optional sampling, optional check against the portal.
#pragma once

#include <QDialog>

class QComboBox;
class QDateEdit;
class QSpinBox;
class QCheckBox;
class QLabel;

namespace skg {

struct PlanRequest {
  QString station;
  QString startIso, endIso;
  int chunkDays = 21;
  int everyDays = 0; // 0: cover the period
  bool verify = true;
};

class PlanDialog : public QDialog {
  Q_OBJECT
public:
  explicit PlanDialog(const QString &ledgerPath, QWidget *parent = nullptr);
  PlanRequest request() const;

private:
  void updateHint();

  QString ledgerPath_;
  QComboBox *station_;
  QDateEdit *start_;
  QDateEdit *end_;
  QSpinBox *chunkDays_;
  QSpinBox *everyDays_;
  QCheckBox *verify_;
  QLabel *hint_;
};

} // namespace skg
