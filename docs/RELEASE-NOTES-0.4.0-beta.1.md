# SismoKaos Graphical Toolkit 0.4.0-beta.1

A pre-release for testing within the laboratory. It is not the final 0.4.0.

## 1. Introduction

The SismoKaos Graphical Toolkit is a desktop application for the study of
seismicity and continuous seismic recordings in the period before an
earthquake. It gathers the laboratory's analysis tools in one program:

- **acquisition** of continuous waveforms, from AFAD's portal and from FDSN
  services;
- **seismicity indicators** computed from an earthquake catalog in sliding
  windows (the b-value and related quantities);
- **waveform features** extracted from continuous recordings, after a
  calibration on windows in which the catalog lists no earthquake;
- **classification** of those features by time before the event, scored
  against a baseline;
- **seismic regions** derived from a catalog;
- **ayzek**, an earthquake early-warning pipeline, shipped with the toolkit
  as a separate program.

Version 0.4 organises these tools around a **study**. The earlier versions
offered each tool on its own page, and a comparison across earthquakes had
to be assembled by hand. A study makes that comparison the unit of work.

## 2. The study as the unit of analysis

A study consists of:

- **cases**: earthquakes, each with a span of days before it and the
  stations it is observed from. A reference date with no earthquake can be
  entered as a case and serves as a control;
- **recipes**: saved settings of a tool;
- **runs**: the result of one recipe on one case and station.

Every recipe is applied to every case in the same way. The results are then
set side by side: each score beside its baseline, and each indicator or
feature of all cases on a common axis of days before the event.

Two properties are intended to make such a comparison defensible.

**Identical processing.** A case differs from another in its earthquake,
its stations and its data, and in nothing else. Settings live in the recipe
and cannot drift between cases.

**Traceability.** A run records its settings, the version of the program,
and the identity of the catalog and of the waveform files it read. When any
of these changes afterwards, the run is marked stale and says why. A
warning raised while a result was computed (a sparse catalog, a station
with little data in the span) stays attached to that result and is shown
wherever the result is shown.

The toolkit computes and compares; it does not interpret. Whether a
quantity carries information about an approaching earthquake is a question
for the study and its controls, not something the program asserts.

## 3. Implementation

The toolkit is a native application, written in C++20 with the Qt 6 widget
libraries. It is not a web application in a browser shell: it does not use
Electron or an embedded browser. The analyses are compiled code within the
program, so no Python environment has to be installed or maintained beside
it.

The choice is practical. A study reads a year or more of continuous data
per station; those records are read, decompressed and processed on the
user's own machine, on several processor cores at once. miniSEED records
are read directly, also from inside zip archives, without unpacking an
archive to disk first for the scan.

Builds are provided for Linux, macOS and Windows. The interface is in
English and Turkish. The source is published under the GNU General Public
License, version 3 or later.

## 4. What version 0.4 adds

**Cases.** Earthquakes are chosen from a list of large earthquakes that
ships with the toolkit, with no file needed, or from the user's own
catalog. The list can be filtered by magnitude, depth, date, place, and
distance to given stations. Several earthquakes can be added at once.

**Waveform data.** Existing data is imported by naming its folder; a folder
of station folders counts as one. The station of each file is read from its
miniSEED header, so the layout of the folders does not matter. A quick pass
establishes which stations are present; an exact pass over the coverage
follows in the background and its result is kept, so the same data is not
read twice, also from another study or another drive. Missing data can be
requested from within a case: one panel lists what is absent and downloads
it.

**Runs.** A matrix of cases and stations against recipes shows the state of
every result. Runs are queued, with a set number at work at once; each has
a single progress indicator, can be stopped alone, and writes a log that
can be followed while it runs. Free disk space is checked beforehand. A
finished result is never damaged by a later run, and the calibration of a
Features run is kept, so a restarted run does not calibrate again.

**Comparison.** Scores of every case with a baseline beside each; any
indicator or feature of all cases on one axis of days before the event; the
cases on a map.

**Maps.** On the Cases, Compare and Data pages, with fault sources and
station coverage.

**General.** Every option explains itself when the pointer rests on it. A
light and a dark theme. Notifications when long work ends. A study is open
in one window at a time.

## 5. Changes that affect numerical results

Several corrections change numbers. They apply to the runs of a study and,
where noted, to the tool pages that 0.3.0 already had. A result from 0.3.0
can therefore differ from the same analysis in this version, and results
from the two versions should not be combined.

Version 0.3.0 had no studies. In a study made with an earlier development
build, **every run, of all three tools, is marked stale once**, with the
reason given on the cell. Such runs are to be made again.

- **Catalog times.** A time without a time zone in a catalog is read as UTC
  on every computer. Before, it was read in the computer's own zone, so on
  a computer set to Turkey time such events were three hours off. This
  affects the Indicators, the Features calibration and the class labels,
  on the tool pages as well as in a study.
- **The earthquake is excluded from its own span.** It is now identified in
  the catalog within 15 hours of the case's time, also when the case was
  chosen from another list or the catalog's clock is shifted. Before, in
  those situations the earthquake and its first aftershocks could be
  counted among the events preceding it.
