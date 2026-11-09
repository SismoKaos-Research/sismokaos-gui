// The Early warning tab: a launcher for ayzek (third_party/ayzek, GPL-3.0-
// or-later), which is shipped beside the application and not ported. The
// tab builds ayzek's command line from a form (ayzek/launcher.h), runs it as
// a ProcessJob, and shows the alarms, magnitudes and locations it prints as
// a table beside the full log, with the counts of its final report.
#pragma once

#include <QAbstractTableModel>
#include <QWidget>

#include <vector>

#include "ayzek/launcher.h"
#include "core/module.h"

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableView;

namespace skg {

class Job;

class EarlyWarningModule : public Module {
  Q_OBJECT
public:
  using Module::Module;
  QString id() const override { return QStringLiteral("earlywarning"); }
  QString title() const override { return tr("Early warning"); }
  QString description() const override {
    return tr("Runs ayzek, the earthquake early-warning pipeline, on "
              "replayed miniSEED files or a live SeedLink stream");
  }
  QStringList requiredTools() const override {
    return {QStringLiteral("ayzek")};
  }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;
};

// The tagged lines of a run (ALARM, PLOC, PDMAG, LOCATE, ...), newest last.
class AyzekEventsModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column { Tag, Alarm, Time, Text, ColumnCount };
  explicit AyzekEventsModel(QObject *parent = nullptr);
  void clear();
  void append(const ayzek::Event &e);
  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
  std::vector<ayzek::Event> rows_;
};

class EarlyWarningWidget : public QWidget {
  Q_OBJECT
public:
  EarlyWarningWidget(AppContext ctx, QWidget *parent = nullptr);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void refreshInstall();
  void chooseExecutable();
  void addFiles();
  void removeFiles();
  void browseInto(QLineEdit *field, bool directory, const QString &filter);
  ayzek::Options options() const;
  void loadSettings();
  void saveSettings() const;
  void modeChanged();
  void run();
  void stop();
  void onLine(const QString &line);
  void finished();
  void openOutputDir();
  void say(const QString &message, bool warning = false);
  void updateControls();

  AppContext ctx_;
  Job *running_ = nullptr;
  QString modelsDirInUse_;

  // Installation
  QLabel *installLabel_, *install_;
  QPushButton *installBrowse_, *installReset_;
  // Run
  QTabWidget *tabs_;
  QLabel *modeLabel_;
  QComboBox *mode_;
  QGroupBox *replayBox_, *liveBox_, *commonBox_;
  QListWidget *files_;
  QPushButton *addFiles_, *removeFiles_;
  QLabel *speedLabel_, *fromLabel_, *catalogLabel_;
  QDoubleSpinBox *speed_;
  QLineEdit *from_, *catalog_;
  QPushButton *catalogBrowse_;
  QLabel *serverLabel_, *streamsLabel_, *archiveLabel_, *durationLabel_;
  QLineEdit *server_, *streams_, *archive_;
  QPushButton *archiveBrowse_;
  QSpinBox *duration_;
  QLabel *detectorLabel_, *minStationsLabel_, *thresholdLabel_,
      *triggerWindowsLabel_, *instantLabel_, *releaseLabel_, *locateLabel_,
      *outputLabel_, *sitesLabel_, *stationsLabel_, *modelsLabel_, *extraLabel_;
  QComboBox *detector_, *locate_;
  QSpinBox *minStations_, *triggerWindows_;
  QDoubleSpinBox *threshold_, *instant_, *release_;
  QCheckBox *assess_, *scores_, *verbose_, *noMagnitude_;
  QLineEdit *output_, *stations_, *models_, *extra_;
  QPushButton *outputBrowse_, *stationsBrowse_, *modelsBrowse_;
  QPlainTextEdit *sites_;
  QPushButton *run_, *stop_;
  QLabel *status_;
  // Results
  AyzekEventsModel *events_;
  QTableView *eventsView_;
  QLabel *summary_;
  QPlainTextEdit *log_;
  QPushButton *openOutput_;
  QLabel *licence_;
};

} // namespace skg
