#include "fx/mseed.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace skg::fx {

namespace {

template <typename T> T readAt(const QByteArray &d, int off, bool big) {
  const uchar *p = reinterpret_cast<const uchar *>(d.constData()) + off;
  return big ? qFromBigEndian<T>(p) : qFromLittleEndian<T>(p);
}

double sampleRate(int factor, int mult) {
  if (factor > 0)
    return mult > 0   ? double(factor) * mult
           : mult < 0 ? double(factor) / -mult
                      : factor;
  if (factor < 0)
    return mult > 0   ? double(-mult) / factor
           : mult < 0 ? 1.0 / (double(factor) * mult)
                      : -1.0 / factor;
  return 0.0;
}

QString ascii(const QByteArray &d, int off, int len) {
  return QString::fromLatin1(d.mid(off, len)).trimmed();
}

template <unsigned Bits> qint32 signExtend(quint32 v) {
  constexpr quint32 mask = (1u << Bits) - 1u;
  constexpr quint32 sign = 1u << (Bits - 1u);
  v &= mask;
  return qint32((v ^ sign) - sign);
}

// Steim1/2 share the frame layout: 16 words per 64-byte frame, the first a
// control word of 2-bit nibbles, and in the first frame words 1 and 2 hold
// the forward and reverse integration constants.
void decodeSteim(const QByteArray &rec, int dataOffset, int recLen, int count,
                 bool big, bool steim2, std::vector<double> &out) {
  const int frames = (recLen - dataOffset) / 64;
  if (frames < 1)
    throw std::runtime_error("Steim record without a full frame");
  const qint32 x0 = readAt<qint32>(rec, dataOffset + 4, big);
  std::vector<qint32> diffs;
  diffs.reserve(count + 8);
  for (int f = 0; f < frames && int(diffs.size()) < count; ++f) {
    const int fp = dataOffset + f * 64;
    const quint32 ctrl = readAt<quint32>(rec, fp, big);
    for (int w = 1; w < 16 && int(diffs.size()) < count; ++w) {
      if (f == 0 && (w == 1 || w == 2))
        continue;
      const quint32 ck = (ctrl >> ((15 - w) * 2)) & 0x3u;
      const int wp = fp + w * 4;
      if (ck == 0)
        continue;
      if (ck == 1) { // four 8-bit differences, in byte order of the word
        const quint32 word = readAt<quint32>(rec, wp, big);
        diffs.push_back(qint8(word >> 24));
        diffs.push_back(qint8((word >> 16) & 0xFF));
        diffs.push_back(qint8((word >> 8) & 0xFF));
        diffs.push_back(qint8(word & 0xFF));
        continue;
      }
      if (!steim2) {
        if (ck == 2) {
          diffs.push_back(readAt<qint16>(rec, wp, big));
          diffs.push_back(readAt<qint16>(rec, wp + 2, big));
        } else {
          diffs.push_back(readAt<qint32>(rec, wp, big));
        }
        continue;
      }
      const quint32 word = readAt<quint32>(rec, wp, big);
      const quint32 dnib = (word >> 30) & 0x3u;
      if (ck == 2) {
        if (dnib == 1)
          diffs.push_back(signExtend<30>(word));
        else if (dnib == 2) {
          diffs.push_back(signExtend<15>(word >> 15));
          diffs.push_back(signExtend<15>(word));
        } else if (dnib == 3) {
          diffs.push_back(signExtend<10>(word >> 20));
          diffs.push_back(signExtend<10>(word >> 10));
          diffs.push_back(signExtend<10>(word));
        }
      } else { // ck == 3
        if (dnib == 0)
          for (int k = 4; k >= 0; --k)
            diffs.push_back(signExtend<6>(word >> (k * 6)));
        else if (dnib == 1)
          for (int k = 5; k >= 0; --k)
            diffs.push_back(signExtend<5>(word >> (k * 5)));
        else if (dnib == 2)
          for (int k = 6; k >= 0; --k)
            diffs.push_back(signExtend<4>(word >> (k * 4)));
      }
    }
  }
  if (int(diffs.size()) < count)
    throw std::runtime_error(
        "Steim record holds fewer differences than samples");
  out.reserve(count);
  qint32 cur = x0;
  out.push_back(cur);
  for (int i = 1; i < count; ++i) {
    cur += diffs[i];
    out.push_back(cur);
  }
}

} // namespace

qint64 daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const qint64 era = (y >= 0 ? y : y - 399) / 400;
  const qint64 yoe = y - era * 400;
  const qint64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const qint64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

QString RecordHeader::channelId() const {
  return QStringLiteral("%1.%2.%3.%4").arg(network, station, location, channel);
}

char RecordHeader::component() const {
  if (channel.isEmpty())
    return 0;
  switch (channel.back().toUpper().toLatin1()) {
  case 'Z':
    return 'Z';
  case 'N':
  case '2':
    return 'N';
  case 'E':
  case '1':
    return 'E';
  default:
    return 0;
  }
}

