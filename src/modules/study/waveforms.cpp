#include "modules/study/waveforms.h"
#include "modules/study/coveragedialog.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>

#include <exception>
#include <iterator>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

#include "core/ui.h"
#include "modules/study/studysetup.h"
#include "jobs/jobmodel.h"
#include "jobs/taskjob.h"
#include "study/store.h"

namespace skg {

using study::ScanPass;
using study::Study;
using study::WaveIndex;

// ---- the scan ----

WaveScan::WaveScan(AppContext ctx, QObject *parent)
    : QObject(parent), ctx_(ctx) {}

void WaveScan::start() {
  if (running() || !ctx_.study || !ctx_.study->isOpen() || !ctx_.jobs)
    return;
  if (ctx_.study->study().waveRoots.isEmpty()) {
    status_.clear();
    emit updated();
    return;
  }
  run(ScanPass::Identity);
}

void WaveScan::cancel() {
  if (job_)
    job_->cancel();
}

void WaveScan::run(ScanPass pass) {
  const Study study = ctx_.study->study();
  const bool identity = pass == ScanPass::Identity;
  auto *job = new TaskJob(
      identity ? tr("Waveforms: which stations") : tr("Waveforms: coverage"),
      QStringLiteral("waveforms"),
      identity ? tr("first record of every file in the waveform folders")
               : tr("every record header in the waveform folders"),
      [study, pass](TaskContext &t) -> int {
        WaveIndex index = WaveIndex::load(study.dir);
        // Folders the study no longer lists leave the index.
        for (auto it = index.roots.begin(); it != index.roots.end();)
          it = study.waveRootPath(it.key()).isEmpty() ? index.roots.erase(it)
                                                      : std::next(it);
        int failed = 0;
        bool stopped = false;
        for (const study::WaveRoot &root : study.waveRoots) {
          const QString path = study.resolved(root.path);
          t.log(QStringLiteral("%1: %2").arg(root.id, path));
          study::ScanHooks hooks;
          hooks.shouldStop = [&t] { return t.cancelled(); };
          hooks.progress = [&t](int done, int total, const QString &name) {
            t.progress(total > 0 ? double(done) / total : 0.0,
                       QStringLiteral("%1/%2 %3").arg(done).arg(total).arg(
                           name));
          };
          hooks.checkpoint = [&index, &study] { index.save(study.dir); };
          try {
            const study::ScanStats s =
                study::scanRoot(index, root.id, path, pass, hooks);
            t.log(QStringLiteral("  %1 files, %2 read now, %3 not miniSEED, "
                                 "%4 gone")
                      .arg(s.listed)
                      .arg(s.read)
                      .arg(s.skipped)
                      .arg(s.dropped));
            stopped = stopped || s.stopped;
          } catch (const std::exception &e) {
            t.log(QStringLiteral("  error: %1").arg(QString::fromUtf8(e.what())));
            ++failed;
          }
          if (stopped)
            break;
        }
        QString error;
        if (!index.save(study.dir, &error)) {
          t.log(QStringLiteral("error: %1").arg(error));
          return 1;
        }
        return failed > 0 || stopped ? 1 : 0;
      });
  Job *added = ctx_.jobs->add(job);
  job_ = added;
  status_ = identity ? tr("Looking for stations…") : tr("Reading coverage…");
  emit updated();
  connect(added, &Job::progressChanged, this, [this, added, identity] {
    const QString text = added->progress().text;
    status_ = (identity ? tr("Looking for stations: %1")
                        : tr("Reading coverage: %1"))
                  .arg(text.section(QLatin1Char(' '), 0, 0));
    emit updated();
  });
  connect(added, &Job::stateChanged, this, [this, added, pass] {
    const Job::State st = added->state();
    if (st == Job::State::Running || st == Job::State::Queued)
      return;
    job_.clear();
    if (st == Job::State::Done && pass == ScanPass::Identity) {
      emit updated(); // the stations are usable before the coverage is read
      run(ScanPass::Coverage);
      return;
    }
    status_ = st == Job::State::Done
                  ? QString()
                  : st == Job::State::Cancelled
                        ? tr("The scan was stopped; what it had read is kept.")
                        : tr("The scan ended with errors; see the Jobs panel.");
    emit updated();
  });
}

// ---- the dialog ----

namespace {

QString dayText(double epoch) {
  return QDateTime::fromSecsSinceEpoch(qint64(epoch), QTimeZone::UTC)
      .toString(QStringLiteral("yyyy-MM-dd"));
}

} // namespace

WaveformsDialog::WaveformsDialog(AppContext ctx, WaveScan *scan,
                                 QWidget *parent)
    : QDialog(parent), ctx_(ctx), scan_(scan) {
  setWindowTitle(tr("Waveforms of this study"));
  intro_ = new QLabel(
      tr("Folders holding the waveforms you already have. Any layout works: "
         "the station, the channels and the times are read from the miniSEED "
         "headers, plain files or zip archives, not from folder or file "
         "names. Other files are listed as skipped."),
      this);
  intro_->setWordWrap(true);
  intro_->setObjectName(QStringLiteral("fieldLabel"));

  folders_ = new QListWidget(this);
  folders_->setObjectName(QStringLiteral("waveFolders"));
  folders_->setMaximumHeight(96);
  setAcceptDrops(true);
  add_ = new QPushButton(tr("Add folders…"), this);
  ui::setPrimary(add_);
  repoint_ = new QPushButton(tr("Moved…"), this);
  remove_ = new QPushButton(tr("Remove"), this);
  rescan_ = new QPushButton(tr("Scan again"), this);
  stop_ = new QPushButton(tr("Stop"), this);
  auto *folderButtons = new QVBoxLayout;
  folderButtons->addWidget(add_);
  folderButtons->addWidget(repoint_);
  folderButtons->addWidget(remove_);
  folderButtons->addStretch(1);
  auto *folderRow = new QHBoxLayout;
  folderRow->addWidget(folders_, 1);
  folderRow->addLayout(folderButtons);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("fieldLabel"));
  auto *scanRow = new QHBoxLayout;
  scanRow->addWidget(status_, 1);
  scanRow->addWidget(stop_);
  scanRow->addWidget(rescan_);

