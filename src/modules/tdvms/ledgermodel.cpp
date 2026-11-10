#include "modules/tdvms/ledgermodel.h"

#include "tdvms/settings.h"

#include <QBrush>
#include <QColor>
#include <QVariant>

namespace skg {

using namespace tdvms;

LedgerModel::LedgerModel(QObject *parent) : QAbstractTableModel(parent) {}

void LedgerModel::setLedgerPath(const QString &path) {
  path_ = path;
  reload();
}

void LedgerModel::reload() {
  beginResetModel();
  rows_ = path_.isEmpty() ? QList<Row>{} : Ledger(path_).rows();
  endResetModel();
}

int LedgerModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : rows_.size();
}

int LedgerModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

static QString shortStamp(const QString &iso) {
  return iso.isEmpty() ? QString() : iso.left(16).replace('T', ' ');
}

QVariant LedgerModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= rows_.size())
    return {};
  const Row &r = rows_[index.row()];
  if (role == Qt::DisplayRole) {
    switch (index.column()) {
    case Station:
      return str(r, "station");
    case Start:
      return shortStamp(str(r, "start"));
    case End:
      return shortStamp(str(r, "end"));
    case State:
      return str(r, "state");
    case Email:
      // Which slot holds the request; the person's address stays off screen.
      return maskAddress(str(r, "email"));
    case Attempts:
      return intOf(r, "attempts");
    case Bytes: {
      const qint64 b = bytesOf(r);
      return b ? QStringLiteral("%1 MB").arg(b / 1e6, 0, 'f', 1) : QString();
    }
    case Note:
      return str(r, "note");
    case Submitted:
      return shortStamp(str(r, "submitted_at"));
    case Fetched:
      return shortStamp(str(r, "fetched_at"));
    }
  }
  if (role == Qt::ForegroundRole && index.column() == State) {
    const QString s = str(r, "state");
    if (s == QLatin1String("fetched"))
      return QBrush(QColor(0x2e, 0x8b, 0x57));
    if (s == QLatin1String("failed"))
      return QBrush(QColor(0xc0, 0x39, 0x2b));
    if (s == QLatin1String("nodata") || s == QLatin1String("retired"))
      return QBrush(QColor(0x80, 0x80, 0x80));
    if (s == QLatin1String("submitted") || s == QLatin1String("claimed"))
      return QBrush(QColor(0x1f, 0x6f, 0xb5));
  }
  if (role == Qt::ToolTipRole)
    return str(r, "note");
  return {};
}

QVariant LedgerModel::headerData(int section, Qt::Orientation o,
                                 int role) const {
  if (o != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section) {
  case Station:
    return tr("Station");
  case Start:
    return tr("Start");
  case End:
    return tr("End");
  case State:
    return tr("State");
  case Email:
    return tr("Slot");
  case Attempts:
    return tr("Tries");
  case Bytes:
    return tr("Size");
  case Note:
    return tr("Note");
  case Submitted:
    return tr("Submitted");
  case Fetched:
    return tr("Fetched");
  }
  return {};
}

} // namespace skg
