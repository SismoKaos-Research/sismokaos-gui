#include "modules/study/studymodule.h"

#include "modules/study/casespage.h"

namespace skg {

QWidget *CasesModule::createWidget(AppContext &ctx, QWidget *parent) {
  return new CasesPage(ctx, parent);
}

} // namespace skg
