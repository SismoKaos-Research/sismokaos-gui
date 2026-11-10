#include "study/waveindex.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>
#include <QXmlStreamReader>

#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "fx/mseed.h"
#include "miniz.h"
#include "study/scancache.h"
#include "core/nativepath.h"

namespace skg::study {

const QString WaveIndex::kFileName = QStringLiteral("waveforms.index.json");

namespace {

// The texts a person reads when the file cannot be used; translated.
class WaveText {
  Q_DECLARE_TR_FUNCTIONS(skg::study::WaveText)
};
constexpr double kDay = 86400.0;
constexpr int kMaxDepth = 8;

QString stationOf(const QString &cid) {
  return cid.section(QLatin1Char('.'), 1, 1);
}
QString networkOf(const QString &cid) {
  return cid.section(QLatin1Char('.'), 0, 0);
}
QString channelLabel(const QString &cid) {
  const QString loc = cid.section(QLatin1Char('.'), 2, 2);
  const QString cha = cid.section(QLatin1Char('.'), 3, 3);
  return loc.isEmpty() ? cha : loc + QLatin1Char('.') + cha;
}
bool sameCode(const QString &a, const QString &b) {
  return a.compare(b, Qt::CaseInsensitive) == 0;
}

// Relative paths of every file under `dir`, not entering "_x" or ".x".
// `hollow` gets the folders (relative; "" is the root) that are there but
// list nothing at all or cannot be read: whether the files the index knows
// under them are gone cannot be told from that.
void listFiles(const QDir &root, const QString &rel, int depth,
               const std::function<bool()> &stop, QStringList &out,
               QStringList &hollow) {
  if (depth > kMaxDepth || (stop && stop()))
    return;
  const QDir dir(rel.isEmpty() ? root.path() : root.filePath(rel));
  const QFileInfoList entries = dir.entryInfoList(
      QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot,
      QDir::Name);
  if (entries.isEmpty() || !dir.isReadable())
    hollow << rel;
  for (const QFileInfo &fi : entries) {
    const QString name = fi.fileName();
    if (name.startsWith(QLatin1Char('.')))
      continue;
    const QString child = rel.isEmpty() ? name : rel + QLatin1Char('/') + name;
    if (fi.isDir()) {
      if (!name.startsWith(QLatin1Char('_')))
        listFiles(root, child, depth + 1, stop, out, hollow);
    } else if (name != WaveIndex::kFileName) {
      out << child;
    }
  }
}

// The first record of a file (or of a zip member) and the name it is filed
// under. The name's period becomes the estimate when the record agrees with
// it: it starts inside the period, or up to a day before it.
void noteHeader(WaveFile &f, const fx::RecordHeader &h, const QString &name) {
  if (!h.valid || h.station.isEmpty())
    return;
  const QString cid = h.channelId();
  auto it = f.channels.find(cid);
  if (it == f.channels.end()) {
    WaveChannel c;
    c.rate = h.sampleRate;
    c.firstStart = h.startEpoch;
    it = f.channels.insert(cid, c);
  } else if (h.startEpoch < it->firstStart) {
    // The members of an archive come in the order of its directory, which
    // need not be the order of time: the earliest start counts.
    it->firstStart = h.startEpoch;
  }
  double from = 0, to = 0;
  if (periodInFileName(name, &from, &to) && h.startEpoch < to &&
      h.startEpoch >= from - kDay)
    it->estimate =
        tdvms::mergeSpans(it->estimate + tdvms::Spans{{h.startEpoch, to}}, 0.0);
}

// What a reader in a worker thread shares with the scan: the stop request,
// and how much of the file (bytes on disk) it has got through.
struct ReadCtx {
  const std::atomic<bool> *stop = nullptr;
  std::atomic<qint64> *bytes = nullptr;
  qint64 counted = 0;   // of this file, already added to *bytes
  bool aborted = false; // stopped before the end: the result is not usable

