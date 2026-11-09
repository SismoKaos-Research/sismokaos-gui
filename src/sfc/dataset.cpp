#include "sfc/dataset.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "analysis/label.h"
#include "fx/csv.h"

namespace skg::sfc {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

QStringList splitSep(const QString &line, QChar sep) {
  if (sep == ',')
    return fx::splitCsv(line);
  return line.split(sep);
}

std::optional<double> numberWith(const QString &raw, QChar decimal) {
  QString t = raw.trimmed();
  if (t.isEmpty())
    return std::nullopt;
  if (decimal != '.')
    t.replace(decimal, '.');
  bool ok = false;
  const double v = t.toDouble(&ok);
  return ok ? std::optional<double>(v) : std::nullopt;
}

QString numberTo(double v, QChar decimal) {
  if (std::isnan(v))
    return {};
  QString t = QString::number(v, 'g', 15);
  if (decimal != '.')
    t.replace('.', decimal);
  return t;
}
} // namespace

int Dataset::column(const QString &name) const {
  return int(featureNames.indexOf(name));
}

std::vector<QDate> Dataset::days() const {
  std::vector<QDate> d(date.begin(), date.end());
  std::sort(d.begin(), d.end());
  d.erase(std::unique(d.begin(), d.end()), d.end());
  return d;
}

Dataset loadSfcCsv(const QString &path, const Settings &s) {
  const QStringList rows = fx::lines(fx::readMaybeGz(path));
  if (rows.isEmpty())
    throw std::runtime_error((path + " is empty").toStdString());
  const QChar sep = s.csvSep.isEmpty() ? ';' : s.csvSep[0];
  const QChar dec = s.csvDecimal.isEmpty() ? ',' : s.csvDecimal[0];
  QStringList header = splitSep(rows.first(), sep);
  for (QString &h : header) {
    h = h.trimmed();
    if (h.startsWith(QChar(0xFEFF)))
      h.remove(0, 1);
  }
  const int date = header.indexOf("Date"), label = header.indexOf("Label"),
            dte = header.indexOf("Days_To_EQ"),
            start = header.indexOf("Window_Start_UTC");
  if (date < 0 || label < 0)
    throw std::runtime_error(
        (path + " needs Date and Label columns").toStdString());
  static const QSet<QString> meta{
      "Station",        "Date",       "Window_Start_UTC",
      "Window_End_UTC", "Days_To_EQ", "Label",
      "Class",          "Split"};
  Dataset d;
  QList<int> featureIdx, extraIdx;
  for (int i = 0; i < header.size(); ++i) {
    if (meta.contains(header[i]) || header[i].isEmpty())
      continue;
    if (s.featureColumns.contains(header[i])) {
      d.featureNames << header[i];
      featureIdx << i;
    } else {
      d.extraColumns << header[i];
      extraIdx << i;
    }
  }
  for (const QString &f : s.featureColumns)
    if (!d.featureNames.contains(f))
      throw std::runtime_error(
          QStringLiteral("feature column %1 is not in %2 (columns: %3)")
              .arg(f, path, header.join(", "))
              .toStdString());
  // Keep the settings' order of features.
  QList<int> ordered;
  QStringList names;
  for (const QString &f : s.featureColumns) {
    ordered << featureIdx[d.featureNames.indexOf(f)];
    names << f;
  }
  featureIdx = ordered;
  d.featureNames = names;
  for (int i = 1; i < rows.size(); ++i) {
    if (rows[i].trimmed().isEmpty())
      continue;
    const QStringList r = splitSep(rows[i], sep);
    if (r.size() < header.size())
      continue;
    const QDate day =
        QDate::fromString(r[date].trimmed().left(10), Qt::ISODate);
    if (!day.isValid())
      continue;
    d.date.push_back(day);
    d.label.push_back(r[label].trimmed().toInt());
    d.daysToEq.push_back(dte >= 0 ? r[dte].trimmed().toInt() : 0);
    d.windowStart.push_back(start >= 0 ? r[start].trimmed() : QString());
    for (int c : featureIdx)
      d.X.push_back(numberWith(r[c], dec).value_or(kNaN));
    for (int c : extraIdx) {
      const auto v = numberWith(r[c], dec);
      d.extra.push_back(v.value_or(kNaN));
    }
  }
  d.cls = d.label;
  return d;
}

