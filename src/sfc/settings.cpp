#include "sfc/settings.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "toml.hpp"

namespace skg::sfc {

// --- grids
// ---------------------------------------------------------------------

QList<QVariantMap> expandGrid(const Grid &grid) {
  QStringList keys = grid.keys();
  std::sort(keys.begin(), keys.end());
  QList<QVariantMap> out{QVariantMap()};
  for (const QString &k : keys) {
    QList<QVariantMap> next;
    for (const QVariantMap &base : out)
      for (const QVariant &v : grid.value(k)) {
        QVariantMap m = base;
        m.insert(k, v);
        next << m;
      }
    out = next;
  }
  return out;
}

namespace {
QString pyValue(const QVariant &v) {
  switch (v.typeId()) {
  case QMetaType::QString:
    return "'" + v.toString() + "'";
  case QMetaType::Bool:
    return v.toBool() ? QStringLiteral("True") : QStringLiteral("False");
  case QMetaType::QVariantList: {
    QStringList parts;
    for (const QVariant &e : v.toList())
      parts << pyValue(e);
    return "(" + parts.join(", ") + (parts.size() == 1 ? ",)" : ")");
  }
  case QMetaType::Double: {
    const double d = v.toDouble();
    if (d == std::floor(d) && std::abs(d) < 1e15)
      return QString::number(d, 'f', 1);
    return QString::number(d, 'g', 15);
  }
  default:
    return v.toString();
  }
}
} // namespace

QString paramsText(const QVariantMap &params) {
  QStringList parts;
  for (auto it = params.begin(); it != params.end(); ++it)
    parts << QStringLiteral("'%1': %2").arg(it.key(), pyValue(it.value()));
  return "{" + parts.join(", ") + "}";
}

// --- classes
// ---------------------------------------------------------------------

QMap<int, int> Settings::labelToClass() const {
  QMap<int, int> map;
  for (const ClassRow &r : classTable)
    map.insert(r.label, labelMerge ? labelMerge->value(r.label, -1) : r.label);
  if (labelMerge) {
    QSet<int> labels;
    for (const ClassRow &r : classTable)
      labels.insert(r.label);
    if (QSet<int>(labelMerge->keyBegin(), labelMerge->keyEnd()) != labels)
      throw std::invalid_argument(
          "label_merge must contain exactly the labels of the class table");
    QList<int> classes = labelMerge->values();
    std::sort(classes.begin(), classes.end());
    classes.erase(std::unique(classes.begin(), classes.end()), classes.end());
    for (int i = 0; i < classes.size(); ++i)
      if (classes[i] != i)
        throw std::invalid_argument(
            "the merged classes must be numbered 0, 1, 2, ... without gaps");
  }
  return map;
}

QMap<int, QString> Settings::classNames() const {
  const QMap<int, int> map = labelToClass();
  QMap<int, QString> names;
  QSet<int> classes(map.cbegin(), map.cend());
  for (int c : classes) {
    int lo = 1 << 30, hi = -1;
    for (const ClassRow &r : classTable)
      if (map.value(r.label) == c) {
        lo = std::min(lo, r.firstDay);
        hi = std::max(hi, r.lastDay);
      }
    names.insert(c, QStringLiteral("%1-%2 days").arg(lo).arg(hi));
  }
  return names;
}

QStringList Settings::labelsOf() const {
  QStringList out;
  for (const ClassRow &r : classTable)
    out << QString::number(r.label);
  return out;
}

// --- TOML
// ---------------------------------------------------------------------

namespace {

QVariant fromNode(const toml::node &n) {
  if (auto v = n.as_string())
    return QString::fromStdString(std::string(v->get()));
  if (auto v = n.as_integer())
    return qlonglong(v->get());
  if (auto v = n.as_floating_point())
    return v->get();
  if (auto v = n.as_boolean())
    return v->get();
  if (auto v = n.as_date()) {
    const toml::date d = v->get();
    return QDate(d.year, d.month, d.day);
  }
  if (auto v = n.as_date_time()) {
    const toml::date_time dt = v->get();
    return QDateTime(QDate(dt.date.year, dt.date.month, dt.date.day),
                     QTime(dt.time.hour, dt.time.minute, dt.time.second),
                     QTimeZone::utc());
  }
  if (auto v = n.as_array()) {
    QVariantList list;
    for (const toml::node &e : *v)
      list << fromNode(e);
    return list;
  }
  if (auto v = n.as_table()) {
    QVariantMap map;
    for (const auto &[k, e] : *v)
      map.insert(QString::fromStdString(std::string(k.str())), fromNode(e));
    return map;
  }
  return {};
}

bool isNone(const QVariant &v) {
  return v.typeId() == QMetaType::QString &&
         v.toString().trimmed().toLower() == "none";
}

Grid gridFrom(const QVariant &v) {
  Grid g;
  const QVariantMap m = v.toMap();
  for (auto it = m.begin(); it != m.end(); ++it)
    g.insert(it.key(), it.value().typeId() == QMetaType::QVariantList
                           ? it.value().toList()
                           : QVariantList{it.value()});
  return g;
}

QVariant gridTo(const Grid &g) {
  QVariantMap m;
  for (auto it = g.begin(); it != g.end(); ++it)
    m.insert(it.key(), it.value());
  return m;
}

QMap<int, int> intMapFrom(const QVariant &v) {
  QMap<int, int> out;
  const QVariantMap m = v.toMap();
  for (auto it = m.begin(); it != m.end(); ++it)
    out.insert(it.key().toInt(), it.value().toInt());
  return out;
}

QStringList stringsFrom(const QVariant &v) {
  QStringList out;
  for (const QVariant &e : v.toList())
    out << e.toString();
  return out;
}

QList<QStringList> stringListsFrom(const QVariant &v) {
  QList<QStringList> out;
  for (const QVariant &e : v.toList())
    out << stringsFrom(e);
  return out;
}

QVariant stringListsTo(const QList<QStringList> &l) {
  QVariantList out;
  for (const QStringList &s : l)
    out << QVariant(s);
  return out;
}

toml::table toTable(const QVariantMap &map);
toml::array toArray(const QVariantList &list);

// toml++ takes values by type, so the conversion dispatches on the variant.
template <class Container, class... Key>
void put(Container &c, const QVariant &v, Key &&...key) {
  auto emplace = [&](auto &&value) {
    if constexpr (sizeof...(Key) == 0)
      c.push_back(std::forward<decltype(value)>(value));
    else
      c.insert(std::forward<Key>(key)..., std::forward<decltype(value)>(value));
  };
  switch (v.typeId()) {
  case QMetaType::Bool:
    emplace(v.toBool());
    break;
  case QMetaType::Int:
  case QMetaType::LongLong:
  case QMetaType::UInt:
  case QMetaType::ULongLong:
    emplace(qint64(v.toLongLong()));
    break;
  case QMetaType::Double:
    emplace(v.toDouble());
    break;
  case QMetaType::QDate: {
    const QDate d = v.toDate();
    emplace(
        toml::date{uint16_t(d.year()), uint8_t(d.month()), uint8_t(d.day())});
    break;
  }
  case QMetaType::QVariantList:
    emplace(toArray(v.toList()));
    break;
  case QMetaType::QStringList:
    emplace(toArray(QVariant(v.toStringList()).toList()));
    break;
  case QMetaType::QVariantMap:
    emplace(toTable(v.toMap()));
    break;
  default:
    emplace(v.toString().toStdString());
  }
}

toml::array toArray(const QVariantList &list) {
  toml::array arr;
  for (const QVariant &e : list)
    put(arr, e);
  return arr;
}

toml::table toTable(const QVariantMap &map) {
  toml::table t;
  for (auto it = map.begin(); it != map.end(); ++it)
    put(t, it.value(), it.key().toStdString());
  return t;
}

} // namespace

const QSet<QString> &Settings::projectKeys() {
  static const QSet<QString> keys{"station",
                                  "earthquake_name",
                                  "raw_data_root",
                                  "raw_data_folders",
                                  "raw_data_dirs",
                                  "csv_format",
                                  "eq_date",
                                  "eq_cutoff_hour",
                                  "max_shift_days",
                                  "min_day_hours",
                                  "replacement_rules",
                                  "class_table",
                                  "fixed_dates",
                                  "n_jobs",
                                  "freqmin",
                                  "freqmax",
                                  "filter_order",
                                  "zero_phase",
                                  "gap_threshold",
                                  "padding_sec",
                                  "sampling_rate",
                                  "win_sec",
                                  "step_sec",
                                  "embedding_delay",
                                  "embedding_dim",
                                  "ros_fit_method",
                                  "ros_search_end_s",
                                  "ros_lower_fraction",
                                  "ros_upper_fraction",
                                  "ros_min_points",
                                  "ros_fixed_window_s",
                                  "wolf_evolve_factor",
                                  "sampen_m",
                                  "sampen_r"};
  return keys;
}

Settings Settings::fromToml(const QString &text, const Settings &base) {
  toml::table tbl;
  try {
    tbl = toml::parse(text.toStdString());
  } catch (const toml::parse_error &e) {
    throw std::runtime_error(std::string("settings: ") + e.what());
  }
  Settings s = base;
  QVariantMap values;
  for (const auto &[k, node] : tbl)
    values.insert(QString::fromStdString(std::string(k.str())).toLower(),
                  fromNode(node));
  // eq_time expands to eq_date and eq_cutoff_hour, as in sfc.
  if (values.contains("eq_time")) {
    const QVariant v = values.take("eq_time");
    QDateTime dt = v.typeId() == QMetaType::QDateTime
                       ? v.toDateTime()
                       : QDateTime::fromString(
                             v.toString().replace('Z', "+00:00"), Qt::ISODate);
    if (!dt.isValid())
      throw std::runtime_error(
          "eq_time: expected a date and UTC time like 2020-01-24T17:55:00Z");
    dt = dt.toUTC();
    values.insert("eq_date", dt.date());
    values.insert("eq_cutoff_hour", dt.time().hour());
  }
  for (auto it = values.begin(); it != values.end(); ++it) {
    const QString k = it.key();
    const QVariant v = it.value();
    const bool none = isNone(v);
    if (k == "station")
      s.station = v.toString();
    else if (k == "earthquake_name")
      s.earthquakeName = v.toString();
    else if (k == "eq_date")
      s.eqDate = v.typeId() == QMetaType::QDate
                     ? v.toDate()
                     : QDate::fromString(v.toString(), Qt::ISODate);
    else if (k == "eq_cutoff_hour")
      s.eqCutoffHour = v.toInt();
    else if (k == "max_shift_days")
      s.maxShiftDays = v;
    else if (k == "min_day_hours")
      s.minDayHours = v.toDouble();
    else if (k == "class_table") {
      s.classTable.clear();
      for (const QVariant &row : v.toList()) {
        const QVariantList r = row.toList();
        if (r.size() != 5)
          throw std::runtime_error("class_table rows need (label, first_day, "
                                   "last_day, step, count)");
        ClassRow c{r[0].toInt(), r[1].toInt(), r[2].toInt(), std::nullopt,
                   r[4].toInt()};
        if (!isNone(r[3]))
          c.step = r[3].toInt();
        s.classTable << c;
      }
    } else if (k == "fixed_dates") {
      s.fixedDates.clear();
      const QVariantMap m = v.toMap();
      for (auto f = m.begin(); f != m.end(); ++f) {
        QList<QDate> dates;
        for (const QVariant &d : f.value().toList())
          dates << (d.typeId() == QMetaType::QDate
                        ? d.toDate()
                        : QDate::fromString(d.toString(), Qt::ISODate));
        s.fixedDates.insert(f.key().toInt(), dates);
      }
    } else if (k == "csv_format") {
      const QVariantMap m = v.toMap();
      s.csvSep = m.value("sep", s.csvSep).toString();
      s.csvDecimal = m.value("decimal", s.csvDecimal).toString();
    } else if (k == "feature_columns")
      s.featureColumns = stringsFrom(v);
    else if (k == "label_merge") {
      if (none)
        s.labelMerge.reset();
      else if (v.typeId() == QMetaType::QVariantList) {
        QMap<int, int> m;
        const QVariantList l = v.toList();
        if (l.size() != s.classTable.size())
          throw std::runtime_error(
              "label_merge: one class per label of the class table");
        for (int i = 0; i < l.size(); ++i)
          m.insert(s.classTable[i].label, l[i].toInt());
        s.labelMerge = m;
      } else
        s.labelMerge = intMapFrom(v);
    } else if (k == "class_months") {
      s.classMonths.clear();
      const QVariantMap m = v.toMap();
      for (auto c = m.begin(); c != m.end(); ++c) {
        QList<int> months;
        for (const QVariant &e : c.value().toList())
          months << e.toInt();
        s.classMonths.insert(c.key().toInt(), months);
      }
    } else if (k == "random_seed")
      s.randomSeed = v.toInt();
    else if (k == "train_size")
      s.trainSize = v.toDouble();
    else if (k == "val_size")
      s.valSize = v.toDouble();
    else if (k == "scaler")
      s.scaler = v.toString();
    else if (k == "clip_after_scaling")
      s.clipAfterScaling =
          none ? std::nullopt : std::optional<double>(v.toDouble());
    else if (k == "run_name")
      s.runName = v.toString();
    else if (k == "models_to_run")
      s.modelsToRun = stringsFrom(v);
    else if (k == "class_weight")
      s.classWeight =
          none ? std::nullopt : std::optional<QString>(v.toString());
    else if (k == "select_by")
      s.selectBy = v.toString();
    else if (k == "cv_folds")
      s.cvFolds = none ? std::nullopt : std::optional<int>(v.toInt());
    else if (k == "save_models")
      s.saveModels = v.toBool();
    else if (k == "logistic_grid")
      s.logisticGrid = gridFrom(v);
    else if (k == "logistic_max_iter")
      s.logisticMaxIter = v.toInt();
    else if (k == "svm_grids") {
      s.svmGrids.clear();
      const QVariantMap m = v.toMap();
      for (auto g = m.begin(); g != m.end(); ++g)
        s.svmGrids.insert(g.key(), gridFrom(g.value()));
    } else if (k == "svm_max_train_windows")
      s.svmMaxTrainWindows =
          none ? std::nullopt : std::optional<int>(v.toInt());
    else if (k == "ann_grid")
      s.annGrid = gridFrom(v);
    else if (k == "ann_fixed")
      s.annFixed = v.toMap();
    else if (k == "ann_max_epochs")
      s.annMaxEpochs = v.toInt();
    else if (k == "ann_patience")
      s.annPatience = v.toInt();
    else if (k == "ann_balance_classes")
      s.annBalanceClasses = v.toBool();
    else if (k == "tree_models_to_run")
      s.treeModelsToRun = stringsFrom(v);
    else if (k == "tree_grids") {
      s.treeGrids.clear();
      const QVariantMap m = v.toMap();
      for (auto g = m.begin(); g != m.end(); ++g)
        s.treeGrids.insert(g.key(), gridFrom(g.value()));
    } else if (k == "compare_runs")
      s.compareRuns =
          none ? std::nullopt : std::optional<QStringList>(stringsFrom(v));
    else if (k == "scatter_pairs")
      s.scatterPairs = stringListsFrom(v);
    else if (k == "scatter_triples")
      s.scatterTriples = stringListsFrom(v);
    else if (k == "scatter_combination_features")
      s.scatterCombinationFeatures = stringsFrom(v);
    else if (k == "scatter_triple_features")
      s.scatterTripleFeatures = stringsFrom(v);
    else if (k == "run_steps")
      s.runSteps = stringsFrom(v);
    else if (k == "stop_on_error")
      s.stopOnError = v.toBool();
    else if (k == "xgb_device" || k == "scatter_3d_html") {
      // accepted, not used
    } else
      s.passthrough.insert(k, v);
  }
  return s;
}

Settings Settings::fromToml(const QString &text) {
  return fromToml(text, Settings());
}

Settings Settings::load(const QString &path) { return load(path, Settings()); }

Settings Settings::load(const QString &path, const Settings &base) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(("cannot read " + path).toStdString());
  return fromToml(QString::fromUtf8(f.readAll()), base);
}

