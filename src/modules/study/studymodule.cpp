#include "modules/study/studymodule.h"

#include "modules/study/casespage.h"
#include "modules/study/comparepage.h"
#include "modules/study/runspage.h"

namespace skg {

QWidget *CasesModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new CasesPage(ctx, parent);
}

QWidget *RunsModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new RunsPage(ctx, parent);
}

QWidget *CompareModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new ComparePage(ctx, parent);
}

} // namespace skg
