// sfc run: the steps in their fixed order, each writing what the Python
// tool writes under <project>/results/<station>/<event>/N/models/<run>/:
// data understanding, the split, its figures, the model families, the
// error timeline of the best model, the comparison; and the HTML report.
#pragma once

#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

#include "sfc/dataset.h"
#include "sfc/settings.h"
#include "sfc/tuning.h"

namespace skg::sfc {

struct RunHooks {
  LogFn log; // every line of the reports, as sfc prints them
  StopFn shouldStop;
  std::function<void(int done, int total, const QString &what)> progress;
};

struct StepOutcome {
  QString step, status; // "OK" | "FAILED" | "SKIPPED"
  double seconds = 0;
  QString error;
};

const QStringList &allSteps(); // sfc's RUN_STEPS in order

// Runs the steps (default: the settings' run_steps) on a prepared dataset
// (classes merged, NaN dropped). Writes settings.toml into the run folder.
std::vector<StepOutcome> runSteps(const QString &projectDir, const Settings &s,
                                  const Dataset &d, const QStringList &steps,
                                  const RunHooks &hooks);

// The steps one by one, for tests and the GUI.
void stepDataUnderstanding(const QString &runDir, const Settings &s,
                           const Dataset &d, const LogFn &log);
void stepSplit(const QString &runDir, const Settings &s, const Dataset &d,
               const LogFn &log);
void stepSplitVisualization(const QString &runDir, const Settings &s,
                            const Dataset &d, const LogFn &log,
                            const StopFn &stop);
// family: "baseline" (logistic + SVMs), "ann", "trees"
void stepModels(const QString &projectDir, const Settings &s, const Dataset &d,
                const QString &family, const RunHooks &hooks);
void stepErrorTimeline(const QString &projectDir, const Settings &s,
                       const Dataset &d, const LogFn &log);
void stepCompare(const QString &projectDir, const Settings &s,
                 const LogFn &log);

// One self-contained HTML page of the runs (default: all). Returns the path.
QString writeHtmlReport(const QString &projectDir, const Settings &s,
                        const QStringList &runs = {});

} // namespace skg::sfc
