#include "ayzek/launcher.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace skg::ayzek {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("ayzek", s); }

QString num(double v) { return QString::number(v, 'g', 10); }

const QStringList kTags = {QStringLiteral("ALARM"),  QStringLiteral("PLOC"),
                           QStringLiteral("PDMAG"),  QStringLiteral("LOCATE"),
                           QStringLiteral("ASSESS"), QStringLiteral("EVENT"),
                           QStringLiteral("ISSUED"), QStringLiteral("MERGE"),
                           QStringLiteral("RANGE")};

// "    label   N  pct" -> N, when the trimmed line starts with `label`:
// the first integer after the label, a parenthesised note skipped.
std::optional<int> countAfter(const QString &line, const QString &label) {
  const QString t = line.trimmed();
  if (!t.startsWith(label))
    return std::nullopt;
  QString rest = t.mid(label.size());
  rest.remove(QRegularExpression(QStringLiteral("\\([^)]*\\)")));
  const QRegularExpressionMatch m =
      QRegularExpression(QStringLiteral("(\\d+)")).match(rest);
  if (!m.hasMatch())
    return std::nullopt;
  return m.captured(1).toInt();
}
} // namespace

std::optional<Install> installAt(const QString &executable) {
  const QFileInfo fi(executable);
  if (executable.isEmpty() || !fi.isFile() || !fi.isExecutable())
    return std::nullopt;
  Install in;
  in.executable = fi.absoluteFilePath();
  in.dir = fi.absolutePath();
  const QString models = in.dir + QStringLiteral("/models");
  if (QFileInfo(models + QStringLiteral("/stations.csv")).exists())
    in.modelsDir = models;
  return in;
}

QString detectorArg(Detector d) {
  switch (d) {
  case Detector::Model:
    return QStringLiteral("6s");
  case Detector::Transformer:
    return QStringLiteral("transformer");
  case Detector::StaLta:
    return QStringLiteral("stalta");
  case Detector::FilterPicker:
    return QStringLiteral("filterpicker");
  }
  return QStringLiteral("6s");
}

QStringList arguments(const Options &o) {
  QStringList a;
  a << QStringLiteral("--no-color");
  if (!o.modelsDir.isEmpty())
    a << QStringLiteral("--models") << o.modelsDir;
  if (!o.stations.isEmpty())
    a << QStringLiteral("--stations") << o.stations;
  if (!o.pdGains.isEmpty())
    a << QStringLiteral("--pd-gains") << o.pdGains;
  if (!o.catalog.isEmpty())
    a << QStringLiteral("--catalog") << o.catalog;
  if (o.detector != Detector::Model)
    a << QStringLiteral("--detector") << detectorArg(o.detector);
  if (o.minStations > 0)
    a << QStringLiteral("--min-stations") << QString::number(o.minStations);
  if (o.threshold >= 0)
    a << QStringLiteral("--threshold") << num(o.threshold);
  if (o.triggerWindows > 0)
    a << QStringLiteral("--trigger-windows")
      << QString::number(o.triggerWindows);
  if (o.instantThreshold >= 0)
    a << QStringLiteral("--instant-threshold") << num(o.instantThreshold);
  if (o.release >= 0)
    a << QStringLiteral("--release") << num(o.release);
  if (o.locate == Locate::Geometry)
    a << QStringLiteral("--locate") << QStringLiteral("geometry");
  else if (o.locate == Locate::Picks)
    a << QStringLiteral("--locate") << QStringLiteral("picks");
  if (o.assess) {
    a << QStringLiteral("--assess");
    if (!o.assessCsv.isEmpty())
      a << QStringLiteral("--assess-csv") << o.assessCsv;
  }
  if (!o.scoresDir.isEmpty())
    a << QStringLiteral("--scores") << o.scoresDir;
  if (!o.recordFile.isEmpty())
    a << QStringLiteral("--record") << o.recordFile;
  if (o.noMagnitude)
    a << QStringLiteral("--no-magnitude");
  if (o.noPick)
    a << QStringLiteral("--no-pick");
  if (o.verbose)
    a << QStringLiteral("--verbose");
  for (const Site &s : o.sites)
    a << QStringLiteral("--site")
      << QStringLiteral("%1,%2,%3").arg(s.name, num(s.lat), num(s.lon));
  a << o.extra;
  if (o.mode == Mode::Live) {
    a << QStringLiteral("--seedlink") << o.server << QStringLiteral("--streams")
      << o.streams;
    if (!o.archiveDir.isEmpty())
      a << QStringLiteral("--archive") << o.archiveDir;
    if (o.durationS > 0)
      a << QStringLiteral("--duration") << num(o.durationS);
  } else {
    a << QStringLiteral("--speed") << num(o.speed);
    if (!o.from.isEmpty())
      a << QStringLiteral("--from") << o.from;
    a << o.files;
  }
  return a;
}