QVariantMap Settings::asMap() const {
  QVariantMap m = passthrough;
  auto none = [] { return QVariant(QStringLiteral("none")); };
  m.insert("station", station);
  m.insert("earthquake_name", earthquakeName);
  m.insert("eq_date", eqDate);
  m.insert("eq_cutoff_hour", eqCutoffHour);
  m.insert("max_shift_days", maxShiftDays);
  m.insert("min_day_hours", minDayHours);
  QVariantList table;
  for (const ClassRow &r : classTable)
    table << QVariant(QVariantList{r.label, r.firstDay, r.lastDay,
                                   r.step ? QVariant(*r.step) : none(),
                                   r.count});
  m.insert("class_table", table);
  QVariantMap fixed;
  for (auto it = fixedDates.begin(); it != fixedDates.end(); ++it) {
    QVariantList dates;
    for (const QDate &d : it.value())
      dates << d;
    fixed.insert(QString::number(it.key()), dates);
  }
  m.insert("fixed_dates", fixed);
  m.insert("csv_format", QVariantMap{{"sep", csvSep}, {"decimal", csvDecimal}});
  m.insert("feature_columns", featureColumns);
  if (labelMerge) {
    QVariantMap lm;
    for (auto it = labelMerge->begin(); it != labelMerge->end(); ++it)
      lm.insert(QString::number(it.key()), it.value());
    m.insert("label_merge", lm);
  } else
    m.insert("label_merge", none());
  QVariantMap cm;
  for (auto it = classMonths.begin(); it != classMonths.end(); ++it) {
    QVariantList months;
    for (int mo : it.value())
      months << mo;
    cm.insert(QString::number(it.key()), months);
  }
  m.insert("class_months", cm);
  m.insert("random_seed", randomSeed);
  m.insert("train_size", trainSize);
  m.insert("val_size", valSize);
  m.insert("scaler", scaler);
  m.insert("clip_after_scaling",
           clipAfterScaling ? QVariant(*clipAfterScaling) : none());
  m.insert("run_name", runName);
  m.insert("models_to_run", modelsToRun);
  m.insert("class_weight", classWeight ? QVariant(*classWeight) : none());
  m.insert("select_by", selectBy);
  m.insert("cv_folds", cvFolds ? QVariant(*cvFolds) : none());
  m.insert("save_models", saveModels);
  m.insert("logistic_grid", gridTo(logisticGrid));
  m.insert("logistic_max_iter", logisticMaxIter);
  QVariantMap sg;
  for (auto it = svmGrids.begin(); it != svmGrids.end(); ++it)
    sg.insert(it.key(), gridTo(it.value()));
  m.insert("svm_grids", sg);
  m.insert("svm_max_train_windows",
           svmMaxTrainWindows ? QVariant(*svmMaxTrainWindows) : none());
  m.insert("ann_grid", gridTo(annGrid));
  m.insert("ann_fixed", annFixed);
  m.insert("ann_max_epochs", annMaxEpochs);
  m.insert("ann_patience", annPatience);
  m.insert("ann_balance_classes", annBalanceClasses);
  m.insert("tree_models_to_run", treeModelsToRun);
  QVariantMap tg;
  for (auto it = treeGrids.begin(); it != treeGrids.end(); ++it)
    tg.insert(it.key(), gridTo(it.value()));
  m.insert("tree_grids", tg);
  m.insert("compare_runs", compareRuns ? QVariant(*compareRuns) : none());
  m.insert("scatter_combination_features", scatterCombinationFeatures);
  m.insert("scatter_pairs", stringListsTo(scatterPairs));
  m.insert("scatter_triple_features", scatterTripleFeatures);
  m.insert("scatter_triples", stringListsTo(scatterTriples));
  m.insert("run_steps", runSteps);
  m.insert("stop_on_error", stopOnError);
  return m;
}