  bool stopping() const {
    return stop && stop->load(std::memory_order_relaxed);
  }
  void add(qint64 n) {
    counted += n;
    if (bytes)
      bytes->fetch_add(n, std::memory_order_relaxed);
  }
};

void noteSpans(WaveFile &f, const tdvms::ScanResult &got) {
  for (auto it = got.begin(); it != got.end(); ++it) {
    if (it->spans.isEmpty())
      continue;
    WaveChannel &c = f.channels[it.key()];
    const bool fresh = c.spans.isEmpty();
    c.rate = it->rate;
    c.spans = tdvms::mergeSpans(c.spans + it->spans, 1.5 / it->rate);
    if (fresh || c.spans.first().first < c.firstStart)
      c.firstStart = c.spans.first().first;
  }
}

// One zip member through `feed`, in the chunks miniz hands out. Returns
// false when `feed` asked to stop or threw (the exception is in `failure`).
template <typename Feed>
bool inflateMember(mz_zip_archive &zip, mz_uint i, Feed &&feed,
                   std::exception_ptr &failure) {
  struct Ctx {
    Feed *feed;
    std::exception_ptr *failure;
    bool stopped = false;
  } ctx{&feed, &failure};
  auto cb = [](void *opaque, mz_uint64, const void *buf, size_t len) -> size_t {
    auto *c = static_cast<Ctx *>(opaque);
    try {
      if (!(*c->feed)(QByteArray(static_cast<const char *>(buf), int(len)))) {
        c->stopped = true;
        return 0;
      }
    } catch (...) {
      *c->failure = std::current_exception();
      c->stopped = true;
      return 0;
    }
    return len;
  };
  mz_zip_reader_extract_to_callback(&zip, i, cb, &ctx, 0);
  return !ctx.stopped;
}

// The quick pass reads the archive's directory and the start of each member
// (the inflate is stopped after the first record); the coverage pass streams
// every member through the header scanner, a chunk at a time.
void readZip(const QString &path, ScanPass pass, WaveFile &f, ReadCtx &rc) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, minizPath(path).constData(), 0)) {
    f.skipped = QStringLiteral("not a readable zip archive");
    return;
  }
  const mz_uint n = mz_zip_reader_get_num_files(&zip);
  int members = 0, bad = 0;
  for (mz_uint i = 0; i < n && !rc.aborted; ++i) {
    if (mz_zip_reader_is_file_a_directory(&zip, i))
      continue;
    ++members;
    mz_zip_archive_file_stat stat{};
    const bool haveStat = mz_zip_reader_file_stat(&zip, i, &stat);
    std::exception_ptr failure;
    if (pass == ScanPass::Identity) {
      QByteArray head;
      inflateMember(
          zip, i,
          [&head](const QByteArray &b) {
            head += b;
            return head.size() < 512;
          },
          failure);
      const fx::RecordHeader h = fx::parseHeader(head);
      if (h.valid)
        noteHeader(f, h,
                   haveStat ? QString::fromUtf8(stat.m_filename) : QString());
      else
        ++bad;
    } else {
      tdvms::MseedScanner scanner;
      // Progress is counted in bytes on disk: the member's share of what
      // has been inflated so far.
      const double ratio =
          haveStat && stat.m_uncomp_size > 0
              ? double(stat.m_comp_size) / double(stat.m_uncomp_size)
              : 1.0;
      double owed = 0;
      inflateMember(
          zip, i,
          [&](const QByteArray &b) {
            if (rc.stopping()) {
              rc.aborted = true;
              return false;
            }
            scanner.feed(b);
            owed += double(b.size()) * ratio;
            if (owed >= 262144.0) {
              rc.add(qint64(owed));
              owed -= double(qint64(owed));
            }
            return true;
          },
          failure);
      if (rc.aborted)
        break;
      if (failure) {
        ++bad;
        continue;
      }
      try {
        const tdvms::ScanResult got = scanner.finish();
        if (got.isEmpty())
          ++bad;
        noteSpans(f, got);
      } catch (const std::exception &) {
        ++bad;
      }
    }
  }
  mz_zip_reader_end(&zip);
  Q_UNUSED(bad); // a member that is not miniSEED just adds nothing
  if (rc.aborted)
    return;
  if (f.channels.isEmpty())
    f.skipped = members == 0 ? QStringLiteral("empty zip archive")
                             : QStringLiteral("no miniSEED in the archive");
}

void readPlain(const QString &path, ScanPass pass, WaveFile &f, ReadCtx &rc) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    f.skipped = QStringLiteral("cannot be opened");
    return;
  }
  const QByteArray head = file.read(512);
  const fx::RecordHeader h = fx::parseHeader(head);
  if (!h.valid) {
    f.skipped = QStringLiteral("not miniSEED");
    return;
  }
  noteHeader(f, h, QFileInfo(path).fileName());
  if (pass == ScanPass::Identity)
    return;
  // The exact pass starts from nothing: what it finds replaces the estimate.
  f.channels.clear();
  tdvms::MseedScanner scanner;
  try {
    scanner.feed(head);
    rc.add(head.size());
    while (!file.atEnd()) {
      if (rc.stopping()) {
        rc.aborted = true;
        return;
      }
      const QByteArray chunk = file.read(1 << 20);
      scanner.feed(chunk);
      rc.add(chunk.size());
    }
    noteSpans(f, scanner.finish());
  } catch (const std::exception &e) {
    // What was read before the damage still counts.
    try {
      noteSpans(f, scanner.finish());
    } catch (const std::exception &) {
    }
    bool any = false;
    for (const WaveChannel &c : std::as_const(f.channels))
      any = any || !c.spans.isEmpty();
    if (!any)
      f.skipped = QString::fromUtf8(e.what());
  }
}

QJsonArray spansToJson(const tdvms::Spans &spans) {
  QJsonArray out;
  for (const tdvms::Span &s : spans)
    out.append(QJsonArray{s.first, s.second});
  return out;
}

tdvms::Spans spansFromJson(const QJsonValue &value) {
  tdvms::Spans out;
  for (const QJsonValue v : value.toArray()) {
    const QJsonArray pair = v.toArray();
    if (pair.size() == 2)
      out.append({pair[0].toDouble(), pair[1].toDouble()});
  }
  return out;
}

// The days (since the epoch) the spans touch inside [lo, hi).
void addDays(QSet<qint64> &days, const tdvms::Spans &spans, double lo,
             double hi) {
  for (const tdvms::Span &s : tdvms::clipSpans(spans, lo, hi)) {
    if (s.second <= s.first)
      continue;
    const qint64 first = qint64(std::floor(s.first / kDay));
    const qint64 last = qint64(std::floor((s.second - 1e-6) / kDay));
    for (qint64 d = first; d <= last; ++d)
      days.insert(d);
  }
}

} // namespace

QStringList WaveFile::stationCodes() const {
  QStringList out;
  for (auto it = channels.begin(); it != channels.end(); ++it) {
    const QString code = stationOf(it.key());
    if (!out.contains(code))
      out << code;
  }
  return out;
}

