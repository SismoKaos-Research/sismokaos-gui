// The Classification tab (the sfc port): a project folder with its
// project.toml, the labelled windows (sfc's CSV, or an fx feature table
// labelled by the class table around the event), the experiment settings,
// the run as a job, and the experiments with their figures and report.
#pragma once

#include <QWidget>

#include <memory>
#include <optional>

#include <QVariantMap>

#include "core/module.h"
#include "fx/npy.h"
#include "sfc/dataset.h"
#include "sfc/settings.h"

class QLineEdit;
class QPushButton;
class QLabel;
class QComboBox;
class QTabWidget;
class QPlainTextEdit;
class QListWidget;
class QSpinBox;
class QDateTimeEdit;

namespace skg {

class Job;
class SettingsPane;
class ExperimentsPane;

// What the panes share.
class StudyInputsBar;

struct ClassificationState {
  QString projectDir;
  QString projectFile; // <project>/project.toml
  sfc::Settings settings;
  QString sourcePath; // the CSV or table
  bool sourceIsSfcCsv = true;
  std::optional<fx::NpyTable> table;   // when the source is an fx table
  QStringList tableColumns;            // numeric columns of the table
  std::optional<sfc::Dataset> dataset; // labelled, before prepareClasses
  QStringList dataLog;
};

class ClassificationModule : public Module {
  Q_OBJECT
public:
  using Module::Module;
  QString id() const override { return QStringLiteral("classification"); }
  QString title() const override { return tr("Classification"); }
  QString description() const override {
    return tr("Classifying the windows by their time to the event: split by "
              "day, tuned models, metrics, "
              "experiments and reports");
  }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;
};

class ClassificationWidget : public QWidget {
  Q_OBJECT
public:
  ClassificationWidget(AppContext ctx, QWidget *parent = nullptr);

  // The page's settings as a study recipe's (study/runs.h lists the keys).
  // The station and the event are the case's, so they are left out.
  QVariantMap recipeSettings() const;

protected:
  void changeEvent(QEvent *e) override;
  void showEvent(QShowEvent *e) override;

private:
  void retranslate();
  void chooseProject();
  void setProject(const QString &dir);
  void chooseSource();
  void loadSource();
  void sourceLoaded(const QString &path,
                    std::shared_ptr<std::optional<fx::NpyTable>> table,
                    const QString &error);
  void applyStudyInputs();
  void rebuildDataset();
  void saveProject();
  void startRun();
  void stop();
  void updateControls();
  void say(const QString &message, bool warning = false);

  AppContext ctx_;
  std::shared_ptr<ClassificationState> state_;
  StudyInputsBar *studyBar_;
  bool shownOnce_ = false; // the last project and table load on first show
  Job *running_ = nullptr;

  QLineEdit *project_, *source_;
  QPushButton *projectBrowse_, *sourceBrowse_, *save_, *run_, *stop_;
  QLabel *projectLabel_, *sourceLabel_, *status_, *dataInfo_;
  QListWidget *steps_;
  QTabWidget *tabs_;
  SettingsPane *settingsPane_;
  ExperimentsPane *experimentsPane_;
  QPlainTextEdit *log_;
};

} // namespace skg
