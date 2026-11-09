#include "analysis/shapefile.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>
#include <QtEndian>

#include <cstring>
#include <stdexcept>

namespace skg::analysis {

namespace {

QByteArray readAll(const QString &path, bool required) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    if (required)
      throw std::runtime_error(("cannot open " + path).toStdString());
    return {};
  }
  return f.readAll();
}

double le64(const char *p) {
  double v;
  quint64 u = qFromLittleEndian<quint64>(reinterpret_cast<const uchar *>(p));
  std::memcpy(&v, &u, 8);
  return v;
}
qint32 le32(const char *p) {
  return qFromLittleEndian<qint32>(reinterpret_cast<const uchar *>(p));
}
qint32 be32(const char *p) {
  return qFromBigEndian<qint32>(reinterpret_cast<const uchar *>(p));
}

Shape parseShape(const char *rec, int len) {
  Shape s;
  if (len < 4)
    throw std::runtime_error("shapefile: truncated record");
  const int type = le32(rec);
  auto need = [&](int bytes) {
    if (len < bytes)
      throw std::runtime_error("shapefile: truncated record");
  };
  switch (type) {
  case 0:
    return s;
  case 1:
  case 11:
  case 21: { // Point, PointZ, PointM
    need(20);
    s.kind = Shape::Kind::Point;
    const QPointF p(le64(rec + 4), le64(rec + 12));
    s.parts.push_back({p});
    s.bbox = QRectF(p, p);
    return s;
  }
  case 8:
  case 18:
  case 28: { // MultiPoint
    need(40);
    s.kind = Shape::Kind::MultiPoint;
    s.bbox = QRectF(QPointF(le64(rec + 4), le64(rec + 12)),
                    QPointF(le64(rec + 20), le64(rec + 28)));
    const int n = le32(rec + 36);
    need(40 + 16 * n);
    std::vector<QPointF> pts;
    for (int i = 0; i < n; ++i)
      pts.emplace_back(le64(rec + 40 + 16 * i), le64(rec + 48 + 16 * i));
    s.parts.push_back(std::move(pts));
    return s;
  }
  case 3:
  case 13:
  case 23: // PolyLine, PolyLineZ, PolyLineM
  case 5:
  case 15:
  case 25: { // Polygon, PolygonZ, PolygonM
    need(44);
    s.kind = (type % 10 == 3) ? Shape::Kind::PolyLine : Shape::Kind::Polygon;
    s.bbox = QRectF(QPointF(le64(rec + 4), le64(rec + 12)),
                    QPointF(le64(rec + 20), le64(rec + 28)));
    const int numParts = le32(rec + 36), numPoints = le32(rec + 40);
    need(44 + 4 * numParts + 16 * numPoints);
    std::vector<int> starts;
    for (int i = 0; i < numParts; ++i)
      starts.push_back(le32(rec + 44 + 4 * i));
    starts.push_back(numPoints);
    const char *pts = rec + 44 + 4 * numParts;
    for (int p = 0; p < numParts; ++p) {
      std::vector<QPointF> part;
      for (int i = starts[size_t(p)]; i < starts[size_t(p) + 1]; ++i)
        part.emplace_back(le64(pts + 16 * i), le64(pts + 16 * i + 8));
      s.parts.push_back(std::move(part));
    }
    return s;
  }
  default:
    throw std::runtime_error(
        QStringLiteral("shapefile: unsupported shape type %1")
            .arg(type)
            .toStdString());
  }
}

QString decodeField(const QByteArray &raw, QStringDecoder *dec, bool latin5) {
  QByteArray b = raw;
  while (b.endsWith(' ') || b.endsWith('\0'))
    b.chop(1);
  int lead = 0;
  while (lead < b.size() && (b[lead] == ' ' || b[lead] == '\0'))
    ++lead;
  b.remove(0, lead);
  if (latin5)
    return decodeLatin5(b);
  return dec && dec->isValid() ? QString(dec->decode(b)) : QString::fromUtf8(b);
}