bool WaveFile::estimated() const {
  if (pass >= 2)
    return false;
  for (const WaveChannel &c : channels)
    if (!c.estimate.isEmpty())
      return true;
  return false;
}

QJsonObject waveFileToJson(const WaveFile &f) {
  QJsonObject o{{"size", double(f.size)}, {"mtime", f.mtime}, {"pass", f.pass}};
  if (!f.skipped.isEmpty())
    o.insert("skipped", f.skipped);
  QJsonObject channels;
  for (auto it = f.channels.begin(); it != f.channels.end(); ++it) {
    QJsonObject c{{"rate", it->rate},
                  {"first", it->firstStart},
                  {"spans", spansToJson(it->spans)}};
    if (!it->estimate.isEmpty())
      c.insert("estimate", spansToJson(it->estimate));
    channels.insert(it.key(), c);
  }
  o.insert("channels", channels);
  return o;
}

WaveFile waveFileFromJson(const QJsonObject &o) {
  WaveFile f;
  f.size = qint64(o.value("size").toDouble());
  f.mtime = o.value("mtime").toDouble();
  f.pass = o.value("pass").toInt();
  f.skipped = o.value("skipped").toString();
  const QJsonObject channels = o.value("channels").toObject();
  for (auto it = channels.begin(); it != channels.end(); ++it) {
    const QJsonObject c = it->toObject();
    WaveChannel ch;
    ch.rate = c.value("rate").toDouble();
    ch.firstStart = c.value("first").toDouble();
    ch.spans = spansFromJson(c.value("spans"));
    ch.estimate = spansFromJson(c.value("estimate"));
    f.channels.insert(it.key(), ch);
  }
  return f;
}

// Version 1 had no estimates. Its entries are kept; the ones that only went
// through the quick pass are marked unread, so the next quick pass looks at
// their names (it costs one record each).
WaveIndex WaveIndex::load(const QString &studyDir) {
  WaveIndex idx;
  const QString path = QDir(studyDir).filePath(kFileName);
  // Read whole and closed before anything else is done: a reader that keeps
  // the file open stops a writer from replacing it on Windows. A file that
  // is there but will not open is being replaced this moment: tried again.
  QByteArray bytes;
  bool opened = false;
  for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
    {
      QFile f(path);
      if (f.open(QIODevice::ReadOnly)) {
        bytes = f.readAll();
        opened = true;
      }
    }
    if (!opened) {
      if (!QFileInfo::exists(path))
        return idx;
      QThread::msleep(10 * (attempt + 1));
    }
  }
  if (!opened)
    return idx;
  const QJsonObject top = QJsonDocument::fromJson(bytes).object();
  const int version = top.value("version").toInt();
  if (version != 1 && version != 2)
    return idx;
  const QJsonObject roots = top.value("roots").toObject();
  for (auto r = roots.begin(); r != roots.end(); ++r) {
    const QJsonObject files = r->toObject().value("files").toObject();
    QMap<QString, WaveFile> &into = idx.roots[r.key()];
    for (auto it = files.begin(); it != files.end(); ++it) {
      WaveFile file = waveFileFromJson(it->toObject());
      if (version == 1 && file.pass == 1 && file.skipped.isEmpty())
        file.pass = 0;
      into.insert(it.key(), file);
    }
  }
  return idx;
}

bool WaveIndex::save(const QString &studyDir, QString *error,
                     int attempts) const {
  QJsonObject rootsJson;
  for (auto r = roots.begin(); r != roots.end(); ++r) {
    QJsonObject files;
    for (auto it = r->begin(); it != r->end(); ++it)
      files.insert(it.key(), waveFileToJson(*it));
    rootsJson.insert(r.key(), QJsonObject{{"files", files}});
  }
  const QJsonObject top{{"version", 2}, {"roots", rootsJson}};
  const QByteArray bytes = QJsonDocument(top).toJson(QJsonDocument::Compact);
  const QString path = QDir(studyDir).filePath(kFileName);
  // The rename into place fails for a moment when something has the old
  // file open (a reader in another thread, a virus scanner): tried again,
  // with a growing pause.
  QString why;
  for (int attempt = 0; attempt < std::max(1, attempts); ++attempt) {
    if (attempt > 0)
      QThread::msleep(20 * attempt);
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() &&
        f.commit())
      return true;
    why = QStringLiteral("%1: %2").arg(path, f.errorString());
  }
  if (error)
    *error = why;
  return false;
}

std::shared_ptr<const WaveIndex> WaveIndex::shared(const QString &studyDir) {
  struct Entry {
    qint64 size = -1;
    qint64 mtime = 0;
    std::shared_ptr<const WaveIndex> index;
  };
  static QMutex mutex;
  static QMap<QString, Entry> cache;
  const QFileInfo fi(QDir(studyDir).filePath(kFileName));
  const qint64 size = fi.exists() ? fi.size() : -1;
  const qint64 mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
  const QMutexLocker lock(&mutex);
  Entry &e = cache[fi.absoluteFilePath()];
  if (!e.index || e.size != size || e.mtime != mtime) {
    e.index = std::make_shared<const WaveIndex>(load(studyDir));
    e.size = size;
    e.mtime = mtime;
  }
  return e.index;
}

bool WaveIndex::isEmpty() const { return fileCount() == 0; }

int WaveIndex::fileCount() const {
  int n = 0;
  for (const auto &files : roots)
    n += int(files.size());
  return n;
}

int WaveIndex::pendingCoverage() const {
  int n = 0;
  for (const auto &files : roots)
    for (const WaveFile &f : files)
      if (f.skipped.isEmpty() && f.pass < 2)
        ++n;
  return n;
}

