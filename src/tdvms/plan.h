// Enumerate a station's windows into the ledger, idempotently. Port of
// tdvms/plan.py (the portal check lives in portal.h).
#pragma once

#include <QList>
#include <QPair>
#include <QString>

#include "tdvms/ledger.h"

namespace skg::tdvms {

constexpr int kChunkDays = 21; // window length when covering the period
constexpr int kSampleDays = 1; // window length when sampling with everyDays

// "TU.KAND" -> "KAND": the portal lists bare codes; the network travels in
// the request payload.
QString normaliseStation(const QString &code);

// The (start, end) ISO pairs to plan. Back to back by default; with
// everyDays a window opens every that many days. Throws
// std::invalid_argument on a window under a day or longer than its stride.
QList<QPair<QString, QString>> planWindows(const QString &startIso,
                                           const QString &endIso, int chunkDays,
                                           int everyDays = -1);

// Appends the missing windows under the ledger lock; returns how many.
int enumerateChunks(Ledger &ledger, const QString &station,
                    const QString &startIso, const QString &endIso,
                    int chunkDays, int everyDays = -1);

// Appends the given windows (ISO start and end) for a station, skipping the
// ones the ledger already holds; returns how many were added.
int addChunks(Ledger &ledger, const QString &station,
              const QList<QPair<QString, QString>> &windows);

} // namespace skg::tdvms
