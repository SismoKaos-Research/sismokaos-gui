#include "study/store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSysInfo>

#include <exception>

namespace skg::study {

const QString StudyStore::kSettingRecent = QStringLiteral("study/recent");
const QString StudyStore::kSettingLast = QStringLiteral("study/last");

const QString StudyStore::kLockFileName = QStringLiteral(".study.lock");

StudyStore::StudyStore(QSettings *settings, QObject *parent)
    : QObject(parent), settings_(settings) {}

StudyStore::~StudyStore() = default; // releases the lock

namespace {

bool sameFolder(const QString &a, const QString &b) {
  const QString ca = QFileInfo(a).canonicalFilePath();
  const QString cb = QFileInfo(b).canonicalFilePath();
  return !ca.isEmpty() && ca == cb;
}

} // namespace

std::unique_ptr<QLockFile> StudyStore::takeLock(const QString &dir,
                                                bool breakLock, bool *unlocked,
                                                QString *error) {
  *unlocked = false;
  refusedBy_ = {};
  const QString path = QDir(dir).filePath(kLockFileName);
  auto lock = std::make_unique<QLockFile>(path);
  // Never stale by age (a study stays open for days); a lock whose process
  // is gone on this computer is still recognised and taken over.
  lock->setStaleLockTime(0);
  if (lock->tryLock(0))
    return lock;
  if (lock->error() != QLockFile::LockFailedError) {
    // The folder takes no lock file (read-only media, no permission): the
    // study can be looked at, and holdsLock() says it is not ours to write.
    *unlocked = true;
    return nullptr;
  }
  LockHolder holder;
  holder.refused = true;
  lock->getLockInfo(&holder.pid, &holder.host, &holder.app);
  holder.otherHost = !holder.host.isEmpty() &&
                     holder.host.compare(QSysInfo::machineHostName(),
                                         Qt::CaseInsensitive) != 0;
  if (breakLock && holder.otherHost) {
    QFile::remove(path);
    if (lock->tryLock(0))
      return lock;
  }
  refusedBy_ = holder;
  if (error) {
    if (holder.otherHost)
      *error = tr("This study is open on another computer (%1, by %2). "
                  "Close it there first. If that computer no longer has it "
                  "open (it crashed or was switched off), it can be opened "
                  "here anyway.")
                   .arg(holder.host, holder.app.isEmpty()
                                         ? QStringLiteral("sismokaos-gui")
                                         : holder.app);
    else
      *error = tr("This study is already open in another window (%1, "
                  "process %2). A study is open in one window at a time, so "
                  "that two do not write its results over each other. Close "
                  "it there first.")
                   .arg(holder.app.isEmpty() ? QStringLiteral("sismokaos-gui")
                                             : holder.app)
                   .arg(holder.pid);
  }
  return nullptr;
}

bool StudyStore::open(const QString &dir, QString *error, bool breakLock) {
  refusedBy_ = {};
  Study loaded;
  try {
    loaded = Study::load(dir);
  } catch (const std::exception &e) {
    if (error)
      *error = QString::fromUtf8(e.what());
    return false;
  }
  // The study that is already open here keeps the lock it has.
  const bool again = open_ && sameFolder(study_.dir, loaded.dir);
  if (!again) {
    bool unlocked = false;
    std::unique_ptr<QLockFile> lock =
        takeLock(loaded.dir, breakLock, &unlocked, error);
    if (!lock && !unlocked)
      return false; // the study that was open stays open
    lock_ = std::move(lock); // releases the previous study's
  }
  study_ = loaded;
  open_ = true;
  stamp_ = stampOf(study_.dir);
  remember(study_.dir);
  emit changed();
  return true;
}

QString StudyStore::nameOf(const QString &dir) {
  try {
    const QString name = Study::load(dir).name;
    if (!name.isEmpty())
      return name;
  } catch (const std::exception &) {
  }
  return QFileInfo(dir).fileName();
}

bool StudyStore::create(const QString &dir, const QString &name,
                        QString *error) {
  if (QFileInfo::exists(QDir(dir).filePath(Study::kFileName))) {
    if (error)
      *error = tr("%1 already holds a study").arg(dir);
    return false;
  }
  Study s;
  s.dir = QDir(dir).absolutePath();
  s.name = name.isEmpty() ? QFileInfo(s.dir).fileName() : name;
  try {
    s.save();
  } catch (const std::exception &e) {
    if (error)
      *error = QString::fromUtf8(e.what());
    return false;
  }
  bool unlocked = false;
  std::unique_ptr<QLockFile> lock = takeLock(s.dir, false, &unlocked, error);
  if (!lock && !unlocked) {
    QFile::remove(QDir(s.dir).filePath(Study::kFileName));
    return false;
  }
  lock_ = std::move(lock);
  study_ = s;
  open_ = true;
  stamp_ = stampOf(study_.dir);
  remember(study_.dir);
  emit changed();
  return true;
}

void StudyStore::close() {
  if (!open_)
    return;
  open_ = false;
  study_ = Study();
  lock_.reset();
  stamp_.clear();
  settings_->remove(kSettingLast);
  emit changed();
}

QString StudyStore::stampOf(const QString &dir) {
  const QFileInfo fi(QDir(dir).filePath(Study::kFileName));
  if (!fi.exists())
    return {};
  return QString::number(fi.size()) + QLatin1Char('@') +
         QString::number(fi.lastModified().toMSecsSinceEpoch());
}

bool StudyStore::changedOnDisk() const {
  return open_ && !stamp_.isEmpty() && stampOf(study_.dir) != stamp_;
}

bool StudyStore::reload(QString *error) {
  if (!open_) {
    if (error)
      *error = tr("No study is open");
    return false;
  }
  return open(study_.dir, error);
}

bool StudyStore::update(const std::function<void(Study &)> &edit,
                        QString *error) {
  if (!open_) {
    if (error)
      *error = tr("No study is open");
    return false;
  }
  // Something else wrote study.toml since it was read (an editor, a copy of
  // the application that broke the lock): saving now would undo that.
  if (changedOnDisk()) {
    if (error)
      *error = tr("The study file changed on disk since it was read, so "
                  "this change was not saved over it. Reload the study "
                  "(File > Reload the study from disk) and make the change "
                  "again.");
    return false;
  }
  Study next = study_;
  edit(next);
  try {
    next.save();
  } catch (const std::exception &e) {
    if (error)
      *error = QString::fromUtf8(e.what());
    return false;
  }
  study_ = next;
  stamp_ = stampOf(study_.dir);
  emit changed();
  return true;
}

QStringList StudyStore::recent() const {
  QStringList out;
  for (const QString &dir : settings_->value(kSettingRecent).toStringList())
    if (QFileInfo::exists(QDir(dir).filePath(Study::kFileName)))
      out << dir;
  return out;
}

bool StudyStore::reopenLast(QString *error) {
  const QString last = settings_->value(kSettingLast).toString();
  if (last.isEmpty())
    return true;
  if (!QFileInfo::exists(QDir(last).filePath(Study::kFileName))) {
    if (error)
      *error = tr("The study that was open last, %1, is not there any more "
                  "(a moved folder or an unplugged disk).")
                   .arg(last);
    settings_->remove(kSettingLast);
    return false;
  }
  QString why;
  if (open(last, &why))
    return true;
  if (error)
    *error = tr("The study in %1 could not be opened: %2").arg(last, why);
  return false;
}

void StudyStore::remember(const QString &dir) {
  QStringList list = settings_->value(kSettingRecent).toStringList();
  list.removeAll(dir);
  list.prepend(dir);
  while (list.size() > 8)
    list.removeLast();
  settings_->setValue(kSettingRecent, list);
  settings_->setValue(kSettingLast, dir);
}

} // namespace skg::study