  table_ = new QTableWidget(this);
  table_->setObjectName(QStringLiteral("waveStations"));
  table_->setColumnCount(10);
  table_->setHorizontalHeaderLabels(
      {tr("Station"), tr("Network"), tr("Channels"), tr("Rate"),
       tr("First day"), tr("Last day"), tr("Days with data"), tr("Files"),
       tr("Coordinates"), tr("Notes")});
  table_->verticalHeader()->hide();
  table_->setShowGrid(false);
  table_->setAlternatingRowColors(true);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setStretchLastSection(true);
  table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft |
                                                  Qt::AlignVCenter);
  ui::setEmptyHint(table_, tr("No stations yet.\nAdd the folder that holds "
                              "your miniSEED files, or drop it here."));

  problem_ = new QLabel(this);
  problem_->setObjectName(QStringLiteral("fieldLabel"));
  problem_->setWordWrap(true);
  coords_ = new QPushButton(tr("Type coordinates…"), this);
  xml_ = new QPushButton(tr("Load StationXML…"), this);
  skipped_ = new QPushButton(this);
  near_ = new QPushButton(tr("Find earthquakes near these…"), this);
  near_->setObjectName(QStringLiteral("waveFindNear"));
  auto *below = new QHBoxLayout;
  below->addWidget(coords_);
  below->addWidget(xml_);
  below->addWidget(near_);
  coverage_ = new QPushButton(tr("Station coverage…"), this);
  coverage_->setObjectName(QStringLiteral("waveCoverage"));
  coverage_->setToolTip(ui::helpText(
      tr("A map of what the selected stations cover (all of them when none "
         "is selected): the area within a distance of them and the "
         "earthquakes of the catalog that fall inside it.")));
  below->addWidget(coverage_);
  below->addStretch(1);
  below->addWidget(skipped_);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);

  auto *layout = new QVBoxLayout(this);
  layout->addWidget(intro_);
  layout->addLayout(folderRow);
  layout->addLayout(scanRow);
  layout->addWidget(table_, 1);
  layout->addWidget(problem_);
  layout->addLayout(below);
  layout->addWidget(buttons);
  resize(980, 640);

  folders_->setToolTip(ui::helpText(
      tr("The waveform folders of this study. Each is scanned through all "
         "its sub-folders, except those whose name starts with _ or a dot.")));
  add_->setToolTip(ui::helpText(
      tr("Adds one or several folders of miniSEED files (plain or zipped) "
         "and scans them. Hold Ctrl or Shift to pick more than one. A folder "
         "of station folders (stations/CODE/…) is added once, as a whole; "
         "folders can also be dropped on this window.")));
  near_->setToolTip(ui::helpText(
      tr("Goes on to adding a case, listing only the earthquakes close to "
         "the stations selected in the table (all stations when none is "
         "selected).")));
  repoint_->setToolTip(ui::helpText(
      tr("Tells the study that the selected folder now lives somewhere else, "
         "for example on another drive. Files that did not change are not "
         "read again.")));
  remove_->setToolTip(ui::helpText(
      tr("Takes the selected folder out of the study. Nothing on disk is "
         "deleted.")));
  rescan_->setToolTip(ui::helpText(
      tr("Looks at the folders again. Only new or changed files are read.")));
  stop_->setToolTip(ui::helpText(
      tr("Stops the scan. What it has read so far is kept.")));
  table_->setToolTip(ui::helpText(
      tr("One row per station found in the files. Days with data is exact "
         "once the coverage has been read; before that it shows a dash.")));
  coords_->setToolTip(ui::helpText(
      tr("For a station the station list does not know: type its latitude "
         "and longitude once. They are kept in the study and used for "
         "distances and for the Features calibration.")));
  xml_->setToolTip(ui::helpText(
      tr("Reads station coordinates from a StationXML file (as FDSN data "
         "centres provide) for the stations the station list does not "
         "know.")));
  skipped_->setToolTip(ui::helpText(
      tr("Lists the files that were not recognised as miniSEED, with the "
         "reason.")));

  connect(add_, &QPushButton::clicked, this, &WaveformsDialog::addFolder);
  connect(near_, &QPushButton::clicked, this, [this] {
    QStringList codes;
    const bool any = !table_->selectedItems().isEmpty();
    for (int row = 0; row < table_->rowCount(); ++row) {
      const QTableWidgetItem *it = table_->item(row, 0);
      if (it && (!any || it->isSelected()) && !codes.contains(it->text()))
        codes << it->text();
    }
    findNear_ = codes;
    if (!codes.isEmpty())
      accept();
  });
  connect(coverage_, &QPushButton::clicked, this, [this] {
    QStringList codes;
    const bool any = !table_->selectedItems().isEmpty();
    for (int row = 0; row < table_->rowCount(); ++row) {
      const QTableWidgetItem *it = table_->item(row, 0);
      if (it && (!any || it->isSelected()) && !codes.contains(it->text()))
        codes << it->text();
    }
    CoverageDialog dialog(ctx_, this, codes);
    dialog.exec();
  });
  connect(repoint_, &QPushButton::clicked, this, &WaveformsDialog::repoint);
  connect(remove_, &QPushButton::clicked, this, &WaveformsDialog::removeFolder);
  connect(rescan_, &QPushButton::clicked, scan_, &WaveScan::start);
  connect(stop_, &QPushButton::clicked, scan_, &WaveScan::cancel);
  connect(coords_, &QPushButton::clicked, this,
          &WaveformsDialog::typeCoordinates);
  connect(xml_, &QPushButton::clicked, this, &WaveformsDialog::loadStationXml);
  connect(skipped_, &QPushButton::clicked, this, &WaveformsDialog::showSkipped);
  connect(folders_, &QListWidget::itemSelectionChanged, this,
          &WaveformsDialog::refresh);
  connect(table_, &QTableWidget::itemSelectionChanged, this, [this] {
    coords_->setEnabled(!table_->selectedItems().isEmpty());
  });
  connect(scan_, &WaveScan::updated, this, &WaveformsDialog::refresh);
  connect(ctx_.study, &study::StudyStore::changed, this,
          &WaveformsDialog::refresh);
  refresh();
}

