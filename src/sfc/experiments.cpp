#include "sfc/experiments.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

#include "fx/csv.h"

namespace skg::sfc {

namespace {
const QStringList kHeader{"time",       "run",      "model",     "params",
                          "n_features", "features", "train_acc", "train_f1",
                          "train_auc",  "val_acc",  "val_f1",    "val_auc",
                          "test_acc",   "test_f1",  "test_auc"};

QString r4(double v) {
  return std::isnan(v)
             ? QString()
             : QString::number(std::round(v * 10000) / 10000, 'g', 10);
}

QStringList splitSemicolon(const QString &line) {
  // params may hold ';'? sfc writes them unquoted; pandas quotes fields with
  // the separator, so handle quotes.
  QStringList out;
  QString cur;
  bool quoted = false;
  for (int i = 0; i < line.size(); ++i) {
    const QChar c = line[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          cur += '"';
          ++i;
        } else
          quoted = false;
      } else
        cur += c;
    } else if (c == '"')
      quoted = true;
    else if (c == ';') {
      out << cur;
      cur.clear();
    } else
      cur += c;
  }
  out << cur;
  return out;
}

QString quoteIfNeeded(const QString &s) {
  if (s.contains(';') || s.contains('"') || s.contains('\n'))
    return '"' + QString(s).replace('"', QStringLiteral("\"\"")) + '"';
  return s;
}
} // namespace

double Trial::metric(const QString &name) const {
  if (name == "train_acc")
    return trainAcc;
  if (name == "train_f1")
    return trainF1;
  if (name == "train_auc")
    return trainAuc;
  if (name == "val_acc")
    return valAcc;
  if (name == "val_f1")
    return valF1;
  if (name == "val_auc")
    return valAuc;
  if (name == "test_acc")
    return testAcc;
  if (name == "test_f1")
    return testF1;
  if (name == "test_auc")
    return testAuc;
  return std::nan("");
}

QString resultsDir(const QString &projectDir, const Settings &s) {
  return QDir(projectDir)
      .filePath(
          QStringLiteral("results/%1/%2/N").arg(s.station, s.earthquakeName));
}
QString modelsDir(const QString &projectDir, const Settings &s) {
  return resultsDir(projectDir, s) + QStringLiteral("/models");
}
QString runDir(const QString &projectDir, const Settings &s) {
  return modelsDir(projectDir, s) + '/' + s.runName;
}

bool logExperiment(const QString &path, const QString &run,
                   const QString &model, const QVariantMap &params,
                   const QStringList &features, const Metrics &train,
                   const Metrics &val, const Metrics &test) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  const bool fresh = !QFileInfo::exists(path) || QFileInfo(path).size() == 0;
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Append))
    return false;
  if (fresh)
    f.write(kHeader.join(';').toUtf8() + '\n');
  QStringList row{QDateTime::currentDateTime().toString(
                      QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                  quoteIfNeeded(run),
                  model,
                  quoteIfNeeded(paramsText(params)),
                  QString::number(features.size()),
                  quoteIfNeeded(features.join(' '))};
  for (const Metrics *m : {&train, &val, &test})
    row << r4(m->accuracy) << r4(m->f1Macro) << r4(m->rocAucMacro);
  f.write(row.join(';').toUtf8() + '\n');
  return true;
}

