#include "study/store.h"

#include <QDir>
#include <QFileInfo>

#include <exception>

namespace skg::study {

const QString StudyStore::kSettingRecent = QStringLiteral("study/recent");
const QString StudyStore::kSettingLast = QStringLiteral("study/last");

StudyStore::StudyStore(QSettings *settings, QObject *parent)
    : QObject(parent), settings_(settings) {}

bool StudyStore::open(const QString &dir, QString *error) {
  try {
    study_ = Study::load(dir);
  } catch (const std::exception &e) {
    if (error)
      *error = QString::fromUtf8(e.what());
    return false;
  }
  open_ = true;
  remember(study_.dir);
  emit changed();
  return true;
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
  study_ = s;
  open_ = true;
  remember(study_.dir);
  emit changed();
  return true;
}

void StudyStore::close() {
  if (!open_)
    return;
  open_ = false;
  study_ = Study();
  settings_->remove(kSettingLast);
  emit changed();
}

bool StudyStore::update(const std::function<void(Study &)> &edit,
                        QString *error) {
  if (!open_) {
    if (error)
      *error = tr("No study is open");
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

void StudyStore::reopenLast() {
  const QString last = settings_->value(kSettingLast).toString();
  if (!last.isEmpty() &&
      QFileInfo::exists(QDir(last).filePath(Study::kFileName)))
    open(last);
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