int WaveIndex::estimatedFiles() const {
  int n = 0;
  for (const auto &files : roots)
    for (const WaveFile &f : files)
      if (f.skipped.isEmpty() && f.estimated())
        ++n;
  return n;
}

QList<SkippedFile> WaveIndex::skipped() const {
  QList<SkippedFile> out;
  for (auto r = roots.begin(); r != roots.end(); ++r)
    for (auto it = r->begin(); it != r->end(); ++it)
      if (!it->skipped.isEmpty())
        out << SkippedFile{r.key(), it.key(), it->skipped};
  return out;
}

bool WaveIndex::hasStation(const QString &code) const {
  for (const auto &files : roots)
    for (const WaveFile &f : files)
      for (auto it = f.channels.begin(); it != f.channels.end(); ++it)
        if (sameCode(stationOf(it.key()), code))
          return true;
  return false;
}

QList<WaveStation> WaveIndex::stations() const {
  struct Acc {
    WaveStation st;
    QMap<QString, QSet<qint64>> ratesByChannel; // rate in mHz
    QSet<qint64> days;
    bool any = false;
    bool allCovered = true;
    bool allEstimated = true; // every file not yet exact has an estimate
  };
  QMap<QString, Acc> acc; // "NET.STA"
  for (auto r = roots.begin(); r != roots.end(); ++r) {
    for (auto f = r->begin(); f != r->end(); ++f) {
      if (!f->skipped.isEmpty())
        continue;
      const QStringList codes = f->stationCodes();
      const QString folder = f.key().contains(QLatin1Char('/'))
                                 ? f.key().section(QLatin1Char('/'), -2, -2)
                                 : QString();
      QSet<QString> counted;
      for (auto c = f->channels.begin(); c != f->channels.end(); ++c) {
        const QString key =
            networkOf(c.key()) + QLatin1Char('.') + stationOf(c.key());
        Acc &a = acc[key];
        WaveStation &st = a.st;
        st.network = networkOf(c.key());
        st.code = stationOf(c.key());
        const QString label = channelLabel(c.key());
        if (!st.channels.contains(label))
          st.channels << label;
        if (!st.rates.contains(c->rate))
          st.rates << c->rate;
        a.ratesByChannel[label].insert(qint64(std::llround(c->rate * 1000)));
        // Until the file has been read, its estimate stands in for its spans.
        const tdvms::Spans &have = f->exact() ? c->spans : c->estimate;
        const double start = have.isEmpty()
                                 ? c->firstStart
                                 : std::min(c->firstStart, have.first().first);
        const double end = have.isEmpty() ? c->firstStart : have.last().second;
        if (!a.any || start < st.first)
          st.first = start;
        if (!a.any || end > st.last)
          st.last = end;
        a.any = true;
        addDays(a.days, have, -1e18, 1e18);
        if (!counted.contains(key)) {
          counted.insert(key);
          ++st.files;
          if (f->pass < 2) {
            a.allCovered = false;
            if (!f->estimated())
              a.allEstimated = false;
          }
          if (codes.size() > 1)
            st.sharedFiles = true;
          // A folder named for something else: not the code, not containing
          // it, and not just a year or a day number.
          static const QRegularExpression digits(QStringLiteral("^\\d+$"));
          if (!folder.isEmpty() &&
              !folder.contains(st.code, Qt::CaseInsensitive) &&
              !digits.match(folder).hasMatch() &&
              !st.otherFolders.contains(folder))
            st.otherFolders << folder;
        }
      }
    }
  }
  QList<WaveStation> out;
  for (Acc &a : acc) {
    for (const QSet<qint64> &rates : std::as_const(a.ratesByChannel))
      if (rates.size() > 1)
        a.st.mixedRates = true;
    a.st.days = int(a.days.size());
    a.st.coverageKnown = a.allCovered;
    a.st.extentEstimated = !a.allCovered && a.allEstimated;
    std::sort(a.st.channels.begin(), a.st.channels.end());
    std::sort(a.st.rates.begin(), a.st.rates.end());
    out << a.st;
  }
  std::sort(out.begin(), out.end(),
            [](const WaveStation &x, const WaveStation &y) {
              return x.code != y.code ? x.code < y.code : x.network < y.network;
            });
  return out;
}

int WaveIndex::daysCovered(const QString &code, const QDateTime &from,
                           const QDateTime &to, bool *approximate,
                           bool *atLeast) const {
  const double lo = double(from.toSecsSinceEpoch());
  const double hi = double(to.toSecsSinceEpoch());
  QSet<qint64> days;
  bool approx = false, lower = false;
  for (const auto &files : roots)
    for (const WaveFile &f : files) {
      if (!f.skipped.isEmpty())
        continue;
      for (auto c = f.channels.begin(); c != f.channels.end(); ++c) {
        if (!sameCode(stationOf(c.key()), code))
          continue;
        if (f.pass >= 2) {
          addDays(days, c->spans, lo, hi);
        } else if (!c->estimate.isEmpty()) {
          approx = true;
          addDays(days, c->estimate, lo, hi);
        } else {
          approx = lower = true;
          if (c->firstStart >= lo && c->firstStart < hi)
            days.insert(qint64(std::floor(c->firstStart / kDay)));
        }
      }
    }
  if (approximate)
    *approximate = approx;
  if (atLeast)
    *atLeast = lower;
  return int(days.size());
}

