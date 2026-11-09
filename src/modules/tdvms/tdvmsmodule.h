// The tdvms tab: campaigns under a root folder, the chunk table, the run
// controls, and the Coverage and FDSN panes. Everything runs in-process
// through the skg_tdvms library; nothing shells out.
#pragma once

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
  QStringList requiredTools() const override { return {}; }
  QWidget *createWidget(AppContext &ctx, QWidget *parent) override;
};

class TdvmsWidget : public QWidget {
  Q_OBJECT
public:
  TdvmsWidget(AppContext ctx, QWidget *parent = nullptr);

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
};

} // namespace skg
