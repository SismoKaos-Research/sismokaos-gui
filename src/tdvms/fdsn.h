// Whole days of one station from an FDSN dataselect service, beside a
// campaign. Port of tdvms/fdsn.py. State is a second ledger
// (<name>_fdsn.jsonl or campaigns/<name>/fdsn.jsonl) with one row per station
// and UTC day; files are named like TDVMS archive members so sfc reads them.
#pragma once

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>

#include <functional>

#include "tdvms/campaign.h"
#include "tdvms/http.h"
#include "tdvms/ledger.h"

namespace skg::tdvms {

extern const QString kKoeri; // http://eida.koeri.boun.edu.tr
extern const QString kAdoptedNote;

QString fdsnStatePath(const Campaign &c);
QString fdsnDefaultOut(const Campaign &c);
QString fdsnFileName(const QString &network, const QString &station,
                     const QDate &day, const QString &channels);

// Hours of data per channel inside the UTC day, from miniSEED bytes.
QMap<QString, double> dayHours(const QByteArray &data, const QDate &day);

// Adds a pending row per day the ledger does not hold; returns how many.
int fdsnPlan(Ledger &ledger, const QString &station, const QList<QDate> &days,
             const QString &network, const QString &channels,
             const QString &service);
// Marks fetched every pending day whose file is already on disk.
int fdsnAdopt(Ledger &ledger, const QString &outDir);
// Puts back to pending what a pull that died mid-day left claimed.
int fdsnReclaim(Ledger &ledger);
// A station's failed days back to pending.
int fdsnRetryFailed(Ledger &ledger, const QString &station);

struct FdsnOptions {
  int retries = 5;
  int timeoutMs = 300000;
  int pauseMs = 30000; // multiplied by the attempt number between tries
};

using Log = std::function<void(const QString &)>;
// Works through every pending day, oldest first. Returns outcome counts by
// state. Stops early when shouldStop() says so.
QMap<QString, int> fdsnWork(Ledger &ledger, const QString &outDir, Http &http,
                            const FdsnOptions &opt = {}, const Log &log = {},
                            const std::function<bool()> &shouldStop = {},
                            const std::function<void(int, int)> &progress = {});

// The days between two dates (end exclusive).
QList<QDate> daysBetween(const QDate &start, const QDate &end);

} // namespace skg::tdvms
