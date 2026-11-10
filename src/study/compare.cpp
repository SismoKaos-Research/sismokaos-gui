#include "study/compare.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

#include "analysis/label.h"
#include "cbi/table.h"
#include "fx/npy.h"
#include "sfc/experiments.h"

namespace skg::study {

class CompareText {
  Q_DECLARE_TR_FUNCTIONS(skg::study::CompareText)
};

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// What was read from a run's files, kept until its run.json changes (a new
// run replaces the folder, so its record is a new file).
template <class T> class RunCache {
public:
  template <class Make>
  T get(const QString &runDir, const QString &what, Make &&make) {
    const QFileInfo fi(QDir(runDir).filePath(RunRecord::kFileName));
    const qint64 stamp = fi.lastModified().toMSecsSinceEpoch() ^ (fi.size() << 44);
    const QString key = runDir + QLatin1Char('\n') + what;
    {
      const QMutexLocker lock(&mutex_);
      const auto it = map_.constFind(key);
      if (it != map_.constEnd() && it->first == stamp)
        return it->second;
    }
    T value = make(); // throws: nothing is remembered
    const QMutexLocker lock(&mutex_);
    if (map_.size() >= 512)
      map_.clear();
    map_.insert(key, qMakePair(stamp, value));
    return value;
  }

private:
  QMutex mutex_;
  QHash<QString, QPair<qint64, T>> map_;
};

struct Scores {
  QString best, base;
  double f1 = kNaN, auc = kNaN, baseF1 = kNaN, baseAuc = kNaN;
};
struct Daily {
  std::vector<double> days, values;
  bool found = false; // the table has the column
};

RunCache<Scores> &scoreCache() {
  static RunCache<Scores> c;
  return c;
}
RunCache<QStringList> &columnCache() {
  static RunCache<QStringList> c;
  return c;
}
RunCache<cbi::IndicatorTable> &tableCache() {
  static RunCache<cbi::IndicatorTable> c;
  return c;
}
RunCache<Daily> &dailyCache() {
  static RunCache<Daily> c;
  return c;
}

struct RowKey {
  const Case *c;
  QString station;
};

// The rows a recipe has: one per case and station for the per-station
// tools, one per case otherwise.
QList<RowKey> rowsOf(const Study &s, const Recipe &r) {
  QList<RowKey> out;
  for (const Case &c : s.cases) {
    if (!perStation(r.tool)) {
      out << RowKey{&c, QString()};
      continue;
    }
    for (const StationRef &st : c.stations)
      out << RowKey{&c, st.code};
  }
  return out;
}

QString outputPath(const Cell &cell, const QString &name) {
  if (!cell.record)
    return {};
  const QString rel = cell.record->outputs.value(name).toString();
  return rel.isEmpty() ? QString() : QDir(cell.dir).filePath(rel);
}

QString num(double v, int decimals) {
  return std::isnan(v) ? QString() : QString::number(v, 'f', decimals);
}

} // namespace

bool isBaselineModel(const QString &model) {
  return model == QLatin1String("logistic") ||
         model.startsWith(QLatin1String("svm_"));
}

QList<ScoreRow> scoreRows(const Study &s, const Recipe &classification) {
  QList<ScoreRow> out;
  for (const RowKey &k : rowsOf(s, classification)) {
    ScoreRow row;
    row.caseId = k.c->id;
    row.caseName = k.c->name;
    row.station = k.station;
    row.reference = k.c->reference;
    row.magnitude = k.c->magnitude;
    row.distanceKm = kNaN;
    for (const StationRef &st : k.c->stations)
      if (st.code == k.station)
        row.distanceKm = st.distanceKm;
    row.testF1 = row.testAuc = row.baseF1 = row.baseAuc = kNaN;
    const Cell cell = cellOf(s, *k.c, k.station, classification);
    row.state = cell.state;
    if (cell.state == CellState::Done)
      row.warnings = cell.warnings;
    const QString csv = outputPath(cell, QStringLiteral("experiments"));
    if (cell.state == CellState::Done && !csv.isEmpty()) {
      const Scores sc =
          scoreCache().get(cell.dir, QStringLiteral("scores"), [&csv] {
            Scores out;
            const std::vector<sfc::Trial> trials = sfc::loadTrials(csv, true);
            const sfc::Trial *best = nullptr, *base = nullptr;
            for (const sfc::Trial &t : trials) {
              if (!best || t.valF1 > best->valF1)
                best = &t;
              if (isBaselineModel(t.model) && (!base || t.valF1 > base->valF1))
                base = &t;
            }
            if (best) {
              out.best = best->model;
              out.f1 = best->testF1;
              out.auc = best->testAuc;
            }
            if (base) {
              out.base = base->model;
              out.baseF1 = base->testF1;
              out.baseAuc = base->testAuc;
            }
            return out;
          });
      row.bestModel = sc.best;
      row.testF1 = sc.f1;
      row.testAuc = sc.auc;
      row.baselineModel = sc.base;
      row.baseF1 = sc.baseF1;
      row.baseAuc = sc.baseAuc;
    }
    out << row;
  }
  return out;
}

