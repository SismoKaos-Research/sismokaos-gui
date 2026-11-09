// miniSEED 2 reading: record headers, an index of a file's records, and
// decoding of the sample encodings Turkish broadband archives use (Steim1,
// Steim2, 16/32-bit integers, 32/64-bit floats), in either byte order.
// Record length comes from blockette 1000. The Steim decoders follow
// chaosmap's `io/mseed.hpp` (SismoKaos-Research), extended to both byte
// orders; the header reader is the one tdvms/coverage.cpp already uses.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <vector>

namespace skg::fx {

struct RecordHeader {
  QString network, station, location, channel;
  double startEpoch = 0; // seconds since 1970, time correction applied
  int sampleCount = 0;
  double sampleRate = 0;
  int recordLength = 0; // bytes, from blockette 1000
  int dataOffset = 0;
  int encoding = -1; // blockette 1000: 1 int16, 3 int32, 4 float32, 5 float64,
                     // 10 Steim1, 11 Steim2
  bool bigEndianData = true;
  bool valid = false;
  QString channelId() const; // NET.STA.LOC.CHA
  // 'E', 'N' or 'Z' from the channel's last letter (1/2 map to E/N), or 0.
  char component() const;
};

// Parses the fixed header and blockette 1000 of the record starting at
// `data[offset]`. Needs at least 64 bytes; `valid` is false otherwise.
RecordHeader parseHeader(const QByteArray &data, int offset = 0);

// Decodes the samples of one whole record (header + data). Throws
// std::runtime_error on an unsupported encoding or a truncated payload.
std::vector<double> decodeRecord(const QByteArray &record,
                                 const RecordHeader &hdr);

struct RecordRef {
  int file = 0; // index into the file list
  qint64 offset = 0;
  int length = 0;
  double startEpoch = 0;
  char component = 0;
  int count = 0;
  double rate = 0;
  QString channelId;
};

// Every data record of the file with an E/N/Z component and a positive rate.
// Throws std::runtime_error on a file that is not miniSEED.
QList<RecordRef> indexFile(const QString &path, int fileIndex);

// Days since 1970 of a proleptic Gregorian date.
qint64 daysFromCivil(int y, int m, int d);

} // namespace skg::fx
