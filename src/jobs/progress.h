// Progress parsers read the lines a tool prints and turn them into a
// fraction and a short text. One parser per tool family; a new tool with its
// own output format adds a subclass here and names it in its ToolDesc.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace skg {

struct ProgressState {
  double fraction = -1; // [0, 1], or -1 while unknown
  QString text;         // "features 3/17", "split", ...
  QString stage;
};

class ProgressParser {
public:
  virtual ~ProgressParser() = default;
  // Returns true when the line carried progress. `hide` set to true means the
  // line is machine output and should not appear in the log.
  virtual bool parse(const QString &line, ProgressState &st, bool &hide) = 0;
};

// "[3/17] ..." at the start of a line (Sismokaos-featureExtract and most
// hand-written loops).
class CounterParser : public ProgressParser {
public:
  bool parse(const QString &line, ProgressState &st, bool &hide) override;
};

// sfc --progress json: "@sfc {"event": ..., "stage": ..., "done": 3, "total":
// 17}"
class SfcJsonParser : public ProgressParser {
public:
  bool parse(const QString &line, ProgressState &st, bool &hide) override;

private:
  bool event(const QJsonObject &ev, ProgressState &st);
};

// Tries each parser in turn; the first that understands the line wins.
class ChainParser : public ProgressParser {
public:
  void add(std::unique_ptr<ProgressParser> p) {
    parsers_.push_back(std::move(p));
  }
  bool parse(const QString &line, ProgressState &st, bool &hide) override;

private:
  std::vector<std::unique_ptr<ProgressParser>> parsers_;
};

// Parser by name, as a ToolDesc names it: "sfc-json", "counter", "" (default
// chain of all known parsers). Unknown names give the default chain.
std::unique_ptr<ProgressParser> makeProgressParser(const QString &name);

} // namespace skg
