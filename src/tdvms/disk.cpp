#include "tdvms/disk.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

namespace skg::tdvms {

qint64 freeBytes(const QString &path) {
  QString p = QFileInfo(path).absoluteFilePath();
  while (!QFileInfo::exists(p)) {
    const QString parent = QFileInfo(p).absolutePath();
    if (parent == p)
      break;
    p = parent;
  }
  return QStorageInfo(p).bytesAvailable();
}

qint64 reserveBytes(const QList<Row> &rows, std::optional<double> minFreeGb) {
  if (minFreeGb)
    return static_cast<qint64>(*minFreeGb * 1e9);
  qint64 largest = 0;
  for (const Row &r : rows)
    if (str(r, "state") == QLatin1String("fetched"))
      largest = std::max(largest, bytesOf(r));
  return std::max(2 * largest, static_cast<qint64>(kFloorGb * 1e9));
}

QString diskCheck(const QString &outDir, const QList<Row> &rows,
                  std::optional<double> minFreeGb) {
  if (minFreeGb && *minFreeGb == 0)
    return {};
  const qint64 need = reserveBytes(rows, minFreeGb);
  const qint64 free = freeBytes(outDir);
  if (free >= need)
    return {};
  return QStringLiteral("%1 GB free for %2, below the %3 GB kept in reserve -- "
                        "not downloading or submitting until there is room")
      .arg(free / 1e9, 0, 'f', 1)
      .arg(outDir)
      .arg(need / 1e9, 0, 'f', 1);
}

} // namespace skg::tdvms