bool WaveformsDialog::edit(const std::function<void(Study &)> &change) {
  QString error;
  if (ctx_.study->update(change, &error))
    return true;
  problem_->setProperty("auto", false);
  problem_->setText(tr("The study could not be saved: %1").arg(error));
  return false;
}

void WaveformsDialog::refresh() {
  if (!ctx_.study->isOpen())
    return;
  const Study &s = ctx_.study->study();
  const QString keepFolder =
      folders_->currentItem()
          ? folders_->currentItem()->data(Qt::UserRole).toString()
          : QString();
  {
    const QSignalBlocker block(folders_);
    folders_->clear();
    for (const study::WaveRoot &r : s.waveRoots) {
      const QString path = s.resolved(r.path);
      auto *item = new QListWidgetItem(
          QDir(path).exists() ? path : tr("%1  (not found)").arg(path),
          folders_);
      item->setData(Qt::UserRole, r.id);
      if (r.id == keepFolder)
        folders_->setCurrentItem(item);
    }
  }
  const bool selected = folders_->currentItem() != nullptr;
  repoint_->setEnabled(selected && !scan_->running());
  remove_->setEnabled(selected && !scan_->running());
  rescan_->setEnabled(!scan_->running() && !s.waveRoots.isEmpty());
  stop_->setVisible(scan_->running());

  const auto index = WaveIndex::shared(s.dir);
  const QString path = s.stationListPath();
  if (path != listedFrom_) {
    listedFrom_ = path;
    try {
      listed_ = analysis::loadStations(path);
    } catch (const std::exception &) {
      listed_.clear();
    }
  }
  const auto inList = [this](const QString &code) {
    for (const analysis::Station &st : listed_)
      if (st.code.compare(code, Qt::CaseInsensitive) == 0)
        return true;
    return false;
  };
  const QString keepStation =
      table_->currentRow() >= 0 && table_->item(table_->currentRow(), 0)
          ? table_->item(table_->currentRow(), 0)->text()
          : QString();
  const QList<study::WaveStation> stations = index->stations();
  int unknown = 0;
  {
    const QSignalBlocker block(table_);
    table_->setRowCount(int(stations.size()));
    for (int i = 0; i < int(stations.size()); ++i) {
      const study::WaveStation &w = stations[i];
      QStringList rates;
      for (double r : w.rates)
        rates << tr("%1 Hz").arg(r);
      QString coords = tr("unknown");
      bool known = true;
      if (inList(w.code)) {
        coords = tr("station list");
      } else {
        known = false;
        for (const study::ExtraStation &e : s.extraStations)
          if (e.code.compare(w.code, Qt::CaseInsensitive) == 0) {
            coords = e.source == QLatin1String("stationxml") ? tr("StationXML")
                                                              : tr("typed");
            known = true;
          }
      }
      QStringList notes;
      if (!known) {
        ++unknown;
        notes << tr("⚠ no coordinates");
      }
      if (w.mixedRates)
        notes << tr("⚠ mixed sample rates");
      if (!w.otherFolders.isEmpty())
        notes << tr("⚠ in a folder named %1")
                     .arg(w.otherFolders.join(QStringLiteral(", ")));
      if (w.sharedFiles)
        notes << tr("shares files with other stations");
      const QStringList cells = {
          w.code,
          w.network,
          w.channels.join(QStringLiteral(" ")),
          rates.join(QStringLiteral(", ")),
          dayText(w.first),
          w.coverageKnown ? dayText(w.last - 1e-3)
                          : tr("≥ %1").arg(dayText(w.last)),
          w.coverageKnown ? QString::number(w.days) : QStringLiteral("–"),
          QString::number(w.files),
          coords,
          notes.join(QStringLiteral(" · "))};
      for (int col = 0; col < int(cells.size()); ++col)
        table_->setItem(i, col, new QTableWidgetItem(cells[col]));
      // The notes can be longer than the column is wide.
      table_->item(i, 9)->setToolTip(notes.join(QLatin1Char('\n')));
      if (w.code == keepStation)
        table_->selectRow(i);
    }
    table_->resizeColumnsToContents();
  }
  coords_->setEnabled(!table_->selectedItems().isEmpty());
  const int nSkipped = int(index->skipped().size());
  skipped_->setText(tr("Skipped files (%1)…").arg(nSkipped));
  skipped_->setEnabled(nSkipped > 0);
  const int pending = index->pendingCoverage();
  QString status = scan_->status();
  if (status.isEmpty() && !index->isEmpty())
    status = pending > 0
                 ? tr("%1 stations in %2 files; coverage not read yet for %3 "
                      "of them.")
                       .arg(stations.size())
                       .arg(index->fileCount())
                       .arg(pending)
                 : tr("%1 stations in %2 files; coverage read for all.")
                       .arg(stations.size())
                       .arg(index->fileCount());
  status_->setText(status);
  if (unknown > 0) {
    problem_->setProperty("auto", true);
    problem_->setText(
        tr("⚠ %n station(s) are not in the station list. Select the row and "
           "type the coordinates, or load a StationXML file; without them "
           "the station cannot be offered for a case.",
           nullptr, unknown));
  } else if (problem_->property("auto").toBool()) {
    problem_->clear();
  }
}

