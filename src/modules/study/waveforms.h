// Importing waveforms the user already has: the background scan of the
// study's waveform folders, and the dialog that lists the folders and shows
// what was found in them before any of it is used.
#pragma once

#include <QDialog>
#include <QObject>
#include <QPointer>

#include <functional>
#include <vector>

#include "analysis/events.h"
#include "core/module.h"
#include "study/study.h"
#include "study/waveindex.h"

class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

namespace skg {

class Job;

// Scans every waveform folder of the open study as jobs: first which
// stations are there (fast), then the exact coverage. The index file in the
// study folder is rewritten as the scan goes; `updated` says when.
class WaveScan : public QObject {
  Q_OBJECT
public:
  WaveScan(AppContext ctx, QObject *parent = nullptr);

  bool running() const { return !job_.isNull(); }
  // Does nothing while a scan is running or no study is open.
  void start();
  void cancel();
  // "Reading coverage: 120/600" while running, or how the last scan ended.
  QString status() const { return status_; }

signals:
  void updated(); // the index changed, or the status did

private:
  void run(study::ScanPass pass);

  AppContext ctx_;
  QPointer<Job> job_;
  QString status_;
};

class WaveformsDialog : public QDialog {
  Q_OBJECT
public:
  WaveformsDialog(AppContext ctx, WaveScan *scan, QWidget *parent = nullptr);

private:
  void refresh();
  void addFolder();
  void repoint();
  void removeFolder();
  void typeCoordinates();
  void loadStationXml();
  void showSkipped();
  bool edit(const std::function<void(study::Study &)> &change);

  AppContext ctx_;
  WaveScan *scan_;
  std::vector<analysis::Station> listed_; // the station list alone
  QString listedFrom_;
  QLabel *intro_, *status_, *problem_;
  QListWidget *folders_;
  QPushButton *add_, *repoint_, *remove_, *rescan_, *stop_;
  QTableWidget *table_;
  QPushButton *coords_, *xml_, *skipped_;
};

} // namespace skg
