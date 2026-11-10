// Settings keys the shell and the Settings page share, with their defaults.
#pragma once

namespace skg::appsettings {

// Tell the user when a long job (or the last of several) ends while the
// window is not in front.
inline constexpr char kNotifyKey[] = "notify/enabled";
inline constexpr bool kNotifyDefault = true;
// Only work that ran at least this long is announced, in seconds.
inline constexpr char kNotifyAfterKey[] = "notify/afterSeconds";
inline constexpr int kNotifyAfterDefault = 60;
// Keep the computer from going to sleep while jobs run.
inline constexpr char kKeepAwakeKey[] = "jobs/keepAwake";
inline constexpr bool kKeepAwakeDefault = false;
// The first-run card on the study pages was closed.
inline constexpr char kFirstRunDismissedKey[] = "ui/firstRunDismissed";
// The folder each file dialog was last used in: dialogs/<key>.
inline constexpr char kDialogFolderPrefix[] = "dialogs/";

} // namespace skg::appsettings
