// Small CSV helpers shared by the catalog readers: a whole file (gzip or
// not), RFC 4180-ish line splitting, header lookup by alternative names,
// tolerant numbers (decimal comma accepted).
#pragma once

#include <QByteArray>
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

} // namespace skg::fx
