#include "tdvms/slots.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>

#include <stdexcept>

#include "tdvms/settings.h"

namespace skg::tdvms {

QStringList poolAddresses(const QString &address, int count) {
  if (count < 1)
    throw std::invalid_argument(
        QStringLiteral("a pool needs at least one slot, got %1")
            .arg(count)
            .toStdString());
  const int at = address.lastIndexOf('@');
  if (at < 0)
    throw std::invalid_argument(QStringLiteral("'%1' is not an email address")
                                    .arg(address)
                                    .toStdString());
  const QString local = address.left(at), domain = address.mid(at + 1);
  if (local.contains('+'))
    throw std::invalid_argument(
        QStringLiteral("'%1' is already a plus-address; give the base address "
                       "(e.g. %2@%3) and let the pool extend it")
            .arg(address, local.section('+', 0, 0), domain)
            .toStdString());
  QStringList out;
  for (int i = 1; i <= count; ++i)
    out << QStringLiteral("%1+a%2@%3").arg(local).arg(i).arg(domain);
  return out;
}

bool Slot::remoteBusy(const QDateTime &now) const {
  if (!busyUntil.isValid())
    return false;
  return (now.isValid() ? now : QDateTime::currentDateTimeUtc()) < busyUntil;
}

bool Slot::free(const QDateTime &now) const {
  return !holding && !remoteBusy(now);
}

void Slot::markBusy(int cooldownSeconds) {
  busyUntil = QDateTime::currentDateTimeUtc().addSecs(cooldownSeconds);
  lastResult = QStringLiteral("busy");
}

Pool::Pool(const QString &address, int count) {
  for (const QString &e : poolAddresses(address, count))
    slots_.append(Slot{e, std::nullopt, {}, {}});
}

Pool &Pool::refresh(const Ledger &ledger) {
  QMap<QString, Row> held;
  for (const Row &r : ledger.rows())
    if (kInFlight.contains(str(r, "state")) && !str(r, "email").isEmpty())
      held.insert(str(r, "email").trimmed().toLower(), r);
  for (Slot &s : slots_) {
    const auto it = held.constFind(s.email.toLower());
    s.holding = it == held.constEnd() ? std::nullopt : std::optional<Row>(*it);
  }
  return *this;
}

Slot *Pool::get(const QString &email) {
  if (email.isEmpty())
    return nullptr;
  const QString e = email.trimmed().toLower();
  for (Slot &s : slots_)
    if (s.email.toLower() == e)
      return &s;
  return nullptr;
}

const Slot *Pool::get(const QString &email) const {
  return const_cast<Pool *>(this)->get(email);
}

QList<Slot *> Pool::free() {
  QList<Slot *> out;
  for (Slot &s : slots_)
    if (s.free())
      out << &s;
  return out;
}

QStringList Pool::addresses() const {
  QStringList out;
  for (const Slot &s : slots_)
    out << s.email;
  return out;
}

// --- the pool lock
// ------------------------------------------------------------

namespace {
QString infoPath(const QString &lockPath) { return lockPath + ".json"; }

QJsonObject readInfo(const QString &lockPath) {
  QFile f(infoPath(lockPath));
  if (!f.open(QIODevice::ReadOnly))
    return {};
  return QJsonDocument::fromJson(f.readAll()).object();
}

void writeInfo(const QString &lockPath, const QJsonObject &info) {
  QFile f(infoPath(lockPath));
  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    f.write(QJsonDocument(info).toJson(QJsonDocument::Compact) + "\n");
}

QString describe(const QJsonObject &info) {
  if (info.isEmpty())
    return QStringLiteral("another process");
  return QStringLiteral("pid %1, ledger %2")
      .arg(info.value("pid").toVariant().toString(),
           info.value("ledger").toString());
}
} // namespace

QString PoolLock::lockPath(const QString &address, const QString &stateRoot) {
  const QString base = stateRoot.isEmpty()
                           ? stateDir()
                           : QDir(stateRoot).filePath(QStringLiteral("tdvms"));
  // ".qlock", not ".lock": the Python tool's flock file of that name holds
  // JSON text that QLockFile would misread (see ledger.cpp).
  return QDir(base).filePath(address.trimmed().toLower() + ".qlock");
}

PoolLock::PoolLock(const QString &address, const QString &ledgerPath,
                   const QJsonObject &info, const QString &stateRoot)
    : address_(address), stateRoot_(stateRoot) {
  const QString path = lockPath(address, stateRoot);
  QDir().mkpath(QFileInfo(path).absolutePath());
  lock_ = std::make_unique<QLockFile>(path);
  lock_->setStaleLockTime(0); // a run holds its pool for days; never "stale"
  if (!lock_->tryLock(0)) {
    const QJsonObject who = readInfo(path);
    lock_.reset();
    throw PoolHeld(describe(who).toStdString());
  }
  relabel(ledgerPath, info);
}

PoolLock::~PoolLock() {
  if (lock_) {
    QFile::remove(infoPath(lockPath(address_, stateRoot_)));
    lock_->unlock();
  }
}

void PoolLock::relabel(const QString &ledgerPath, const QJsonObject &info) {
  QJsonObject full = info;
  full.insert("pid", static_cast<qint64>(QCoreApplication::applicationPid()));
  full.insert("ledger", QFileInfo(ledgerPath).absoluteFilePath());
  full.insert("address", address_);
  writeInfo(lockPath(address_, stateRoot_), full);
}

QJsonObject PoolLock::holder(const QString &address, const QString &stateRoot) {
  const QString path = lockPath(address, stateRoot);
  if (!QFileInfo::exists(path))
    return {};
  QLockFile probe(path);
  probe.setStaleLockTime(0);
  if (probe.tryLock(0)) {
    probe.unlock();
    return {};
  }
  QJsonObject info = readInfo(path);
  if (info.isEmpty()) {
    qint64 pid = 0;
    QString host, app;
    if (probe.getLockInfo(&pid, &host, &app))
      info.insert("pid", pid);
  }
  return info;
}

QList<QJsonObject> PoolLock::holders(const QString &stateRoot) {
  const QString base = stateRoot.isEmpty()
                           ? stateDir()
                           : QDir(stateRoot).filePath(QStringLiteral("tdvms"));
  QList<QJsonObject> out;
  for (const QFileInfo &f :
       QDir(base).entryInfoList({QStringLiteral("*.qlock")}, QDir::Files)) {
    const QString address =
        f.fileName().chopped(QStringLiteral(".qlock").size());
    const QJsonObject who = holder(address, stateRoot);
    if (!who.isEmpty())
      out << who;
  }
  return out;
}

} // namespace skg::tdvms
