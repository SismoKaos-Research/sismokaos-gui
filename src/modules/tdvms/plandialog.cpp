#include "modules/tdvms/plandialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>

#include "tdvms/catalog.h"
#include "tdvms/history.h"
#include "tdvms/plan.h"
#include <QVBoxLayout>

namespace skg {

using namespace tdvms;

PlanDialog::PlanDialog(const QString &ledgerPath, QWidget *parent)
    : QDialog(parent), ledgerPath_(ledgerPath) {
  setWindowTitle(tr("Plan a station"));
  station_ = new QComboBox(this);
  station_->setEditable(true);
  station_->setInsertPolicy(QComboBox::NoInsert);
  for (const CatalogStation &s : loadStations())
    station_->addItem(
        QStringLiteral("%1   %2  %3").arg(s.code, -6).arg(s.network, s.devices),
        s.code);
  station_->completer()->setCompletionMode(QCompleter::PopupCompletion);
  station_->completer()->setFilterMode(Qt::MatchContains);
  station_->setCurrentIndex(-1);

  start_ = new QDateEdit(QDate::currentDate().addYears(-1), this);
  end_ = new QDateEdit(QDate::currentDate(), this);
  for (QDateEdit *d : {start_, end_}) {
    d->setCalendarPopup(true);
    d->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
  }
  chunkDays_ = new QSpinBox(this);
  chunkDays_->setRange(1, 365);
  chunkDays_->setValue(kChunkDays);
  chunkDays_->setSuffix(tr(" days"));
  everyDays_ = new QSpinBox(this);
  everyDays_->setRange(0, 365);
  everyDays_->setValue(0);
  everyDays_->setSpecialValueText(tr("cover the whole period"));
  everyDays_->setSuffix(tr(" days"));
  verify_ = new QCheckBox(tr("check that the portal lists the station"), this);
  verify_->setChecked(true);
  hint_ = new QLabel(this);
  hint_->setWordWrap(true);

  auto *form = new QFormLayout;
  form->addRow(tr("Station"), station_);
  form->addRow(tr("From"), start_);
  form->addRow(tr("To"), end_);
  form->addRow(tr("Window"), chunkDays_);
  form->addRow(tr("One window every"), everyDays_);
  form->addRow(QString(), verify_);
  form->addRow(QString(), hint_);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Plan"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto *layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);

  connect(station_, &QComboBox::currentTextChanged, this,
          &PlanDialog::updateHint);
  connect(everyDays_, &QSpinBox::valueChanged, this, [this](int v) {
    if (v > 0 && chunkDays_->value() == kChunkDays)
      chunkDays_->setValue(kSampleDays);
    updateHint();
  });
  connect(start_, &QDateEdit::dateChanged, this, &PlanDialog::updateHint);
  connect(end_, &QDateEdit::dateChanged, this, &PlanDialog::updateHint);
  connect(chunkDays_, &QSpinBox::valueChanged, this, &PlanDialog::updateHint);
  updateHint();
}

PlanRequest PlanDialog::request() const {
  PlanRequest r;
  const int i = station_->currentIndex();
  r.station = normaliseStation(
      i >= 0 && station_->itemText(i) == station_->currentText()
          ? station_->itemData(i).toString()
          : station_->currentText().section(' ', 0, 0));
  r.startIso = start_->date().toString(Qt::ISODate);
  r.endIso = end_->date().toString(Qt::ISODate);
  r.chunkDays = chunkDays_->value();
  r.everyDays = everyDays_->value();
  r.verify = verify_->isChecked();
  return r;
}

void PlanDialog::updateHint() {
  const PlanRequest r = request();
  QStringList bits;
  if (!r.station.isEmpty()) {
    try {
      const int n = planWindows(r.startIso, r.endIso, r.chunkDays,
                                r.everyDays > 0 ? r.everyDays : -1)
                        .size();
      bits << tr("%n window(s) for %1", nullptr, n).arg(r.station);
    } catch (const std::exception &e) {
      bits << QString::fromUtf8(e.what());
    }
    const auto hist = gatherHistory(campaignsAround(ledgerPath_));
    if (hist.contains(r.station)) {
      const QString w = hist[r.station].warning();
      if (!w.isEmpty())
        bits << tr("Earlier campaigns: %1. Planned anyway if you continue; "
                   "no-data is "
                   "about a period, not forever.")
                    .arg(w);
    }
  }
  hint_->setText(bits.join('\n'));
}

} // namespace skg
