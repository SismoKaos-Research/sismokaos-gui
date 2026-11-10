// Plan a station's windows into a campaign: station from the catalog,
// dates, window length, optional sampling, optional check against the portal.
#pragma once

#include <QDialog>

class QComboBox;
class QDateEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QDate;

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
  // Starts the dialog on a station and a period (a case's, from Cases).
  void prefill(const QString &station, const QDate &from, const QDate &to);
  // Why Plan cannot be pressed now, or nothing.
  QString blockedReason() const;

signals:
  // The station in the dialog changed (its code, or empty).
  void stationPicked(const QString &code);

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
  QPushButton *ok_ = nullptr;
};

} // namespace skg