QList<WaveIndex::Ref> WaveIndex::filesFor(const QString &code,
                                          const QDateTime &from,
                                          const QDateTime &to,
                                          int slackDays) const {
  Q_UNUSED(slackDays);
  const double lo = double(from.toSecsSinceEpoch());
  const double hi = double(to.toSecsSinceEpoch());
  QList<Ref> out;
  for (auto r = roots.begin(); r != roots.end(); ++r)
    for (auto f = r->begin(); f != r->end(); ++f) {
      if (!f->skipped.isEmpty())
        continue;
      bool take = false;
      for (auto c = f->channels.begin(); c != f->channels.end() && !take; ++c) {
        if (!sameCode(stationOf(c.key()), code))
          continue;
        if (f->pass >= 2)
          take = !tdvms::clipSpans(c->spans, lo, hi).isEmpty();
        else if (!c->estimate.isEmpty())
          // An estimate may be off at its ends: a day of margin both ways.
          take = !tdvms::clipSpans(c->estimate, lo - kDay, hi + kDay).isEmpty();
        else
          // Nothing says how far the file reaches: any file that starts
          // before the period ends may hold some of it.
          take = c->firstStart < hi;
      }
      if (take)
        out << Ref{r.key(), f.key(), f->pass >= 2};
    }
  return out;
}

int defaultScanWorkers() {
  return std::clamp(QThread::idealThreadCount() - 2, 1, 6);
}

