// What each recipe setting is called and what it does, in one place: the
// recipe dialog and the Runs page show the readable name and explain the
// setting on hover; study.toml and run.json keep the raw key.
#pragma once

#include <QString>

namespace skg {

struct SettingInfo {
  QString label; // readable name; the key itself when the key is unknown
  QString help;  // one or two sentences, plain text; may be empty
  // Label, help and the raw key as one tooltip.
  QString tooltip(const QString &key) const;
};

SettingInfo settingInfo(const QString &tool, const QString &key);

} // namespace skg
