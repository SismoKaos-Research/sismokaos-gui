// The active fault lines (MTA's Diri Fay Haritası shapefile, or any polyline
// shapefile) and the nearest-fault questions SCAP's catalog page answers:
// how far a station or an epicentre is from a fault, which one, and in
// which direction the point lies from it.
#pragma once

#include <QRectF>
#include <QString>

#include <optional>
#include <vector>

#include "analysis/geo.h"
#include "analysis/shapefile.h"

namespace skg::analysis {

struct Fault {
  QString name;        // FAYADI
  QString segment;     // SEGMENTADI
  int typeCode = 0;    // FAYTIPI: 1 DYK, 2 HF, 3 KF, 4 OKF
  QString slip;        // KAYMA_TURU
  double lengthKm = 0; // UZUNLUK
  QString activity, description, kind, direction, paleoseismology;
  std::vector<QPointF> line; // (lon, lat)
  QRectF bbox;
  QString label() const;             // "Doğu Anadolu Fayı (Pütürge Segmenti)"
  static QString typeName(int code); // "DYK" ... or "Kod:n"
};

struct NearestFault {
  const Fault *fault = nullptr;
  double km = 0;
  double lat = 0, lon = 0; // the point on the fault
  double azimuth = 0;      // from the fault point to the query point
  QString direction(const QString &lang) const {
    return compassDirection(azimuth, lang);
  }
};

class FaultIndex {
public:
  FaultIndex() = default;
  explicit FaultIndex(
      const ShapeLayer &layer); // every polyline shape becomes a Fault
  explicit FaultIndex(std::vector<Fault> faults);

  const std::vector<Fault> &faults() const { return faults_; }
  bool empty() const { return faults_.empty(); }
  // The nearest fault to a point, searching nearby cells first.
  std::optional<NearestFault> nearest(double lat, double lon) const;
  // Faults whose bounding box touches the rectangle (lon/lat), for drawing.
  std::vector<const Fault *> within(const QRectF &lonLat) const;

private:
  std::vector<Fault> faults_;
  QRectF bbox_;
  std::vector<std::vector<int>> cells_; // grid of fault indices by bbox
  int nx_ = 0, ny_ = 0;
  void build();
  int cellX(double lon) const;
  int cellY(double lat) const;
};

// Reads the shapefile and indexes it. Throws std::runtime_error.
FaultIndex loadFaults(const QString &shapefile);

} // namespace skg::analysis
