#include "tdvms/campaign.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <stdexcept>

namespace skg::tdvms {

const char *const kCampaignsDir = "campaigns";

namespace {
const QRegularExpression kNameRe(QStringLiteral("^[a-z0-9][a-z0-9_-]{0,39}$"));
const QString kPointer = QStringLiteral("raw.path");
const QString kLedgerSuffix = QStringLiteral("_ledger.jsonl");

QString rawOfHome(const QDir &home) {
  QFile pointer(home.filePath(kPointer));
  if (pointer.exists() && pointer.open(QIODevice::ReadOnly)) {
    const QString text = QString::fromUtf8(pointer.readAll()).trimmed();
    if (!text.isEmpty())
      return text;
  }
  return home.filePath(QStringLiteral("raw"));
}

QString flatRaw(const QDir &dir, const QString &name) {
  return dir.filePath(name == QLatin1String("tdvms")
                          ? QStringLiteral("afad_raw")
                          : name + "_raw");
}
} // namespace

bool validCampaignName(const QString &name) {
  return kNameRe.match(name).hasMatch();
}

Campaign campaignForLedger(const QString &ledgerPath) {
  const QFileInfo fi(ledgerPath);
  const QDir dir = fi.dir();
  if (fi.fileName() == QLatin1String("ledger.jsonl") &&
      QFileInfo(dir.absolutePath()).dir().dirName() ==
          QLatin1String(kCampaignsDir)) {
    return {dir.dirName(), fi.filePath(), rawOfHome(dir),
            dir.filePath(QStringLiteral("run.log"))};
  }
  if (fi.fileName().endsWith(kLedgerSuffix)) {
    const QString name = fi.fileName().chopped(kLedgerSuffix.size());
    return {name, fi.filePath(), flatRaw(dir, name),
            dir.filePath(name + "_run.log")};
  }
  return {fi.completeBaseName(), fi.filePath(),
          dir.filePath(QStringLiteral("afad_raw")),
          dir.filePath(fi.completeBaseName() + ".log")};
}

QList<Campaign> discoverCampaigns(const QString &root) {
  QList<Campaign> found;
  const QDir rootDir(root);
  const QDir camps(rootDir.filePath(QLatin1String(kCampaignsDir)));
  for (const QFileInfo &sub :
       camps.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    const QString ledger = sub.filePath() + "/ledger.jsonl";
    if (QFileInfo::exists(ledger))
      found << campaignForLedger(ledger);
  }
  for (const QFileInfo &f :
       rootDir.entryInfoList({"*" + kLedgerSuffix}, QDir::Files, QDir::Name)) {
    const QString name = f.fileName().chopped(kLedgerSuffix.size());
    found << Campaign{name, f.filePath(), flatRaw(rootDir, name),
                      rootDir.filePath(name + "_run.log")};
  }
  return found;
}

Campaign newCampaign(const QString &root, const QString &name,
                     const QString &rawTarget) {
  if (!validCampaignName(name))
    throw std::invalid_argument(
        "a campaign name is lowercase letters, digits, - and _");
  for (const Campaign &c : discoverCampaigns(root))
    if (c.name == name)
      throw std::invalid_argument(
          QStringLiteral("a campaign called '%1' already exists")
              .arg(name)
              .toStdString());
  const QDir home(
      QDir(root).filePath(QLatin1String(kCampaignsDir) + "/" + name));
  QDir().mkpath(home.absolutePath());
  const QString raw = home.filePath(QStringLiteral("raw"));
  if (!rawTarget.isEmpty()) {
    const QString target = QFileInfo(rawTarget).absoluteFilePath();
    QDir().mkpath(target);
    bool linked = QFileInfo::exists(raw);
#ifndef Q_OS_WIN // QFile::link writes a .lnk shortcut there, not a directory
                 // link
    if (!linked)
      linked = QFile::link(target, raw);
#endif
    if (!linked) {
      QFile pointer(home.filePath(kPointer));
      if (pointer.open(QIODevice::WriteOnly | QIODevice::Truncate))
        pointer.write((target + "\n").toUtf8());
    }
  } else {
    QDir().mkpath(raw);
  }
  return campaignForLedger(home.filePath(QStringLiteral("ledger.jsonl")));
}

Campaign findCampaign(const QString &spec, const QString &root) {
  const QFileInfo fi(spec);
  if (fi.suffix() == QLatin1String("jsonl") || fi.isFile())
    return campaignForLedger(spec);
  QStringList names;
  for (const Campaign &c : discoverCampaigns(root)) {
    if (c.name == spec)
      return c;
    names << c.name;
  }
  throw std::out_of_range(
      QStringLiteral("no campaign called '%1' under %2 (found: %3)")
          .arg(spec, QDir(root).absolutePath(),
               names.isEmpty() ? QStringLiteral("none") : names.join(", "))
          .toStdString());
}

} // namespace skg::tdvms