QString validate(const Options &o) {
  if (o.mode == Mode::Replay) {
    if (o.files.isEmpty())
      return tr("Add at least one miniSEED file (one per station).");
    for (const QString &f : o.files)
      if (!QFileInfo(f).isFile())
        return tr("File not found: %1").arg(f);
    if (o.speed < 0)
      return tr("The speed must be 0 (as fast as possible) or more.");
  } else {
    if (o.server.trimmed().isEmpty())
      return tr("Give the SeedLink server as HOST:PORT.");
    if (o.streams.trimmed().isEmpty())
      return tr("Give the streams as NET.STA[,NET.STA...].");
#ifdef Q_OS_WIN
    return tr("Live SeedLink input is not available in the Windows build of "
              "ayzek; replay files instead.");
#endif
  }
  if (!o.catalog.isEmpty() && !QFileInfo(o.catalog).isFile())
    return tr("Catalog not found: %1").arg(o.catalog);
  if (!o.modelsDir.isEmpty() && !QFileInfo(o.modelsDir).isDir())
    return tr("Models directory not found: %1").arg(o.modelsDir);
  return {};
}

QList<Site> parseSites(const QString &text) {
  QList<Site> out;
  for (const QString &raw : text.split('\n')) {
    const QStringList p = raw.trimmed().split(',');
    if (p.size() != 3)
      continue;
    bool okLat = false, okLon = false;
    Site s;
    s.name = p[0].trimmed();
    s.lat = p[1].trimmed().toDouble(&okLat);
    s.lon = p[2].trimmed().toDouble(&okLon);
    if (!s.name.isEmpty() && okLat && okLon)
      out.push_back(s);
  }
  return out;
}

QString sitesText(const QList<Site> &sites) {
  QStringList lines;
  for (const Site &s : sites)
    lines << QStringLiteral("%1,%2,%3").arg(s.name, num(s.lat), num(s.lon));
  return lines.join('\n');
}

QString stripAnsi(const QString &line) {
  static const QRegularExpression re(QStringLiteral("\x1b\\[[0-9;]*m"));
  QString s = line;
  s.remove(re);
  return s;
}

std::optional<Event> parseEventLine(const QString &rawLine) {
  const QString line = stripAnsi(rawLine);
  static const QRegularExpression re(
      QStringLiteral("^([A-Z]+)\\s+(?:#(\\d+)\\s+)?(?:(\\d{2}:\\d{2}:\\d{2}(?:"
                     "\\.\\d+)?)\\s+)?(.*)$"));
  const QRegularExpressionMatch m = re.match(line);
  if (!m.hasMatch() || !kTags.contains(m.captured(1)))
    return std::nullopt;
  Event e;
  e.tag = m.captured(1);
  if (!m.captured(2).isEmpty())
    e.id = m.captured(2).toInt();
  e.time = m.captured(3);
  e.text = m.captured(4).trimmed();
  return e;
}

Summary parseSummary(const QStringList &log) {
  Summary s;
  int alarms = 0;
  bool inEvents = false;
  for (const QString &raw : log) {
    const QString line = stripAnsi(raw);
    if (const auto e = parseEventLine(line); e && e->tag == "ALARM")
      ++alarms;
    const QString t = line.trimmed();
    if (t.startsWith(QStringLiteral("AFAD events within"))) {
      inEvents = true;
      if (const auto n =
              countAfter(t.mid(t.indexOf(QStringLiteral("km")) + 2), QString()))
        s.events = *n;
      continue;
    }
    if (t.startsWith(QStringLiteral("Alarms compared")))
      inEvents = false;
    if (const auto n = countAfter(t, QStringLiteral("correct")))
      s.correct = *n;
    else if (const auto n = countAfter(t, QStringLiteral("false")))
      s.falseAlarms = *n;
    else if (const auto n = countAfter(t, QStringLiteral("detected before S")))
      s.detectedBeforeS = *n;
    else if (inEvents) {
      if (const auto n = countAfter(t, QStringLiteral("detected")))
        s.detected = *n;
      else if (const auto n = countAfter(t, QStringLiteral("missed")))
        s.missed = *n;
    }
  }
  if (alarms > 0 || s.correct >= 0)
    s.alarms = alarms;
  return s;
}

} // namespace skg::ayzek