RecordHeader parseHeader(const QByteArray &d, int off) {
  RecordHeader h;
  if (d.size() - off < 64)
    return h;
  const char q = d[off + 6];
  if (!(q == 'D' || q == 'R' || q == 'Q' || q == 'M'))
    return h;
  bool big = true;
  quint16 year = readAt<quint16>(d, off + 20, true);
  if (!(year >= 1900 && year <= 2100)) {
    big = false;
    year = readAt<quint16>(d, off + 20, false);
    if (!(year >= 1900 && year <= 2100))
      return h;
  }
  h.station = ascii(d, off + 8, 5);
  h.location = ascii(d, off + 13, 2);
  h.channel = ascii(d, off + 15, 3);
  h.network = ascii(d, off + 18, 2);
  const quint16 doy = readAt<quint16>(d, off + 22, big);
  const int hour = uchar(d[off + 24]), minute = uchar(d[off + 25]),
            sec = uchar(d[off + 26]);
  const quint16 frac = readAt<quint16>(d, off + 28, big);
  h.sampleCount = readAt<quint16>(d, off + 30, big);
  const qint16 factor = readAt<qint16>(d, off + 32, big);
  const qint16 mult = readAt<qint16>(d, off + 34, big);
  h.sampleRate = sampleRate(factor, mult);
  const uchar activity = uchar(d[off + 36]);
  const int nblk = uchar(d[off + 39]);
  const qint32 corr = readAt<qint32>(d, off + 40, big);
  h.dataOffset = readAt<quint16>(d, off + 44, big);
  int b = readAt<quint16>(d, off + 46, big);
  h.bigEndianData = big;
  for (int i = 0; i < nblk && b >= 48 && off + b + 8 <= d.size(); ++i) {
    const quint16 kind = readAt<quint16>(d, off + b, big);
    const int next = readAt<quint16>(d, off + b + 2, big);
    if (kind == 1000) {
      h.encoding = uchar(d[off + b + 4]);
      h.bigEndianData = uchar(d[off + b + 5]) != 0;
      // 2^8 .. 2^20 bytes; anything else is damage, and shifting by it
      // would give a length the reader then walks off the buffer with.
      const int exponent = uchar(d[off + b + 6]);
      if (exponent < 8 || exponent > 20)
        return RecordHeader{};
      h.recordLength = 1 << exponent;
    }
    if (next == 0 || next <= b)
      break;
    b = next;
  }
  if (h.recordLength == 0)
    return h; // no blockette 1000: length unknown
  const qint64 days = daysFromCivil(year, 1, 1) + doy - 1;
  h.startEpoch = double(days) * 86400.0 + hour * 3600.0 + minute * 60.0 + sec +
                 frac * 1e-4;
  if (!(activity & 0x02))
    h.startEpoch += corr * 1e-4;
  h.valid = true;
  return h;
}

std::vector<double> decodeRecord(const QByteArray &rec, const RecordHeader &h) {
  std::vector<double> out;
  if (!h.valid || h.dataOffset < 48 || h.dataOffset >= rec.size())
    throw std::runtime_error("record header unusable for decoding");
  const int n = h.sampleCount;
  const bool big = h.bigEndianData;
  auto need = [&](int bytes) {
    if (h.dataOffset + bytes > rec.size())
      throw std::runtime_error(
          "record payload is shorter than its sample count");
  };
  switch (h.encoding) {
  case 1:
    need(n * 2);
    for (int i = 0; i < n; ++i)
      out.push_back(readAt<qint16>(rec, h.dataOffset + i * 2, big));
    break;
  case 3:
    need(n * 4);
    for (int i = 0; i < n; ++i)
      out.push_back(readAt<qint32>(rec, h.dataOffset + i * 4, big));
    break;
  case 4:
    need(n * 4);
    for (int i = 0; i < n; ++i) {
      const quint32 bits = readAt<quint32>(rec, h.dataOffset + i * 4, big);
      float f;
      std::memcpy(&f, &bits, 4);
      out.push_back(f);
    }
    break;
  case 5:
    need(n * 8);
    for (int i = 0; i < n; ++i) {
      const quint64 bits = readAt<quint64>(rec, h.dataOffset + i * 8, big);
      double v;
      std::memcpy(&v, &bits, 8);
      out.push_back(v);
    }
    break;
  case 10:
  case 11:
    decodeSteim(rec, h.dataOffset, std::min<int>(h.recordLength, rec.size()), n,
                big, h.encoding == 11, out);
    break;
  default:
    throw std::runtime_error(QStringLiteral("unsupported miniSEED encoding %1")
                                 .arg(h.encoding)
                                 .toStdString());
  }
  return out;
}

quint16 ChannelTable::idOf(const RecordHeader &h) {
  const QString id = h.channelId();
  for (int i = 0; i < channels.size(); ++i)
    if (channels[i].id == id && std::abs(channels[i].rate - h.sampleRate) < 1e-9)
      return quint16(i);
  if (channels.size() >= 65535)
    throw std::runtime_error("more than 65535 channels in the input files");
  channels.append({id, h.sampleRate, h.component()});
  return quint16(channels.size() - 1);
}

std::vector<RecordRef> indexFile(const QString &path, int fileIndex,
                                 ChannelTable &channels) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error(("cannot open " + path).toStdString());
  std::vector<RecordRef> out;
  // The last channel seen, since records of one channel come in runs.
  QString lastId;
  double lastRate = 0;
  quint16 lastChannel = 0;
  qint64 pos = 0;
  const qint64 size = f.size();
  while (pos + 64 <= size) {
    f.seek(pos);
    const QByteArray head = f.read(128);
    const RecordHeader h = parseHeader(head, 0);
    if (!h.valid)
      throw std::runtime_error(QStringLiteral("not miniSEED at byte %1 of %2")
                                   .arg(pos)
                                   .arg(path)
                                   .toStdString());
    if (h.sampleRate > 0 && h.sampleCount > 0 && h.component() != 0) {
      RecordRef r;
      r.file = fileIndex;
      r.offset = pos;
      r.length = h.recordLength;
      r.startEpoch = h.startEpoch;
      r.count = quint16(h.sampleCount);
      const QString id = h.channelId();
      if (out.empty() || id != lastId || h.sampleRate != lastRate) {
        lastChannel = channels.idOf(h);
        lastId = id;
        lastRate = h.sampleRate;
      }
      r.channel = lastChannel;
      out.push_back(r);
    }
    pos += h.recordLength;
  }
  return out;
}

} // namespace skg::fx
