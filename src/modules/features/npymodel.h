// A structured .npy table (or a CSV) as a read-only table model, for previews.
#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QStringList>
#include <QVariant>

namespace skg {

class NpyModel : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit NpyModel(QObject *parent = nullptr);

  // Both throw std::runtime_error. Rows beyond `maxRows` are left out.
  void loadNpy(const QString &path, int maxRows = 2000);
  void loadCsv(const QString &path, int maxRows = 2000);
  void clear();
  int totalRows() const { return total_; }

  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
  QStringList columns_;
  QList<QStringList> rows_;
  int total_ = 0;
};

} // namespace skg