void WaveformsDialog::addFolder() {
  addFolders(
      pickFolders(this, tr("Folders of waveforms"), ctx_.study->study().dir));
}

void WaveformsDialog::addFolders(const QStringList &paths) {
  if (paths.isEmpty())
    return;
  Study::RootsAdded r;
  if (!edit([&](Study &s) { r = s.addWaveRoots(paths); }))
    return;
  QStringList notes;
  if (!r.covered.isEmpty())
    notes << tr("Already covered by a listed folder: %1")
                 .arg(r.covered.join(QStringLiteral(", ")));
  if (!r.replaced.isEmpty())
    notes << tr("Now covered by a folder that holds it, so taken off the "
                "list (its files are read again once): %1")
                 .arg(r.replaced.join(QStringLiteral(", ")));
  if (!notes.isEmpty()) {
    problem_->setProperty("auto", false);
    problem_->setText(notes.join(QStringLiteral("\n")));
  }
  if (!r.added.isEmpty())
    scan_->start();
}

void WaveformsDialog::dragEnterEvent(QDragEnterEvent *e) {
  if (!foldersOf(e->mimeData()).isEmpty())
    e->acceptProposedAction();
}

void WaveformsDialog::dropEvent(QDropEvent *e) {
  const QStringList dropped = foldersOf(e->mimeData());
  if (dropped.isEmpty())
    return;
  addFolders(dropped);
  e->acceptProposedAction();
}

