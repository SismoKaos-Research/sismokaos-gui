#include "study/scancache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

namespace skg::study {

namespace {

constexpr int kVersion = 1;

QMutex &sharedMutex() {
  static QMutex m;
  return m;
}
QString &sharedDirRef() {
  static QString dir;
  return dir;
}

QString hashOf(const QString &text) {
  return QString::fromLatin1(
      QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1)
          .toHex());
}

// <dir>/<kind>/<first two hex>/<hash>.json
QString shardPath(const QString &dir, const char *kind, const QString &hash) {
  return QStringLiteral("%1/%2/%3/%4.json")
      .arg(dir, QLatin1String(kind), hash.left(2), hash);
}

QString pathKey(const QString &absolutePath, qint64 size, double mtime) {
  const QFileInfo fi(absolutePath);
  QString real = fi.canonicalFilePath();
  if (real.isEmpty())
    real = fi.absoluteFilePath();
  return hashOf(QStringLiteral("%1|%2|%3")
                    .arg(real)
                    .arg(size)
                    .arg(qint64(std::llround(mtime * 1000.0))));
}

// The first record of every channel the quick pass saw: what a moved file
// must still start with.
QJsonObject firstsOf(const WaveFile &identity) {
  QJsonObject o;
  for (auto it = identity.channels.begin(); it != identity.channels.end(); ++it)
    o.insert(it.key(), it->firstStart);
  return o;
}

// The same as one line, to the millisecond: part of the key a result is
// filed under by name, so two files that only look alike (same name, size
// and time, other data) never share an entry.
QString firstsLine(const WaveFile &identity) {
  QString line;
  for (auto it = identity.channels.begin(); it != identity.channels.end(); ++it)
    line += QStringLiteral("%1=%2;")
                .arg(it.key())
                .arg(qint64(std::llround(it->firstStart * 1000.0)));
  return line;
}

// `bucket` is the modification time in steps of two seconds.
QString nameKey(const QString &fileName, qint64 size, qint64 bucket,
                const WaveFile &identity) {
  return hashOf(QStringLiteral("%1|%2|%3|%4")
                    .arg(fileName)
                    .arg(size)
                    .arg(bucket)
                    .arg(firstsLine(identity)));
}
qint64 bucketOf(double mtime) { return qint64(std::floor(mtime / 2.0)); }

QJsonObject readShard(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  return o.value("v").toInt() == kVersion ? o : QJsonObject();
}

bool writeShard(const QString &path, const QJsonObject &o) {
  if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    return false;
  QSaveFile f(path);
  return f.open(QIODevice::WriteOnly) &&
         f.write(QJsonDocument(o).toJson(QJsonDocument::Compact)) >= 0 &&
         f.commit();
}

// The stored result as an entry for a file of this size and time.
bool resultOf(const QJsonObject &shard, qint64 size, double mtime,
              WaveFile *out) {
  if (shard.isEmpty() || qint64(shard.value("size").toDouble()) != size)
    return false;
  WaveFile f = waveFileFromJson(shard.value("file").toObject());
  if (!f.exact() || !f.skipped.isEmpty() || f.channels.isEmpty())
    return false;
  f.size = size;
  f.mtime = mtime;
  if (out)
    *out = f;
  return true;
}

} // namespace

QString ScanCache::sharedDir() {
  const QMutexLocker lock(&sharedMutex());
  return sharedDirRef();
}

void ScanCache::setSharedDir(const QString &dir) {
  const QMutexLocker lock(&sharedMutex());
  sharedDirRef() = dir;
}

QString ScanCache::defaultDir() {
  const QString base =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return base.isEmpty() ? QString()
                        : QDir(base).filePath(QStringLiteral("waveform-scans"));
}

ScanCache::ScanCache(const QString &dir) : dir_(dir) {}

bool ScanCache::lookup(const QString &absolutePath, qint64 size, double mtime,
                       WaveFile *out) const {
  if (dir_.isEmpty())
    return false;
  return resultOf(
      readShard(shardPath(dir_, "path", pathKey(absolutePath, size, mtime))),
      size, mtime, out);
}

bool ScanCache::lookupMoved(const QString &fileName, qint64 size, double mtime,
                            const WaveFile &identity, WaveFile *out) const {
  if (dir_.isEmpty() || identity.channels.isEmpty())
    return false;
  const QJsonObject now = firstsOf(identity);
  const qint64 bucket = bucketOf(mtime);
  for (qint64 b : {bucket, bucket - 1, bucket + 1}) {
    const QJsonObject shard =
        readShard(shardPath(dir_, "name", nameKey(fileName, size, b, identity)));
    if (shard.isEmpty())
      continue;
    // The same first record(s), to the millisecond.
    const QJsonObject then = shard.value("first").toObject();
    if (then.size() != now.size())
      continue;
    bool same = true;
    for (auto it = now.begin(); it != now.end() && same; ++it)
      same = then.contains(it.key()) &&
             std::abs(then.value(it.key()).toDouble() - it->toDouble()) < 1e-3;
    if (same && resultOf(shard, size, mtime, out))
      return true;
  }
  return false;
}

void ScanCache::store(const QString &absolutePath, qint64 size, double mtime,
                      const WaveFile &exact, const WaveFile &identity) const {
  if (dir_.isEmpty() || !exact.exact() || !exact.skipped.isEmpty() ||
      exact.channels.isEmpty())
    return;
  const QString name = QFileInfo(absolutePath).fileName();
  QJsonObject o{{"v", kVersion},
                {"name", name},
                {"size", double(size)},
                {"mtime", mtime},
                {"first", firstsOf(identity)},
                {"file", waveFileToJson(exact)}};
  writeShard(shardPath(dir_, "path", pathKey(absolutePath, size, mtime)), o);
  if (!identity.channels.isEmpty())
    writeShard(shardPath(dir_, "name",
                         nameKey(name, size, bucketOf(mtime), identity)),
               o);
}

ScanCache::Usage ScanCache::usage() const {
  Usage u;
  if (dir_.isEmpty() || !QDir(dir_).exists())
    return u;
  QDirIterator it(dir_, {QStringLiteral("*.json")}, QDir::Files,
                  QDirIterator::Subdirectories);
  const QString byPath = QDir(dir_).filePath(QStringLiteral("path"));
  while (it.hasNext()) {
    const QFileInfo fi(it.next());
    u.bytes += fi.size();
    if (fi.absoluteFilePath().startsWith(byPath))
      ++u.files;
  }
  return u;
}

bool ScanCache::clear() const {
  if (dir_.isEmpty())
    return true;
  bool ok = true;
  // Only what this class wrote: the two folders of results.
  for (const char *kind : {"path", "name"}) {
    QDir d(QDir(dir_).filePath(QLatin1String(kind)));
    if (d.exists())
      ok = d.removeRecursively() && ok;
  }
  return ok;
}

} // namespace skg::study
