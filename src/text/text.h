// Text as catalogs bring it: bytes in an encoding nobody states, and place
// names an event service has already damaged.
//
//   decode       the text of a CSV file: UTF-8 when the bytes are valid UTF-8
//                (a byte order mark is dropped), otherwise Windows-1254, the
//                Turkish code page AFAD exports and spreadsheets use. Never
//                Latin-1, which has no ğ, ı, İ, ş.
//
//   repairPlace  the USGS event service writes "?" for letters it cannot
//                encode in the place names of its older earthquakes
//                ("13 km N of Do?anyol, Turkey", "28 km SSW of ?mranl?"). The
//                letters are put back from a list of populated places
//                (GeoNames, data/PROVENANCE.md): the one place near the
//                earthquake whose name fits the pattern. A name that fits no
//                place, or more than one, is left as it is.
#pragma once

#include <QByteArray>
#include <QString>

namespace skg::text {

QString decode(const QByteArray &bytes);
// Windows-1254 (ISO 8859-9 plus the 0x80-0x9F block of Windows-1252).
QString fromWindows1254(const QByteArray &bytes);

// `place` as a catalog gives it, the earthquake's position. Returns the
// place with its letters restored, or unchanged.
QString repairPlace(const QString &place, double lat, double lon);
// Whether `place` holds a "?" that stands for a lost letter (one inside or
// at the edge of a word).
bool placeLooksDamaged(const QString &place);

} // namespace skg::text
