// How much room the downloads have. Port of tdvms/disk.py: below a reserve
// (twice the largest archive so far, at least 2 GB, or a fixed figure) the
// loop neither downloads nor submits.
#pragma once

#include <QList>
#include <QString>

#include <optional>

#include "tdvms/ledger.h"

namespace skg::tdvms {

constexpr double kFloorGb = 2.0;

qint64 freeBytes(const QString &path);
qint64 reserveBytes(const QList<Row> &rows, std::optional<double> minFreeGb);
// Empty while there is room; otherwise a sentence saying why not. A
// minFreeGb of 0 turns the guard off.
QString diskCheck(const QString &outDir, const QList<Row> &rows,
                  std::optional<double> minFreeGb);

} // namespace skg::tdvms
