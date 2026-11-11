#include "tdvms/fetch.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimeZone>

#include "miniz.h"
#include <algorithm>

namespace skg::tdvms {

namespace {
const QRegularExpression kMemberRe(QStringLiteral(
    R"(^(?:[A-Z0-9]{2})_([A-Z0-9]+)_(\d{8})_(\d{6})_(\d{8})_(\d{6}))"));

QDateTime stamp(const QString &ddmmyyyy, const QString &hhmmss) {
  return naiveUtc(ddmmyyyy, QStringLiteral("ddMMyyyy"), hhmmss,
                  QStringLiteral("HHmmss"));
}
} // namespace

MemberWindow windowFromMember(const QString &memberName) {
  MemberWindow w;
  const QString base = QFileInfo(memberName).fileName();
  const auto m = kMemberRe.match(base);
  if (!m.hasMatch())
    return w;
  w.station = m.captured(1);
  w.start = stamp(m.captured(2), m.captured(3));
  w.end = stamp(m.captured(4), QStringLiteral("000000"));
  w.valid = w.start.isValid() && w.end.isValid();
  return w;
}

DownloadOutcome download(Http &http, const QString &url, const QString &outDir,
                         int timeoutMs,
                         const std::function<bool()> &shouldStop) {
  DownloadOutcome out;
  QDir().mkpath(outDir);
  const QString path =
      QDir(outDir).filePath(QStringLiteral("incoming.%1.%2.zip.part")
                                .arg(QCoreApplication::applicationPid())
                                .arg(qHash(url) % 1000000));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    out.error =
        QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
    return out;
  }
  const HttpResult r =
      http.getToDevice(QUrl(url), &file, timeoutMs, shouldStop);
  const bool writeFailed = file.error() != QFileDevice::NoError;
  const QString fileError = file.errorString();
  file.close();
  if (writeFailed) {
    QFile::remove(path);
    // ENOSPC shows up as a resource error on every platform Qt supports.
    out.diskFull =
        fileError.contains(QLatin1String("space"), Qt::CaseInsensitive) ||
        file.error() == QFileDevice::ResourceError;
    out.error = fileError;
    return out;
  }
  if (r.error == QLatin1String("stopped")) {
    QFile::remove(path);
    out.stopped = true;
    out.error = r.error;
    return out;
  }
  if (!r.error.isEmpty() || !r.ok()) {
    QFile::remove(path);
    out.error =
        r.error.isEmpty() ? QStringLiteral("HTTP %1").arg(r.status) : r.error;
    return out;
  }
  if (r.contentType.contains(QLatin1String("html"), Qt::CaseInsensitive)) {
    QFile::remove(path);
    out.expired = true;
    out.error =
        QStringLiteral("link returned %1, not an archive -- it has expired")
            .arg(r.contentType);
    return out;
  }
  out.path = path;
  return out;
}

bool Inspection::hasWaveform() const {
  for (const QString &m : members)
    if (m.endsWith(QLatin1String(".mseed"), Qt::CaseInsensitive))
      return true;
  return false;
}

Inspection inspectArchive(const QString &path) {
  Inspection out;
  out.size = QFileInfo(path).size();
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, path.toLocal8Bit().constData(), 0)) {
    out.problem = Inspection::Problem::Expired;
    out.detail = QStringLiteral("%1 bytes and not a valid zip").arg(out.size);
    return out;
  }
  const mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; ++i) {
    if (mz_zip_reader_is_file_a_directory(&zip, i))
      continue;
    char name[1024];
    mz_zip_reader_get_filename(&zip, i, name, sizeof name);
    out.members << QString::fromUtf8(name);
  }
  mz_zip_reader_end(&zip);
  if (out.members.isEmpty()) {
    out.problem = Inspection::Problem::EmptyArchive;
    out.detail = QStringLiteral("%1 B, no members").arg(out.size);
  }
  return out;
}

QByteArray readMember(const QString &zipPath, const QString &member,
                      qint64 maxBytes) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, zipPath.toLocal8Bit().constData(), 0))
    return {};
  QByteArray out;
  size_t size = 0;
  void *data = mz_zip_reader_extract_file_to_heap(
      &zip, member.toUtf8().constData(), &size, 0);
  if (data) {
    out = QByteArray(static_cast<const char *>(data),
                     int(std::min<size_t>(size, size_t(maxBytes))));
    mz_free(data);
  }
  mz_zip_reader_end(&zip);
  return out;
}

