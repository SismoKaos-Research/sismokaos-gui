// A reader for ESRI shapefiles with point, polyline and polygon shapes
// (plus their Z and M variants) and their dBASE attributes, enough for the
// MTA active fault lines and province boundaries. Text in the .dbf is
// decoded by the .cpg file when present, else as ISO-8859-9 (the MTA file).
#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

#include <vector>

namespace skg::analysis {

struct Shape {
  enum class Kind { Null, Point, PolyLine, Polygon, MultiPoint };
  Kind kind = Kind::Null;
  // Parts in (lon, lat). A point shape is one part with one vertex.
  std::vector<std::vector<QPointF>> parts;
  QRectF bbox; // left = min lon, top = min lat (Qt rect, y grows "up" here)
};

struct ShapeLayer {
  std::vector<Shape> shapes;
  QStringList fields;               // dBASE field names, in order
  std::vector<QVariantMap> records; // one per shape, field -> value
  QRectF bbox;
  QString projection; // the .prj text, if any
};

// Reads <base>.shp, .shx (optional), .dbf (optional) and .cpg/.prj (optional).
// `base` may carry any of those extensions. Throws std::runtime_error.
ShapeLayer readShapefile(const QString &base);

// ISO-8859-9 and windows-1254 to QString.
QString decodeLatin5(const QByteArray &bytes, bool windows1254 = false);

} // namespace skg::analysis