namespace {

// One file a scan has to read.
struct ScanJob {
  QString rootId, rel, path;
  qint64 size = 0;
  double mtime = 0;
  WaveFile known; // its entry so far, when the file has not changed
  bool hasKnown = false;
  bool first = false; // of a station that is read before the others
  QString station;    // what the order goes by
};

struct ScanResultOf {
  int job = -1;
  WaveFile file;
  bool fromCache = false;
  bool aborted = false;
};

WaveFile readFile(const ScanJob &job, ScanPass pass, ReadCtx &rc) {
  WaveFile f;
  f.size = job.size;
  f.mtime = job.mtime;
  f.pass = int(pass);
  try {
    if (job.rel.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
      readZip(job.path, pass, f, rc);
    else
      readPlain(job.path, pass, f, rc);
  } catch (const std::exception &e) {
    f.skipped = QString::fromUtf8(e.what());
  }
  if (!f.skipped.isEmpty())
    f.channels.clear();
  return f;
}

// One file, start to finish, in a worker: the cache by path, the quick look
// (unless the index has it), the cache by name, and only then the whole file.
ScanResultOf scanOne(int jobIndex, const ScanJob &job, ScanPass pass,
                     ScanCache *cache, const std::atomic<bool> &stop,
                     std::atomic<qint64> &bytes) {
  ScanResultOf out;
  out.job = jobIndex;
  ReadCtx rc;
  rc.stop = &stop;
  rc.bytes = &bytes;
  const auto finish = [&](const WaveFile &f, bool fromCache) {
    rc.add(job.size - rc.counted); // the whole file counts as done
    out.file = f;
    out.fromCache = fromCache;
    return out;
  };
  if (cache && cache->lookup(job.path, job.size, job.mtime, &out.file))
    return finish(out.file, true);
  // What the file starts with: needed for the estimate, and to believe a
  // cached result filed under the file's name alone.
  WaveFile identity;
  if (job.hasKnown && job.known.pass == 1 && job.known.skipped.isEmpty()) {
    identity = job.known;
  } else {
    ReadCtx quick; // a few kilobytes: not worth counting or interrupting
    identity = readFile(job, ScanPass::Identity, quick);
  }
  if (!identity.skipped.isEmpty())
    return finish(identity, false);
  if (cache && cache->lookupMoved(job.path, job.size, job.mtime, identity,
                                  &out.file)) {
    cache->store(job.path, job.size, job.mtime, out.file, identity);
    return finish(out.file, true);
  }
  if (pass == ScanPass::Identity)
    return finish(identity, false);
  const WaveFile exact = readFile(job, ScanPass::Coverage, rc);
  if (rc.aborted) {
    out.aborted = true;
    return out;
  }
  if (cache)
    cache->store(job.path, job.size, job.mtime, exact, identity);
  return finish(exact, false);
}

} // namespace

ScanStats scanRoots(WaveIndex &index, const QList<ScanFolder> &folders,
                    ScanPass pass, const ScanHooks &hooks,
                    const ScanOptions &options) {
  ScanStats stats;
  const auto stopAsked = [&hooks] {
    return hooks.shouldStop && hooks.shouldStop();
  };
  // What is there, what is gone, and what has to be read.
  std::vector<ScanJob> jobs;
  for (const ScanFolder &folder : folders) {
    const QDir root(folder.path);
    if (folder.path.isEmpty() || !root.exists()) {
      stats.missing << folder.path;
      continue;
    }
    QStringList listed, hollow;
    listFiles(root, QString(), 0, hooks.shouldStop, listed, hollow);
    if (stopAsked()) {
      stats.stopped = true;
      return stats;
    }
    stats.listed += int(listed.size());
    QMap<QString, WaveFile> &files = index.roots[folder.id];
    const QSet<QString> present(listed.begin(), listed.end());
    // A folder that is there but lists nothing says nothing about the files
    // the index knows under it: the mount point of a drive that is not
    // connected looks like that, and so does a share that dropped out.
    // Their entries stay until the user says the files are gone.
    const auto underHollow = [&hollow](const QString &rel) -> const QString * {
      for (const QString &h : hollow)
        if (h.isEmpty() || rel.startsWith(h + QLatin1Char('/')))
          return &h;
      return nullptr;
    };
    QSet<QString> kept;
    for (auto it = files.begin(); it != files.end();) {
      if (present.contains(it.key())) {
        ++it;
        continue;
      }
      const QString *h =
          options.forgetEmptyFolders ? nullptr : underHollow(it.key());
      if (h) {
        kept.insert(*h);
        ++it;
      } else {
        it = files.erase(it);
        ++stats.dropped;
      }
    }
    for (const QString &h : std::as_const(kept))
      stats.unavailable << (h.isEmpty() ? root.path() : root.filePath(h));
    for (const QString &rel : std::as_const(listed)) {
      const QFileInfo fi(root.filePath(rel));
      ScanJob job;
      job.rootId = folder.id;
      job.rel = rel;
      job.path = fi.filePath();
      job.size = fi.size();
      job.mtime = fi.lastModified().toMSecsSinceEpoch() / 1000.0;
      const auto known = files.constFind(rel);
      if (known != files.constEnd() && known->size == job.size &&
          sameFileTime(known->mtime, job.mtime)) {
        if (known->pass >= int(pass) || !known->skipped.isEmpty())
          continue;
        job.known = *known;
        job.hasKnown = true;
      }
      // The station the order goes by: what the quick pass found, or the
      // folder the file is in.
      const QStringList codes =
          job.hasKnown ? job.known.stationCodes() : QStringList();
      job.station = !codes.isEmpty() ? codes.first()
                    : rel.contains(QLatin1Char('/'))
                        ? rel.section(QLatin1Char('/'), -2, -2)
                        : QString();
      for (const QString &code : options.firstStations)
        job.first = job.first || sameCode(code, job.station);
      stats.bytes += job.size;
      jobs.push_back(job);
    }
  }
  std::stable_sort(
      jobs.begin(), jobs.end(), [](const ScanJob &a, const ScanJob &b) {
        if (a.first != b.first)
          return a.first;
        const int c = a.station.compare(b.station, Qt::CaseInsensitive);
        if (c != 0)
          return c < 0;
        return a.rootId != b.rootId ? a.rootId < b.rootId : a.rel < b.rel;
      });
  const int total = int(jobs.size());
  const int unfiledBefore = options.cache ? options.cache->writeFailures() : 0;
  if (total == 0) {
    if (hooks.progress)
      hooks.progress(0, 0, QString());
    return stats;
  }

  // The workers take the jobs in order and hand back what they read; this
  // thread alone touches the index and calls the hooks.
  std::atomic<bool> stop{false};
  std::atomic<qint64> bytes{0};
  std::atomic<int> next{0};
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<ScanResultOf> ready;
  const int workers = std::clamp(options.workers, 1, std::max(1, total));
  // Workers that have not finished; they count themselves out as they end,
  // which may be while the later ones are still being started.
  int running = workers;
  std::vector<std::thread> pool;
  pool.reserve(size_t(workers));
  for (int w = 0; w < workers; ++w)
    pool.emplace_back([&] {
      for (;;) {
        const int i = next.fetch_add(1);
        if (i >= total || stop.load())
          break;
        ScanResultOf r;
        try {
          r = scanOne(i, jobs[size_t(i)], pass, options.cache, stop, bytes);
        } catch (...) {
          r.job = i;
          r.file.size = jobs[size_t(i)].size;
          r.file.mtime = jobs[size_t(i)].mtime;
          r.file.pass = int(pass);
          r.file.skipped = QStringLiteral("could not be read");
        }
        const std::lock_guard<std::mutex> lock(mutex);
        ready.push_back(std::move(r));
        wake.notify_one();
      }
      const std::lock_guard<std::mutex> lock(mutex);
      --running;
      wake.notify_one();
    });

  int done = 0, sinceCheckpoint = 0;
  QElapsedTimer sinceSave;
  sinceSave.start();
  // Whatever a hook throws, the workers are stopped and joined first.
  const auto joinAll = [&] {
    for (std::thread &t : pool)
      if (t.joinable())
        t.join();
  };
  try {
    for (;;) {
      std::deque<ScanResultOf> got;
      bool finished = false;
      {
        std::unique_lock<std::mutex> lock(mutex);
        wake.wait_for(lock,
                      std::chrono::milliseconds(std::max(1, options.pollMs)),
                      [&] { return !ready.empty() || running == 0; });
        got.swap(ready);
        finished = running == 0;
      }
      for (ScanResultOf &r : got) {
        if (r.aborted)
          continue;
        const ScanJob &job = jobs[size_t(r.job)];
        if (!r.file.skipped.isEmpty())
          ++stats.skipped;
        if (r.fromCache)
          ++stats.cached;
        else
          ++stats.read;
        index.roots[job.rootId].insert(job.rel, r.file);
        ++done;
        ++sinceCheckpoint;
        if (hooks.progress)
          hooks.progress(done, total, job.rel);
      }
      if (hooks.bytes)
        hooks.bytes(std::min(bytes.load(), stats.bytes), stats.bytes, done,
                    total);
      if (!stop.load() && stopAsked()) {
        stats.stopped = true;
        stop.store(true);
      }
      if (finished && got.empty())
        break;
      if (hooks.checkpoint && sinceCheckpoint > 0 &&
          (sinceCheckpoint >= options.checkpointFiles ||
           double(sinceSave.elapsed()) >= options.checkpointSeconds * 1000.0)) {
        sinceCheckpoint = 0;
        sinceSave.restart();
        hooks.checkpoint();
      }
    }
  } catch (...) {
    stop.store(true);
    joinAll();
    throw;
  }
  joinAll();
  if (options.cache)
    stats.unfiled = options.cache->writeFailures() - unfiledBefore;
  if (done < total)
    stats.stopped = true;
  if (hooks.progress)
    hooks.progress(done, total, QString());
  return stats;
}

ScanStats scanRoot(WaveIndex &index, const QString &rootId,
                   const QString &rootPath, ScanPass pass,
                   const ScanHooks &hooks, const ScanOptions &options) {
  if (rootPath.isEmpty() || !QDir(rootPath).exists())
    throw std::runtime_error(WaveText::tr("the folder %1 does not exist")
                                 .arg(rootPath)
                                 .toStdString());
  return scanRoots(index, {ScanFolder{rootId, rootPath}}, pass, hooks, options);
}

bool sameFileTime(double stored, double onDisk) {
  const double d = std::abs(stored - onDisk);
  if (d <= 0.0105)
    return true;
  const auto whole = [](double t) {
    return std::abs(t - std::round(t)) < 1e-3;
  };
  return d <= 2.0 + 1e-3 && (whole(stored) || whole(onDisk));
}

QStringList unavailableWaveRoots(const Study &study, const WaveIndex &index) {
  QStringList out;
  for (const WaveRoot &r : study.waveRoots) {
    const auto known = index.roots.constFind(r.id);
    if (known == index.roots.constEnd() || known->isEmpty())
      continue;
    const QDir dir(study.resolved(r.path));
    if (dir.exists() &&
        (!dir.isReadable() ||
         dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)
             .isEmpty()))
      out << dir.path();
  }
  return out;
}