QString Settings::toToml() const {
  const QVariantMap m = asMap();
  std::ostringstream out;
  // Scalars and arrays first, then tables, so the file parses as TOML.
  toml::table scalars, tables;
  for (auto it = m.begin(); it != m.end(); ++it) {
    if (it.value().typeId() == QMetaType::QVariantMap)
      put(tables, it.value(), it.key().toStdString());
    else
      put(scalars, it.value(), it.key().toStdString());
  }
  out << scalars << "\n";
  for (const auto &[k, node] : tables) {
    toml::table one;
    one.insert(k.str(), *node.as_table());
    out << "\n" << one << "\n";
  }
  return QString::fromStdString(out.str());
}

bool Settings::save(const QString &path) const {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(toToml().toUtf8());
  return f.commit();
}

QStringList Settings::differences(const QVariantMap &a, const QVariantMap &b,
                                  bool experimentOnly) {
  QSet<QString> keys(a.keyBegin(), a.keyEnd());
  keys.unite(QSet<QString>(b.keyBegin(), b.keyEnd()));
  QStringList out;
  for (const QString &k : keys) {
    if (k == "run_name" || (experimentOnly && projectKeys().contains(k)))
      continue;
    if (a.value(k) != b.value(k))
      out << k;
  }
  std::sort(out.begin(), out.end());
  return out;
}

// --- Rng ---------------------------------------------------------------------

quint64 Rng::next() {
  s_ += 0x9E3779B97F4A7C15ull;
  quint64 z = s_;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

double Rng::uniform() { return double(next() >> 11) / double(1ull << 53); }

int Rng::below(int n) { return n <= 1 ? 0 : int(next() % quint64(n)); }

std::vector<int> Rng::permutation(int n) {
  std::vector<int> p(static_cast<size_t>(std::max(0, n)));
  for (int i = 0; i < n; ++i)
    p[static_cast<size_t>(i)] = i;
  for (int i = n - 1; i > 0; --i)
    std::swap(p[static_cast<size_t>(i)], p[static_cast<size_t>(below(i + 1))]);
  return p;
}

std::vector<int> Rng::choice(int n, int k, bool replace) {
  std::vector<int> out;
  if (replace) {
    for (int i = 0; i < k; ++i)
      out.push_back(below(n));
    return out;
  }
  std::vector<int> p = permutation(n);
  p.resize(static_cast<size_t>(std::min(k, n)));
  return p;
}

} // namespace skg::sfc
