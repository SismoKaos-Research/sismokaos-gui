// "Get waveforms for <case>": the guided way through the Data page. It lists
// the stations a case needs, says what the study's waveform folders already
// hold for the case's span and what is missing, and gets the missing days in
// one step: it keeps one download for the study (under <study>/downloads),
// writes the requests for every station, registers the folders with the
// study and starts the transfers as jobs. Nothing here asks the user to know
// what a campaign, a ledger or a slot is; the expert view below the panel
// shows the same download with all of that.
//
// The same panel offers to take up a download of the study that was left
// unfinished.
#pragma once

#include <QDate>
#include <QFrame>
#include <QList>
#include <QPair>
#include <QMap>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <memory>

#include "core/module.h"
#include "jobs/taskjob.h"
#include "tdvms/campaign.h"
#include "tdvms/catalog.h"
#include "tdvms/fdsn.h"
#include "tdvms/guide.h"
#include "tdvms/http.h"
#include "tdvms/runner.h"
#include "tdvms/settings.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

namespace skg {

class Job;

class CaseDownloads : public QFrame {
  Q_OBJECT
public:
  // What reaches outside the program, replaceable so that tests never do.
  struct Hooks {
    std::function<std::unique_ptr<tdvms::Http>()> http;
    std::function<TaskJob::Work(const tdvms::Campaign &,
                                const tdvms::RunOptions &,
                                const tdvms::Notify &)>
        portalRun;
    std::function<tdvms::Credentials()> credentials;
    std::function<QString()> address;      // the address requests are made with
    std::function<bool(const QString &)> addressBusy; // another run holds it
    tdvms::FdsnOptions fdsn; // tries and pauses of the FDSN transfers
  };

  enum class Mode {
    Off,    // no study open: the panel is not shown
    Idle,   // a study, nothing asked for: one line saying where to start
    Case,   // the stations and span of a case
    Resume, // an unfinished download of the study, no case in hand
  };

  explicit CaseDownloads(AppContext ctx, QWidget *parent = nullptr);

  void setHooks(const Hooks &hooks);
  void setPortalStations(const QList<tdvms::CatalogStation> &stations);
  // The service and channel pattern FDSN days are asked with (the FDSN
  // form's values).
  void setFdsnDefaults(const QString &service, const QString &channels);
  void setNotify(const tdvms::Notify &notify) { notify_ = notify; }

  // What a case needs: its stations and the days [first, dayAfterLast).
  void setCase(const QString &name, const QStringList &stations,
               const QDate &first, const QDate &dayAfterLast,
               const QString &network);
  // The station list changed from outside (a click on the map).
  void setStations(const QStringList &stations);
  QStringList stations() const { return stations_; }
  // Forgets the case; an unfinished download is offered again.
  void dismiss();

  Mode mode() const { return mode_; }
  bool guiding() const { return mode_ == Mode::Case || mode_ == Mode::Resume; }
  bool running() const;
  bool portalRunning() const;
  // Why "Download the missing data" cannot be pressed now, or nothing.
  QString blockedReason() const;
  // The study's own download; its ledgerPath is empty when there is none.
  tdvms::Campaign download() const;
  // Brings everything up to date with the study, its index and the requests.
  void refresh();

  // The pieces of the one-click download, public for tests: write the
  // requests, start the transfers.
  struct Planned {
    int portalRequests = 0, fdsnDays = 0;
  };
  Planned plan();
  void start();

signals:
  void mailboxRequested();
  // The study's download exists now (or was changed): show it.
  void downloadChanged(const tdvms::Campaign &download);
  void stationsChanged();
  void modeChanged();
  void runningChanged();
  void dismissed();

protected:
  void changeEvent(QEvent *e) override;

private:
  struct Row {
    QString code, network;
    tdvms::Source source = tdvms::Source::Portal;
    bool known = false; // the portal's list holds it
    int have = 0;       // days of the span the study has
    bool approximate = false, atLeast = false;
    QList<QDate> missing;
    // What pressing the button would add.
    QList<QPair<QString, QString>> windows; // portal requests
    QList<QDate> fdsnDays;
    tdvms::RequestProgress progress; // of the requests already written
  };

  void retranslate();
  void rebuild();
  void compute();
  void fillTable();
  void updateTexts();
  void sourceChosen(const QString &code, int index);
  void go();
  void stop();
  void retry();
  void jobEnded();
  void nudgeStudy();
  bool studyOpen() const;
  QString studyDir() const;
  bool mailboxReady() const;
  QString stateText(const Row &row) const;
  QString sourceHelp() const;
  int spanDays() const;

  AppContext ctx_;
  Hooks hooks_;
  tdvms::Notify notify_;
  QList<tdvms::CatalogStation> portal_;
  QString fdsnService_, fdsnChannels_;

  Mode mode_ = Mode::Off;
  QString case_;
  QStringList stations_;
  QDate first_, end_; // end exclusive
  QString network_;
  QMap<QString, tdvms::Source> chosen_; // the user's choice per station
  QList<Row> rows_;
  qint64 portalBytes_ = 0, fdsnBytes_ = 0, freeBytes_ = -1;
  QString diskNote_;
  // What the study's index says about each station of the case, kept until
  // the index or the question changes: asking it day by day is not cheap.
  struct Cover {
    int have = 0;
    bool approximate = false, atLeast = false;
    QList<QDate> missing;
  };
  std::shared_ptr<const void> indexSeen_;
  QString coverKey_;
  QMap<QString, Cover> cover_;
  QString finished_; // how the last transfers ended

  QPointer<Job> portalJob_, fdsnJob_;
  QTimer *ticker_;

  QLabel *title_;
  QLabel *lead_;
  QTableWidget *table_;
  QLabel *summary_;
  QLabel *mailNote_;
  QPushButton *mailButton_;
  QLabel *why_;
  QLabel *done_;
  QPushButton *go_;
  QPushButton *stop_;
  QPushButton *retry_;
  QPushButton *cases_;
  QPushButton *recipes_;
  QPushButton *dismiss_;
  QWidget *body_;
};

} // namespace skg