QString scoresCsv(const QList<ScoreRow> &rows) {
  QString out = QStringLiteral(
      "case;station;kind;magnitude;distance_km;best_model;test_f1;"
      "baseline_model;baseline_test_f1;test_f1_minus_baseline;test_auc;"
      "baseline_test_auc;test_auc_minus_baseline;warnings\n");
  for (const ScoreRow &r : rows) {
    QString name = r.caseName;
    name.replace(QLatin1Char(';'), QLatin1Char(','));
    out += QStringList{name,
                       r.station,
                       r.reference ? QStringLiteral("reference")
                                   : QStringLiteral("earthquake"),
                       num(r.magnitude, 1),
                       num(r.distanceKm, 1),
                       r.bestModel,
                       num(r.testF1, 4),
                       r.baselineModel,
                       num(r.baseF1, 4),
                       num(r.testF1 - r.baseF1, 4),
                       num(r.testAuc, 4),
                       num(r.baseAuc, 4),
                       num(r.testAuc - r.baseAuc, 4),
                       QString(r.warnings.join(QStringLiteral(" | ")))
                           .replace(QLatin1Char(';'), QLatin1Char(','))}
               .join(QLatin1Char(';')) +
           QLatin1Char('\n');
  }
  return out;
}

QStringList seriesColumns(const Study &s, const Recipe &r) {
  for (const RowKey &k : rowsOf(s, r)) {
    const Cell cell = cellOf(s, *k.c, k.station, r);
    if (cell.state != CellState::Done)
      continue;
    try {
      if (r.tool == kToolIndicators) {
        const QString path = outputPath(cell, QStringLiteral("table"));
        if (!path.isEmpty())
          return tableCache()
              .get(cell.dir, QStringLiteral("table"),
                   [&path] { return cbi::readTableCsv(path); })
              .columns;
      } else if (r.tool == kToolFeatures) {
        const QString path = outputPath(cell, QStringLiteral("features"));
        if (path.isEmpty())
          continue;
        return columnCache().get(cell.dir, QStringLiteral("columns"), [&path] {
          QStringList cols;
          for (const fx::NpyField &f : analysis::loadTable(path).fields)
            if (f.dtype.contains(QLatin1Char('f')) &&
                f.name != QLatin1String("Zaman_Dk"))
              cols << f.name;
          return cols;
        });
      }
    } catch (const std::exception &) {
      continue;
    }
  }
  return {};
}

