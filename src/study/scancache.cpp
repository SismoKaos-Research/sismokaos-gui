#include "study/scancache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <cmath>

namespace skg::study {

namespace {

constexpr int kVersion = 2; // 2: with the file's fingerprint

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
    line += QStringLiteral("%1=%2;").arg(it.key()).arg(
        qint64(std::llround(it->firstStart * 1000.0)));
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

// A result by name is filed as <key>-<start of the fingerprint>.json, so
// files that agree in everything the key holds but not in content each keep
// their own.
QString nameShardPath(const QString &dir, const QString &key,
                      const QString &print) {
  return QStringLiteral("%1/name/%2/%3-%4.json")
      .arg(dir, key.left(2), key, print.left(16));
}

QJsonObject readShard(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
  return o.value("v").toInt() == kVersion ? o : QJsonObject();
}

// The folder, made if need be. Several threads, and several applications,
// file their first results at the same moment: making the same missing
// parents at once can fail for all but one of them (seen on Windows), so
// one thread at a time here, and a folder that is there after a failed
// attempt, or after a short wait, counts.
bool ensureDir(const QString &path) {
  static QMutex making;
  const QMutexLocker lock(&making);
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (QFileInfo(path).isDir() || QDir().mkpath(path) ||
        QFileInfo(path).isDir())
      return true;
    QThread::msleep(5 * (attempt + 1));
  }
  return false;
}

// Written beside its place and renamed into it. A rename can fail for a
// moment when something else has the new file open (another instance
// writing the same result, a virus scanner): tried a few times, and a
// result that is there at the end is as good as ours.
bool writeShard(const QString &path, const QJsonObject &o, QString *error) {
  if (!ensureDir(QFileInfo(path).absolutePath())) {
    *error = QStringLiteral("cannot make the folder of %1").arg(path);
    return false;
  }
  const QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Compact);
  for (int attempt = 0; attempt < 4; ++attempt) {
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() &&
        f.commit())
      return true;
    *error = QStringLiteral("%1: %2").arg(path, f.errorString());
    QThread::msleep(10 * (attempt + 1));
  }
  return !readShard(path).isEmpty();
}

// The stored result as an entry for a file of this size and time, when it
// was made from a file of this content.
bool resultOf(const QJsonObject &shard, qint64 size, double mtime,
              const QString &print, WaveFile *out) {
  if (shard.isEmpty() || qint64(shard.value("size").toDouble()) != size ||
      print.isEmpty() || shard.value("print").toString() != print)
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

QString ScanCache::fingerprint(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return {};
  const qint64 size = f.size();
  QCryptographicHash hash(QCryptographicHash::Sha1);
  hash.addData(QByteArray::number(size));
  constexpr qint64 kWhole = 1 << 20, kEnd = 64 * 1024, kBlock = 4096;
  if (size <= kWhole) {
    hash.addData(f.readAll());
  } else {
    hash.addData(f.read(kEnd));
    for (int i = 1; i <= 6; ++i)
      if (f.seek(size / 7 * i))
        hash.addData(f.read(kBlock));
    if (f.seek(size - kEnd))
      hash.addData(f.read(kEnd));
  }
  return QString::fromLatin1(hash.result().toHex());
}

QString ScanCache::identityLine(const QString &absolutePath,
                                bool onlyIfKnown) {
  struct Entry {
    qint64 size = -1, mtime = 0;
    QString print;
  };
  static QMutex mutex;
  static QHash<QString, Entry> known;
  const QFileInfo fi(absolutePath);
  if (!fi.isFile())
    return {};
  const qint64 size = fi.size();
  const qint64 mtime = fi.lastModified().toMSecsSinceEpoch();
  QString print;
  {
    const QMutexLocker lock(&mutex);
    const auto it = known.constFind(absolutePath);
    if (it != known.constEnd() && it->size == size && it->mtime == mtime)
      print = it->print;
  }
  if (print.isEmpty() && onlyIfKnown)
    return {};
  if (print.isEmpty()) {
    print = fingerprint(absolutePath);
    if (print.isEmpty())
      return {};
    const QMutexLocker lock(&mutex);
    known.insert(absolutePath, Entry{size, mtime, print});
  }
  return QStringLiteral("%1|%2|%3").arg(fi.fileName()).arg(size).arg(
      print.left(16));
}

bool ScanCache::lookup(const QString &absolutePath, qint64 size, double mtime,
                       WaveFile *out) const {
  if (dir_.isEmpty())
    return false;
  const QJsonObject shard =
      readShard(shardPath(dir_, "path", pathKey(absolutePath, size, mtime)));
  // Only when there is a result is the file itself looked at.
  return !shard.isEmpty() &&
         resultOf(shard, size, mtime, fingerprint(absolutePath), out);
}

bool ScanCache::lookupMoved(const QString &absolutePath, qint64 size,
                            double mtime, const WaveFile &identity,
                            WaveFile *out) const {
  if (dir_.isEmpty() || identity.channels.isEmpty())
    return false;
  const QString name = QFileInfo(absolutePath).fileName();
  const QJsonObject now = firstsOf(identity);
  const qint64 bucket = bucketOf(mtime);
  QString print; // worked out once there is a candidate
  for (qint64 b : {bucket, bucket - 1, bucket + 1}) {
    const QString key = nameKey(name, size, b, identity);
    const QDir folder(QStringLiteral("%1/name/%2").arg(dir_, key.left(2)));
    const QStringList candidates =
        folder.entryList({key + QStringLiteral("-*.json")}, QDir::Files);
    if (candidates.isEmpty())
      continue;
    if (print.isEmpty())
      print = fingerprint(absolutePath);
    if (print.isEmpty())
      return false;
    const QJsonObject shard = readShard(nameShardPath(dir_, key, print));
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
    if (same && resultOf(shard, size, mtime, print, out))
      return true;
  }
  return false;
}

bool ScanCache::store(const QString &absolutePath, qint64 size, double mtime,
                      const WaveFile &exact, const WaveFile &identity) const {
  if (dir_.isEmpty() || !exact.exact() || !exact.skipped.isEmpty() ||
      exact.channels.isEmpty())
    return true; // nothing to file
  const QString print = fingerprint(absolutePath);
  if (print.isEmpty())
    return true; // the file is gone: nothing worth keeping
  const QString name = QFileInfo(absolutePath).fileName();
  QJsonObject o{{"v", kVersion},
                {"name", name},
                {"size", double(size)},
                {"mtime", mtime},
                {"print", print},
                {"first", firstsOf(identity)},
                {"file", waveFileToJson(exact)}};
  QString error;
  bool ok = writeShard(
      shardPath(dir_, "path", pathKey(absolutePath, size, mtime)), o, &error);
  if (!identity.channels.isEmpty())
    ok = writeShard(
             nameShardPath(dir_, nameKey(name, size, bucketOf(mtime), identity),
                           print),
             o, &error) &&
         ok;
  if (!ok) {
    failures_.fetch_add(1);
    qWarning("scan cache: a result could not be filed (%s)", qPrintable(error));
  }
  return ok;
}

ScanCache::Usage ScanCache::usage() const {
  Usage u;
  if (dir_.isEmpty() || !QDir(dir_).exists())
    return u;
  // Every waveform file with a result has one under "path"; "name" holds
  // the same results again for files that moved.
  for (const char *kind : {"path", "name"}) {
    QDirIterator it(QDir(dir_).filePath(QLatin1String(kind)),
                    {QStringLiteral("*.json")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      u.bytes += it.fileInfo().size();
      if (kind[0] == 'p')
        ++u.files;
    }
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
