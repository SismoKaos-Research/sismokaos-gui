#include "study/study.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "analysis/geo.h"
#include "toml.hpp"

namespace skg::study {

const QString Study::kFileName = QStringLiteral("study.toml");

namespace {

const QString kBuiltInStations = QStringLiteral(":/tdvms/data/stations.csv");

[[noreturn]] void fail(const QString &what) {
  throw std::runtime_error(what.toStdString());
}

// ---- writing ----

QString quoted(const QString &s) {
  QString out = QStringLiteral("\"");
  for (const QChar c : s) {
    switch (c.unicode()) {
    case '"':
      out += QStringLiteral("\\\"");
      break;
    case '\\':
      out += QStringLiteral("\\\\");
      break;
    case '\n':
      out += QStringLiteral("\\n");
      break;
    case '\r':
      out += QStringLiteral("\\r");
      break;
    case '\t':
      out += QStringLiteral("\\t");
      break;
    default:
      if (c.unicode() < 0x20)
        out += QStringLiteral("\\u%1").arg(int(c.unicode()), 4, 16,
                                           QLatin1Char('0'));
      else
        out += c;
    }
  }
  return out + QLatin1Char('"');
}

QString number(double v) {
  if (std::isnan(v))
    return QStringLiteral("nan");
  if (std::isinf(v))
    return v > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
  QString s = QString::number(v, 'g', 15);
  if (!s.contains(QLatin1Char('.')) && !s.contains(QLatin1Char('e')))
    s += QStringLiteral(".0"); // keep it a float for TOML
  return s;
}

QString value(const QVariant &v) {
  switch (v.metaType().id()) {
  case QMetaType::Bool:
    return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  case QMetaType::Int:
  case QMetaType::UInt:
  case QMetaType::LongLong:
  case QMetaType::ULongLong:
    return QString::number(v.toLongLong());
  case QMetaType::Double:
  case QMetaType::Float:
    return number(v.toDouble());
  case QMetaType::QStringList:
  case QMetaType::QVariantList: {
    QStringList parts;
    for (const QVariant &e : v.toList())
      parts << value(e);
    return QLatin1Char('[') + parts.join(QStringLiteral(", ")) +
           QLatin1Char(']');
  }
  default:
    return quoted(v.toString());
  }
}

QString stringList(const QStringList &l) {
  QStringList parts;
  for (const QString &s : l)
    parts << quoted(s);
  return QLatin1Char('[') + parts.join(QStringLiteral(", ")) + QLatin1Char(']');
}

// A key is written bare when TOML allows it.
QString key(const QString &k) {
  bool bare = !k.isEmpty();
  for (const QChar c : k)
    bare = bare &&
           (c.isLetterOrNumber() || c == QLatin1Char('_') ||
            c == QLatin1Char('-')) &&
           c.unicode() < 128;
  return bare ? k : quoted(k);
}

// ---- reading ----

QString str(const toml::table &t, const char *k, const QString &def = {}) {
  if (const auto v = t[k].value<std::string>())
    return QString::fromStdString(*v);
  return def;
}

double num(const toml::table &t, const char *k, double def) {
  if (const auto v = t[k].value<double>())
    return *v;
  return def;
}

QStringList strings(const toml::table &t, const char *k) {
  QStringList out;
  if (const toml::array *a = t[k].as_array())
    for (const toml::node &n : *a)
      if (const auto v = n.value<std::string>())
        out << QString::fromStdString(*v);
  return out;
}

QVariant variant(const toml::node &n) {
  if (const auto *v = n.as_boolean())
    return v->get();
  if (const auto *v = n.as_integer())
    return qlonglong(v->get());
  if (const auto *v = n.as_floating_point())
    return v->get();
  if (const auto *v = n.as_string())
    return QString::fromStdString(v->get());
  if (const toml::array *a = n.as_array()) {
    QVariantList l;
    for (const toml::node &e : *a)
      l << variant(e);
    return l;
  }
  return {};
}

QString uniqueSlug(const QString &name, const QStringList &taken) {
  const QString base = slug(name);
  QString id = base;
  for (int n = 2; taken.contains(id); ++n)
    id = QStringLiteral("%1-%2").arg(base).arg(n);
  return id;
}

} // namespace

QString slug(const QString &text) {
  static const QHash<QChar, QChar> fold = {
      {QChar(0x00E7), 'c'}, {QChar(0x00C7), 'c'}, {QChar(0x011F), 'g'},
      {QChar(0x011E), 'g'}, {QChar(0x0131), 'i'}, {QChar(0x0130), 'i'},
      {QChar(0x00F6), 'o'}, {QChar(0x00D6), 'o'}, {QChar(0x015F), 's'},
      {QChar(0x015E), 's'}, {QChar(0x00FC), 'u'}, {QChar(0x00DC), 'u'}};
  QString out;
  bool dash = false;
  for (const QChar raw : text) {
    QChar c = fold.value(raw, raw);
    if (c.unicode() < 128 && c.isLetterOrNumber()) {
      if (dash && !out.isEmpty())
        out += QLatin1Char('-');
      out += c.toLower();
      dash = false;
    } else {
      dash = true;
    }
  }
  return out.isEmpty() ? QStringLiteral("item") : out;
}

Study Study::load(const QString &dir) {
  QFile f(QDir(dir).filePath(kFileName));
  if (!f.open(QIODevice::ReadOnly))
    fail(QStringLiteral("%1: %2").arg(f.fileName(), f.errorString()));
  return fromToml(QString::fromUtf8(f.readAll()), dir);
}

void Study::save() const {
  if (dir.isEmpty())
    fail(QStringLiteral("the study has no folder"));
  if (!QDir().mkpath(dir))
    fail(QStringLiteral("cannot create %1").arg(dir));
  QSaveFile f(QDir(dir).filePath(kFileName));
  if (!f.open(QIODevice::WriteOnly) || f.write(toToml().toUtf8()) < 0 ||
      !f.commit())
    fail(QStringLiteral("%1: %2").arg(f.fileName(), f.errorString()));
}

QString Study::resolved(const QString &path) const {
  if (path.isEmpty() || path.startsWith(QLatin1Char(':')) ||
      QDir::isAbsolutePath(path))
    return path;
  return QDir::cleanPath(QDir(dir).filePath(path));
}

QString Study::stored(const QString &absolutePath) const {
  if (absolutePath.isEmpty() || dir.isEmpty() ||
      absolutePath.startsWith(QLatin1Char(':')))
    return absolutePath;
  const QString abs = QDir::cleanPath(absolutePath);
  const QString rel = QDir(dir).relativeFilePath(abs);
  if (QDir::isAbsolutePath(rel))
    return abs; // another drive
  if (!rel.startsWith(QStringLiteral("..")))
    return rel;
  // Beside the study: relative only when the two share a real folder, not
  // just the root (or /home, /mnt): then they can move together.
  const QStringList a = QDir::cleanPath(dir).split(QLatin1Char('/')),
                    b = abs.split(QLatin1Char('/'));
  int common = 0;
  while (common < a.size() && common < b.size() && a[common] == b[common])
    ++common;
  // a[0] is "" on Unix ("/x/y") and the drive on Windows ("C:/x").
  const int shared = common - 1;
  return shared >= 2 ? rel : abs;
}

QString Study::stationListPath() const {
  return stationList.isEmpty() ? kBuiltInStations : resolved(stationList);
}

QString Study::dataDirFor(const StationRef &s) const {
  if (!s.dataDir.isEmpty())
    return resolved(s.dataDir);
  const QStringList roots = waveformRoots();
  for (const QString &root : roots)
    if (QDir(QDir(root).filePath(s.code)).exists())
      return QDir(root).filePath(s.code);
  return roots.isEmpty() ? QString() : QDir(roots.first()).filePath(s.code);
}

QStringList Study::waveformRoots() const {
  QStringList out;
  for (const WaveRoot &r : waveRoots) {
    const QString abs = QDir::cleanPath(resolved(r.path));
    if (!abs.isEmpty() && !out.contains(abs))
      out << abs;
  }
  return out;
}

bool Study::addWaveformRoot(const QString &absolutePath) {
  const QString abs = QDir::cleanPath(absolutePath);
  if (abs.isEmpty() || waveformRoots().contains(abs))
    return false;
  addWaveRoot(abs);
  return true;
}

QString Study::addWaveRoot(const QString &absolutePath) {
  const QString clean = QDir::cleanPath(absolutePath);
  for (const WaveRoot &r : waveRoots)
    if (QDir::cleanPath(resolved(r.path)) == clean)
      return r.id;
  int n = 1;
  const auto taken = [this](const QString &id) {
    for (const WaveRoot &r : waveRoots)
      if (r.id == id)
        return true;
    return false;
  };
  while (taken(QStringLiteral("w%1").arg(n)))
    ++n;
  waveRoots << WaveRoot{QStringLiteral("w%1").arg(n), stored(clean)};
  return waveRoots.last().id;
}

QString Study::waveRootPath(const QString &id) const {
  for (const WaveRoot &r : waveRoots)
    if (r.id == id)
      return resolved(r.path);
  return {};
}

std::vector<analysis::Station> Study::stations() const {
  std::vector<analysis::Station> out;
  try {
    out = analysis::loadStations(stationListPath());
  } catch (const std::exception &) {
    if (extraStations.isEmpty())
      throw;
  }
  for (const ExtraStation &e : extraStations) {
    const bool known =
        std::any_of(out.begin(), out.end(), [&e](const analysis::Station &s) {
          return s.code.compare(e.code, Qt::CaseInsensitive) == 0;
        });
    if (known)
      continue;
    analysis::Station st;
    st.code = e.code;
    st.network = e.network;
    st.lat = e.lat;
    st.lon = e.lon;
    out.push_back(st);
  }
  return out;
}

QList<MissingInput> Study::missingInputs() const {
  QList<MissingInput> out;
  QStringList seen;
  const auto check = [&](MissingInput::Kind kind, const QString &storedPath) {
    if (storedPath.isEmpty() || storedPath.startsWith(QLatin1Char(':')))
      return;
    const QString abs = resolved(storedPath);
    if (QFileInfo::exists(abs) || seen.contains(abs))
      return;
    seen << abs;
    out.append({kind, abs});
  };
  check(MissingInput::Kind::Catalog, catalog);
  check(MissingInput::Kind::StationList, stationList);
  for (const WaveRoot &r : waveRoots)
    check(MissingInput::Kind::WaveformRoot, r.path);
  for (const Case &c : cases) {
    check(MissingInput::Kind::FaultFile, c.zone.faultGeoJson);
    for (const StationRef &st : c.stations)
      check(MissingInput::Kind::StationData, st.dataDir);
  }
  return out;
}

int Study::relocate(const QString &oldPath, const QString &newPath) {
  QStringList a = QDir::cleanPath(oldPath).split(QLatin1Char('/')),
              b = QDir::cleanPath(newPath).split(QLatin1Char('/'));
  while (a.size() > 1 && b.size() > 1 && a.last() == b.last()) {
    a.removeLast();
    b.removeLast();
  }
  QString oldRoot = a.join(QLatin1Char('/')), newRoot = b.join(QLatin1Char('/'));
  if (oldRoot.isEmpty())
    oldRoot = QStringLiteral("/");
  if (newRoot.isEmpty())
    newRoot = QStringLiteral("/");
  const QString oldPrefix =
      oldRoot.endsWith(QLatin1Char('/')) ? oldRoot : oldRoot + QLatin1Char('/');
  int changed = 0;
  const auto fix = [&](QString &field) {
    if (field.isEmpty() || field.startsWith(QLatin1Char(':')))
      return;
    const QString abs = resolved(field);
    if (QFileInfo::exists(abs))
      return;
    QString moved;
    if (abs == oldRoot)
      moved = newRoot;
    else if (abs.startsWith(oldPrefix))
      moved = QDir(newRoot).filePath(abs.mid(oldPrefix.size()));
    else
      return;
    moved = QDir::cleanPath(moved);
    if (!QFileInfo::exists(moved))
      return;
    field = stored(moved);
    ++changed;
  };
  fix(catalog);
  fix(stationList);
  // A waveform folder keeps its id: the index of what it holds stays valid.
  for (WaveRoot &r : waveRoots)
    fix(r.path);
  for (Case &c : cases) {
    fix(c.zone.faultGeoJson);
    for (StationRef &st : c.stations)
      fix(st.dataDir);
  }
  return changed;
}

const Case *Study::findCase(const QString &id) const {
  for (const Case &c : cases)
    if (c.id == id)
      return &c;
  return nullptr;
}

const Recipe *Study::findRecipe(const QString &id) const {
  for (const Recipe &r : recipes)
    if (r.id == id)
      return &r;
  return nullptr;
}

QString Study::freeCaseId(const QString &n) const {
  QStringList taken;
  for (const Case &c : cases)
    taken << c.id;
  return uniqueSlug(n, taken);
}

QString Study::freeRecipeId(const QString &n) const {
  QStringList taken;
  for (const Recipe &r : recipes)
    taken << r.id;
  return uniqueSlug(n, taken);
}

QString Study::toToml() const {
  QString out;
  const auto line = [&out](const QString &k, const QString &v,
                           const QString &indent = {}) {
    out += indent + k + QStringLiteral(" = ") + v + QLatin1Char('\n');
  };
  out += QStringLiteral("# A sismokaos-gui study. The application rewrites "
                        "this file on every change:\n# edited values are "
                        "read, comments and unknown keys are not kept.\n");
  line(QStringLiteral("schema"), QStringLiteral("1"));
  line(QStringLiteral("name"), quoted(name));
  line(QStringLiteral("catalog"), quoted(catalog));
  line(QStringLiteral("catalog_utc_offset_hours"),
       number(catalogUtcOffsetHours));
  line(QStringLiteral("station_list"), quoted(stationList));
  for (const WaveRoot &r : waveRoots) {
    out += QStringLiteral("\n[[waveforms]]\n");
    line(QStringLiteral("id"), quoted(r.id));
    line(QStringLiteral("path"), quoted(r.path));
  }
  for (const ExtraStation &e : extraStations) {
    out += QStringLiteral("\n[[station]]\n");
    line(QStringLiteral("code"), quoted(e.code));
    line(QStringLiteral("network"), quoted(e.network));
    line(QStringLiteral("latitude"), number(e.lat));
    line(QStringLiteral("longitude"), number(e.lon));
    line(QStringLiteral("source"), quoted(e.source));
  }
  for (const Case &c : cases) {
    out += QStringLiteral("\n[[case]]\n");
    line(QStringLiteral("id"), quoted(c.id));
    line(QStringLiteral("name"), quoted(c.name));
    line(QStringLiteral("kind"),
         quoted(c.reference ? QStringLiteral("reference")
                            : QStringLiteral("earthquake")));
    line(QStringLiteral("time"), quoted(c.time.toUTC().toString(Qt::ISODate)));
    line(QStringLiteral("latitude"), number(c.lat));
    line(QStringLiteral("longitude"), number(c.lon));
    if (!c.reference) {
      if (!std::isnan(c.depthKm))
        line(QStringLiteral("depth_km"), number(c.depthKm));
      if (!std::isnan(c.magnitude))
        line(QStringLiteral("magnitude"), number(c.magnitude));
    }
    line(QStringLiteral("place"), quoted(c.place));
    line(QStringLiteral("span_days"), QString::number(c.spanDays));
    line(QStringLiteral("zone"),
         quoted(c.zone.kind == Zone::Kind::Fault ? QStringLiteral("fault")
                                                 : QStringLiteral("circle")));
    line(QStringLiteral("radius_km"), number(c.zone.radiusKm));
    line(QStringLiteral("fault_geojson"), quoted(c.zone.faultGeoJson));
    line(QStringLiteral("strands"), stringList(c.zone.strands));
    line(QStringLiteral("buffer_km"), number(c.zone.bufferKm));
    const QString in = QStringLiteral("  ");
    for (const StationRef &s : c.stations) {
      out += QStringLiteral("\n  [[case.station]]\n");
      line(QStringLiteral("code"), quoted(s.code), in);
      line(QStringLiteral("latitude"), number(s.lat), in);
      line(QStringLiteral("longitude"), number(s.lon), in);
      line(QStringLiteral("distance_km"), number(s.distanceKm), in);
      line(QStringLiteral("azimuth_deg"), number(s.azimuthDeg), in);
      line(QStringLiteral("data_dir"), quoted(s.dataDir), in);
    }
  }
  for (const Recipe &r : recipes) {
    out += QStringLiteral("\n[[recipe]]\n");
    line(QStringLiteral("id"), quoted(r.id));
    line(QStringLiteral("name"), quoted(r.name));
    line(QStringLiteral("tool"), quoted(r.tool));
    line(QStringLiteral("revision"), QString::number(r.revision));
    line(QStringLiteral("fitted"), stringList(r.fitted));
    out += QStringLiteral("\n  [recipe.settings]\n");
    for (auto it = r.settings.begin(); it != r.settings.end(); ++it)
      line(key(it.key()), value(it.value()), QStringLiteral("  "));
  }
  return out;
}

Study Study::fromToml(const QString &text, const QString &dir) {
  toml::table tbl;
  try {
    tbl = toml::parse(text.toStdString());
  } catch (const toml::parse_error &e) {
    fail(QStringLiteral("study.toml, line %1: %2")
             .arg(e.source().begin.line)
             .arg(QString::fromUtf8(e.description().data(),
                                    qsizetype(e.description().size()))));
  }
  const auto schema = tbl["schema"].value<int64_t>().value_or(1);
  if (schema != 1)
    fail(QStringLiteral("study.toml has schema %1; this version reads 1")
             .arg(schema));
  Study s;
  s.dir = QDir(dir).absolutePath();
  s.name = str(tbl, "name", QFileInfo(dir).fileName());
  s.catalog = str(tbl, "catalog");
  s.catalogUtcOffsetHours = num(tbl, "catalog_utc_offset_hours", 0.0);
  s.stationList = str(tbl, "station_list");
  if (const toml::array *arr = tbl["waveforms"].as_array())
    for (const toml::node &n : *arr)
      if (const toml::table *t = n.as_table()) {
        WaveRoot r{str(*t, "id"), str(*t, "path")};
        if (!r.id.isEmpty() && !r.path.isEmpty())
          s.waveRoots << r;
      }
  // Files from before [[waveforms]] named their folders in data_root and in
  // the waveform_roots list (data_root first): each becomes a folder here.
  {
    QStringList old;
    old << str(tbl, "data_root");
    old += strings(tbl, "waveform_roots");
    for (const QString &path : std::as_const(old))
      if (!path.isEmpty())
        s.addWaveformRoot(s.resolved(path));
  }
  if (const toml::array *arr = tbl["station"].as_array())
    for (const toml::node &n : *arr)
      if (const toml::table *t = n.as_table()) {
        ExtraStation e;
        e.code = str(*t, "code");
        e.network = str(*t, "network");
        e.lat = num(*t, "latitude", 0);
        e.lon = num(*t, "longitude", 0);
        e.source = str(*t, "source", QStringLiteral("typed"));
        if (!e.code.isEmpty())
          s.extraStations << e;
      }
  if (const toml::array *arr = tbl["case"].as_array()) {
    for (const toml::node &n : *arr) {
      const toml::table *t = n.as_table();
      if (!t)
        continue;
      Case c;
      c.id = str(*t, "id");
      c.name = str(*t, "name", c.id);
      if (c.id.isEmpty())
        fail(QStringLiteral("study.toml: a case has no id"));
      if (s.findCase(c.id))
        fail(QStringLiteral("study.toml: two cases have the id %1").arg(c.id));
      c.reference = str(*t, "kind") == QLatin1String("reference");
      c.time = QDateTime::fromString(str(*t, "time"), Qt::ISODate);
      if (!c.time.isValid())
        fail(QStringLiteral("study.toml: case %1 has no valid time").arg(c.id));
      c.time = c.time.toUTC();
      c.lat = num(*t, "latitude", 0);
      c.lon = num(*t, "longitude", 0);
      c.depthKm = num(*t, "depth_km", c.depthKm);
      c.magnitude = num(*t, "magnitude", c.magnitude);
      c.place = str(*t, "place");
      c.spanDays = int(num(*t, "span_days", 365));
      c.zone.kind = str(*t, "zone") == QLatin1String("fault")
                        ? Zone::Kind::Fault
                        : Zone::Kind::Circle;
      c.zone.radiusKm = num(*t, "radius_km", 100.0);
      c.zone.faultGeoJson = str(*t, "fault_geojson");
      c.zone.strands = strings(*t, "strands");
      c.zone.bufferKm = num(*t, "buffer_km", 30.0);
      if (const toml::array *sts = (*t)["station"].as_array()) {
        for (const toml::node &sn : *sts) {
          const toml::table *st = sn.as_table();
          if (!st)
            continue;
          StationRef r;
          r.code = str(*st, "code");
          r.lat = num(*st, "latitude", 0);
          r.lon = num(*st, "longitude", 0);
          r.distanceKm = num(*st, "distance_km", 0);
          r.azimuthDeg = num(*st, "azimuth_deg", 0);
          r.dataDir = str(*st, "data_dir");
          if (!r.code.isEmpty())
            c.stations << r;
        }
      }
      s.cases << c;
    }
  }
  if (const toml::array *arr = tbl["recipe"].as_array()) {
    for (const toml::node &n : *arr) {
      const toml::table *t = n.as_table();
      if (!t)
        continue;
      Recipe r;
      r.id = str(*t, "id");
      r.name = str(*t, "name", r.id);
      r.tool = str(*t, "tool");
      if (r.id.isEmpty() || r.tool.isEmpty())
        fail(QStringLiteral("study.toml: a recipe has no id or no tool"));
      if (s.findRecipe(r.id))
        fail(
            QStringLiteral("study.toml: two recipes have the id %1").arg(r.id));
      r.revision = int(num(*t, "revision", 1));
      r.fitted = strings(*t, "fitted");
      if (const toml::table *st = (*t)["settings"].as_table())
        for (const auto &[k, v] : *st)
          r.settings.insert(
              QString::fromUtf8(k.data(), qsizetype(k.str().size())),
              variant(v));
      s.recipes << r;
    }
  }
  return s;
}

Case caseFromEvent(const analysis::Event &e) {
  Case c;
  c.time = QDateTime::fromSecsSinceEpoch(qint64(std::floor(e.epoch)),
                                         QTimeZone::UTC);
  c.lat = e.lat;
  c.lon = e.lon;
  c.depthKm = e.depthKm;
  c.magnitude = e.mag;
  c.place = e.place;
  const QString where =
      e.place.isEmpty() ? QStringLiteral("M%1").arg(e.mag, 0, 'f', 1) : e.place;
  c.name = QStringLiteral("%1 %2").arg(where).arg(c.time.date().year());
  return c;
}

Case referenceCase(const QString &name, const QDateTime &timeUtc, double lat,
                   double lon) {
  Case c;
  c.reference = true;
  c.name = name;
  c.time = timeUtc.toUTC();
  c.lat = lat;
  c.lon = lon;
  return c;
}

QList<StationRef> nearestStations(const std::vector<analysis::Station> &all,
                                  double lat, double lon, double maxKm,
                                  int limit) {
  QList<StationRef> out;
  for (const analysis::Station &s : all) {
    StationRef r;
    r.code = s.code;
    r.lat = s.lat;
    r.lon = s.lon;
    r.distanceKm = analysis::haversineKm(lat, lon, s.lat, s.lon);
    if (r.distanceKm > maxKm)
      continue;
    r.azimuthDeg = analysis::bearingDeg(lat, lon, s.lat, s.lon);
    out << r;
  }
  std::sort(out.begin(), out.end(),
            [](const StationRef &a, const StationRef &b) {
              return a.distanceKm < b.distanceKm;
            });
  if (limit > 0 && out.size() > limit)
    out.resize(limit);
  return out;
}

} // namespace skg::study
