#include "jobs/progress.h"

#include <QJsonDocument>
#include <QRegularExpression>

namespace skg {

namespace {
const QRegularExpression kCounter(QStringLiteral(R"(^\s*\[(\d+)/(\d+)\])"));
const QString kSfcPrefix = QStringLiteral("@sfc ");
} // namespace

bool CounterParser::parse(const QString &line, ProgressState &st, bool &hide) {
  hide = false;
  const auto m = kCounter.match(line);
  if (!m.hasMatch())
    return false;
  const double i = m.captured(1).toDouble();
  const double n = m.captured(2).toDouble();
  if (n <= 0)
    return false;
  st.fraction = i / n;
  st.text = QStringLiteral("%1/%2").arg(qint64(i)).arg(qint64(n));
  return true;
}

bool SfcJsonParser::parse(const QString &line, ProgressState &st, bool &hide) {
  hide = false;
  if (!line.startsWith(kSfcPrefix))
    return false;
  hide = true; // machine line, whether or not we understand it
  const QJsonDocument doc =
      QJsonDocument::fromJson(line.mid(kSfcPrefix.size()).toUtf8());
  return doc.isObject() && event(doc.object(), st);
}

bool SfcJsonParser::event(const QJsonObject &ev, ProgressState &st) {
  const QString name = ev.value(QLatin1String("event")).toString();
  if (name.isEmpty())
    return false;
  if (ev.contains(QLatin1String("stage")))
    st.stage = ev.value(QLatin1String("stage")).toString();
  if (ev.contains(QLatin1String("done")) &&
      ev.contains(QLatin1String("total"))) {
    const double done = ev.value(QLatin1String("done")).toDouble();
    const double total = ev.value(QLatin1String("total")).toDouble();
    if (total > 0) {
      st.fraction = done / total;
      st.text = QStringLiteral("%1 %2/%3")
                    .arg(st.stage.isEmpty() ? name : st.stage)
                    .arg(qint64(done))
                    .arg(qint64(total));
      return true;
    }
  }
  if (name == QLatin1String("step_start")) {
    st.text = ev.value(QLatin1String("step")).toString(st.stage);
    return true;
  }
  if (name == QLatin1String("stage")) {
    st.text = st.stage;
    return true;
  }
  if (name.endsWith(QLatin1String("_done"))) {
    st.fraction = 1.0;
    st.text = name;
    return true;
  }
  return true;
}

bool ChainParser::parse(const QString &line, ProgressState &st, bool &hide) {
  hide = false;
  for (auto &p : parsers_) {
    bool h = false;
    if (p->parse(line, st, h)) {
      hide = h;
      return true;
    }
    if (h) { // understood as machine output, carried no progress
      hide = true;
      return false;
    }
  }
  return false;
}

std::unique_ptr<ProgressParser> makeProgressParser(const QString &name) {
  if (name == QLatin1String("sfc-json"))
    return std::make_unique<SfcJsonParser>();
  if (name == QLatin1String("counter"))
    return std::make_unique<CounterParser>();
  auto chain = std::make_unique<ChainParser>();
  chain->add(std::make_unique<SfcJsonParser>());
  chain->add(std::make_unique<CounterParser>());
  return chain;
}

} // namespace skg
