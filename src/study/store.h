// The study that is open in the application: opening, creating, editing
// (every edit is saved at once) and the list of recently opened studies.
//
// A study is open in one window at a time. Opening takes a lock file in the
// study folder (kLockFileName) and closing releases it; a second window, or
// a second copy of the application, is refused with who holds it. A lock
// left by a crash on this computer is recognised (its process is gone) and
// taken over silently. A lock held from another computer cannot be checked,
// so that one, and only that one, can be broken on request (`breakLock`).
// A folder the lock cannot be written to (read-only media) opens without
// one: holdsLock() is then false, and nothing should be run into it.
#pragma once

#include <QLockFile>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

#include "study/study.h"

namespace skg::study {

class StudyStore : public QObject {
  Q_OBJECT
public:
  explicit StudyStore(QSettings *settings, QObject *parent = nullptr);
  ~StudyStore() override;

  QSettings *settings() const { return settings_; }
  bool isOpen() const { return open_; }
  const Study &study() const { return study_; }

  // Each returns false and sets `error` when it cannot be done. A refusal
  // because the study is open elsewhere also fills refusedBy().
  bool open(const QString &dir, QString *error = nullptr,
            bool breakLock = false);
  bool create(const QString &dir, const QString &name,
              QString *error = nullptr);
  void close();
  // Applies `edit` to a copy, saves it, and makes it the open study.
  bool update(const std::function<void(Study &)> &edit,
              QString *error = nullptr);

  // Whether study.toml was written by something else since this store read
  // or saved it. update() then refuses, so that the other change is not
  // undone; reload() reads the file again (and keeps the lock).
  bool changedOnDisk() const;
  bool reload(QString *error = nullptr);

  // Whether this store holds the open study's lock (see the top).
  bool holdsLock() const { return lock_ != nullptr; }
  // Who held the lock when the last open() or create() was refused for it.
  struct LockHolder {
    bool refused = false;   // the last attempt failed on the lock
    qint64 pid = 0;
    QString host, app;
    // Held from another computer: it cannot be told whether that one still
    // has the study open, so the user may decide to open it anyway.
    bool otherHost = false;
  };
  const LockHolder &refusedBy() const { return refusedBy_; }

  // Folders of the studies opened lately, newest first; missing ones are
  // dropped.
  QStringList recent() const;
  // The name a study folder gives itself in its study.toml, without opening
  // it; the folder's own name when that cannot be read.
  static QString nameOf(const QString &dir);
  // Opens the study that was open when the application last closed. False
  // with the reason in `error` when there was one and it cannot be opened;
  // true when it opened or there was none.
  bool reopenLast(QString *error = nullptr);

  static const QString kSettingRecent;
  static const QString kSettingLast;
  static const QString kLockFileName;

signals:
  // Opened, closed or edited.
  void changed();

private:
  void remember(const QString &dir);
  // The lock of the study in `dir`: null with `error` set when someone else
  // holds it; `unlocked` says the folder takes no lock at all.
  std::unique_ptr<QLockFile> takeLock(const QString &dir, bool breakLock,
                                      bool *unlocked, QString *error);

  static QString stampOf(const QString &dir); // size and time of study.toml

  QSettings *settings_;
  Study study_;
  bool open_ = false;
  QString stamp_; // study.toml as this store last read or wrote it
  std::unique_ptr<QLockFile> lock_;
  LockHolder refusedBy_;
};

} // namespace skg::study