bool saveSfcCsv(const QString &path, const Dataset &d, const Settings &s,
                double winSec) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  const QString sep = s.csvSep.isEmpty() ? ";" : s.csvSep;
  const QChar dec = s.csvDecimal.isEmpty() ? ',' : s.csvDecimal[0];
  QStringList header{"Station",        "Date",       "Window_Start_UTC",
                     "Window_End_UTC", "Days_To_EQ", "Label"};
  header << d.featureNames;
  f.write(header.join(sep).toUtf8() + '\n');
  for (int r = 0; r < d.rows(); ++r) {
    QStringList cells{s.station, d.date[size_t(r)].toString(Qt::ISODate)};
    const QString start = d.windowStart[size_t(r)];
    QString end;
    if (!start.isEmpty()) {
      QDateTime dt =
          QDateTime::fromString(start, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
      dt.setTimeZone(QTimeZone::utc());
      end = dt.addSecs(qint64(winSec))
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    cells << start << end << QString::number(d.daysToEq[size_t(r)])
          << QString::number(d.label[size_t(r)]);
    for (int c = 0; c < d.nFeatures(); ++c)
      cells << numberTo(d.at(r, c), dec);
    f.write(cells.join(sep).toUtf8() + '\n');
  }
  return f.commit();
}

// --- day selection
// ---------------------------------------------------------------

namespace {
QList<QDate> plannedDays(const Settings &s, const ClassRow &row) {
  QList<QDate> out;
  if (!row.step) {
    const QList<QDate> fixed = s.fixedDates.value(row.label);
    return fixed.mid(0, row.count);
  }
  for (int n = row.firstDay; n <= row.lastDay && out.size() < row.count;
       n += std::max(1, *row.step))
    out << s.eqDate.addDays(-n);
  return out;
}

int maxShift(const QVariant &setting, const std::optional<int> &step) {
  if (setting.typeId() == QMetaType::QString && setting.toString() == "step")
    return step ? *step / 2 : 1;
  return setting.toInt();
}

std::optional<QDate> pickDay(const QDate &day, int first, int last,
                             const QSet<QDate> &candidates,
                             const QSet<QDate> &allPlanned, const QDate &eq,
                             int shift) {
  if (candidates.contains(day))
    return day;
  for (int k = 1; k <= shift; ++k)
    for (const QDate &c : {day.addDays(-k), day.addDays(k)}) {
      const int before = int(c.daysTo(eq));
      if (before >= first && before <= last && candidates.contains(c) &&
          !allPlanned.contains(c))
        return c;
    }
  return std::nullopt;
}
} // namespace

DaySelection selectDays(const Settings &s,
                        const QMap<QDate, int> &windowsPerDay, int minWindows) {
  DaySelection sel;
  QSet<QDate> available(windowsPerDay.keyBegin(), windowsPerDay.keyEnd());
  QSet<QDate> complete;
  for (auto it = windowsPerDay.begin(); it != windowsPerDay.end(); ++it)
    if (minWindows <= 0 || it.value() >= minWindows)
      complete.insert(it.key());
  QMap<int, QList<QDate>> planned;
  QSet<QDate> allPlanned;
  for (const ClassRow &row : s.classTable) {
    planned.insert(row.label, plannedDays(s, row));
    for (const QDate &d : planned[row.label])
      allPlanned.insert(d);
  }
  if (available.contains(s.eqDate)) {
    DaySelection::Row r;
    r.date = r.planned = s.eqDate;
    r.label = 0;
    r.daysToEq = 0;
    r.windows = windowsPerDay.value(s.eqDate);
    sel.rows << r;
  } else
    sel.log << QStringLiteral("[WARNING] No data for the earthquake day %1.")
                   .arg(s.eqDate.toString(Qt::ISODate));
  for (const ClassRow &row : s.classTable) {
    const int shift = maxShift(s.maxShiftDays, row.step);
    int found = 0;
    QStringList replaced, kept, skipped;
    for (const QDate &day : planned[row.label]) {
      std::optional<QDate> chosen =
          pickDay(day, row.firstDay, row.lastDay, complete, allPlanned,
                  s.eqDate, shift);
      if (!chosen && available.contains(day))
        chosen = day;
      if (!chosen)
        chosen = pickDay(day, row.firstDay, row.lastDay, available, allPlanned,
                         s.eqDate, shift);
      if (!chosen) {
        skipped << day.toString(Qt::ISODate);
        sel.skipped[row.label] << day;
        continue;
      }
      DaySelection::Row r;
      r.date = *chosen;
      r.planned = day;
      r.label = row.label;
      r.daysToEq = int(chosen->daysTo(s.eqDate));
      r.windows = windowsPerDay.value(*chosen);
      if (*chosen != day) {
        r.reason = available.contains(day) ? QStringLiteral("incomplete")
                                           : QStringLiteral("no file");
        replaced << QStringLiteral("%1->%2").arg(day.toString(Qt::ISODate),
                                                 chosen->toString(Qt::ISODate));
        allPlanned.insert(*chosen);
      }
      if (!complete.contains(*chosen))
        kept << QStringLiteral("%1 (%2 windows)")
                    .arg(chosen->toString(Qt::ISODate))
                    .arg(r.windows);
      sel.rows << r;
      ++found;
    }
    QString line = QStringLiteral("[SELECT] Label %1: %2/%3 days")
                       .arg(row.label)
                       .arg(found)
                       .arg(planned[row.label].size());
    if (!replaced.isEmpty())
      line += " | replaced: " + replaced.join(", ");
    if (!kept.isEmpty())
      line += " | incomplete, used: " + kept.join(", ");
    if (!skipped.isEmpty())
      line += " | no data: " + skipped.join(", ");
    sel.log << line;
  }
  std::sort(sel.rows.begin(), sel.rows.end(),
            [](const DaySelection::Row &a, const DaySelection::Row &b) {
              return a.date < b.date;
            });
  return sel;
}

QMap<QDate, int> fxWindowsPerDay(const fx::NpyTable &table, double winSec) {
  QMap<QDate, int> out;
  for (int r = 0; r < table.rows; ++r)
    if (const auto span = analysis::windowSpan(table, r, winSec)) {
      const QDate d = QDateTime::fromMSecsSinceEpoch(
                          qint64((span->start) * 1000.0), QTimeZone::utc())
                          .date();
      ++out[d];
    }
  return out;
}

Dataset fromFxTable(const fx::NpyTable &table, const Settings &s,
                    const DaySelection &selection,
                    const QStringList &featureColumns, double winSec) {
  Dataset d;
  d.featureNames = featureColumns;
  QList<int> cols;
  for (const QString &f : featureColumns) {
    int idx = -1;
    for (int i = 0; i < table.fields.size(); ++i)
      if (table.fields[i].name == f && !table.fields[i].dtype.contains('U'))
        idx = i;
    if (idx < 0)
      throw std::runtime_error(
          ("feature column not in the table: " + f).toStdString());
    cols << idx;
  }
  QMap<QDate, const DaySelection::Row *> byDay;
  for (const DaySelection::Row &r : selection.rows)
    byDay.insert(r.date, &r);
  for (int r = 0; r < table.rows; ++r) {
    const auto span = analysis::windowSpan(table, r, winSec);
    if (!span)
      continue;
    const QDateTime start = QDateTime::fromMSecsSinceEpoch(
        qint64(span->start * 1000.0), QTimeZone::utc());
    const QDate day = start.date();
    const auto it = byDay.find(day);
    if (it == byDay.end())
      continue;
    // The event day: only the hours before the cutoff.
    if (day == s.eqDate && start.time().hour() >= s.eqCutoffHour)
      continue;
    d.date.push_back(day);
    d.label.push_back(it.value()->label);
    d.daysToEq.push_back(it.value()->daysToEq);
    d.windowStart.push_back(
        start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    for (int c : cols)
      d.X.push_back(table.numeric[c][size_t(r)]);
  }
  d.cls = d.label;
  return d;
}

int prepareClasses(Dataset &d, const Settings &s, QStringList *log) {
  const QMap<int, int> map = s.labelToClass();
  // Merge, then the month filter per class, then drop NaN rows.
  std::vector<char> keep(size_t(d.rows()), 1);
  for (int r = 0; r < d.rows(); ++r) {
    d.cls[size_t(r)] = map.value(d.label[size_t(r)], d.label[size_t(r)]);
    for (auto it = s.classMonths.begin(); it != s.classMonths.end(); ++it)
      if (d.cls[size_t(r)] == it.key() &&
          !it.value().contains(d.date[size_t(r)].month()))
        keep[size_t(r)] = 0;
  }
  int nan = 0;
  for (int r = 0; r < d.rows(); ++r) {
    if (!keep[size_t(r)])
      continue;
    for (int c = 0; c < d.nFeatures(); ++c)
      if (std::isnan(d.at(r, c))) {
        keep[size_t(r)] = 0;
        ++nan;
        break;
      }
  }
  Dataset out;
  out.featureNames = d.featureNames;
  out.extraColumns = d.extraColumns;
  const int ne = d.extraColumns.size();
  for (int r = 0; r < d.rows(); ++r) {
    if (!keep[size_t(r)])
      continue;
    out.label.push_back(d.label[size_t(r)]);
    out.cls.push_back(d.cls[size_t(r)]);
    out.date.push_back(d.date[size_t(r)]);
    out.daysToEq.push_back(d.daysToEq[size_t(r)]);
    out.windowStart.push_back(d.windowStart[size_t(r)]);
    for (int c = 0; c < d.nFeatures(); ++c)
      out.X.push_back(d.at(r, c));
    for (int c = 0; c < ne; ++c)
      out.extra.push_back(d.extra[size_t(r) * size_t(ne) + size_t(c)]);
  }
  if (log)
    *log << QStringLiteral(
                "[PREP] %1 windows with NaN features dropped (%2 left)")
                .arg(nan)
                .arg(out.rows());
  d = std::move(out);
  return nan;
}

} // namespace skg::sfc