void readDbf(const QByteArray &dbf, const QString &cpg, ShapeLayer &layer) {
  if (dbf.size() < 32)
    return;
  const uchar *d = reinterpret_cast<const uchar *>(dbf.constData());
  const int records = int(qFromLittleEndian<quint32>(d + 4));
  const int headerLen = qFromLittleEndian<quint16>(d + 8),
            recordLen = qFromLittleEndian<quint16>(d + 10);
  struct Field {
    QString name;
    char type;
    int length, decimals;
  };
  std::vector<Field> fields;
  for (int off = 32; off + 32 <= headerLen && d[off] != 0x0D; off += 32) {
    Field f;
    f.name = QString::fromLatin1(reinterpret_cast<const char *>(d + off))
                 .left(11)
                 .section(QChar('\0'), 0, 0)
                 .trimmed();
    f.type = char(d[off + 11]);
    f.length = d[off + 16];
    f.decimals = d[off + 17];
    fields.push_back(f);
    layer.fields << f.name;
  }
  // Codepage: the .cpg text, or the header's language driver byte, else
  // Latin-5.
  QString codepage = cpg.trimmed();
  if (codepage.isEmpty()) {
    switch (d[29]) {
    case 0x57:
    case 0x58:
    case 0x03:
      codepage = QStringLiteral("windows-1252");
      break;
    case 0x65:
      codepage = QStringLiteral("IBM866");
      break;
    default:
      break;
    }
  }
  const bool latin5 =
      codepage.isEmpty() ||
      codepage.contains(QLatin1String("8859-9"), Qt::CaseInsensitive) ||
      codepage.contains(QLatin1String("1254"));
  QStringDecoder dec(codepage.toLatin1().constData());
  if (codepage.compare(QLatin1String("UTF-8"), Qt::CaseInsensitive) == 0 ||
      codepage.startsWith(QLatin1String("65001")))
    dec = QStringDecoder(QStringDecoder::Utf8);
  for (int r = 0; r < records; ++r) {
    const int base = headerLen + r * recordLen;
    if (base + recordLen > dbf.size())
      break;
    QVariantMap rec;
    if (d[base] == '*') { // deleted
      layer.records.push_back(rec);
      continue;
    }
    int pos = base + 1;
    for (const Field &f : fields) {
      const QByteArray raw = dbf.mid(pos, f.length);
      pos += f.length;
      switch (f.type) {
      case 'N':
      case 'F': {
        const QString t = QString::fromLatin1(raw).trimmed();
        bool ok = false;
        const double v = t.toDouble(&ok);
        rec.insert(f.name, ok ? QVariant(v) : QVariant());
        break;
      }
      case 'L':
        rec.insert(f.name, raw.isEmpty()
                               ? QVariant()
                               : QVariant(QByteArray("TtYy").contains(raw[0])));
        break;
      case 'D': {
        const QDate date = QDate::fromString(QString::fromLatin1(raw).trimmed(),
                                             QStringLiteral("yyyyMMdd"));
        rec.insert(f.name, date.isValid() ? QVariant(date) : QVariant());
        break;
      }
      default:
        rec.insert(f.name, decodeField(raw, &dec, latin5));
      }
    }
    layer.records.push_back(rec);
  }
}

} // namespace

QString decodeLatin5(const QByteArray &bytes, bool windows1254) {
  static const char16_t cp1252High[32] = {
      0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0xFFFD, 0xFFFD,
      0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0xFFFD, 0x0178};
  QString out;
  out.reserve(bytes.size());
  for (unsigned char c : bytes) {
    char16_t u = c;
    switch (c) {
    case 0xD0:
      u = 0x011E;
      break;
    case 0xDD:
      u = 0x0130;
      break;
    case 0xDE:
      u = 0x015E;
      break;
    case 0xF0:
      u = 0x011F;
      break;
    case 0xFD:
      u = 0x0131;
      break;
    case 0xFE:
      u = 0x015F;
      break;
    default:
      if (windows1254 && c >= 0x80 && c < 0xA0)
        u = cp1252High[c - 0x80];
    }
    out.append(QChar(u));
  }
  return out;
}

ShapeLayer readShapefile(const QString &base) {
  QString stem = base;
  for (const char *ext :
       {".shp", ".shx", ".dbf", ".prj", ".cpg", ".SHP", ".DBF"})
    if (stem.endsWith(QLatin1String(ext))) {
      stem.chop(4);
      break;
    }
  const QByteArray shp = readAll(stem + QStringLiteral(".shp"), true);
  if (shp.size() < 100 || be32(shp.constData()) != 9994)
    throw std::runtime_error((stem + ".shp is not a shapefile").toStdString());
  ShapeLayer layer;
  const char *d = shp.constData();
  layer.bbox = QRectF(QPointF(le64(d + 36), le64(d + 44)),
                      QPointF(le64(d + 52), le64(d + 60)));
  const int fileLen = be32(d + 24) * 2; // 16-bit words
  int pos = 100;
  while (pos + 8 <= std::min<int>(fileLen, shp.size())) {
    const int contentLen = be32(d + pos + 4) * 2;
    if (pos + 8 + contentLen > shp.size())
      throw std::runtime_error(
          "shapefile: record runs past the end of the file");
    layer.shapes.push_back(parseShape(d + pos + 8, contentLen));
    pos += 8 + contentLen;
  }
  const QByteArray dbf = readAll(stem + QStringLiteral(".dbf"), false);
  const QByteArray cpg = readAll(stem + QStringLiteral(".cpg"), false);
  if (!dbf.isEmpty())
    readDbf(dbf, QString::fromLatin1(cpg), layer);
  while (layer.records.size() < layer.shapes.size())
    layer.records.emplace_back();
  layer.projection =
      QString::fromLatin1(readAll(stem + QStringLiteral(".prj"), false))
          .trimmed();
  return layer;
}

} // namespace skg::analysis
