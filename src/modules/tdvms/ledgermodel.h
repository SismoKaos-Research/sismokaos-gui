// A ledger as a table: one row per chunk, re-read on demand.
#pragma once

#include <QAbstractTableModel>

#include "tdvms/ledger.h"
#include <QVariant>

namespace skg {

class LedgerModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column {
    Station,
    Start,
    End,
    State,
    Email,
    Attempts,
    Bytes,
    Note,
    Submitted,
    Fetched,
    ColumnCount
  };

  explicit LedgerModel(QObject *parent = nullptr);

  void setLedgerPath(const QString &path);
  const QString &ledgerPath() const { return path_; }
  void reload();
  const tdvms::Row &rowAt(int row) const { return rows_.at(row); }
  const QList<tdvms::Row> &rows() const { return rows_; }

  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
  QString path_;
  QList<tdvms::Row> rows_;
};

} // namespace skg
