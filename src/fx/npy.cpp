#include "fx/npy.h"

#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtEndian>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace skg::fx {

namespace {

int fieldBytes(const QString &dtype) {
  if (dtype == QLatin1String("<f8") || dtype == QLatin1String("<i8"))
    return 8;
  if (dtype.startsWith(QLatin1String("<U")))
    return dtype.mid(2).toInt() * 4;
  throw std::invalid_argument(("unsupported npy dtype " + dtype).toStdString());
}

bool isString(const QString &dtype) {
  return dtype.startsWith(QLatin1String("<U"));
}

} // namespace

QVariant NpyTable::value(int row, int field) const {
  if (row < 0 || row >= rows || field < 0 || field >= fields.size())
    return {};
  if (isString(fields[field].dtype))
    return strings[field].value(row);
  return numeric[field][row];
}

QByteArray encodeNpy(const NpyTable &t) {
  QString descr = "[";
  int rowBytes = 0;
  for (int i = 0; i < t.fields.size(); ++i) {
    const NpyField &f = t.fields[i];
    rowBytes += fieldBytes(f.dtype);
    if (isString(f.dtype) && t.strings.value(i).size() != t.rows)
      throw std::invalid_argument("string column length mismatch");
    if (!isString(f.dtype) && int(t.numeric.value(i).size()) != t.rows)
      throw std::invalid_argument("numeric column length mismatch");
    descr += QStringLiteral("('%1', '%2'), ").arg(f.name, f.dtype);
  }
  descr += "]";
  QByteArray header =
      QStringLiteral("{'descr': %1, 'fortran_order': False, 'shape': (%2,), }")
          .arg(descr)
          .arg(t.rows)
          .toUtf8();
  // Version 1.0: magic(6) + version(2) + header length(2) + header, padded
  // with spaces to a multiple of 64 and ending in a newline.
  int total = 10 + header.size() + 1;
  const int pad = (64 - total % 64) % 64;
  header += QByteArray(pad, ' ');
  header += '\n';
  QByteArray out;
  out.append("\x93NUMPY", 6);
  out.append(char(1));
  out.append(char(0));
  const quint16 len = quint16(header.size());
  char lenBytes[2];
  qToLittleEndian<quint16>(len, lenBytes);
  out.append(lenBytes, 2);
  out.append(header);

  QByteArray body(qsizetype(rowBytes) * t.rows, '\0');
  char *p = body.data();
  for (int r = 0; r < t.rows; ++r) {
    for (int i = 0; i < t.fields.size(); ++i) {
      const NpyField &f = t.fields[i];
      if (isString(f.dtype)) {
        const int chars = f.dtype.mid(2).toInt();
        const QString s = t.strings[i][r];
        const auto ucs4 = s.toUcs4();
        for (int c = 0; c < chars; ++c) {
          const quint32 cp = c < int(ucs4.size()) ? ucs4[c] : 0u;
          qToLittleEndian<quint32>(cp, p);
          p += 4;
        }
      } else if (f.dtype == QLatin1String("<i8")) {
        const qint64 v = qint64(t.numeric[i][r]);
        qToLittleEndian<qint64>(v, p);
        p += 8;
      } else {
        const double v = t.numeric[i][r];
        quint64 bits;
        std::memcpy(&bits, &v, 8);
        qToLittleEndian<quint64>(bits, p);
        p += 8;
      }
    }
  }
  out.append(body);
  return out;
}

bool writeNpy(const QString &path, const NpyTable &table) {
  QSaveFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    return false;
  f.write(encodeNpy(table));
  return f.commit();
}

NpyTable decodeNpy(const QByteArray &bytes) {
  if (bytes.size() < 10 || !bytes.startsWith(QByteArray("\x93NUMPY", 6)))
    throw std::runtime_error("not a .npy file");
  const int major = uchar(bytes[6]);
  int headerLen, headerStart;
  if (major == 1) {
    headerLen = qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar *>(bytes.constData() + 8));
    headerStart = 10;
  } else {
    headerLen = int(qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar *>(bytes.constData() + 8)));
    headerStart = 12;
  }
  const QString header = QString::fromUtf8(bytes.mid(headerStart, headerLen));
  NpyTable t;
  static const QRegularExpression fieldRe(
      QStringLiteral(R"(\('([^']*)',\s*'([^']*)'\))"));
  auto it = fieldRe.globalMatch(header);
  while (it.hasNext()) {
    const auto m = it.next();
    t.fields.append({m.captured(1), m.captured(2)});
  }
  if (t.fields.isEmpty())
    throw std::runtime_error("npy file has no structured dtype");
  static const QRegularExpression shapeRe(
      QStringLiteral(R"('shape':\s*\((\d+),?\))"));
  const auto sm = shapeRe.match(header);
  if (!sm.hasMatch())
    throw std::runtime_error("npy header has no 1-D shape");
  t.rows = sm.captured(1).toInt();
  int rowBytes = 0;
  for (const NpyField &f : t.fields)
    rowBytes += fieldBytes(f.dtype);
  const int dataStart = headerStart + headerLen;
  if (bytes.size() < dataStart + qsizetype(rowBytes) * t.rows)
    throw std::runtime_error("npy data is shorter than its shape says");
  for (const NpyField &f : t.fields) {
    t.numeric.append(isString(f.dtype) ? std::vector<double>{}
                                       : std::vector<double>(t.rows));
    t.strings.append(isString(f.dtype) ? QStringList(t.rows, QString())
                                       : QStringList{});
  }
  const uchar *p =
      reinterpret_cast<const uchar *>(bytes.constData()) + dataStart;
  for (int r = 0; r < t.rows; ++r) {
    for (int i = 0; i < t.fields.size(); ++i) {
      const NpyField &f = t.fields[i];
      if (isString(f.dtype)) {
        const int chars = f.dtype.mid(2).toInt();
        QString s;
        for (int c = 0; c < chars; ++c) {
          const quint32 cp = qFromLittleEndian<quint32>(p + c * 4);
          if (cp == 0)
            break;
          s += QChar::fromUcs4(cp);
        }
        t.strings[i][r] = s;
        p += chars * 4;
      } else if (f.dtype == QLatin1String("<i8")) {
        t.numeric[i][r] = double(qFromLittleEndian<qint64>(p));
        p += 8;
      } else {
        const quint64 bits = qFromLittleEndian<quint64>(p);
        double v;
        std::memcpy(&v, &bits, 8);
        t.numeric[i][r] = v;
        p += 8;
      }
    }
  }
  return t;
}

NpyTable readNpy(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(("cannot open " + path).toStdString());
  return decodeNpy(f.readAll());
}

QByteArray
encodeWindowNpy(const QList<QPair<QString, std::vector<double>>> &channels) {
  NpyTable t;
  for (const auto &[name, data] : channels) {
    t.fields.append({name, QStringLiteral("<f8")});
    t.rows = std::max<int>(t.rows, int(data.size()));
  }
  for (const auto &[name, data] : channels) {
    std::vector<double> col(t.rows, std::numeric_limits<double>::quiet_NaN());
    std::copy(data.begin(), data.end(), col.begin());
    t.numeric.append(col);
    t.strings.append(QStringList());
  }
  return encodeNpy(t);
}

} // namespace skg::fx
