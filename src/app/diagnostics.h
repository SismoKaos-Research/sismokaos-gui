// What a problem report needs, as text for the clipboard: the version, the
// system, and the size of the open study. Nothing personal goes in: the home
// folder is written as "~", e-mail addresses are removed, and of the study
// only its folder's name, never where it is.
#pragma once

#include <QString>

class QSettings;

namespace skg {

class JobModel;
namespace study {
class StudyStore;
}

struct DiagnosticsInput {
  const JobModel *jobs = nullptr;
  const study::StudyStore *study = nullptr;
  QString language; // "en", "tr"
  QString theme;    // "light", "dark", "system"
};

// The report, already scrubbed.
QString diagnosticsText(const DiagnosticsInput &in);
// `text` with the home folder as "~", e-mail addresses as "<address>" and the
// user's login name, where it stands as a path part, as "<user>". `home`
// and `user` default to this user's.
QString scrubbed(QString text, QString home = {}, QString user = {});

// Where a tester reads the manual and files a report.
QString manualUrl();
QString reportUrl();

} // namespace skg
