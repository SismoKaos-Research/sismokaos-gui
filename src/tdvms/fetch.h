// Download one link and decide what actually arrived. Port of
// tdvms/fetch.py. Each check here was bought with lost data: HTML with 200
// (an expired link), a 22-byte zip (transient), a valid zip holding one
// 82-byte no-data notice, an archive that matches no chunk (never guessed).
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

#include "tdvms/http.h"
#include "tdvms/ledger.h"

namespace skg::tdvms {

// <NET>_<STA>_<DDMMYYYY>_<HHMMSS>_<DDMMYYYY>_<HHMMSS>[_<CH>...]; any network.
struct MemberWindow {
  QString station;
  QDateTime start, end; // UTC
  bool valid = false;
};
MemberWindow windowFromMember(const QString &memberName);

struct DownloadOutcome {
  QString path;          // the .part file, when the body was an archive
  bool expired = false;  // HTML came back: the link has expired
  bool diskFull = false; // ran out of room; the partial file is gone
  bool stopped = false;  // cancelled by the caller
  QString error;         // transport or HTTP failure; path empty
  bool ok() const { return !path.isEmpty(); }
};
// Streams a link to <outDir>/incoming.<pid>.<hash>.zip.part. Any failure
// removes the partial file.
DownloadOutcome download(Http &http, const QString &url, const QString &outDir,
                         int timeoutMs = 120000,
                         const std::function<bool()> &shouldStop = {});

struct Inspection {
  enum class Problem { None, Expired, EmptyArchive };
  Problem problem = Problem::None;
  QStringList members; // files only, no directory entries
  qint64 size = 0;
  QString detail;
  bool hasWaveform() const; // any member ending in .mseed
};
Inspection inspectArchive(const QString &path);
// Up to maxBytes of one member, inflated. Empty on error.
QByteArray readMember(const QString &zipPath, const QString &member,
                      qint64 maxBytes = 200);

struct FiledArchive {
  enum class Kind { Fetched, Duplicate };
  Kind kind = Kind::Fetched;
  QString path; // the final archive, or the .dup copy
  QString detail;
};
// Moves a verified archive to <destRoot>/<STATION>/<STATION>_<date>.zip.
// Never replaces an existing archive silently: without `force` the new file
// is kept beside it as .dup<pid>.zip and Kind::Duplicate is returned.
FiledArchive fileArchive(const QString &path, const Row &row,
                         const QString &destRoot, int memberCount, qint64 size,
                         bool force = false);

QString archivePath(const QString &raw, const Row &row);

} // namespace skg::tdvms

namespace skg::tdvms {

// File an archive already on disk against its window (the recovery path,
// port of tdvms/ingest.py). The file is never deleted on a failed parse.
struct IngestResult {
  bool ok = false;
  QString chunkId; // when matched
  QString message; // what happened, for the log
};
IngestResult ingestFromDisk(Ledger &ledger, const QString &path,
                            const QString &outDir, bool force = false);

} // namespace skg::tdvms
