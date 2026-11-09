// What every campaign so far has learnt about each station. Port of
// tdvms/history.py: warns, never refuses, and names the silent period.
#pragma once

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include "tdvms/campaign.h"

namespace skg::tdvms {

struct StationRecord {
  QString code;
  int fetched = 0, nodata = 0, failed = 0, retired = 0;
  QString silentFrom, silentTo; // the span its no-data windows cover
  QSet<QString> campaigns;

  int answered() const { return fetched + nodata; }
  // "dead", "patchy", or "" when there is nothing to warn about.
  QString kind() const;
  QString warning() const; // empty when kind() is empty
};

QMap<QString, StationRecord> gatherHistory(const QList<Campaign> &campaigns);
// Every campaign near a ledger: beside it, in the working dir, and in the
// project a campaigns/<name>/ledger sits in.
QList<Campaign> campaignsAround(const QString &ledgerPath);

} // namespace skg::tdvms
