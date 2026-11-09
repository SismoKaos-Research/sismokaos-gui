#include "modules/features/bandsmodel.h"

#include <algorithm>

namespace skg {

BandsModel::BandsModel(QObject *parent) : QAbstractTableModel(parent) {}

void BandsModel::setBands(const QList<fx::Band> &bands) {
  beginResetModel();
  bands_ = bands;
  endResetModel();
}

void BandsModel::addBand(const fx::Band &band) {
  beginInsertRows({}, bands_.size(), bands_.size());
  bands_ << band;
  endInsertRows();
}

void BandsModel::removeBands(const QList<int> &rows) {
  QList<int> sorted = rows;
  std::sort(sorted.begin(), sorted.end(), std::greater<int>());
  for (int r : sorted) {
    if (r < 0 || r >= bands_.size())
      continue;
    beginRemoveRows({}, r, r);
    bands_.removeAt(r);
    endRemoveRows();
  }
}

int BandsModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : bands_.size();
}
int BandsModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant BandsModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole))
    return {};
  const fx::Band &b = bands_[index.row()];
  switch (index.column()) {
  case Name:
    return role == Qt::DisplayRole && b.name.isEmpty() ? b.label() : b.name;
  case FreqMin:
    return b.freqmin;
  case FreqMax:
    return b.freqmax;
  case Fs:
    return b.fs;
  case WinSec:
    return b.winSec;
  case StepSec:
    return b.stepSec;
  case StaSec:
    return b.staSec;
  case LtaSec:
    return b.ltaSec;
  case SplitHz:
    if (b.spectralSplitHz)
      return *b.spectralSplitHz;
    return role == Qt::DisplayRole ? QVariant(tr("default")) : QVariant();
  }
  return {};
}

bool BandsModel::setData(const QModelIndex &index, const QVariant &value,
                         int role) {
  if (!index.isValid() || role != Qt::EditRole)
    return false;
  fx::Band &b = bands_[index.row()];
  bool ok = true;
  switch (index.column()) {
  case Name:
    b.name = value.toString().trimmed();
    break;
  case FreqMin:
    b.freqmin = value.toDouble(&ok);
    break;
  case FreqMax:
    b.freqmax = value.toDouble(&ok);
    break;
  case Fs:
    b.fs = value.toDouble(&ok);
    break;
  case WinSec:
    b.winSec = value.toInt(&ok);
    break;
  case StepSec:
    b.stepSec = value.toInt(&ok);
    break;
  case StaSec:
    b.staSec = value.toDouble(&ok);
    break;
  case LtaSec:
    b.ltaSec = value.toInt(&ok);
    break;
  case SplitHz: {
    const QString s = value.toString().trimmed();
    if (s.isEmpty())
      b.spectralSplitHz.reset();
    else {
      const double v = s.toDouble(&ok);
      if (ok)
        b.spectralSplitHz = v;
    }
    break;
  }
  }
  if (!ok)
    return false;
  emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
  return true;
}

QVariant BandsModel::headerData(int section, Qt::Orientation o,
                                int role) const {
  if (o != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case Name:
    return tr("Band");
  case FreqMin:
    return tr("f min (Hz)");
  case FreqMax:
    return tr("f max (Hz)");
  case Fs:
    return tr("fs (Hz)");
  case WinSec:
    return tr("Window (s)");
  case StepSec:
    return tr("Step (s)");
  case StaSec:
    return tr("STA (s)");
  case LtaSec:
    return tr("LTA (s)");
  case SplitHz:
    return tr("Split (Hz)");
  }
  return {};
}

Qt::ItemFlags BandsModel::flags(const QModelIndex &index) const {
  return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
}

} // namespace skg
