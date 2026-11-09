// Where a campaign's files live. Port of tdvms/campaign.py.
//
// Two layouts: the folder one, campaigns/<name>/{ledger.jsonl,raw/,run.log},
// and the flat one, <name>_ledger.jsonl beside <name>_raw/ and <name>_run.log.
// tdvms_ledger.jsonl, the CLI's default, downloads to afad_raw. Downloads on
// another disk are reached through `raw` as a symlink or a `raw.path` file.
#pragma once

#include <QList>
#include <QString>

namespace skg::tdvms {

struct Campaign {
  QString name;
  QString ledgerPath;
  QString raw; // where archives are filed
  QString log;
};

extern const char *const kCampaignsDir; // "campaigns"
bool validCampaignName(const QString &name);

QList<Campaign> discoverCampaigns(const QString &root);
Campaign campaignForLedger(const QString &ledgerPath);
// Creates campaigns/<name>/ and its raw folder (linked to rawTarget when
// given). Throws std::invalid_argument on a bad or taken name.
Campaign newCampaign(const QString &root, const QString &name,
                     const QString &rawTarget = QString());
// A ledger path, or a campaign name under root. Throws std::out_of_range.
Campaign findCampaign(const QString &spec, const QString &root);

} // namespace skg::tdvms