void WaveformsDialog::repoint() {
  if (!folders_->currentItem())
    return;
  const QString id = folders_->currentItem()->data(Qt::UserRole).toString();
  const QString path = QFileDialog::getExistingDirectory(
      this, tr("Where the folder is now"),
      ctx_.study->study().waveRootPath(id));
  if (path.isEmpty())
    return;
  const QString old = ctx_.study->study().waveRootPath(id);
  if (edit([&](Study &s) {
        // The same step as "locate missing inputs": whatever else the study
        // names under the folder that moved follows it.
        s.relocate(old, path);
        for (study::WaveRoot &r : s.waveRoots)
          if (r.id == id && QDir::cleanPath(s.resolved(r.path)) !=
                                QDir::cleanPath(path))
            r.path = s.stored(path); // the old place is still there
      }))
    scan_->start();
}

void WaveformsDialog::removeFolder() {
  if (!folders_->currentItem())
    return;
  const QString id = folders_->currentItem()->data(Qt::UserRole).toString();
  if (!edit([&id](Study &s) {
        s.waveRoots.removeIf(
            [&id](const study::WaveRoot &r) { return r.id == id; });
      }))
    return;
  // The index forgets the folder at once, not only at the next scan.
  WaveIndex index = WaveIndex::load(ctx_.study->study().dir);
  if (index.roots.remove(id) > 0)
    index.save(ctx_.study->study().dir);
  refresh();
}

