// Small CSV helpers shared by the catalog readers: a whole file (gzip or
// not), RFC 4180-ish line splitting, header lookup by alternative names,
// tolerant numbers (decimal comma accepted).
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <initializer_list>
#include <optional>

namespace skg::fx {

// The file's bytes, inflated when the name ends in .gz. Throws
// std::runtime_error.
QByteArray readMaybeGz(const QString &path);
QStringList splitCsv(const QString &line);
// Lines of a UTF-8 text, CR stripped.
QStringList lines(const QByteArray &text);
// Index of the first header matching one of `names` (case-insensitive, BOM
// tolerated), or -1.
int column(const QStringList &headers,
           std::initializer_list<const char *> names);
std::optional<double> number(const QStringList &row, int idx);

// An ISO 8601 date, or date and time, as seconds since 1970 UTC:
// "2023-02-06", "2023-02-06T01:17:34", "2023-02-06 01:17:34.25", with "Z" or
// an offset ("+03:00") when it has one. A text WITHOUT a zone is UTC,
// whatever the time zone of the machine: catalogs and tables are written in
// UTC, and reading them as local time would move every event by hours on a
// machine set to another zone (and by one more hour inside a daylight-saving
// gap). Nothing in the application reads such a text as local time.
std::optional<double> parseIsoUtc(const QString &text);
// The same instant as a QDateTime in UTC, to the millisecond; invalid when
// the text is not such a date.
QDateTime isoUtcDateTime(const QString &text);

} // namespace skg::fx