QString archivePath(const QString &raw, const Row &row) {
  return QDir(raw).filePath(
      QStringLiteral("%1/%1_%2.zip")
          .arg(str(row, "station"), str(row, "start").left(10)));
}

FiledArchive fileArchive(const QString &path, const Row &row,
                         const QString &destRoot, int memberCount, qint64 size,
                         bool force) {
  FiledArchive out;
  const QString station = str(row, "station");
  const QDir dest(QDir(destRoot).filePath(station));
  QDir().mkpath(dest.absolutePath());
  const QString final = archivePath(destRoot, row);
  if (QFileInfo::exists(final) && !force) {
    const QString keep =
        dest.filePath(QStringLiteral("%1_%2.dup%3.zip")
                          .arg(station, str(row, "start").left(10))
                          .arg(QCoreApplication::applicationPid()));
    QFile::remove(keep);
    QFile::rename(path, keep);
    out.kind = FiledArchive::Kind::Duplicate;
    out.path = keep;
    out.detail =
        QStringLiteral("%1 matches no chunk; kept as %2")
            .arg(QFileInfo(final).fileName(), QFileInfo(keep).fileName());
    return out;
  }
  if (QFileInfo::exists(final))
    QFile::remove(final);
  if (!QFile::rename(path, final)) {
    out.kind = FiledArchive::Kind::Duplicate;
    out.path = path;
    out.detail = QStringLiteral("could not move %1 to %2").arg(path, final);
    return out;
  }
  out.kind = FiledArchive::Kind::Fetched;
  out.path = final;
  out.detail = QStringLiteral("%1 MB, %2 file(s)")
                   .arg(size / 1e6, 0, 'f', 1)
                   .arg(memberCount);
  return out;
}

} // namespace skg::tdvms

namespace skg::tdvms {

IngestResult ingestFromDisk(Ledger &ledger, const QString &path,
                            const QString &outDir, bool force) {
  IngestResult out;
  if (!QFileInfo::exists(path)) {
    out.message = QStringLiteral("%1 does not exist").arg(path);
    return out;
  }
  const Inspection insp = inspectArchive(path);
  if (insp.problem != Inspection::Problem::None) {
    out.message =
        QStringLiteral("%1 -- %2 left where it is").arg(insp.detail, path);
    return out;
  }
  const MemberWindow w = windowFromMember(insp.members.first());
  if (!w.valid) {
    out.message = QStringLiteral(
                      "'%1' does not carry a TDVMS window; %2 left where it is")
                      .arg(insp.members.first(), path);
    return out;
  }
  const QString want = w.start.toString(QStringLiteral("yyyy-MM-dd"));
  std::optional<Row> row;
  for (const Row &r : ledger.rows())
    if (str(r, "station") == w.station && str(r, "start").left(10) == want) {
      row = r;
      break;
    }
  if (!row) {
    out.message = QStringLiteral("%1 %2 is not in %3; %4 left where it is")
                      .arg(w.station, want, ledger.path(), path);
    return out;
  }
  out.chunkId = chunkId(*row);
  if (!insp.hasWaveform()) {
    const QString notice =
        QString::fromUtf8(readMember(path, insp.members.first(), 200))
            .trimmed();
    ledger.update(out.chunkId, {{"state", "nodata"},
                                {"fetched_at", nowStamp()},
                                {"bytes", double(insp.size)},
                                {"note", notice}});
    out.ok = true;
    out.message = QStringLiteral("[NODATA] %1 -- %2").arg(out.chunkId, notice);
    return out;
  }
  const FiledArchive filed =
      fileArchive(path, *row, outDir, insp.members.size(), insp.size, force);
  if (filed.kind == FiledArchive::Kind::Fetched) {
    ledger.update(out.chunkId,
                  {{"state", "fetched"},
                   {"fetched_at", nowStamp()},
                   {"bytes", double(insp.size)},
                   {"note", QStringLiteral("%1 file(s), ingested from disk")
                                .arg(insp.members.size())}});
    out.ok = true;
    out.message = QStringLiteral("[OK] %1 -- %2 -> %3")
                      .arg(out.chunkId, filed.detail, filed.path);
    return out;
  }
  out.message = QStringLiteral("[WARN] %1").arg(filed.detail);
  return out;
}

} // namespace skg::tdvms
