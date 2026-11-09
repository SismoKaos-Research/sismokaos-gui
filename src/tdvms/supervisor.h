// One cycle of the campaign: reap, reclaim, retire, submit, in one process
// under one lock. Port of tdvms/supervisor.py. The order matters: an answer
// frees a slot that this same cycle can refill; a stuck slot is released
// before anything is submitted; a dead station stops taking slots first.
#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "tdvms/fetch.h"
#include "tdvms/ledger.h"
#include "tdvms/mail.h"
#include "tdvms/portal.h"
#include "tdvms/slots.h"

namespace skg::tdvms {

struct SupervisorConfig {
  QString outDir = QStringLiteral("afad_raw");
  std::optional<double> minFreeGb; // nullopt: twice the largest archive
  int claimTimeout = 600;          // s before a `claimed` row died mid-POST
  int submitTimeout = 21600;       // s before a `submitted` row is lost
  int maxAttempts = 3;
  int nodataStreak = 8;
  int busyCooldown = 1800;
  bool retire = true;
  QStringList stations; // restrict submissions; empty = round-robin
  bool claimUnknown = false;
  bool dryRun = false;
  int downloadTimeoutMs = 120000;
};

// The inbox, as the supervisor needs it; ImapMailbox is the real one, the
// tests pass a fake.
class Mailbox {
public:
  virtual ~Mailbox() = default;
  // One pass: every message matching the search, classified. Throws
  // std::runtime_error when the inbox cannot be read.
  virtual QList<MailEvent> read(const QSet<QString> &addresses,
                                bool claimUnknown) = 0;
  // Marks a message read; flags it too when it did not go cleanly. May throw.
  virtual void consume(const QString &uid, bool ok) = 0;
  virtual void close() {}
};

class ImapMailbox : public Mailbox {
public:
  ImapMailbox(QString host, QString user, QString password,
              QString folder = QStringLiteral("INBOX"),
              QString search = QStringLiteral("(UNSEEN)"));
  ~ImapMailbox() override;
  QList<MailEvent> read(const QSet<QString> &addresses,
                        bool claimUnknown) override;
  void consume(const QString &uid, bool ok) override;
  void close() override;

private:
  QString host_, user_, password_, folder_, search_;
  std::unique_ptr<class ImapClient> client_;
};

struct CycleResult {
  QStringList fetched, nodata, requeued, reclaimed, retired, submitted;
  QStringList busy, rejected, foreign, notes;
  QString paused;    // why nothing was downloaded or submitted
  QString mailError; // why the inbox could not be read
  bool quiet() const;
  QString summary() const;
};

using Log = std::function<void(const QString &)>;

// Reap, reclaim, retire, submit -- once. `mailbox`, `portal` and `http` may
// be null (no mail read, nothing submitted, no download).
CycleResult runCycle(Ledger &ledger, Pool &pool, const SupervisorConfig &cfg,
                     Mailbox *mailbox, Portal *portal, Http *http,
                     const Log &log = {},
                     const std::function<bool()> &shouldStop = {});

} // namespace skg::tdvms
