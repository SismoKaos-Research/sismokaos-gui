// The Features tab: a data directory of miniSEED (and zips), a config
// (config.json, Rust or Python layout), the editor, and the runs: full
// extraction (calibration + features), preprocess only, dry run. Everything
// runs in-process through skg_fx as a TaskJob; results are listed beside.
#pragma once

#include <QWidget>

#include "core/module.h"
#include "fx/config.h"

class QLineEdit;
class QPushButton;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QTabWidget;

namespace skg {

class Job;
class ConfigPane;
class ResultsPane;

class FeaturesModule : public Module {
  Q_OBJECT
public:
  using Module::Module;
  QString id() const override { return QStringLiteral("features"); }
  QString title() const override { return tr("Features"); }
  QString description() const override {
    return tr("Preprocessing and feature extraction from miniSEED, with the "
              "chaos parameters "
              "calibrated on quiet periods");
  }
  QStringList requiredTools() const override { return {}; }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;
};

class FeaturesWidget : public QWidget {
  Q_OBJECT
public:
  FeaturesWidget(AppContext ctx, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *e) override;

private:
  enum class Mode { Run, PreprocessOnly, DryRun };
  void retranslate();
  void chooseDir(QLineEdit *target);
  void loadConfig();
  void saveConfig();
  void start(Mode mode);
  void stop();
  void updateControls();
  void say(const QString &line);
  fx::Config assemble() const;

  AppContext ctx_;
  Job *running_ = nullptr;

  QLineEdit *dataDir_, *outDir_, *configPath_;
  QPushButton *dataBrowse_, *outBrowse_, *configLoad_, *configSave_;
  QCheckBox *reuseCalibration_;
  QLineEdit *calibrationFile_;
  QPushButton *calibrationBrowse_;
  QLineEdit *bandFilter_;
  QPushButton *run_, *preprocess_, *dryRun_, *stop_;
  QLabel *runNote_;
  QPlainTextEdit *status_;
  QLabel *dataLabel_, *outLabel_, *configLabel_, *bandsLabel_;
  ConfigPane *config_;
  ResultsPane *results_;
  QTabWidget *tabs_;
};

} // namespace skg
