# sismokaos-gui 0.4.0-beta.1

A test version for the lab. It is not the final 0.4.0.

## What this beta is, and who should install it

Version 0.4 turns the application from a set of separate tools into one
workflow: a **study** runs the same analyses on several earthquakes and puts
the results side by side. Almost every page is new or changed.

Install it if you are willing to try that workflow on your own data and
tell us what breaks or confuses. Do not use it yet for results you intend to
publish: it has had one session on real data, and several of its fixes
change numbers (see "Changes to results" below).

The beta does not replace 0.3.0 for anyone by default. The ordinary install
command and the update check of 0.3.0 keep offering 0.3.0.

## Install

Linux and macOS:

```sh
curl -fsSL https://raw.githubusercontent.com/SismoKaos-Research/sismokaos-gui/main/install.sh | sh -s -- --version 0.4.0-beta.1
```

Windows (PowerShell):

```powershell
$env:SISMOKAOS_VERSION = "0.4.0-beta.1"; irm https://raw.githubusercontent.com/SismoKaos-Research/sismokaos-gui/main/install.ps1 | iex
```

The beta takes the place of the installed version. Your settings, campaigns
and results are kept. It will offer a later beta or the final 0.4.0 when one
is out.

**Going back to 0.3.0:** `install.sh --version 0.3.0`, or on Windows
`$env:SISMOKAOS_VERSION = "0.3.0"` before the same one-liner. A study made
with the beta is not understood by 0.3.0, which has no studies; the tool
pages work as before.

The manual is the README of this version:
<https://github.com/SismoKaos-Research/sismokaos-gui/blob/v0.4.0-beta.1/README.md>.
Start at "First study in ten minutes".

## What is new

**Studies**
- A study is a folder: its earthquakes (cases), the stations each is studied
  from, saved settings (recipes), and every result.
- Four numbered steps in the sidebar: Cases, Data, Runs, Compare. Each page
  has a button to the next step, and a waiting cell says what is missing and
  has a button that goes there.
- Earthquakes are picked from a built-in list of large earthquakes, with no
  file needed, or from your own catalog; several can be added at once.
- A reference date, a date with no earthquake, can be studied the same way
  for comparison.

**Waveforms**
- Import what you have: add a folder (a folder of station folders is one
  folder). The station of each file is read from its miniSEED header, so
  the layout does not matter. Zip archives are read as they are.
- A quick scan tells which stations are there without reading the files
  through; an exact scan of the coverage follows in the background and is
  kept, so the same data is not read twice, also from another study or
  another drive.
- Or download: from a case, one panel lists what is missing and downloads
  it, from AFAD's portal or an FDSN service.

**Runs**
- A matrix of cases and stations against recipes, with a queue, one progress
  bar per run, stopping of one run or all, a log window that follows a run,
  a list of what ran and for how long, a disk-space check before a run, and
  a view of what the study takes on disk.
- A run never damages a finished one, and a calibration is kept so that a
  restarted Features run does not calibrate again.

**Compare**
- Scores of every case with a baseline beside each; any indicator or feature
  of all cases on one axis of days before the event; the cases on a map.

**Everywhere**
- Maps on the Cases, Compare and Data pages, with fault sources and station
  coverage.
- Every option explains itself when the pointer rests on it.
- A new look with a light and a dark theme; Turkish throughout.
- Notifications when long work ends; one study open in one window at a time.
- Seismic regions from a catalog, on the Analysis page.

## Changes to results: read this

Several fixes change numbers. They apply to the study's runs and, where
noted, to the tool pages that 0.3.0 already had, so a result from 0.3.0 can
differ from the same analysis in this version. Do not put old and new
numbers side by side.

Version 0.3.0 had no studies. If you have a study from an earlier
development build, **every run in it reads as stale once**, with the reason
on the cell: run them again.

- **Catalog times.** A time with no time zone in a catalog is now read as
  UTC on every computer. Before, it was read in the computer's own zone, so
  on a computer set to Turkey time such events were three hours off. This
  affects the Indicators, the Features calibration and the class labels,
  on the tool pages as well as in a study.
- **The earthquake stays out of its own span.** It is now found in the
  catalog also when the case was picked from another list or the catalog's
  clock is shifted. Before, in those situations the earthquake and its first
  aftershocks could be counted as events before it.
- **Classification windows.** A window that ends after the earthquake is no
  longer counted as "before" it. Before, an earthquake in the first minutes
  of an hour left a few windows that held the shaking in the first class.
  The Classification page cuts the same way now.
- **Features read only the span.** In a study, a Features run reads the
  records from the span's start to the earthquake. The calibration therefore
  no longer sees data from after the earthquake, which it did with archives
  that reach beyond it.
- **One sensor per station.** When a station's files hold a broadband and an
  accelerometer, or two location codes, one is read and the run says which.
  Before, their samples could end up mixed in one series; this holds for
  the Features page too.
- **Sparse catalogs.** The Indicators now wait, and Features runs carry a
  warning, when the study's catalog does not go down to small magnitudes
  (the built-in list, a USGS download).

Also fixed, without a change to correct results: a download that broke off
part way was kept as a complete file; archives under a folder with Turkish
letters in its name could not be read on Windows; an unplugged drive could
empty a study's waveform index; two windows on one study could overwrite
each other.

## Known limits

- **One session on real data.** One small study was set up on a real archive
  and its Features runs were started and stopped. No study has been carried
  through to a comparison on real data.
- **Downloads from the new panel have only met simulated services**, never
  AFAD's portal, a real mailbox or a real FDSN service. The download engine
  underneath is the earlier tool.
- **Windows and macOS are covered by automated tests only.** Nobody has used
  this version by hand on either.
- **A full catalog is not shipped and cannot be downloaded from AFAD.**
  AFAD's web service returns only the latest 1,000 earthquakes. For the
  Indicators and the Features calibration, export the period from AFAD's
  website and choose the file (*Use my own file…*). Cite AFAD as the source.
- **The scan of a large archive takes time.** The exact pass has to read
  through every zip. You do not have to wait for it: estimates stand in.
- **Fault lines on the maps** are the fault sources of a hazard model
  (EFSM20), not an active fault map. The MTA map is not shipped.
- **Turkish wording** is complete but under review.
- **Early warning** does not follow the open study.

## What we most want tested

1. **A first study from scratch on Windows and on macOS**: new study, the
   setup dialog, a case from the built-in list. Does anything look broken,
   cut off or untranslated?
2. **Import of an existing archive of station folders.** Are the stations
   and the days found what you know you have? How long did the scan take,
   on which kind of drive?
3. **A download for one small case** from the panel on the Data page: one or
   two stations, a short span. Did the requests go out, did the files
   arrive, did the Cases page then show them?
4. **A Features run on one station**, then Classification on it, then
   Compare. Note the time it took.
5. **The Turkish interface**: wrong or awkward terms, texts that do not fit.

## How to report

*Help → Report a problem…* opens the report form and copies the diagnostics
to the clipboard; paste them into the form. They hold the version, the
system and the size of the open study, and no personal paths or addresses.
Say what you did, what you expected and what happened, and attach the log of
the job (*Copy log* in the Jobs panel, or *Copy* in a run's log window).

Before you paste a log or attach a screenshot, look it over. Mailbox
addresses are masked on the Data page and in its logs, but check; and a
screenshot can show folder names you may not want to share.

If you prefer, tell us directly; a short note is better than none.
