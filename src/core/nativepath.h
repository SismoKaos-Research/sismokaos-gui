// File names for the C libraries the application hands paths to.
//
// Qt's own file classes take a QString and do the right thing everywhere.
// The vendored C libraries take a `const char *`, and what those bytes must
// be differs:
//
//   - miniz (zip archives) opens files itself. On Windows it decodes the
//     name as UTF-8 and calls the wide-character API; elsewhere it passes the
//     bytes to fopen(). So it wants UTF-8 on Windows and the file system's
//     encoding elsewhere. The local 8-bit code page is wrong on Windows: a
//     path with "ğ" or "İ" in it is then not valid UTF-8 and nothing opens.
//   - libsvm calls plain fopen(), which on Windows takes the ANSI code page
//     and cannot name every file. For such a library the path is made plain
//     ASCII where the system can do it (the short 8.3 name).
//
// Windows also refuses plain paths longer than MAX_PATH from these APIs; a
// path that long is given in its extended form (\\?\C:\...), which the
// wide-character API accepts.
//
// Header only, QtCore only: every library of the application may include it.
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#ifdef Q_OS_WIN
// Declared here instead of including <windows.h>: that header defines macros
// (min, max, near, far, small, ERROR, ...) that would reach into every file
// that includes this one. The declaration is the documented one.
extern "C" __declspec(dllimport) unsigned long __stdcall
GetShortPathNameW(const wchar_t *longPath, wchar_t *shortPath,
                  unsigned long shortPathLength);
#endif

namespace skg {

// The extended form of an absolute Windows path with backslashes:
// C:\a\b -> \\?\C:\a\b and \\server\share\a -> \\?\UNC\server\share\a.
// A path that already has the prefix, or is not absolute, is returned as is.
inline QString windowsExtendedPath(const QString &nativeAbsolute) {
  if (nativeAbsolute.startsWith(QLatin1String("\\\\?\\")) ||
      nativeAbsolute.startsWith(QLatin1String("\\\\.\\")))
    return nativeAbsolute;
  if (nativeAbsolute.startsWith(QLatin1String("\\\\")))
    return QStringLiteral("\\\\?\\UNC\\") + nativeAbsolute.mid(2);
  if (nativeAbsolute.size() >= 3 && nativeAbsolute[1] == QLatin1Char(':') &&
      nativeAbsolute[2] == QLatin1Char('\\'))
    return QStringLiteral("\\\\?\\") + nativeAbsolute;
  return nativeAbsolute;
}

// From this length on a plain path is refused by the Windows file APIs (260
// with the terminator; folders give out at 248).
inline constexpr int kWindowsPlainPathLimit = 248;

// What miniz must be given to open `path`. `windows` says which rule
// applies; it is a parameter so the Windows rule can be tested anywhere.
inline QByteArray minizPathFor(const QString &path, bool windows) {
  if (!windows)
    return QFile::encodeName(path);
  QString native = QDir::cleanPath(path);
  native.replace(QLatin1Char('/'), QLatin1Char('\\'));
  if (native.size() >= kWindowsPlainPathLimit)
    native = windowsExtendedPath(native);
  return native.toUtf8();
}

inline QByteArray minizPath(const QString &path) {
#ifdef Q_OS_WIN
  // Absolute, so that a long path can take its extended form.
  return minizPathFor(QFileInfo(path).absoluteFilePath(), true);
#else
  return minizPathFor(path, false);
#endif
}

inline bool isPlainAscii(const QString &text) {
  for (const QChar c : text)
    if (c.unicode() > 0x7e || c.unicode() < 0x20)
      return false;
  return true;
}

// What a library that calls fopen() must be given to open the EXISTING file
// or folder `path`. On Windows: the path itself when it is plain ASCII, else
// its short (8.3) form when the volume has one, else the ANSI bytes, which
// work as long as the code page has every character. `ok`, when given, is
// set to false in the one case that cannot work: no short name and
// characters the code page lacks.
inline QByteArray cLibraryPath(const QString &path, bool *ok = nullptr) {
  if (ok)
    *ok = true;
#ifdef Q_OS_WIN
  const QString native =
      QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  if (isPlainAscii(native) && native.size() < kWindowsPlainPathLimit)
    return native.toLatin1();
  const QString asked = windowsExtendedPath(native);
  const unsigned long need = ::GetShortPathNameW(
      reinterpret_cast<const wchar_t *>(asked.utf16()), nullptr, 0);
  if (need > 0) {
    QString shortName(int(need), QChar(0));
    const unsigned long got = ::GetShortPathNameW(
        reinterpret_cast<const wchar_t *>(asked.utf16()),
        reinterpret_cast<wchar_t *>(shortName.data()), need);
    if (got > 0 && got < need) {
      shortName.truncate(int(got));
      if (shortName.startsWith(QLatin1String("\\\\?\\UNC\\")))
        shortName = QStringLiteral("\\\\") + shortName.mid(8);
      else if (shortName.startsWith(QLatin1String("\\\\?\\")))
        shortName = shortName.mid(4);
      if (isPlainAscii(shortName) &&
          shortName.size() < kWindowsPlainPathLimit)
        return shortName.toLatin1();
    }
  }
  const QByteArray local = native.toLocal8Bit();
  if (ok)
    *ok = QString::fromLocal8Bit(local) == native &&
          native.size() < kWindowsPlainPathLimit;
  return local;
#else
  return QFile::encodeName(path);
#endif
}

} // namespace skg
