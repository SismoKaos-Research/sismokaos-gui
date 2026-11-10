#include "modules/study/coveragedialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QThread>
#include <QVBoxLayout>

#include <exception>
#include <memory>

#include "core/theme.h"
#include "core/ui.h"
#include "study/store.h"
#include "study/waveindex.h"
#include "study/catalogs.h"

namespace skg {

QList<CoverageStation> stationsWithWaveforms(const study::Study &s,
                                             QStringList *without) {
  std::vector<analysis::Station> known;
  try {
    known = s.stations();
  } catch (const std::exception &) {
    known.clear();
  }
  QList<CoverageStation> out;
  for (const study::WaveStation &w :
       study::WaveIndex::shared(s.dir)->stations()) {
    bool found = false;
    for (const analysis::Station &k : known)
      if (k.code.compare(w.code, Qt::CaseInsensitive) == 0) {
        out << CoverageStation{w.code, k.lat, k.lon};
        found = true;
        break;
      }
    if (!found && without)
      *without << w.code;
  }
  return out;
}

QList<CoverageStation> stationsOfCases(const study::Study &s) {
  QList<CoverageStation> out;
  QStringList seen;
  for (const study::Case &c : s.cases)
    for (const study::StationRef &st : c.stations)
      if (!seen.contains(st.code)) {
        seen << st.code;
        out << CoverageStation{st.code, st.lat, st.lon};
      }
  return out;
}

std::vector<analysis::Event> coverageEvents(const study::Study &s,
                                            QString *source) {
  if (!s.catalog.isEmpty() && QFileInfo::exists(s.catalogPath())) {
    try {
      std::vector<analysis::Event> ev =
          analysis::loadEvents(s.catalogPath(), s.catalogUtcOffsetHours);
      if (source)
        *source = CoverageDialog::tr("the study's catalog");
      return ev;
    } catch (const std::exception &) {
      // Fall through to the bundled list.
    }
  }
  if (source)
    *source = CoverageDialog::tr("the built-in list of M 5+ earthquakes, USGS");
  return study::builtInEvents();
}

CoverageDialog::CoverageDialog(AppContext ctx, QWidget *parent,
                               const QStringList &chosen)
    : QDialog(parent), ctx_(ctx), chosen_(chosen) {
  setWindowTitle(tr("What the stations cover"));
  whichLabel_ = new QLabel(tr("Stations"), this);
  whichLabel_->setObjectName(QStringLiteral("fieldLabel"));
  which_ = new QComboBox(this);
  which_->setObjectName(QStringLiteral("coverageWhich"));
  which_->addItem(tr("With waveforms in the study's folders"));
  which_->addItem(tr("Ticked in the cases"));
  if (!chosen_.isEmpty())
    which_->addItem(tr("Selected in the Waveforms dialog (%n)", nullptr,
                       int(chosen_.size())));
  whichLabel_->setBuddy(which_);
  ui::setHelp(which_,
              tr("Which stations the area is drawn around: the ones the "
                 "study's waveform folders hold, or the ones ticked in its "
                 "cases whether their waveforms are there yet or not."));
  note_ = new QLabel(this);
  note_->setObjectName(QStringLiteral("fieldLabel"));
  note_->setWordWrap(true);
  coverage_ = new CoverageMap(ctx_.theme, this);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  auto *top = new QHBoxLayout;
  top->addWidget(whichLabel_);
  top->addWidget(which_, 1);
  auto *layout = new QVBoxLayout(this);
  layout->addLayout(top);
  layout->addWidget(note_);
  layout->addWidget(coverage_, 1);
  layout->addWidget(buttons);
  resize(900, 720);
  ui::polishPage(this);

  if (ctx_.study && ctx_.study->isOpen()) {
    const study::Study s = ctx_.study->study();
    // Start on the stations asked for; else on those there is data of;
    // before any, on the cases'.
    if (!chosen_.isEmpty())
      which_->setCurrentIndex(2);
    else if (stationsWithWaveforms(s).isEmpty() && !stationsOfCases(s).isEmpty())
      which_->setCurrentIndex(1);
    // The catalog can be large: read it on a worker thread and count when
    // it is in. The map shows the stations and their area meanwhile.
    loading_ = true;
    coverage_->setEvents({}, tr("reading the catalog…"));
    struct Loaded {
      std::vector<analysis::Event> events;
      QString source;
    };
    auto loaded = std::make_shared<Loaded>();
    QThread *worker = QThread::create(
        [s, loaded] { loaded->events = coverageEvents(s, &loaded->source); });
    connect(worker, &QThread::finished, this, [this, loaded] {
      events_ = std::move(loaded->events);
      source_ = loaded->source;
      loading_ = false;
      coverage_->setEvents(events_, source_);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
  }
  connect(which_, &QComboBox::currentIndexChanged, this, &CoverageDialog::fill);
  fill();
}

void CoverageDialog::fill() {
  if (!ctx_.study || !ctx_.study->isOpen()) {
    coverage_->setStations({});
    note_->setText(tr("No study is open."));
    return;
  }
  const study::Study &s = ctx_.study->study();
  QStringList without;
  const bool indexed = which_->currentIndex() == 0;
  QList<CoverageStation> stations;
  if (which_->currentIndex() == 2) {
    // The rows selected in the Waveforms dialog, where they have
    // coordinates.
    QStringList none;
    const QList<CoverageStation> all = stationsWithWaveforms(s, &none);
    for (const QString &code : chosen_) {
      bool found = false;
      for (const CoverageStation &st : all)
        if (st.code.compare(code, Qt::CaseInsensitive) == 0) {
          stations << st;
          found = true;
        }
      if (!found)
        without << code;
    }
  } else {
    stations = indexed ? stationsWithWaveforms(s, &without) : stationsOfCases(s);
  }
  coverage_->setStations(stations);
  QStringList notes;
  if (stations.isEmpty())
    notes << (which_->currentIndex() == 2
                  ? tr("None of the selected stations has known coordinates.")
              : indexed
                  ? tr("The study's waveform folders hold no station with "
                       "known coordinates yet. Add a folder under Waveforms…, "
                       "or look at the stations ticked in the cases.")
                  : tr("No station is ticked in any case yet."));
  if (!without.isEmpty())
    notes << tr("Not drawn, no coordinates: %1. Give them under Waveforms….")
                 .arg(without.join(QStringLiteral(", ")));
  note_->setText(notes.join(QLatin1Char('\n')));
  note_->setVisible(!notes.isEmpty());
}

} // namespace skg
