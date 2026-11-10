// The study that is open in the application: opening, creating, editing
// (every edit is saved at once) and the list of recently opened studies.
#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <functional>

#include "study/study.h"

namespace skg::study {

class StudyStore : public QObject {
  Q_OBJECT
public:
  explicit StudyStore(QSettings *settings, QObject *parent = nullptr);

  bool isOpen() const { return open_; }
  const Study &study() const { return study_; }

  // Each returns false and sets `error` when it cannot be done.
  bool open(const QString &dir, QString *error = nullptr);
  bool create(const QString &dir, const QString &name,
              QString *error = nullptr);
  void close();
  // Applies `edit` to a copy, saves it, and makes it the open study.
  bool update(const std::function<void(Study &)> &edit,
              QString *error = nullptr);

  // Folders of the studies opened lately, newest first; missing ones are
  // dropped.
  QStringList recent() const;
  // Opens the study that was open when the application last closed.
  void reopenLast();

  static const QString kSettingRecent;
  static const QString kSettingLast;

signals:
  // Opened, closed or edited.
  void changed();

private:
  void remember(const QString &dir);

  QSettings *settings_;
  Study study_;
  bool open_ = false;
};

} // namespace skg::study
