// NumPy .npy files with a structured dtype: the format the Python tool
// writes (hourly E/N/Z windows, and the feature table with a U50 window id,
// f8 columns). Written so that numpy.load reads them unchanged, and read back
// for the GUI's previews.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <vector>

namespace skg::fx {

struct NpyField {
  QString name;
  QString dtype; // "<f8", "<U50", "<i8"
};

struct NpyTable {
  QList<NpyField> fields;
  int rows = 0;
  // Column-major storage: doubles for numeric fields, strings for U fields.
  QList<std::vector<double>>
      numeric;                // one per numeric field (empty for strings)
  QList<QStringList> strings; // one per string field (empty for numeric)
  QVariant value(int row, int field) const;
};

// Serialises a structured array. Throws std::invalid_argument on a bad
// dtype or mismatched lengths.
QByteArray encodeNpy(const NpyTable &table);
bool writeNpy(const QString &path, const NpyTable &table);
// Parses a structured .npy (v1/v2 header, little-endian f8/i8/U fields).
// Throws std::runtime_error on anything else.
NpyTable decodeNpy(const QByteArray &bytes);
NpyTable readNpy(const QString &path);

// Convenience for the hourly windows: fields E, N, Z (those present) as f8.
QByteArray
encodeWindowNpy(const QList<QPair<QString, std::vector<double>>> &channels);

} // namespace skg::fx
