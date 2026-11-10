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
//
//   fold         a search key: "İzmir", "IZMIR", "ızmır" and "izmir" all
//                become "izmir".
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

// A key for searching and comparing names the way people type them: lower
// case, with the Turkish letters and other accents reduced to plain Latin
// ones (ğ→g, ş→s, ç→c, ö→o, ü→u, â→a), and all four of I, İ, ı, i becoming
// "i". The same on every machine: no locale is consulted, so "izmir" finds
// "İzmir" and "sirnak" finds "Şırnak" whatever language the system is in.
QString fold(const QString &text);
// Whether `haystack` contains `needle`, both folded. An empty needle fits.
bool foldedContains(const QString &haystack, const QString &needle);

} // namespace skg::text
