// The Indicators tab (the catalog-based-indicators port): an AFAD catalog,
// which events to keep (fault zone, start date, homogenised magnitudes,
// declustering), the window and smoothing settings, the run as a job, and
// the resulting table and figures.
#pragma once

#include <QWidget>

#include <memory>

#include <QVariantMap>

#include "cbi/run.h"
#include "core/module.h"

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;

namespace skg {

class Job;
class IndicatorsResultsPane;

// What the panes share: the last run's result.
struct IndicatorsState {
  cbi::RunResult result;
  bool hasResult = false;
};

class StudyInputsBar;

class IndicatorsModule : public Module {
  Q_OBJECT
public:
  using Module::Module;
  QString id() const override { return QStringLiteral("indicators"); }
  QString title() const override { return tr("Indicators"); }
  QString description() const override {
    return tr("Sliding-window seismicity indicators from an earthquake "
              "catalog: b- and a-value, magnitude deficit, energy release, "
              "η, μ, with moving averages and Bollinger bands");
  }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;
};

class IndicatorsWidget : public QWidget {
  Q_OBJECT
public:
  IndicatorsWidget(AppContext ctx, QWidget *parent = nullptr);

  // The page's settings as a study recipe's (study/runs.h lists the keys).
  // The catalog, the zone and the dates are the case's, so they are left
  // out. Empty with `error` set when a setting has no recipe form (a window
  // that is not in months).
  QVariantMap recipeSettings(QString *error) const;

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void applyStudyInputs();
  void loadSettings();
  void saveSettings();
  bool options(cbi::RunOptions *o, QString *err) const;
  void browse(QLineEdit *edit, const QString &filter, bool dir = false);
  void describeCatalog();
  void estimateMc();
  void fitConversion();
  void startRun();
  void stop();
  void updateControls();
  void say(const QString &message, bool warning = false);

  AppContext ctx_;
  StudyInputsBar *studyBar_;
  std::shared_ptr<IndicatorsState> state_;
  Job *running_ = nullptr;

  QLineEdit *catalog_, *zoneFile_, *strands_, *conversion_, *ma_, *outRoot_;
  QPushButton *catalogBrowse_, *zoneBrowse_, *conversionBrowse_, *outBrowse_,
      *describe_, *estimateMc_, *fit_, *run_, *stop_;
  QComboBox *zone_, *windowUnit_, *stepUnit_;
  QDoubleSpinBox *buffer_, *lat_, *lon_, *radius_, *mc_, *magBin_, *sigma_,
      *bbK_, *labelMag_;
  QSpinBox *window_, *step_, *bbWindow_, *labelDays_, *dpi_;
  QCheckBox *useStart_, *homogenize_, *decluster_, *maCombined_, *bollinger_,
      *plots_;
  QDateEdit *start_;
  QLabel *catalogLabel_, *zoneLabel_, *zoneFileLabel_, *strandsLabel_,
      *bufferLabel_, *circleLabel_, *startLabel_, *conversionLabel_, *mcLabel_,
      *magBinLabel_, *windowLabel_, *stepLabel_, *sigmaLabel_, *maLabel_,
      *bbLabel_, *labelLabel_, *outLabel_, *dpiLabel_, *status_;
  QWidget *circleRow_, *windowRow_, *stepRow_, *bbRow_, *labelRow_;
  QTabWidget *tabs_;
  IndicatorsResultsPane *results_;
  QPlainTextEdit *log_;
};

} // namespace skg
