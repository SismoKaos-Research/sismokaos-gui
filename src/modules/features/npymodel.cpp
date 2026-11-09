#include "modules/features/npymodel.h"

#include <QFile>
#include <QTextStream>

#include "fx/npy.h"

#include <cmath>
#include <stdexcept>

namespace skg {

NpyModel::NpyModel(QObject *parent) : QAbstractTableModel(parent) {}

void NpyModel::clear() {
  beginResetModel();
  columns_.clear();
  rows_.clear();
  total_ = 0;
  endResetModel();
}

void NpyModel::loadNpy(const QString &path, int maxRows) {
  const fx::NpyTable t = fx::readNpy(path);
  beginResetModel();
  columns_.clear();
  rows_.clear();
  for (const fx::NpyField &f : t.fields)
    columns_ << f.name;
  total_ = t.rows;
  const int n = std::min(t.rows, maxRows);
  for (int r = 0; r < n; ++r) {
    QStringList row;
    for (int c = 0; c < t.fields.size(); ++c) {
      const QVariant v = t.value(r, c);
      if (v.typeId() == QMetaType::Double) {
        const double d = v.toDouble();
        row << (std::isnan(d) ? QString() : QString::number(d, 'g', 7));
      } else
        row << v.toString();
    }
    rows_ << row;
  }
  endResetModel();
}

void NpyModel::loadCsv(const QString &path, int maxRows) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    throw std::runtime_error(("cannot read " + path).toStdString());
  QTextStream in(&f);
  beginResetModel();
  columns_.clear();
  rows_.clear();
  total_ = 0;
  if (!in.atEnd())
    columns_ = in.readLine().split(',');
  while (!in.atEnd()) {
    const QString line = in.readLine();
    if (line.isEmpty())
      continue;
    ++total_;
    if (rows_.size() < maxRows)
      rows_ << line.split(',');
  }
  endResetModel();
}

int NpyModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : rows_.size();
}
int NpyModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : columns_.size();
}

QVariant NpyModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || role != Qt::DisplayRole)
    return {};
  return rows_[index.row()].value(index.column());
}

QVariant NpyModel::headerData(int section, Qt::Orientation o, int role) const {
  if (role != Qt::DisplayRole)
    return {};
  if (o == Qt::Horizontal)
    return columns_.value(section);
  return section + 1;
}

} // namespace skg
