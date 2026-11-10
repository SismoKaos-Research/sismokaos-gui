#include "text/text.h"

#include <QFile>
#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <cmath>
#include <mutex>

static void initTextResources() { Q_INIT_RESOURCE(text); }

namespace skg::text {

namespace {

struct Place {
  QString name;
  double lat = 0, lon = 0;
};

// The places by the length of their names: a pattern only fits names as
// long as itself.
const QHash<int, QList<Place>> &placesByLength() {
  static QHash<int, QList<Place>> places;
  static std::once_flag once;
  std::call_once(once, [] {
    initTextResources();
    QFile f(QStringLiteral(":/text/places.tsv"));
    if (!f.open(QIODevice::ReadOnly))
      return;
    for (const QByteArray &raw : f.readAll().split('\n')) {
      const QList<QByteArray> cols = raw.split('\t');
      if (cols.size() < 3)
        continue;
      Place p;
      p.name = QString::fromUtf8(cols[0]);
      p.lat = cols[1].toDouble();
      p.lon = cols[2].toDouble();
      places[int(p.name.size())].append(p);
    }
  });
  return places;
}

double distanceKm(double lat1, double lon1, double lat2, double lon2) {
  const double rad = 3.14159265358979323846 / 180.0;
  const double dLat = (lat2 - lat1) * rad, dLon = (lon2 - lon1) * rad;
  const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(lat1 * rad) * std::cos(lat2 * rad) *
                       std::sin(dLon / 2) * std::sin(dLon / 2);
  return 6371.0 * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

// "?" stands for one letter outside ASCII; every other character must be
// the same.
bool fits(const QString &pattern, const QString &name) {
  if (pattern.size() != name.size())
    return false;
  for (int i = 0; i < pattern.size(); ++i) {
    if (pattern[i] == QLatin1Char('?')) {
      if (name[i].unicode() < 128)
        return false;
    } else if (pattern[i] != name[i]) {
      return false;
    }
  }
  return true;
}

// The one name that fits `pattern` among the places about `expectKm` from
// the point (within `tolKm`); empty when none or several do.
QString uniqueFit(const QString &pattern, double lat, double lon,
                  double expectKm, double tolKm) {
  QSet<QString> names;
  for (const Place &p : placesByLength().value(int(pattern.size()))) {
    if (!fits(pattern, p.name))
      continue;
    if (std::abs(distanceKm(lat, lon, p.lat, p.lon) - expectKm) > tolKm)
      continue;
    names.insert(p.name);
    if (names.size() > 1)
      return {};
  }
  return names.size() == 1 ? *names.constBegin() : QString();
}

} // namespace

QString fromWindows1254(const QByteArray &bytes) {
  // 0x80-0x9F as Windows-1252 has them (0 where the code page has nothing).
  static const char16_t high[32] = {
      0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0,      0,
      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0,      0x0178};
  QString out;
  out.reserve(bytes.size());
  for (const char ch : bytes) {
    const auto b = static_cast<unsigned char>(ch);
    char16_t u = b;
    if (b >= 0x80 && b <= 0x9F)
      u = high[b - 0x80] ? high[b - 0x80] : char16_t(0xFFFD);
    else if (b == 0xD0)
      u = 0x011E; // Ğ
    else if (b == 0xDD)
      u = 0x0130; // İ
    else if (b == 0xDE)
      u = 0x015E; // Ş
    else if (b == 0xF0)
      u = 0x011F; // ğ
    else if (b == 0xFD)
      u = 0x0131; // ı
    else if (b == 0xFE)
      u = 0x015F; // ş
    out.append(QChar(u));
  }
  return out;
}

QString decode(const QByteArray &bytes) {
  QByteArray body = bytes;
  if (body.startsWith("\xEF\xBB\xBF"))
    body.remove(0, 3);
  QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
  const QString text = utf8.decode(body);
  if (!utf8.hasError())
    return text;
  return fromWindows1254(body);
}

bool placeLooksDamaged(const QString &place) {
  // A "?" touching a letter; a question mark after a space or at the end of
  // a sentence is somebody's punctuation.
  static const QRegularExpression re(
      QStringLiteral("\\p{L}\\?|\\?\\p{L}"),
      QRegularExpression::UseUnicodePropertiesOption);
  return place.contains(re);
}

QString repairPlace(const QString &place, double lat, double lon) {
  if (!placeLooksDamaged(place))
    return place;
  // "13 km N of Do?anyol, Turkey": the place name is what follows "of" in
  // its comma-separated part, and the distance says how far off it lies.
  // Other catalogs give the name alone; it is then looked for nearby.
  static const QRegularExpression lead(
      QStringLiteral("^\\s*(\\d+(?:\\.\\d+)?)\\s*km\\s+[NSEW]{1,3}\\s+of\\s+"),
      QRegularExpression::CaseInsensitiveOption);
  QStringList parts = place.split(QLatin1Char(','));
  bool changed = false;
  for (QString &part : parts) {
    if (!part.contains(QLatin1Char('?')))
      continue;
    const QRegularExpressionMatch m = lead.match(part);
    const int from = m.hasMatch() ? int(m.capturedLength(0)) : 0;
    const double expect = m.hasMatch() ? m.captured(1).toDouble() : 0.0;
    const double tol = m.hasMatch() ? std::max(15.0, 0.3 * expect) : 60.0;
    // The name, without the blanks around it.
    int a = from, b = int(part.size());
    while (a < b && part[a].isSpace())
      ++a;
    while (b > a && part[b - 1].isSpace())
      --b;
    const QString pattern = part.mid(a, b - a);
    const QString name = uniqueFit(pattern, lat, lon, expect, tol);
    if (name.isEmpty())
      continue;
    part = part.left(a) + name + part.mid(b);
    changed = true;
  }
  return changed ? parts.join(QLatin1Char(',')) : place;
}

QString fold(const QString &text) {
  // Decomposed, so that an accented letter is its base letter followed by
  // combining marks, which are then dropped: İ is I + dot above, ğ is
  // g + breve, ş is s + cedilla. The dotless ı has no decomposition and is
  // named.
  const QString d = text.normalized(QString::NormalizationForm_KD);
  QString out;
  out.reserve(d.size());
  for (const QChar ch : d) {
    const QChar::Category cat = ch.category();
    if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining ||
        cat == QChar::Mark_Enclosing)
      continue;
    const char16_t u = ch.unicode();
    if (u == u'I' || u == 0x0131) { // I, ı
      out += QLatin1Char('i');
    } else if (u < 0x80) {
      // ASCII by hand: QChar::toLower is locale-free, and this says so.
      out += (u >= u'A' && u <= u'Z') ? QChar(char16_t(u + 32)) : ch;
    } else if (u == 0x00DF) { // ß
      out += QStringLiteral("ss");
    } else if (u == 0x00D8 || u == 0x00F8) { // Ø ø
      out += QLatin1Char('o');
    } else if (u == 0x0141 || u == 0x0142) { // Ł ł
      out += QLatin1Char('l');
    } else if (u == 0x0110 || u == 0x0111) { // Đ đ
      out += QLatin1Char('d');
    } else {
      out += ch.toLower();
    }
  }
  return out;
}

bool foldedContains(const QString &haystack, const QString &needle) {
  const QString n = fold(needle.trimmed());
  return n.isEmpty() || fold(haystack).contains(n);
}

} // namespace skg::text