std::vector<Trial> loadTrials(const QString &path, bool latest) {
  std::vector<Trial> out;
  if (!QFileInfo::exists(path))
    return out;
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return out;
  const QStringList lines = fx::lines(f.readAll());
  if (lines.isEmpty())
    return out;
  const QStringList header = splitSemicolon(lines.first());
  auto col = [&](const char *name) -> int {
    return static_cast<int>(header.indexOf(QLatin1String(name)));
  };
  const int ct = col("time"), cr = col("run"), cm = col("model"),
            cp = col("params"), cn = col("n_features"), cf = col("features");
  const int m[9] = {col("train_acc"), col("train_f1"), col("train_auc"),
                    col("val_acc"),   col("val_f1"),   col("val_auc"),
                    col("test_acc"),  col("test_f1"),  col("test_auc")};
  for (int i = 1; i < lines.size(); ++i) {
    if (lines[i].trimmed().isEmpty())
      continue;
    const QStringList r = splitSemicolon(lines[i]);
    Trial t;
    t.time = r.value(ct);
    t.run = r.value(cr);
    t.model = r.value(cm);
    t.params = r.value(cp);
    t.nFeatures = r.value(cn).toInt();
    t.features = r.value(cf);
    double *dst[9] = {&t.trainAcc, &t.trainF1, &t.trainAuc, &t.valAcc, &t.valF1,
                      &t.valAuc,   &t.testAcc, &t.testF1,   &t.testAuc};
    for (int k = 0; k < 9; ++k)
      *dst[k] = m[k] >= 0 && m[k] < r.size() && !r[m[k]].isEmpty()
                    ? r[m[k]].toDouble()
                    : std::nan("");
    out.push_back(t);
  }
  if (latest) {
    // the last row per (run, model), in order of first appearance
    QMap<QString, int> last;
    QStringList order;
    for (size_t i = 0; i < out.size(); ++i) {
      const QString key = out[i].run + '\n' + out[i].model;
      if (!last.contains(key))
        order << key;
      last.insert(key, int(i));
    }
    std::vector<Trial> kept;
    for (const QString &k : order)
      kept.push_back(out[static_cast<size_t>(last.value(k))]);
    out = kept;
  }
  return out;
}

QStringList runNames(const QString &dir) {
  QStringList names;
  for (const Trial &t :
       loadTrials(dir + QStringLiteral("/experiments.csv"), false))
    if (!names.contains(t.run))
      names << t.run;
  QFileInfoList folders = QDir(dir).entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
  for (const QFileInfo &f : folders)
    if (!names.contains(f.fileName()) &&
        f.fileName() != QLatin1String("sweeps"))
      names << f.fileName();
  return names;
}

std::optional<QVariantMap> runSettings(const QString &dir, const QString &run) {
  const QString path = dir + '/' + run + QStringLiteral("/settings.toml");
  if (!QFileInfo::exists(path))
    return std::nullopt;
  try {
    return Settings::load(path).asMap();
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

namespace {
QString inlineValue(const QVariant &v) {
  switch (v.typeId()) {
  case QMetaType::Bool:
    return v.toBool() ? "true" : "false";
  case QMetaType::QString:
    return v.toString() == QLatin1String("none") ? QStringLiteral("none")
                                                 : '"' + v.toString() + '"';
  case QMetaType::QDate:
    return v.toDate().toString(Qt::ISODate);
  case QMetaType::QVariantList:
  case QMetaType::QStringList: {
    QStringList parts;
    for (const QVariant &e : v.toList())
      parts << inlineValue(e);
    return "[" + parts.join(", ") + "]";
  }
  case QMetaType::QVariantMap: {
    QStringList parts;
    const QVariantMap m = v.toMap();
    for (auto it = m.begin(); it != m.end(); ++it)
      parts << it.key() + " = " + inlineValue(it.value());
    return "{" + parts.join(", ") + "}";
  }
  default:
    return v.toString();
  }
}
} // namespace

QStringList changeTexts(const QVariantMap &run, const QVariantMap &project) {
  QStringList out;
  for (const QString &k : Settings::differences(run, project)) {
    const QVariant a = run.value(k), b = project.value(k);
    if ((a.typeId() == QMetaType::QStringList ||
         a.typeId() == QMetaType::QVariantList) &&
        (b.typeId() == QMetaType::QStringList ||
         b.typeId() == QMetaType::QVariantList)) {
      const QStringList va = a.toStringList(), vb = b.toStringList();
      bool strings = !va.isEmpty() || !vb.isEmpty();
      for (const QVariant &e : a.toList())
        if (e.typeId() != QMetaType::QString)
          strings = false;
      for (const QVariant &e : b.toList())
        if (e.typeId() != QMetaType::QString)
          strings = false;
      if (strings) {
        QStringList edits;
        for (const QString &s : vb)
          if (!va.contains(s))
            edits << "-" + s;
        for (const QString &s : va)
          if (!vb.contains(s))
            edits << "+" + s;
        out << k + " " + edits.join(' ');
        continue;
      }
    }
    out << k + " = " + inlineValue(a);
  }
  return out;
}

std::vector<RunSummary> summarize(const QString &dir,
                                  const QVariantMap &project) {
  const std::vector<Trial> trials =
      loadTrials(dir + QStringLiteral("/experiments.csv"));
  std::vector<RunSummary> out;
  for (const QString &run : runNames(dir)) {
    RunSummary r;
    r.run = run;
    const Trial *best = nullptr;
    QSet<QString> models;
    for (const Trial &t : trials) {
      if (t.run != run)
        continue;
      models.insert(t.model);
      if (t.time > r.time)
        r.time = t.time;
      if (!best || t.valF1 > best->valF1)
        best = &t;
    }
    r.models = models.size();
    if (best) {
      r.bestModel = best->model;
      r.trainF1 = best->trainF1;
      r.valF1 = best->valF1;
      r.testF1 = best->testF1;
      r.testAcc = best->testAcc;
      r.testAuc = best->testAuc;
    }
    if (const auto s = runSettings(dir, run))
      r.changes = changeTexts(*s, project);
    out.push_back(r);
  }
  return out;
}

bool writeConfusionCsv(const QString &path,
                       const std::vector<std::vector<int>> &cm,
                       const QStringList &labels) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  QStringList header{QString()};
  for (const QString &l : labels)
    header << "pred " + l;
  f.write(header.join(';').toUtf8() + '\n');
  for (int i = 0; i < labels.size(); ++i) {
    QStringList row{"true " + labels[i]};
    for (int j = 0; j < labels.size(); ++j)
      row << QString::number(
          cm[static_cast<size_t>(i)][static_cast<size_t>(j)]);
    f.write(row.join(';').toUtf8() + '\n');
  }
  return f.commit();
}

std::optional<std::vector<std::vector<int>>>
readConfusionCsv(const QString &path, QStringList *labels) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return std::nullopt;
  const QStringList lines = fx::lines(f.readAll());
  if (lines.size() < 2)
    return std::nullopt;
  const QStringList header = lines.first().split(';');
  if (labels) {
    labels->clear();
    for (int i = 1; i < header.size(); ++i)
      *labels << QString(header[i]).remove(QStringLiteral("pred ")).trimmed();
  }
  std::vector<std::vector<int>> cm;
  for (int i = 1; i < lines.size(); ++i) {
    if (lines[i].trimmed().isEmpty())
      continue;
    const QStringList r = lines[i].split(';');
    std::vector<int> row;
    for (int j = 1; j < r.size(); ++j)
      row.push_back(r[j].trimmed().toInt());
    cm.push_back(row);
  }
  return cm;
}

