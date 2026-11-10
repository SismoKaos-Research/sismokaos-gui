// The pieces of "get the waveforms a case needs" that are not GUI: which
// source serves a station, which days are still to be asked for given what
// is already requested, the requests that cover them, and how far a
// station's requests have come. The Data page's guided panel
// (src/modules/tdvms/casedownloads.*) puts them together.
#pragma once

#include <QDate>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "tdvms/campaign.h"
#include "tdvms/catalog.h"
#include "tdvms/ledger.h"

namespace skg::tdvms {

// Where a station's waveforms are asked for.
enum class Source {
  Portal, // AFAD's TDVMS portal: requests by e-mail, archives of ~3 weeks
  Fdsn,   // an FDSN dataselect service: one file per day, no mailbox
  None    // not downloaded
};

// The rule: a station the portal's list holds with an instrument is asked
// for at the portal; any other at an FDSN service.
Source defaultSource(const QString &code,
                     const QList<CatalogStation> &portalStations);
// The network code the portal's list gives the station, or empty.
QString portalNetwork(const QString &code,
                      const QList<CatalogStation> &portalStations);

// The download a study keeps for itself: <study>/downloads/campaigns/
// waveforms, in the folder layout of every other campaign, so the expert
// view shows it too. With `create` the folders and the (empty) list of
// requests are made; without it the result has an empty ledgerPath when
// there is none yet. Throws std::runtime_error when it cannot be created.
extern const char *const kStudyDownloadsDir; // "downloads"
extern const char *const kStudyDownloadName; // "waveforms"
QString studyDownloadsRoot(const QString &studyDir);
Campaign studyDownload(const QString &studyDir, bool create);

// Of `days`, the ones no live request of the station covers: a request that
// is waiting, under way, fetched, or answered "no data" covers its window;
// a failed or retired one does not.
QList<QDate> daysNotRequested(const QList<QDate> &days, const QList<Row> &rows,
                              const QString &station);

// The portal requests for `days`: windows of `chunkDays` on a grid that
// starts at `gridStart` and ends at `gridEnd` (exclusive), keeping only the
// windows that hold one of the days. ISO start and end, as the ledger keeps
// them.
QList<QPair<QString, QString>> windowsForDays(const QDate &gridStart,
                                              const QDate &gridEnd,
                                              const QList<QDate> &days,
                                              int chunkDays);

// How far a station's requests have come, from the ledger's rows.
struct RequestProgress {
  int total = 0;
  int waiting = 0;  // pending
  int underWay = 0; // claimed or submitted
  int fetched = 0;
  int noData = 0;
  int failed = 0;
  qint64 bytes = 0;     // of what was fetched
  QStringList failures; // "2019-01-01: why", oldest first
  int open() const { return waiting + underWay; }
  int done() const { return fetched + noData + failed; }
  bool empty() const { return total == 0; }
};
RequestProgress progressOf(const QList<Row> &rows, const QString &station);
// The stations a ledger has rows for, in first-seen order.
QStringList stationsOf(const QList<Row> &rows);

// A rough size for what is still to come: the average of what this ledger
// has fetched per day so far, or `fallbackPerDay` when it has fetched
// nothing.
qint64 estimateBytes(const QList<Row> &rows, int daysToCome,
                     qint64 fallbackPerDay);
inline constexpr qint64 kPortalBytesPerDay = 35LL * 1000 * 1000;
inline constexpr qint64 kFdsnBytesPerDay = 12LL * 1000 * 1000;

// Puts a station's failed requests back to waiting; returns how many.
int retryFailed(Ledger &ledger, const QString &station);

} // namespace skg::tdvms
