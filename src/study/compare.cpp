#include "study/compare.h"

#include <QDir>
#include <QMap>
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

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

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
    const QString csv = outputPath(cell, QStringLiteral("experiments"));
    if (cell.state == CellState::Done && !csv.isEmpty()) {
      const std::vector<sfc::Trial> trials = sfc::loadTrials(csv, true);
      const sfc::Trial *best = nullptr, *base = nullptr;
      for (const sfc::Trial &t : trials) {
        if (!best || t.valF1 > best->valF1)
          best = &t;
        if (isBaselineModel(t.model) && (!base || t.valF1 > base->valF1))
          base = &t;
      }
      if (best) {
        row.bestModel = best->model;
        row.testF1 = best->testF1;
        row.testAuc = best->testAuc;
      }
      if (base) {
        row.baselineModel = base->model;
        row.baseF1 = base->testF1;
        row.baseAuc = base->testAuc;
      }
    }
    out << row;
  }
  return out;
}

QString scoresCsv(const QList<ScoreRow> &rows) {
  QString out = QStringLiteral(
      "case;station;kind;magnitude;distance_km;best_model;test_f1;"
      "baseline_model;baseline_test_f1;test_f1_minus_baseline;test_auc;"
      "baseline_test_auc;test_auc_minus_baseline\n");
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
                       num(r.testAuc - r.baseAuc, 4)}
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
          return cbi::readTableCsv(path).columns;
      } else if (r.tool == kToolFeatures) {
        const QString path = outputPath(cell, QStringLiteral("features"));
        if (path.isEmpty())
          continue;
        QStringList cols;
        for (const fx::NpyField &f : analysis::loadTable(path).fields)
          if (f.dtype.contains(QLatin1Char('f')) &&
              f.name != QLatin1String("Zaman_Dk"))
            cols << f.name;
        return cols;
      }
    } catch (const std::exception &) {
      continue;
    }
  }
  return {};
}

QList<Series> seriesOf(const Study &s, const Recipe &r, const QString &column) {
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
    const double t0 = double(k.c->time.toSecsSinceEpoch());
    const double span = double(k.c->spanDays);
    try {
      if (r.tool == kToolIndicators) {
        const cbi::IndicatorTable t =
            cbi::readTableCsv(outputPath(cell, QStringLiteral("table")));
        const std::vector<double> *col = t.column(column);
        if (!col)
          continue;
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
        const fx::NpyTable t =
            analysis::loadTable(outputPath(cell, QStringLiteral("features")));
        int timeField = -1, valueField = -1;
        for (int f = 0; f < int(t.fields.size()); ++f) {
          if (t.fields[f].name == QLatin1String("Zaman_Dk"))
            timeField = f;
          if (t.fields[f].name == column)
            valueField = f;
        }
        if (timeField < 0 || valueField < 0)
          continue;
        // The median of each whole day before the event.
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
          std::nth_element(v.begin(), v.begin() + qsizetype(mid), v.end());
          double median = v[mid];
          if (v.size() % 2 == 0) {
            const double below =
                *std::max_element(v.begin(), v.begin() + qsizetype(mid));
            median = (median + below) / 2.0;
          }
          series.daysBefore.push_back(double(it.key()) + 0.5);
          series.values.push_back(median);
        }
      }
    } catch (const std::exception &) {
      continue;
    }
    if (!series.daysBefore.empty())
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

} // namespace skg::study