namespace {
void stampDirs(const QDir &dir, int depth, QCryptographicHash &hash) {
  hash.addData(dir.path().toUtf8());
  // A study folder inside the waveform folder changes whenever the study or
  // its index is saved; that is not new data.
  if (!QFileInfo::exists(dir.filePath(Study::kFileName)))
    hash.addData(QByteArray::number(
        QFileInfo(dir.path()).lastModified().toMSecsSinceEpoch()));
  if (depth >= kMaxDepth)
    return;
  for (const QFileInfo &fi :
       dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
    const QString name = fi.fileName();
    if (!name.startsWith(QLatin1Char('.')) &&
        !name.startsWith(QLatin1Char('_')))
      stampDirs(QDir(fi.filePath()), depth + 1, hash);
  }
}
} // namespace

QString folderStamp(const QString &rootPath) {
  if (rootPath.isEmpty() || !QDir(rootPath).exists())
    return QStringLiteral("missing");
  QCryptographicHash hash(QCryptographicHash::Sha1);
  stampDirs(QDir(rootPath), 0, hash);
  return QString::fromLatin1(hash.result().toHex());
}

bool hasWaveforms(const Study &study, const StationRef &station) {
  const QString dir = study.dataDirFor(station);
  if (!dir.isEmpty() && QDir(dir).exists())
    return true;
  return station.dataDir.isEmpty() &&
         WaveIndex::shared(study.dir)->hasStation(station.code);
}

QDate dateInFileName(const QString &fileName) {
  static const QRegularExpression iso(
      QStringLiteral("(?<!\\d)(\\d{4})-(\\d{2})-(\\d{2})(?!\\d)"));
  static const QRegularExpression dmy(
      QStringLiteral("_(\\d{2})(\\d{2})(\\d{4})_"));
  QRegularExpressionMatch m = iso.match(fileName);
  if (m.hasMatch())
    return QDate(m.captured(1).toInt(), m.captured(2).toInt(),
                 m.captured(3).toInt());
  m = dmy.match(fileName);
  if (m.hasMatch())
    return QDate(m.captured(3).toInt(), m.captured(2).toInt(),
                 m.captured(1).toInt());
  return {};
}

bool periodInFileName(const QString &fileName, double *start, double *end) {
  // "_ddMMyyyy_HHmmss_ddMMyyyy_HHmmss": four runs of digits, 8-6-8-6 long,
  // each after an underscore. Parsed by hand: this runs in scan workers.
  const auto stamp = [&fileName](int at, QDateTime *out) {
    // at: index of the '_' before the date.
    if (at + 16 > fileName.size() || fileName[at] != QLatin1Char('_') ||
        fileName[at + 9] != QLatin1Char('_'))
      return false;
    for (int i : {1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15})
      if (!fileName[at + i].isDigit())
        return false;
    const auto num = [&fileName, at](int from, int len) {
      return fileName.mid(at + from, len).toInt();
    };
    const QDate d(num(5, 4), num(3, 2), num(1, 2));
    const QTime t(num(10, 2), num(12, 2), num(14, 2));
    if (!d.isValid() || !t.isValid())
      return false;
    *out = QDateTime(d, t, QTimeZone::UTC);
    return true;
  };
  for (int at = fileName.indexOf(QLatin1Char('_')); at >= 0;
       at = fileName.indexOf(QLatin1Char('_'), at + 1)) {
    QDateTime a, b;
    if (!stamp(at, &a) || !stamp(at + 16, &b))
      continue;
    // The second stamp must end the run of digits too.
    const int after = at + 32;
    if (after < fileName.size() && fileName[after].isDigit())
      continue;
    if (b <= a || a.daysTo(b) > 3700)
      continue;
    if (start)
      *start = double(a.toSecsSinceEpoch());
    if (end)
      *end = double(b.toSecsSinceEpoch());
    return true;
  }
  return false;
}

