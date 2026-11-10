// The tdvms tab: campaigns under a root folder, the chunk table, the run
// controls, and the Coverage and FDSN panes. Everything runs in-process
// through the skg_tdvms library; nothing shells out.
#pragma once

#include <QDate>
#include <QVariantMap>
#include <QSystemTrayIcon>
#include <QWidget>

#include "core/module.h"
#include "tdvms/campaign.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QPlainTextEdit;
class QTableView;
class QTabWidget;
class QLabel;
class QSortFilterProxyModel;
class QTimer;

namespace skg {

class Job;
class LedgerModel;
class CoveragePane;
class FdsnPane;

class TdvmsModule : public Module {
  Q_OBJECT
public:
  using Module::Module;
  QString id() const override { return QStringLiteral("tdvms"); }
  QString title() const override { return tr("Data"); }
  QString description() const override {
    return tr("AFAD/TDVMS download campaigns and FDSN pulls");
  }
  // The second step of a study: getting the waveforms of its cases.
  Section section() const override { return Section::Study; }
  QStringList requiredTools() const override { return {}; }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;
};

class TdvmsWidget : public QWidget {
  Q_OBJECT
public:
  TdvmsWidget(AppContext ctx, QWidget *parent = nullptr);

  // What a case needs (sent by the Cases page through the Navigator): keys
  // "stations" (list of codes), "start" and "end" (ISO UTC), and optionally
  // "network" and "case" (its name, shown). Fills the FDSN form and the plan
  // dialog; the bar at the top says what is wanted.
  void setWanted(const QVariantMap &args);
  // The folder a download files its archives under becomes one of the open
  // study's waveform folders, so the Cases and Runs pages find them. True
  // when the study did not know it yet.
  bool registerDownloadFolder(const QString &dir);

protected:
  void changeEvent(QEvent *e) override;

private:
  void retranslate();
  void chooseRoot();
  void setRoot(const QString &root);
  void refreshCampaigns();
  void selectCampaign(int index);
  void reloadLedger();
  void newCampaign();
  void plan();
  void applyWanted();
  void ingest();
  void resetSelected();
  void startRun();
  void stopRun();
  void mailbox();
  void refreshCatalogs();
  void updateControls();
  void notifyDesktop(const QString &title, const QString &body, bool urgent);
  tdvms::Campaign current() const;

  AppContext ctx_;
  QList<tdvms::Campaign> campaigns_;
  Job *running_ = nullptr;
  QTimer *ticker_;

  QLineEdit *root_;
  QPushButton *rootBrowse_;
  QComboBox *campaign_;
  QPushButton *newCampaign_;
  QPushButton *plan_;
  QPushButton *ingest_;
  QPushButton *refresh_;
  QPushButton *catalogs_;

  QLineEdit *address_;
  QSpinBox *slots_;
  QSpinBox *interval_;
  QCheckBox *exitWhenDone_;
  QCheckBox *dryRun_;
  QCheckBox *once_;
  QPushButton *mailbox_;
  QPushButton *start_;
  QPushButton *stop_;
  QLabel *runNote_;
  QPlainTextEdit *status_;

  QTabWidget *tabs_;
  LedgerModel *ledger_;
  QSortFilterProxyModel *filter_;
  QComboBox *stateFilter_;
  QTableView *table_;
  QPushButton *reset_;
  CoveragePane *coverage_;
  FdsnPane *fdsn_;
  QSystemTrayIcon *tray_ = nullptr;

  QWidget *wantedBar_;
  QLabel *wantedText_;
  QComboBox *wantedStation_;
  QPushButton *wantedPlan_;
  QPushButton *wantedFdsn_;
  QPushButton *wantedClose_;
  QStringList wantedStations_;
  QDate wantedStart_, wantedEnd_;
  QString wantedNetwork_, wantedCase_;
  QPushButton *showJobs_;
  QString registered_; // the folder last added to the study, for the note
};

} // namespace skg
