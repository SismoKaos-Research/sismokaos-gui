#include "study/waveindex.h"

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
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

#include "fx/mseed.h"
#include "miniz.h"

namespace skg::study {

const QString WaveIndex::kFileName = QStringLiteral("waveforms.index.json");

namespace {

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
void listFiles(const QDir &root, const QString &rel, int depth,
               const std::function<bool()> &stop, QStringList &out) {
  if (depth > kMaxDepth || (stop && stop()))
    return;
  const QDir dir(rel.isEmpty() ? root.path() : root.filePath(rel));
  const QFileInfoList entries = dir.entryInfoList(
      QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo &fi : entries) {
    const QString name = fi.fileName();
    if (name.startsWith(QLatin1Char('.')))
      continue;
    const QString child = rel.isEmpty() ? name : rel + QLatin1Char('/') + name;
    if (fi.isDir()) {
      if (!name.startsWith(QLatin1Char('_')))
        listFiles(root, child, depth + 1, stop, out);
    } else if (name != WaveIndex::kFileName) {
      out << child;
    }
  }
}

void noteHeader(WaveFile &f, const fx::RecordHeader &h) {
  if (!h.valid || h.station.isEmpty())
    return;
  const QString cid = h.channelId();
  if (f.channels.contains(cid))
    return;
  WaveChannel c;
  c.rate = h.sampleRate;
  c.firstStart = h.startEpoch;
  f.channels.insert(cid, c);
}

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

void readZip(const QString &path, ScanPass pass, WaveFile &f) {
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_file(&zip, path.toLocal8Bit().constData(), 0)) {
    f.skipped = QStringLiteral("not a readable zip archive");
    return;
  }
  const mz_uint n = mz_zip_reader_get_num_files(&zip);
  int members = 0, bad = 0;
  for (mz_uint i = 0; i < n; ++i) {
    if (mz_zip_reader_is_file_a_directory(&zip, i))
      continue;
    ++members;
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
        noteHeader(f, h);
      else
        ++bad;
    } else {
      tdvms::MseedScanner scanner;
      inflateMember(
          zip, i,
          [&scanner](const QByteArray &b) {
            scanner.feed(b);
            return true;
          },
          failure);
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
  if (f.channels.isEmpty())
    f.skipped = members == 0 ? QStringLiteral("empty zip archive")
                             : QStringLiteral("no miniSEED in the archive");
}

void readPlain(const QString &path, ScanPass pass, WaveFile &f) {
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
  noteHeader(f, h);
  if (pass == ScanPass::Identity)
    return;
  tdvms::MseedScanner scanner;
  try {
    scanner.feed(head);
    while (!file.atEnd())
      scanner.feed(file.read(1 << 20));
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

QJsonObject toJson(const WaveFile &f) {
  QJsonObject o{{"size", double(f.size)}, {"mtime", f.mtime}, {"pass", f.pass}};
  if (!f.skipped.isEmpty())
    o.insert("skipped", f.skipped);
  QJsonObject channels;
  for (auto it = f.channels.begin(); it != f.channels.end(); ++it) {
    QJsonArray spans;
    for (const tdvms::Span &s : it->spans)
      spans.append(QJsonArray{s.first, s.second});
    channels.insert(it.key(), QJsonObject{{"rate", it->rate},
                                          {"first", it->firstStart},
                                          {"spans", spans}});
  }
  o.insert("channels", channels);
  return o;
}

WaveFile fileFromJson(const QJsonObject &o) {
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
    for (const QJsonValue v : c.value("spans").toArray()) {
      const QJsonArray pair = v.toArray();
      if (pair.size() == 2)
        ch.spans.append({pair[0].toDouble(), pair[1].toDouble()});
    }
    f.channels.insert(it.key(), ch);
  }
  return f;
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

WaveIndex WaveIndex::load(const QString &studyDir) {
  WaveIndex idx;
  QFile f(QDir(studyDir).filePath(kFileName));
  if (!f.open(QIODevice::ReadOnly))
    return idx;
  const QJsonObject top = QJsonDocument::fromJson(f.readAll()).object();
  if (top.value("version").toInt() != 1)
    return idx;
  const QJsonObject roots = top.value("roots").toObject();
  for (auto r = roots.begin(); r != roots.end(); ++r) {
    const QJsonObject files = r->toObject().value("files").toObject();
    QMap<QString, WaveFile> &into = idx.roots[r.key()];
    for (auto it = files.begin(); it != files.end(); ++it)
      into.insert(it.key(), fileFromJson(it->toObject()));
  }
  return idx;
}

bool WaveIndex::save(const QString &studyDir, QString *error) const {
  QJsonObject rootsJson;
  for (auto r = roots.begin(); r != roots.end(); ++r) {
    QJsonObject files;
    for (auto it = r->begin(); it != r->end(); ++it)
      files.insert(it.key(), toJson(*it));
    rootsJson.insert(r.key(), QJsonObject{{"files", files}});
  }
  const QJsonObject top{{"version", 1}, {"roots", rootsJson}};
  QSaveFile f(QDir(studyDir).filePath(kFileName));
  if (!f.open(QIODevice::WriteOnly) ||
      f.write(QJsonDocument(top).toJson(QJsonDocument::Compact)) < 0 ||
      !f.commit()) {
    if (error)
      *error = QStringLiteral("%1: %2").arg(f.fileName(), f.errorString());
    return false;
  }
  return true;
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
  };
  QMap<QString, Acc> acc; // "NET.STA"
  for (auto r = roots.begin(); r != roots.end(); ++r) {
    for (auto f = r->begin(); f != r->end(); ++f) {
      if (!f->skipped.isEmpty())
        continue;
      const QStringList codes = f->stationCodes();
      const QString folder =
          f.key().contains(QLatin1Char('/'))
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
        const double start =
            c->spans.isEmpty() ? c->firstStart : c->spans.first().first;
        const double end =
            c->spans.isEmpty() ? c->firstStart : c->spans.last().second;
        if (!a.any || start < st.first)
          st.first = start;
        if (!a.any || end > st.last)
          st.last = end;
        a.any = true;
        addDays(a.days, c->spans, -1e18, 1e18);
        if (!counted.contains(key)) {
          counted.insert(key);
          ++st.files;
          if (f->pass < 2)
            a.allCovered = false;
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
                           const QDateTime &to, bool *approximate) const {
  const double lo = double(from.toSecsSinceEpoch());
  const double hi = double(to.toSecsSinceEpoch());
  QSet<qint64> days;
  bool approx = false;
  for (const auto &files : roots)
    for (const WaveFile &f : files) {
      if (!f.skipped.isEmpty())
        continue;
      for (auto c = f.channels.begin(); c != f.channels.end(); ++c) {
        if (!sameCode(stationOf(c.key()), code))
          continue;
        if (f.pass >= 2) {
          addDays(days, c->spans, lo, hi);
        } else {
          approx = true;
          if (c->firstStart >= lo && c->firstStart < hi)
            days.insert(qint64(std::floor(c->firstStart / kDay)));
        }
      }
    }
  if (approximate)
    *approximate = approx;
  return int(days.size());
}

QList<WaveIndex::Ref> WaveIndex::filesFor(const QString &code,
                                          const QDateTime &from,
                                          const QDateTime &to,
                                          int slackDays) const {
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
        else
          take = c->firstStart < hi && c->firstStart >= lo - slackDays * kDay;
      }
      if (take)
        out << Ref{r.key(), f.key()};
    }
  return out;
}

ScanStats scanRoot(WaveIndex &index, const QString &rootId,
                   const QString &rootPath, ScanPass pass,
                   const ScanHooks &hooks) {
  const QDir root(rootPath);
  if (rootPath.isEmpty() || !root.exists())
    throw std::runtime_error(
        QStringLiteral("the folder %1 does not exist").arg(rootPath)
            .toStdString());
  ScanStats stats;
  QStringList listed;
  listFiles(root, QString(), 0, hooks.shouldStop, listed);
  if (hooks.shouldStop && hooks.shouldStop()) {
    stats.stopped = true;
    return stats;
  }
  stats.listed = int(listed.size());
  QMap<QString, WaveFile> &files = index.roots[rootId];
  const QSet<QString> present(listed.begin(), listed.end());
  for (auto it = files.begin(); it != files.end();) {
    if (!present.contains(it.key())) {
      it = files.erase(it);
      ++stats.dropped;
    } else {
      ++it;
    }
  }
  int done = 0, sinceCheckpoint = 0;
  for (const QString &rel : std::as_const(listed)) {
    if (hooks.shouldStop && hooks.shouldStop()) {
      stats.stopped = true;
      break;
    }
    ++done;
    const QFileInfo fi(root.filePath(rel));
    const double mtime = fi.lastModified().toMSecsSinceEpoch() / 1000.0;
    const auto known = files.constFind(rel);
    if (known != files.constEnd() && known->size == fi.size() &&
        std::abs(known->mtime - mtime) < 1e-3 &&
        (known->pass >= int(pass) || !known->skipped.isEmpty()))
      continue;
    if (hooks.progress)
      hooks.progress(done, stats.listed, rel);
    WaveFile f;
    f.size = fi.size();
    f.mtime = mtime;
    f.pass = int(pass);
    if (rel.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
      readZip(fi.filePath(), pass, f);
    else
      readPlain(fi.filePath(), pass, f);
    if (!f.skipped.isEmpty()) {
      f.channels.clear();
      ++stats.skipped;
    }
    files.insert(rel, f);
    ++stats.read;
    if (hooks.checkpoint && ++sinceCheckpoint >= 50) {
      sinceCheckpoint = 0;
      hooks.checkpoint();
    }
  }
  if (hooks.progress)
    hooks.progress(done, stats.listed, QString());
  return stats;
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

QStringList waveFilesFor(const Study &study, const WaveIndex &index,
                         const StationRef &station, const QDateTime &from,
                         const QDateTime &to, QString *how) {
  QStringList out;
  if (station.dataDir.isEmpty() && index.hasStation(station.code)) {
    for (const WaveIndex::Ref &r : index.filesFor(station.code, from, to)) {
      const QString root = study.waveRootPath(r.rootId);
      if (!root.isEmpty())
        out << QDir(root).filePath(r.rel);
    }
    out.sort();
    if (how)
      *how = QStringLiteral("the index: %1 file(s) overlap the span")
                 .arg(out.size());
    return out;
  }
  const QString dir = study.dataDirFor(station);
  if (dir.isEmpty() || !QDir(dir).exists()) {
    if (how)
      *how = QStringLiteral("no folder");
    return out;
  }
  const QDate first = from.toUTC().date().addDays(-35);
  const QDate last = to.toUTC().date();
  int dated = 0;
  for (const QFileInfo &fi : QDir(dir).entryInfoList(QDir::Files, QDir::Name)) {
    const QDate d = dateInFileName(fi.fileName());
    if (d.isValid()) {
      ++dated;
      if (d < first || d > last)
        continue;
    }
    out << fi.filePath();
  }
  if (how)
    *how = QStringLiteral("the folder %1, by the dates in the file names "
                          "(%2 of %3 files kept; not indexed)")
               .arg(dir)
               .arg(out.size())
               .arg(QDir(dir).entryList(QDir::Files).size());
  Q_UNUSED(dated);
  return out;
}

QList<ExtraStation> readStationXml(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(
        QStringLiteral("cannot open %1").arg(path).toStdString());
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
    throw std::runtime_error(QStringLiteral("%1: %2")
                                 .arg(path, xml.errorString())
                                 .toStdString());
  if (out.isEmpty())
    throw std::runtime_error(
        QStringLiteral("%1 names no station with coordinates")
            .arg(path)
            .toStdString());
  return out;
}

} // namespace skg::study