WaveChoice chooseWaveFiles(const Study &study, const WaveIndex &index,
                           const StationRef &station, const QDateTime &from,
                           const QDateTime &to) {
  WaveChoice out;
  if (station.dataDir.isEmpty() && index.hasStation(station.code)) {
    for (const WaveIndex::Ref &r : index.filesFor(station.code, from, to)) {
      const QString root = study.waveRootPath(r.rootId);
      if (root.isEmpty())
        continue;
      const QString path = QDir(root).filePath(r.rel);
      out.files << path;
      if (!r.exact)
        out.uncertain << path;
    }
    out.files.sort();
    out.uncertain.sort();
    out.how = out.uncertain.isEmpty()
                  ? QStringLiteral("the index: %1 file(s) overlap the span")
                        .arg(out.files.size())
                  : QStringLiteral("the index: %1 file(s) for the span, %2 of "
                                   "them taken on an estimate from their "
                                   "names (not read through yet)")
                        .arg(out.files.size())
                        .arg(out.uncertain.size());
    return out;
  }
  out.files = waveFilesFor(study, WaveIndex(), station, from, to, &out.how);
  return out;
}

QStringList waveFilesFor(const Study &study, const WaveIndex &index,
                         const StationRef &station, const QDateTime &from,
                         const QDateTime &to, QString *how) {
  QStringList out;
  if (station.dataDir.isEmpty() && index.hasStation(station.code)) {
    const WaveChoice choice = chooseWaveFiles(study, index, station, from, to);
    if (how)
      *how = choice.how;
    return choice.files;
  }
  const QString dir = study.dataDirFor(station);
  if (dir.isEmpty() || !QDir(dir).exists()) {
    if (how)
      *how = QStringLiteral("no folder");
    return out;
  }
  // What the names say. A name with a period (..._ddMMyyyy_HHmmss_ddMMyyyy_
  // HHmmss_...) says what the file covers: it is taken when that overlaps the
  // span, with a day of margin. A name with one date says only where the
  // file starts, not how far it reaches: it is taken unless it starts after
  // the span, or a later file of the folder starts more than five weeks
  // before the span (the downloaders file one request after the other, so
  // the next file's start bounds this one's reach; without a later file
  // nothing bounds it, and a file of a whole year is not dropped). A name
  // with no date is always taken.
  const double lo = double(from.toSecsSinceEpoch());
  const double hi = double(to.toSecsSinceEpoch());
  const QDate spanFirst = from.toUTC().date();
  const QDate spanLast = to.toUTC().date();
  const QFileInfoList all = QDir(dir).entryInfoList(QDir::Files, QDir::Name);
  QList<QDate> starts; // of every dated file, for "the next one"
  for (const QFileInfo &fi : all) {
    const QDate d = dateInFileName(fi.fileName());
    if (d.isValid())
      starts << d;
  }
  std::sort(starts.begin(), starts.end());
  for (const QFileInfo &fi : all) {
    double a = 0, b = 0;
    if (periodInFileName(fi.fileName(), &a, &b)) {
      if (a < hi + kDay && b > lo - kDay)
        out << fi.filePath();
      continue;
    }
    const QDate d = dateInFileName(fi.fileName());
    if (d.isValid()) {
      if (d > spanLast)
        continue;
      const auto next = std::upper_bound(starts.begin(), starts.end(), d);
      if (next != starts.end() && *next < spanFirst.addDays(-35))
        continue;
    }
    out << fi.filePath();
  }
  if (how)
    *how = QStringLiteral("the folder %1, by the dates in the file names "
                          "(%2 of %3 files kept; not indexed)")
               .arg(dir)
               .arg(out.size())
               .arg(all.size());
  return out;
}

QList<ExtraStation> readStationXml(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(
        WaveText::tr("cannot open %1").arg(path).toStdString());
  QXmlStreamReader xml(&f);
  QList<ExtraStation> out;
  QString network;
  ExtraStation cur;
  bool inStation = false, inChannel = false, haveLat = false, haveLon = false;
  while (!xml.atEnd()) {
    const auto token = xml.readNext();
    if (token == QXmlStreamReader::StartElement) {
      const auto name = xml.name();
      if (name == QLatin1String("Network")) {
        network = xml.attributes().value(QLatin1String("code")).toString();
      } else if (name == QLatin1String("Station")) {
        cur = ExtraStation{};
        cur.code = xml.attributes().value(QLatin1String("code")).toString();
        cur.network = network;
        cur.source = QStringLiteral("stationxml");
        inStation = true;
        inChannel = haveLat = haveLon = false;
      } else if (name == QLatin1String("Channel")) {
        inChannel = true;
      } else if (inStation && !inChannel && name == QLatin1String("Latitude")) {
        cur.lat = xml.readElementText().toDouble(&haveLat);
      } else if (inStation && !inChannel &&
                 name == QLatin1String("Longitude")) {
        cur.lon = xml.readElementText().toDouble(&haveLon);
      }
    } else if (token == QXmlStreamReader::EndElement) {
      if (xml.name() == QLatin1String("Channel")) {
        inChannel = false;
      } else if (xml.name() == QLatin1String("Station")) {
        if (!cur.code.isEmpty() && haveLat && haveLon)
          out << cur;
        inStation = false;
      }
    }
  }
  if (xml.hasError())
    throw std::runtime_error(
        QStringLiteral("%1: %2").arg(path, xml.errorString()).toStdString());
  if (out.isEmpty())
    throw std::runtime_error(
        WaveText::tr("%1 names no station with coordinates")
            .arg(path)
            .toStdString());
  return out;
}

} // namespace skg::study
