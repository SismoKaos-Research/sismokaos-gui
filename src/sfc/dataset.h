// The labelled windows the models train on: one row per window with its
// day, label (the class table's row) and class (after merging), and the
// feature matrix. Read from sfc's own labelled CSV, or built from an fx
// feature table by sfc's day selection around the event.
#pragma once

#include <QDate>
#include <QString>
#include <QStringList>

#include <vector>

#include "fx/npy.h"
#include "sfc/settings.h"

namespace skg::sfc {

struct Dataset {
  QStringList featureNames;
  int nFeatures() const { return featureNames.size(); }
  int rows() const { return int(label.size()); }
  std::vector<double> X; // rows x nFeatures, row-major; NaN allowed
  std::vector<int> label, cls;
  std::vector<QDate> date;
  std::vector<int> daysToEq;
  std::vector<QString> windowStart; // "YYYY-MM-DD HH:MM:SS" when known
  double at(int r, int c) const {
    return X[size_t(r) * size_t(nFeatures()) + size_t(c)];
  }
  int column(const QString &name) const; // -1 when absent
  std::vector<QDate> days() const;       // distinct, sorted
  QStringList
      extraColumns; // other numeric columns of the source, kept for plots
  std::vector<double> extra; // rows x extraColumns
};

// sfc's <STATION>_N_features_labeled.csv (sep ';', decimal ','). Throws.
Dataset loadSfcCsv(const QString &path, const Settings &s);
// Writes a dataset in that layout (Station, Date, Window_Start_UTC,
// Window_End_UTC, Days_To_EQ, Label, features), so the Python tool can read
// what the GUI labelled.
bool saveSfcCsv(const QString &path, const Dataset &d, const Settings &s,
                double winSec = 200);

// sfc's day selection applied to the days of an fx table: every label's
// planned days before the event (first..last every step, count of them; or
// fixed dates), a missing or short day replaced by the nearest day in reach,
// the event day as label 0 with the hours before the cutoff.
struct DaySelection {
  struct Row {
    QDate date, planned;
    int label = 0, daysToEq = 0;
    int windows = 0;
    QString reason; // "", "no file", "incomplete"
  };
  QList<Row> rows;
  QMap<int, QList<QDate>>
      skipped; // label -> planned days without a replacement
  QStringList log;
};
// `windowsPerDay`: the days available and how many windows they hold;
// `minWindows` plays MIN_DAY_HOURS's part (0: any day with data is complete).
DaySelection selectDays(const Settings &s,
                        const QMap<QDate, int> &windowsPerDay, int minWindows);

// Builds the dataset from an fx table (Zaman_Dk window ends) with the
// selected days. `featureColumns` are the table columns used as features.
Dataset fromFxTable(const fx::NpyTable &table, const Settings &s,
                    const DaySelection &selection,
                    const QStringList &featureColumns, double winSec = 200);
// The days of an fx table and their window counts.
QMap<QDate, int> fxWindowsPerDay(const fx::NpyTable &table,
                                 double winSec = 200);

// Applies label_merge and class_months, drops rows with a NaN feature.
// Returns the number of rows dropped for NaN.
int prepareClasses(Dataset &d, const Settings &s, QStringList *log = nullptr);

} // namespace skg::sfc
