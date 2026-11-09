// The address pool: which portal queue slots exist and which are free.
// Port of tdvms/slots.py. A slot has a local state (what the ledger says the
// address holds) and a remote one (the portal's last answer); it is free only
// when both agree.
#pragma once

#include <QDateTime>
#include <QList>
#include <QLockFile>
#include <QMap>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

#include "tdvms/ledger.h"

namespace skg::tdvms {

// ("you@gmail.com", 3) -> you+a1@..., you+a2@..., you+a3@...
// Throws std::invalid_argument on a plus-address, a non-address, or n < 1.
QStringList poolAddresses(const QString &address, int count);

struct Slot {
  QString email;
  std::optional<Row> holding; // the in-flight row occupying it
  QDateTime busyUntil;        // set when the portal answered 111
  QString lastResult;         // "accepted", "busy", "rejected", ""

  bool remoteBusy(const QDateTime &now = {}) const;
  bool free(const QDateTime &now = {}) const;
  void markBusy(int cooldownSeconds);
};

class Pool {
public:
  Pool(const QString &address, int count);

  Pool &refresh(const Ledger &ledger); // re-reads holdings, keeps remote state
  Slot *get(const QString &email);     // case-insensitive, nullptr if none
  const Slot *get(const QString &email) const;
  QList<Slot *> free();
  QStringList addresses() const;
  int size() const { return slots_.size(); }
  QList<Slot> &entries() { return slots_; }
  const QList<Slot> &entries() const { return slots_; }

private:
  QList<Slot> slots_;
};

// One process per base address on this machine. The lock lives in the state
// dir (see settings.h); next to it a .json describes the holder (pid, ledger,
// slots, out dir, log) so the GUI can show what runs.
struct PoolHeld : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class PoolLock {
public:
  // Throws PoolHeld naming the holder.
  PoolLock(const QString &address, const QString &ledgerPath,
           const QJsonObject &info, const QString &stateRoot = QString());
  ~PoolLock();
  // Rewrites the holder description (queue moves to the next campaign).
  void relabel(const QString &ledgerPath, const QJsonObject &info);

  static QString lockPath(const QString &address,
                          const QString &stateRoot = QString());
  // Who holds a pool, as its description, or empty when nobody does.
  static QJsonObject holder(const QString &address,
                            const QString &stateRoot = QString());
  static QList<QJsonObject> holders(const QString &stateRoot = QString());

private:
  QString address_;
  QString stateRoot_;
  std::unique_ptr<QLockFile> lock_;
};

} // namespace skg::tdvms
