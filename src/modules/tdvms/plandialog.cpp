#include "modules/tdvms/plandialog.h"

#include "core/ui.h"

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
  ok_ = buttons->button(QDialogButtonBox::Ok);
  ok_->setText(tr("Plan"));
  station_->setObjectName(QStringLiteral("planStation"));
  start_->setObjectName(QStringLiteral("planStart"));
  end_->setObjectName(QStringLiteral("planEnd"));
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

  ui::setHelp(station_,
              tr("The station to request. Type a code or part of one; the "
                 "list is the portal's station catalog with network and "
                 "instrument (Refresh catalogs updates it)."));
  ui::setHelp(start_, tr("The first day of the period to download."));
  ui::setHelp(end_, tr("The end of the period to download."));
  ui::setHelp(chunkDays_,
              tr("Length of one request (a chunk) in days. The period is "
                 "cut into windows of this length, one portal request "
                 "each."));
  ui::setHelp(everyDays_,
              tr("Spacing between the starts of two windows. \"Cover the "
                 "whole period\" puts the windows back to back; a larger "
                 "number downloads only a sample, one window every that "
                 "many days."));
  ui::setHelp(verify_,
              tr("Asks the portal whether the station code exists before "
                 "planning, and shows its network and instrument. A "
                 "strong-motion accelerometer is flagged."));
}

void PlanDialog::prefill(const QString &station, const QDate &from,
                         const QDate &to) {
  const int i = station_->findData(station);
  if (i >= 0)
    station_->setCurrentIndex(i);
  else
    station_->setEditText(station);
  if (from.isValid())
    start_->setDate(from);
  if (to.isValid())
    end_->setDate(to);
  updateHint();
}

QString PlanDialog::blockedReason() const {
  if (request().station.isEmpty())
    return tr("Choose the station to plan.");
  if (start_->date() >= end_->date())
    return tr("\"To\" must be later than \"From\".");
  return {};
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
  const QString why = blockedReason();
  if (ok_)
    ok_->setEnabled(why.isEmpty());
  if (!why.isEmpty()) {
    bits << why;
  } else {
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