- **Classification windows.** A window that ends after the earthquake is no
  longer labelled as preceding it. Before, an earthquake in the first
  minutes of an hour left a few windows containing the shaking in the first
  class. The Classification page applies the same rule.
- **Features are computed from the span only.** In a study, a Features run
  reads the records from the start of the span to the earthquake. The
  calibration therefore no longer sees data from after the earthquake,
  which it did with archives that extend beyond it.
- **One sensor per station.** When a station's files hold a broadband
  sensor and an accelerometer, or two location codes, one is read (the one
  with most data in the span) and the run states which. Before, their
  samples could be mixed in one series; this holds for the Features page
  as well.
- **Sparse catalogs.** Indicators and Features runs carry a warning when
  the study's catalog does not reach down to small magnitudes (a USGS
  download, for example). The run is made; the warning stays with its
  results on the Compare page.

## 6. Other corrections

These do not change correct results.

- A download that broke off part way was kept as a complete file.
- Archives under a folder with Turkish letters in its name could not be
  read on Windows.
- An unplugged drive could empty a study's waveform index.
- Two windows on one study could overwrite each other's changes.
- A `.gz` catalog made of several parts was read only as far as the length
  of its last part, without a message.
- Two files of the same name in different folders of one archive overwrote
  each other when unpacked, so one of them was lost.
- A second copy of the application could delete the unpacked files of a run
  that was still at work (Windows and macOS).
- A long Features run needed several gigabytes of memory for its list of
  records: 137 bytes per record before, 32 now, as measured on a test
  index.
- A finished run whose result could not replace the old one, because a file
  was open in another program, was computed again; it is now kept and put
  in place by the next run.
- Quitting during a run could end the application abruptly; it now waits
  and shows what is still stopping.

## 7. Known limitations

- **One session on real data.** One small study was set up on a real
  archive, and its Features runs were started and stopped. No study has yet
  been carried through to a comparison on real data.
- **Downloads from the new panel have only met simulated services**, never
  AFAD's portal, a real mailbox or a real FDSN service. The download engine
  underneath is that of the earlier versions.
- **Windows and macOS are covered by automated tests only.** Nobody has yet
  used this version by hand on either.
- **A complete catalog is not shipped and cannot be downloaded from AFAD.**
  AFAD's public web service returns only the latest 1,000 earthquakes. For
  the Indicators and the Features calibration, export the period from
  AFAD's website and choose the file (*Use my own file…*). Cite AFAD as the
  source.
- **The exact scan of a large archive takes time**, since every archive has
  to be read through. Work can go on meanwhile: estimates stand in.
- **The fault lines on the maps** are the fault sources of a hazard model
  (EFSM20), not a map of active faults. The MTA map is not shipped.
- **Turkish wording** is complete but under review.
- **The early-warning page** does not follow the open study.

## 8. Installation

Linux and macOS:

```sh
curl -fsSL https://raw.githubusercontent.com/SismoKaos-Research/sismokaos-gui/main/install.sh | sh -s -- --version 0.4.0-beta.1
```

Windows (PowerShell):

```powershell
$env:SISMOKAOS_VERSION = "0.4.0-beta.1"; irm https://raw.githubusercontent.com/SismoKaos-Research/sismokaos-gui/main/install.ps1 | iex
```

This version takes the place of the installed one. Settings, campaigns and
results are kept. It will offer a later pre-release or the final 0.4.0 when
one is out.

The pre-release does not replace 0.3.0 for anyone by default: the ordinary
install command and the update check of 0.3.0 keep offering 0.3.0.

**Returning to 0.3.0:** `install.sh --version 0.3.0`, or on Windows
`$env:SISMOKAOS_VERSION = "0.3.0"` before the same command. A study made
with this version is not understood by 0.3.0, which has no studies; the
tool pages work as before.

The manual is the README of this version:
<https://github.com/SismoKaos-Research/sismokaos-gui/blob/v0.4.0-beta.1/README.md>.
Start at "First study in ten minutes".

Do not use this version yet for results intended for publication, for the
reasons given in sections 5 and 7.

## 9. What we ask testers to examine

1. **A first study from scratch on Windows and on macOS**: a new study, the
   setup dialog, a case from the built-in list. Does anything look broken,
   cut off or untranslated?
2. **Import of an existing archive of station folders.** Are the stations
   and the days found those you know you have? How long did the scan take,
   and on which kind of drive?
3. **A download for one small case** from the panel on the Data page: one
   or two stations, a short span. Did the requests go out, did the files
   arrive, did the Cases page then show them?
4. **A Features run on one station**, then Classification on it, then
   Compare. Note the time it took.
5. **The Turkish interface**: wrong or awkward terms, texts that do not
   fit.

## 10. Reporting

*Help → Report a problem…* opens the report form and copies the diagnostics
to the clipboard; paste them into the form. They hold the version, the
system and the size of the open study, and no personal paths or addresses.
State what you did, what you expected and what happened, and attach the log
of the job (*Copy log* in the Jobs panel, or *Copy* in a run's log window).

Look over a log or a screenshot before sharing it. Mailbox addresses are
masked on the Data page and in its logs, but check; and a screenshot can
show folder names you may not wish to share.

A short note sent to us directly is also welcome.