std::vector<Trial> bestPerModel(const std::vector<Trial> &trials) {
  QMap<QString, const Trial *> best;
  for (const Trial &t : trials)
    if (!best.contains(t.model) || t.valF1 > best.value(t.model)->valF1)
      best.insert(t.model, &t);
  std::vector<Trial> out;
  for (const Trial *t : best)
    out.push_back(*t);
  std::sort(out.begin(), out.end(),
            [](const Trial &a, const Trial &b) { return a.valF1 > b.valF1; });
  return out;
}

bool writeComparisonCsv(const QString &path, const std::vector<Trial> &best) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write("run;model;train_f1;val_f1;test_f1;test_acc;test_auc;params;train_"
          "val_gap;features\n");
  for (const Trial &t : best)
    f.write((QStringList{quoteIfNeeded(t.run), t.model, r4(t.trainF1),
                         r4(t.valF1), r4(t.testF1), r4(t.testAcc),
                         r4(t.testAuc), quoteIfNeeded(t.params),
                         r4(t.trainF1 - t.valF1), quoteIfNeeded(t.features)}
                 .join(';') +
             '\n')
                .toUtf8());
  return f.commit();
}

int removeRun(const QString &dir, const QString &run) {
  QDir(dir + '/' + run).removeRecursively();
  const QString path = dir + QStringLiteral("/experiments.csv");
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return 0;
  const QStringList lines = fx::lines(f.readAll());
  f.close();
  QStringList kept;
  int removed = 0;
  for (int i = 0; i < lines.size(); ++i) {
    if (i > 0 && !lines[i].trimmed().isEmpty() &&
        splitSemicolon(lines[i]).value(1) == run) {
      ++removed;
      continue;
    }
    if (i == 0 || !lines[i].trimmed().isEmpty())
      kept << lines[i];
  }
  if (removed == 0)
    return 0;
  if (kept.size() <= 1)
    QFile::remove(path);
  else {
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      out.write(kept.join('\n').toUtf8() + '\n');
      out.commit();
    }
  }
  return removed;
}

} // namespace skg::sfc
