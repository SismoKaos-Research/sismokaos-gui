// How the study pages say durations, moments and sizes: one form on Runs and
// on Compare. Moments are shown in the computer's local time; the zone is
// said once per page (zoneNote), not on every value.
#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QString>

namespace skg {

class RunWords {
  Q_DECLARE_TR_FUNCTIONS(skg::RunWords)
public:
  // "45 s", "12 min", "2 h 14 min", "3 d 4 h". Negative: empty.
  static QString duration(qint64 seconds);
  // "2026-11-10 23:41", in local time. Invalid: empty.
  static QString moment(const QDateTime &utc);
  // "23:41" in local time (today is clear from the context).
  static QString clock(const QDateTime &utc);
  // "Times are local time (UTC+03:00)."
  static QString zoneNote();
  // "14.2 GB", "310 MB", "12 kB": decimal units, as file managers show them.
  static QString bytes(qint64 n);
};

} // namespace skg
