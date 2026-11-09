// The band list of a feature-extraction config as an editable table.
#pragma once

#include <QAbstractTableModel>
#include <QList>

#include "fx/config.h"
#include <QVariant>

namespace skg {

class BandsModel : public QAbstractTableModel {
  Q_OBJECT
public:
  enum Column {
    Name,
    FreqMin,
    FreqMax,
    Fs,
    WinSec,
    StepSec,
    StaSec,
    LtaSec,
    SplitHz,
    ColumnCount
  };

  explicit BandsModel(QObject *parent = nullptr);

  QList<fx::Band> bands() const { return bands_; }
  void setBands(const QList<fx::Band> &bands);
  // Appends a band copying the run's single-band settings.
  void addBand(const fx::Band &band);
  void removeBands(const QList<int> &rows);

  int rowCount(const QModelIndex &parent = {}) const override;
  int columnCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  bool setData(const QModelIndex &index, const QVariant &value,
               int role) override;
  QVariant headerData(int section, Qt::Orientation o, int role) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;

private:
  QList<fx::Band> bands_;
};

} // namespace skg