void WaveformsDialog::typeCoordinates() {
  const int row = table_->currentRow();
  if (row < 0 || !table_->item(row, 0))
    return;
  const QString code = table_->item(row, 0)->text();
  const QString network = table_->item(row, 1)->text();
  QDialog d(this);
  d.setWindowTitle(tr("Coordinates of %1").arg(code));
  auto *lat = new QDoubleSpinBox(&d);
  lat->setRange(-90, 90);
  lat->setDecimals(4);
  auto *lon = new QDoubleSpinBox(&d);
  lon->setRange(-180, 180);
  lon->setDecimals(4);
  for (const study::ExtraStation &e : ctx_.study->study().extraStations)
    if (e.code == code) {
      lat->setValue(e.lat);
      lon->setValue(e.lon);
    }
  auto *form = new QFormLayout;
  form->addRow(tr("Latitude (°N)"), lat);
  form->addRow(tr("Longitude (°E)"), lon);
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
  connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  auto *l = new QVBoxLayout(&d);
  l->addLayout(form);
  l->addWidget(buttons);
  ui::setHelp(lat, tr("Latitude of the station in decimal degrees, north "
                      "positive."));
  ui::setHelp(lon, tr("Longitude of the station in decimal degrees, east "
                      "positive."));
  if (d.exec() != QDialog::Accepted)
    return;
  const double la = lat->value(), lo = lon->value();
  edit([&](Study &s) {
    s.extraStations.removeIf(
        [&code](const study::ExtraStation &e) { return e.code == code; });
    s.extraStations << study::ExtraStation{code, network, la, lo,
                                           QStringLiteral("typed")};
  });
}

void WaveformsDialog::loadStationXml() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("StationXML file"), ctx_.study->study().dir,
      tr("StationXML (*.xml);;All files (*)"));
  if (path.isEmpty())
    return;
  try {
    const QList<study::ExtraStation> read = study::readStationXml(path);
    const auto index = WaveIndex::shared(ctx_.study->study().dir);
    int taken = 0;
    edit([&](Study &s) {
      for (const study::ExtraStation &e : read) {
        // Only stations the folders hold; a whole network's list is not
        // copied into the study.
        if (!index->hasStation(e.code))
          continue;
        s.extraStations.removeIf([&e](const study::ExtraStation &x) {
          return x.code == e.code;
        });
        s.extraStations << e;
        ++taken;
      }
    });
    problem_->setProperty("auto", false);
    problem_->setText(
        tr("%1 of the %2 stations in the file are in the waveform folders "
           "and now have coordinates.")
            .arg(taken)
            .arg(read.size()));
  } catch (const std::exception &e) {
    problem_->setProperty("auto", false);
    problem_->setText(QString::fromUtf8(e.what()));
  }
}

void WaveformsDialog::showSkipped() {
  const Study &s = ctx_.study->study();
  QStringList lines;
  for (const study::SkippedFile &f : WaveIndex::shared(s.dir)->skipped())
    lines << QStringLiteral("%1\t%2")
                 .arg(QDir(s.waveRootPath(f.rootId)).filePath(f.rel), f.why);
  QDialog d(this);
  d.setWindowTitle(tr("Files that were skipped"));
  auto *text = new QPlainTextEdit(lines.join(QLatin1Char('\n')), &d);
  text->setReadOnly(true);
  text->setLineWrapMode(QPlainTextEdit::NoWrap);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &d);
  connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::accept);
  auto *l = new QVBoxLayout(&d);
  l->addWidget(text);
  l->addWidget(buttons);
  d.resize(760, 420);
  d.exec();
}

} // namespace skg
