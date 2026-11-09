// The campaign loop as work for a TaskJob: cycles until stopped, or until
// every chunk has an outcome. Port of tdvms/run.py's loop and queue.py, and
// of notify.py's Watch (conditions raised once and cleared once).
#pragma once

#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

#include "jobs/taskjob.h"
#include "tdvms/campaign.h"
#include "tdvms/settings.h"
#include "tdvms/supervisor.h"

namespace skg::tdvms {

struct RunOptions {
  QString address; // base address; the pool extends it
  int slotCount = kDefaultSlots;
  int intervalSeconds = 60;
  bool once = false;
  bool exitWhenDone = false;
  QString folder = QStringLiteral("INBOX");
  QString search = QStringLiteral("(UNSEEN)");
  int portalTimeoutMs = 180000;
  SupervisorConfig supervisor;
  Credentials mailbox; // empty: no mail is read
  QStringList notifyTargets;
};

// A notification sink: the GUI shows a tray message and/or posts to ntfy.
using Notify =
    std::function<void(const QString &title, const QString &body, bool urgent)>;

// Turns each cycle's result into notifications, once per condition.
class Watch {
public:
  Watch(Notify notify, QString campaignName, Ledger &ledger, Pool &pool,
        double stallHours = 3.0, int mailFailures = 3);
  void after(const CycleResult &result);

private:
  void raise(const QString &key, const QString &title, const QString &body);
  void clear(const QString &key, const QString &title = {},
             const QString &body = {});
  QString stalled(const QList<Row> &rows) const;

  Notify notify_;
  QString name_;
  Ledger &ledger_;
  Pool &pool_;
  double stallHours_;
  int mailFailures_;
  QSet<QString> active_;
  int mailStreak_ = 0;
  bool done_;
};

// Posts to ntfy-style URLs through `http`; "desktop" targets are handed to
// `desktop` (the GUI's tray). Failures are logged, never thrown.
Notify makeNotifier(const QStringList &targets, Http *http,
                    const Notify &desktop, const Log &log);

// The work of one campaign run. Holds the pool lock for the run; throws
// PoolHeld (before any cycle) when another run owns the address.
TaskJob::Work campaignWork(const Campaign &campaign, const RunOptions &opt,
                           const Notify &desktop = {});
// Several campaigns in turn on one address, each until complete.
TaskJob::Work queueWork(const QList<Campaign> &campaigns, const RunOptions &opt,
                        const Notify &desktop = {});

} // namespace skg::tdvms
