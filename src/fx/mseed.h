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

// The channels met while indexing. A record names its channel by a small
// number into this table instead of carrying the text and the rate itself:
// a station-year of 512-byte records is tens of millions of records, and
// the index of all of them is held in memory for the length of a run.
struct ChannelTable {
  struct Channel {
    QString id;      // NET.STA.LOC.CHA
    double rate = 0; // samples per second
    char component = 0;
    // What is the same for the three components of one sensor: the id with
    // the channel's last letter left off (NET.STA.LOC.HH).
    QString family() const { return id.left(id.size() - 1); }
  };
  QList<Channel> channels;
  // The number of the header's channel at its rate, added when it is new.
  quint16 idOf(const RecordHeader &h);
};

struct RecordRef {
  qint64 offset = 0;
  double startEpoch = 0;
  qint32 file = 0; // index into the file list
  qint32 length = 0;
  quint16 count = 0;   // samples (16 bits in the record header too)
  quint16 channel = 0; // index into the ChannelTable it was indexed with
};
static_assert(sizeof(RecordRef) <= 32, "the record index is kept small");

// Every data record of the file with an E/N/Z component and a positive rate.
// Throws std::runtime_error on a file that is not miniSEED.
std::vector<RecordRef> indexFile(const QString &path, int fileIndex,
                                 ChannelTable &channels);

// Days since 1970 of a proleptic Gregorian date.
qint64 daysFromCivil(int y, int m, int d);

} // namespace skg::fx