QList<Series> seriesOf(const Study &s, const Recipe &r, const QString &column,
                       QStringList *skipped) {
  QList<Series> out;
  for (const RowKey &k : rowsOf(s, r)) {
    const Cell cell = cellOf(s, *k.c, k.station, r);
    if (cell.state != CellState::Done)
      continue;
    Series series;
    series.caseId = k.c->id;
    series.caseName = k.c->name;
    series.station = k.station;
    series.reference = k.c->reference;
    const QString label = k.station.isEmpty()
                              ? k.c->name
                              : k.c->name + QStringLiteral(" · ") + k.station;
    const auto skip = [&](const QString &why) {
      if (skipped)
        *skipped << QStringLiteral("%1: %2").arg(label, why);
    };
    const double t0 = double(k.c->time.toSecsSinceEpoch());
    const double span = double(k.c->spanDays);
    try {
      if (r.tool == kToolIndicators) {
        const QString path = outputPath(cell, QStringLiteral("table"));
        const cbi::IndicatorTable t =
            tableCache().get(cell.dir, QStringLiteral("table"),
                             [&path] { return cbi::readTableCsv(path); });
        const std::vector<double> *col = t.column(column);
        if (!col) {
          skip(CompareText::tr("its table has no column %1").arg(column));
          continue;
        }
        for (int i = 0; i < t.rows(); ++i) {
          // A window is placed at its end: what was known by then.
          const double d =
              (double(t.end[static_cast<size_t>(i)].toSecsSinceEpoch()) - t0) /
              86400.0;
          if (d < -span - 1 || d > 1.0)
            continue;
          series.daysBefore.push_back(std::min(d, 0.0));
          series.values.push_back((*col)[static_cast<size_t>(i)]);
        }
      } else if (r.tool == kToolFeatures) {
        const QString path = outputPath(cell, QStringLiteral("features"));
        // The median of each whole day before the event.
        const Daily daily = dailyCache().get(
            cell.dir,
            QStringLiteral("daily|%1|%2|%3").arg(column).arg(t0).arg(span),
            [&path, &column, t0, span] {
              Daily out;
              const fx::NpyTable t = analysis::loadTable(path);
              int timeField = -1, valueField = -1;
              for (int f = 0; f < int(t.fields.size()); ++f) {
                if (t.fields[f].name == QLatin1String("Zaman_Dk"))
                  timeField = f;
                if (t.fields[f].name == column)
                  valueField = f;
              }
              if (timeField < 0 || valueField < 0)
                return out;
              out.found = true;
              QMap<int, std::vector<double>> perDay;
              const std::vector<double> &minutes = t.numeric[timeField];
              const std::vector<double> &values = t.numeric[valueField];
              for (size_t i = 0; i < minutes.size() && i < values.size(); ++i) {
                const double d = (minutes[i] * 60.0 - t0) / 86400.0;
                if (d < -span || d >= 0.0 || std::isnan(values[i]))
                  continue;
                perDay[int(std::floor(d))].push_back(values[i]);
              }
              for (auto it = perDay.begin(); it != perDay.end(); ++it) {
                std::vector<double> v = it.value();
                const size_t mid = v.size() / 2;
                std::nth_element(v.begin(), v.begin() + qsizetype(mid),
                                 v.end());
                double median = v[mid];
                if (v.size() % 2 == 0) {
                  const double below =
                      *std::max_element(v.begin(), v.begin() + qsizetype(mid));
                  median = (median + below) / 2.0;
                }
                out.days.push_back(double(it.key()) + 0.5);
                out.values.push_back(median);
              }
              return out;
            });
        if (!daily.found) {
          skip(CompareText::tr("its table has no column %1").arg(column));
          continue;
        }
        series.daysBefore = daily.days;
        series.values = daily.values;
      }
    } catch (const std::exception &e) {
      skip(QString::fromUtf8(e.what()));
      continue;
    }
    if (series.daysBefore.empty()) {
      skip(CompareText::tr("no value falls in the case's span"));
      continue;
    }
    out << series;
  }
  return out;
}

QList<FittedRow> fittedRows(const Study &s, const Recipe &r) {
  QList<FittedRow> out;
  for (const RowKey &k : rowsOf(s, r)) {
    const Cell cell = cellOf(s, *k.c, k.station, r);
    if (cell.state != CellState::Done || !cell.record)
      continue;
    out << FittedRow{k.c->name, k.station, cell.record->fitted};
  }
  return out;
}

CompareData loadCompare(const Study &s, const CompareRequest &q) {
  CompareData d;
  if (const Recipe *r = s.findRecipe(q.scoreRecipe);
      r && r->tool == kToolClassification) {
    d.hasScoreRecipe = true;
    d.scores = scoreRows(s, *r);
  }
  const Recipe *r = s.findRecipe(q.seriesRecipe);
  if (!r || r->tool == kToolClassification)
    return d;
  d.hasSeriesRecipe = true;
  d.columns = seriesColumns(s, *r);
  d.column = d.columns.contains(q.column)                       ? q.column
             : d.columns.contains(QStringLiteral("b_value")) ? QStringLiteral("b_value")
                                                             : d.columns.value(0);
  if (!d.column.isEmpty())
    d.series = seriesOf(s, *r, d.column, &d.skipped);
  d.fitted = fittedRows(s, *r);
  return d;
}

} // namespace skg::study
