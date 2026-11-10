# sismokaos-gui

A desktop application for the SismoKaos workflow: downloading continuous
seismic data from AFAD/TDVMS and FDSN services, extracting statistical and
chaotic features from it, relating them to the earthquake catalog and the
active faults around a station with publication figures, and classifying
the windows by their time to an earthquake with tuned models, experiment
tracking and reports. Everything
runs inside the application; no Python, Qt or other installation is needed.
Turkish and English, switchable while running. Linux, Windows and macOS.

For building from source, the code layout and how to add a tab, see
[DEVELOPING.md](DEVELOPING.md).

## Contents

1. [Install and update](#install-and-update)
2. [The window](#the-window)
3. [Studies: the same analysis on several earthquakes](#studies-the-same-analysis-on-several-earthquakes)
4. [Data tab: TDVMS campaigns and FDSN pulls](#data-tab-tdvms-campaigns-and-fdsn-pulls)
5. [Features tab: preprocessing and feature extraction](#features-tab-preprocessing-and-feature-extraction)
6. [Classification tab: models and experiments](#classification-tab-models-and-experiments)
7. [Analysis tab: earthquakes, faults, labels, figures and regions](#analysis-tab-earthquakes-faults-labels-figures-and-regions)
8. [Indicators tab: catalog-based seismicity indicators](#indicators-tab-catalog-based-seismicity-indicators)
9. [Early warning tab: ayzek](#early-warning-tab-ayzek)
10. [Settings tab](#settings-tab)
11. [Where files live](#where-files-live)
12. [Troubleshooting](#troubleshooting)

## Install and update

One command installs the latest release for the current user. The same
command, run again later, updates it.

```sh
# Linux (x86_64) and macOS
curl -fsSL https://raw.githubusercontent.com/SismoKaos-Research/sismokaos-gui/main/install.sh | sh
```

```powershell
# Windows (PowerShell)
irm https://raw.githubusercontent.com/SismoKaos-Research/sismokaos-gui/main/install.ps1 | iex
```

The script downloads the bundle for your platform from the latest GitHub
release, checks it against the release's `SHA256SUMS`, and installs it:

| | installed to | start it from |
|---|---|---|
| Linux | `~/.local/opt/sismokaos-gui/sismokaos-gui.AppImage` | the applications menu (*sismokaos-gui*), or `sismokaos-gui` in a terminal if `~/.local/bin` is on your PATH |
| macOS | `~/Applications/sismokaos-gui.app` | Launchpad or Finder |
| Windows | `%LOCALAPPDATA%\Programs\sismokaos-gui\` | the Start Menu entry *sismokaos-gui* |

Your settings, campaigns and results are stored elsewhere (see [Where files
live](#where-files-live)); installing, updating and uninstalling never touch
them.

**Updating.** The application looks for a newer release when it starts and
offers *Update now*, which closes it, replaces the installation and starts
the new version. *Help → Check for updates…* does the same on demand, and
*Help → Check for updates at start* turns the automatic check off. You can
also run the install command above again, or the copy the installer left
beside the application (`update` on Linux and macOS, `update.ps1` on Windows).

**A specific version.** `install.sh --version 0.2.0`, or on Windows
`$env:SISMOKAOS_VERSION = "0.2.0"` before the one-liner.

**Uninstalling.** `install.sh --uninstall` or `.\install.ps1 -Uninstall`
removes the application, the launcher and the shortcut, and keeps your data.

**macOS note.** The bundle is not notarised. The installer removes the
quarantine flag so it opens normally. If you download the zip by hand
instead, run `xattr -dr com.apple.quarantine sismokaos-gui.app` once.

**Without the script.** Every release on the
[releases page](https://github.com/SismoKaos-Research/sismokaos-gui/releases)
has the AppImage, the Windows zip (unpack anywhere, run
`sismokaos-gui.exe`) and the macOS zip.

## The window

The **sidebar** on the left lists the pages. Under *Study* are **Cases**,
**Runs** and **Compare** (the next section). Under *Tools* are **Data**,
**Features**, **Classification**, **Analysis**, **Indicators** and **Early
warning**; **Settings** is at the bottom. Alt+1 … Alt+9 jump to a page. The
header above each page gives its name and one line on what it is for. The
sections below still call the pages "tabs".

On every page the one filled button is the page's main action (*Run*,
*Start*). An empty table or log says what to do to fill it. Every option
explains itself: rest the pointer on a field, its label or a button and a
box says what it does, its unit and, where there is one, the default.

**Jobs.** The strip at the bottom says what is running and how far it is.
Click *Jobs* on its right (or *View → Jobs*, Ctrl+J) to open the jobs panel,
which lists everything the application runs: a download campaign, a
calibration, a feature extraction. Each job shows its state, progress,
elapsed time and, when selected, its live log. *Cancel* stops a job
cooperatively: it finishes the step it is on and writes nothing half-done.
*Copy log* puts the log on the clipboard for a bug report.

**Language.** The *EN / TR* switch in the header (or *View → Language*)
changes the language immediately; the choice is remembered.

**Theme.** *View → Theme* or the Settings page chooses a light or a dark
look, or follows the system. Figures stay on a white background in both.

Closing the window while jobs run asks first; confirming cancels them.

## Studies: the same analysis on several earthquakes

A **study** runs the same analyses on several earthquakes and puts the
results side by side. It is a folder: one file, `study.toml`, says what the
study is, and every result is written under the folder in a fixed place.
The study selector in the header starts a new study in a folder you choose,
opens an existing one, and lists the recent ones. The tools pages keep
working on their own, with or without a study.

### Cases

A **case** is one earthquake and the place it is studied from, or a
**reference date**: a date with no earthquake, studied the same way so it
can be read beside the earthquakes.

1. Under *Study inputs* choose the **earthquake catalog** (an AFAD export,
   CSV or .gz, and whether its times are UTC or Turkey time) and, for the
   waveform tools, the **waveform folder**: a folder with one sub-folder of
   miniSEED per station, named by the station code.
2. *Add from catalog…* lists the catalog's earthquakes, largest first, with
   a magnitude threshold and a place filter. Pick one; the stations of the
   built-in AFAD list within the distance you set appear below with their
   distance and azimuth. Tick the ones to use, set the **span** (how many
   days before the event are studied) and *Add case*.
3. *Add reference date…* takes a name, a date, and either the place and
   stations of an existing case or coordinates of its own.

The selected case shows on the right: a map (epicentre, the stations in use
and the other candidates, distance rings, the shipped East Anatolian fault
traces), the station list to tick, the **catalog zone** the catalog tools
use (a circle around the epicentre, or a buffer around the fault zone) and
the span. Every change is saved at once. *Remove* takes a case out of the
study; its result folders stay on disk.

### Runs

A **recipe** is one tool's saved settings under a name. The same recipe on
every case is the same analysis. *New recipe* makes one for:

- **Indicators** (one run per case): the catalog events of the case's zone,
  from one window before the span up to, but not including, the event;
  then the indicator table and figures of the Indicators tab. With
  `estimate_mc` the completeness magnitude is estimated for each case
  (maximum curvature) and recorded.
- **Features** (one run per station): the feature extraction of the
  Features tab on the station's waveform folder, with the chaos parameters
  calibrated for that station. `config_json` takes a `config.json` saved on
  the Features tab; empty uses the defaults. The whole folder is processed,
  so keep in it the days the span needs.
- **Classification** (one run per station): the models of the
  Classification tab on the feature table of a features recipe
  (`features_recipe`). `classes` are ranges of days before the event, such
  as `1-30, 31-90`; each case's class table is made from them and its own
  event time, so one recipe fits every case, reference dates included.
  `models` lists what to train (`logistic`, `svm_linear`, `svm_poly`,
  `svm_rbf`, `ann`, `random_forest`, `gradient_boosting`, `xgboost`);
  `quick` uses one small setting per model for a first look; `sfc_toml`
  takes anything else in the Classification tab's own settings layout.

The table of settings under the matrix says for each setting whether it is
**fixed** across cases or **fitted per case**. *Edit recipe…* changes them;
a changed setting raises the recipe's revision.

The **matrix** has a row per case and station and a column per recipe. Each
cell is one of:

| cell | meaning |
|---|---|
| ✓ Done | run, for the recipe and the case as they are now |
| ○ Missing | not run yet |
| ↻ Stale | run, but the recipe or the case has changed since |
| ✕ Failed | the last run ended with an error (click the cell to read it) |
| ⏸ Waiting | cannot run yet: no catalog, no waveform folder for the station, or the features it needs are not done |
| ◔ Running | a job is on it; the percentage is the job's progress |

*Run missing and stale* queues those cells as jobs, two at a time, and goes
on to the cells they unblock (a classification after its features). *Run
the selected cell* runs one. Click a cell to see why it waits and *Open
folder* to see its files.

### Compare

**Scores** (top): for a classification recipe, one row per case and
station with the magnitude, the station's distance, the **best model** (the
one with the highest F1 on the validation days), and its **F1 and AUC on
the test days**. Beside each is the same number for the **baseline**: the
best of the Classification tab's baseline step (logistic regression and the
SVMs), chosen on the validation days the same way; and the difference. The
upper bar is the model, the lower the baseline. Click a column heading to
sort. A row whose run is missing, stale or failed says so and shows no
number. *Export table…* writes the rows as CSV.

**Series** (bottom left): choose an indicators or a features recipe and a
column. Every case is drawn on one axis of **days before its event**, the
event at zero on the right; reference dates are dashed. An indicator window
is placed at its end; a feature column is the median of each day. The
legend is always there; with four series or fewer the names also stand at
the line ends. More than eight series are not drawn (the legend says how
many). *Export figure…* writes a PNG or an SVG.

**Fitted per case** (bottom right): what each case fitted for itself in the
chosen series recipe (the estimated completeness magnitude, the number of
events in the zone, the calibrated chaos parameters), so differences in
setup are in view next to the results.

### What a study writes

```
<study>/study.toml
<study>/<case>/_case/indicators/<recipe>/        catalog.csv, Deprem_Analiz_Grafikleri_*/, run.json
<study>/<case>/<STATION>/features/<recipe>/      the Features tab's outputs, run.json
<study>/<case>/<STATION>/classification/<recipe>/ results/…/models/experiments.csv, run.json
```

The tools' own files are unchanged. `run.json` records the recipe revision
and settings, the case as it was, the inputs with their SHA-256, the
application version, start and end times, how the run ended, and the values
the case fitted. `study.toml` can be edited by hand; its layout is described
at the top of `src/study/study.h`.

## Data tab: TDVMS campaigns and FDSN pulls

This tab is the AFAD/TDVMS download tool (the former `tdvms` command line)
with the same files, so a campaign started on the command line can be
continued here and the other way round, as long as the two do not run at the
same time.

### How TDVMS downloads work

TDVMS does not serve data directly. You request a station and a time window
on the portal, giving an e-mail address; some time later a mail arrives with
a download link; the link expires. The portal accepts a limited number of
open requests per address. The application turns this into a campaign:

- A **campaign** is a folder with a **ledger** (`ledger.jsonl`), the list of
  **chunks**: one per station and time window, with a state.
- Chunk states: `pending` (not yet requested), `submitted` (requested,
  waiting for the mail), `fetched` (archive downloaded and verified),
  `nodata` (the portal said there is nothing for that window), `failed`
  (gave up after retries), `retired` (the station stopped answering and its
  remaining chunks were set aside), `claimed` (a request in flight; put back
  to pending if the run dies).
- **Slots** are the addresses the campaign requests with. From one mailbox
  address such as `you@gmail.com`, the application uses the plus-addresses
  `you+a1@gmail.com`, `you+a2@…`, one per slot, so that every reply can be
  matched to its request. All of them land in the same inbox.
- A **run** repeats one cycle: read the inbox and download the links that
  arrived (*reap*), free slots whose requests are stale (*reclaim*), set
  aside stations that keep failing (*retire*), and submit new requests into
  free slots (*submit*). Downloads are verified (an expired link, an empty
  zip and a "no data" notice are all recognised) and filed as
  `<raw>/<STATION>/<STATION>_<date>.zip`.

### Setting up

1. **Folder with the campaigns.** Choose or type the folder that holds your
   campaigns (*Browse…*). Each campaign is a sub-folder under `campaigns/`;
   old flat layouts (`<name>_ledger.jsonl` beside `<name>_raw/`) are found
   too.
2. **Mailbox…** Enter the IMAP host, user and password of the mailbox the
   portal will write to. For Gmail this is an *app password*, not the account
   password. *Check login* logs in once and reports how many messages the
   inbox holds. The dialog also sets the default number of slots and an
   optional notification target: an ntfy topic URL
   (`https://ntfy.sh/your-topic`), or `desktop` for a system notification
   when a run needs attention. The values are written to `mailbox.env` and
   `settings.env` in the config directory, readable by you only.
3. **New campaign…** Give it a name (lowercase letters, digits, `-`, `_`)
   and, if the archives should go to another disk, the download folder; the
   campaign folder is linked to it.

### Planning

*Plan station…* adds chunks: a station code, the first day, the window
length in days and the spacing between windows (every N days, or *cover the
whole period* for back-to-back windows). *Check that the portal lists the
station* asks TDVMS whether the code exists and shows the network and
instrument (a strong-motion accelerometer is flagged). The dialog warns if an
earlier campaign already covered the station. Plan as many stations as you
like; the chunk table shows them with their states.

*Refresh catalogs* downloads the current station list from the portal and
the M5+ events from USGS, used by the plan dialog and the coverage view.

### Running

Set the **Address** (the mailbox user, usually), the number of **Slots** and
the **Interval** between cycles, then *Start*. The run appears in the Jobs
panel and the status area shows each cycle. Options:

- *stop when every chunk has an outcome*: the run ends by itself when
  nothing is pending or submitted.
- *one cycle only*: do one reap/reclaim/retire/submit pass and stop.
- *dry run*: read the mailbox and plan, but flag no mail and submit nothing.

*Stop* ends the run after the current cycle. A run keeps a lock on the
ledger; if another process (or another copy of the application) holds it,
the table says so.

**Chunk table.** Filter by state with the drop-down. Select chunks and *Reset
to pending* (also on the right-click menu) to request them again, for
example after a `failed`. **Ingest archive…** files an archive you obtained
by other means against the chunk whose window it matches, and asks before
overwriting one that is already there.

### Coverage

The **Coverage** tab reads the headers of every miniSEED record in the
downloaded archives (never the samples) and shows, per station and channel,
how much of the requested time is actually covered and where the gaps are.
The timeline colours each bucket full, partial, gap, no data or pending.
Select a channel to list its gaps of a second or more. Results are cached
per archive in `<raw>/.coverage.json`; *ignore the cache* rereads
everything.

### FDSN

The **FDSN** tab pulls whole UTC days of one station from an FDSN dataselect
service (KOERI, `http://eida.koeri.boun.edu.tr`, by default) into the
campaign, for data the TDVMS portal does not have. Give the station,
network, channel pattern (`BH?`), the first day and the day after the last,
and *Fetch*. Each day is a row in its own ledger (`fdsn.jsonl` in the
campaign) with the same states; days already on disk are adopted, failed
days can be put back with *put this station's failed days back first*, and
*only add the days, do not fetch* plans without downloading. Files are named
like TDVMS archive members so the Features tab reads them the same way.

## Features tab: preprocessing and feature extraction

This tab turns a folder of miniSEED for one station into a table of
features per time window, the input of the classification. It follows the
design of `sismokaos-cli` (the current chaos parameters) and writes the file
formats of the Python tool.

### Inputs

- **Data directory**: the miniSEED files of one station, E, N and Z
  components, any mix of files; zip archives in the folder are unpacked for
  the run. Files that are not miniSEED (catalogs, notes) are skipped.
  A TDVMS campaign's `raw/<STATION>` folder or an FDSN day folder works
  directly.
- **Output directory**: where the results go; defaults to `results` under
  the data directory when left empty.
- **Config**: a `config.json`. *Load…* reads one in either layout (the Rust
  tool's keys or the Python tool's upper-case keys), *Save* writes the
  settings currently shown. Without a config file the defaults are used.

### Settings

The editor on the left has four groups.

**Run.** The station code (a label in the outputs), the sampling rate after
decimation (the native rate must be a whole multiple; a warning says when it
is not), window and step in seconds, STA and LTA lengths, the bandpass
corners, the spectral split frequency, and *chaos features only* to compute
just the fifteen chaotic features and write them to a separate file.

**Chaos features.** The embedding delay tau, the embedding dimension, Wolf's
evolve steps, the minimum samples, the mean period, the Rosenstein rise
interval and horizon, and the sample-entropy `m` and `r`. In a full run,
tau, dimension, mean period and the sample-entropy tolerances are
*calibrated* from the data (next group) and these values are only the
starting point; the calibrated ones are written to the outputs.

**Calibration on quiet periods.** The chaos parameters are estimated on
windows without earthquakes. For that the application needs the AFAD
earthquake catalog (a CSV or `.csv.gz` export, which you point it at) and
the station's position: either from a station list (the AFAD list embedded
in the application is used when the field is empty) or typed in directly.
Events within *maximum distance* and above *minimum magnitude* exclude the
time from *seconds before* the origin until the coda has died down (the
larger of *minimum seconds after* and *coda factor* times the Lee coda
duration). Of the remaining gap-free windows, *quiet windows sampled* are
drawn at random (the *seed* makes it repeatable) and the loudest, beyond
*loudness factor* times the median, are dropped per component. Tau is the
first minimum of the average mutual information (up to *maximum tau*), the
dimension comes from false nearest neighbours (up to *maximum dimension*),
the mean period from the mean frequency; the run uses the medians.

**Bands.** Optional. Each row is a complete run of its own (frequency band,
rate, window, step, STA, LTA, split) written to a sub-folder named after the
band; an empty table means one run with the settings above. *Add band*
copies the current settings into a new row; cells are edited in place.

### Running

- **Run**: calibrate, then extract features window by window, per band.
  Needs the catalog unless a calibration is reused.
- **Preprocess only**: filter and decimate the three components and write
  them as a raw float32 stream, for model training that wants waveforms
  rather than features.
- **Dry run**: validate the settings and list the runs that would happen,
  without touching the data.
- **Stop**: ends the job after the window in progress; nothing partial is
  written.

*Reuse a previous run's calibration* takes a `*_calibration.json` from an
earlier run (same station, same rate) instead of calibrating again. *Bands
to run* limits a banded config to the named bands.

Progress and the log are in the Jobs panel and in the **Status** tab.
Warnings about settings (a corner near Nyquist, an STA longer than the
window) appear there before the run starts; errors that would corrupt the
output (a non-positive rate, duplicate band names) stop it with a message.

### Outputs

For a data directory `<dir>`, in the output directory (or its band
sub-folder):

| file | content |
|---|---|
| `<dir>_features.npy`, `<dir>_features.csv` | one row per window: `Pencere_ID` (`<dir>_w01`, …), `Zaman_Dk` (end of the window, epoch minutes), the features, their `_DEV` columns (difference to the previous window), and `E_INTERP_FRAC`, `N_INTERP_FRAC`, `Z_INTERP_FRAC`, the share of the window each component had to be interpolated over a gap. Chaos-only runs write `<dir>_chaos_features.*`. |
| `<dir>_calibration.json` | the calibrated chaos parameters, their spread, and how many windows were used |
| `<dir>_run_metadata.json` | the complete settings the run used, loadable as a config |
| `<dir>_preprocessed.f32`, `.f32.json` | *Preprocess only*: E N Z interleaved little-endian float32, and a sidecar with the rate, NaN counts and the time segments |

The **Results** tab lists these files and previews them: the tables with
their columns, the calibration with a summary, the stream with its sidecar.
*Open folder* shows the directory in your file manager.

In Python:

```python
import numpy as np
f = np.load("ELZG_features.npy")          # structured array, f["Z_RMS"], f["Pencere_ID"]
w = np.memmap("ELZG_preprocessed.f32", dtype="<f4", mode="r").reshape(-1, 3)
```

Windows that cover a gap carry an `*_INTERP_FRAC` above zero; filter on it
before trusting a chaos measurement there.

## Classification tab: models and experiments

This tab is the `sfc` tool: it labels the windows of a station by how many
days before an earthquake they were recorded, splits them by day, trains and
tunes several classifiers and keeps every experiment comparable. It reads
and writes `sfc`'s own files (`project.toml`, `results/.../models/<run>/`,
`experiments.csv`), so a project started with the Python tool can continue
here. The models themselves come from XGBoost, libsvm and a small neural
network built into the application; numbers differ slightly from the Python
tool's (different random streams and solvers), so keep a paper's tables with
the implementation that produced them.

### Project and data

- **Project folder**: the folder holding `project.toml`; results go under
  `results/<station>/<event>/N/` inside it. A new folder starts from the
  defaults; *Save project.toml* writes the settings.
- **Labelled windows**: either `sfc`'s `<STATION>_N_features_labeled.csv`,
  or a feature table from the Features tab. For the latter the **class
  table** labels the days: label 0 for the days 1 to 30 before the event,
  every day; label 1 for days 31 to 90, every second day; and so on, with
  the planned days moved to the nearest day that has data when one is
  missing, and the event day itself as label 0 up to the cutoff hour. The
  line under the fields reports the windows and days per label; its tooltip
  shows which days were chosen and why. The labelled table is also written
  in `sfc`'s layout beside the results when a run starts.

### Settings

The form covers what changes between experiments; the TOML beside it is the
whole `project.toml` for anything else (the parameter grids, the scatter
lists). *Apply TOML* takes the text back into the form.

- **Event and labels**: station, event name, event time (UTC; only the
  hours before it are used on the event day), the class table, merging
  labels into fewer classes (one class per label, e.g. `0,1,1,2`), and
  months per class as a seasonal control.
- **Features and split**: the feature columns (ticked from the source's
  numeric columns), the scaler fitted on the training days (robust,
  standard, minmax) with clipping, the shares of days per label that go to
  training and validation (the rest is the test set), the seed.
- **Models and tuning**: the run name, the baseline models (logistic
  regression and the SVM kernels), the tree models (random forest, gradient
  boosting, XGBoost), balanced class weights, the metric that picks the
  best parameters (macro F1 or accuracy), k-fold cross-validation over the
  train + validation days instead of the fixed validation days, whether to
  save the models, and the network's epochs, patience and oversampling.

### Run

Tick the steps (the order is fixed): data understanding, split, split
visualization, baseline, ann, trees, error timeline, compare. *Run* starts a
job; its log appears beside the steps and in the Jobs panel, with the same
lines the Python tool prints: every parameter combination with its
validation score, the best one, train/validation/test metrics and the
confusion matrix of every model. A rerun of a run name adds rows to
`experiments.csv`; the latest per model counts.

### Experiments

Every run with the number of models, its best model by validation F1, its
test metrics and the settings in which it differs from the project. Select
a run to see its trials, and every file it wrote: figures (class
distribution, box plots, histograms, correlation, split counts, feature
distributions per split, a PCA scatter, feature scatter pairs, confusion
matrices, ROC curves, the loss curve, error timelines, the model
comparison), the reports and the settings snapshot. *HTML report* writes
one self-contained page with every run's metrics, confusion matrices and
figures and opens it. *Delete run…* removes a run's folder and rows.

## Analysis tab: earthquakes, faults, labels, figures and regions

This tab takes a feature table and puts it next to the earthquake catalog
and the active faults, the way the SCAP platform did: which events happened
near the station and when, which fault is closest, a label per window, and
the figures. Its last pane, *Regions*, needs only the catalog: it splits the
epicentres into seismic regions.

### Inputs (top of the tab)

- **Feature table**: a `<name>_features.npy` or `.csv` from the Features
  tab. The run metadata beside it supplies the station code and the window
  length. SCAP's own result CSVs (`YYYY_MM_DD_HH_wNN` window ids) load too.
- **Earthquake catalog**: the AFAD export, CSV or `.gz`, with the time zone
  its times are in (AFAD exports are UTC; SCAP kept a copy in Turkey time).
- **Station list** and **Station**: the code is looked up in the list (the
  built-in AFAD list when the field is empty); tick *position* to type the
  latitude and longitude instead.
- **Fault lines**: the MTA active fault shapefile (`.shp` with its `.dbf`),
  optional. MTA sells this data and does not allow redistribution, so it is
  not shipped with the application; any polyline shapefile works.

Everything below updates as soon as these are set.

### Catalog

The events of the table's period that matter at the station, by SCAP's
distance bands (all events within 100 km, M3+ to 300 km, M5+ to 500 km, M6+
to 1000 km), with distance, direction and, when the fault lines are loaded,
the nearest fault to each epicentre and which side of it the event lies. The
summary names the main event (the largest M5+). The map shows the station,
the events coloured by band and sized by magnitude, the main event, the
distance rings and the faults; set the half-width to zoom. *Map as PNG/SVG*
and *Events as CSV* export them.

### Labels

Adds SCAP's columns to the table: for every window the events whose origin
time falls inside it, their magnitudes, positions, distances, distance
classes (1: to 100 km, 2: to 300, 3: to 500, 4: beyond) and directions from
the station, the count, and a 0/1 flag. Writes `<name>_etiketli.csv` and
`.npy` beside the table and previews the result.

### Figures

Any numeric column as SCAP drew its metrics: a filled trace (red above zero,
blue below) with the earthquakes on a right-hand magnitude axis, one marker
shape per distance band, coloured by depth, labelled from M5. Three views:
*One day*, *All days* end to end (with the main event as a red line), or a
window of days *Around the main event*. The y range follows SCAP for its
five metrics and the data otherwise; untick *automatic range* to fix it.
Export the shown figure as PNG (at the chosen dpi) or SVG, or every daily
figure plus the combined one into a folder as a job.

### Grid

Station comparison: add the feature tables of several stations, pick the
column, and *Build grid* renders the daily figures of every station for the
days they share, stations down and days across, as one PNG.

### Regions

Seismic regionalisation: the epicentres of the catalog loaded at the top are
grouped into regions, each region is written as a catalog of its own, and
the result is drawn on a map. This pane is a port of the
`earthquake-clustering-eafz` script by Zeynep Çalım (MIT), the code of
Z. Çalım, S. Çimen, T. Yıldırım and Ş. Ersoy, "Analysis and Visualization of
Seismic Activity Before and After a Major Earthquake for Earthquake
Prediction in High-Risk Areas", 2025 33rd Signal Processing and
Communications Applications Conference (SIU), pp. 1–4,
doi:[10.1109/SIU66497.2025.11111859](https://doi.org/10.1109/SIU66497.2025.11111859).

Only the latitude and longitude columns are used (`latitude`/`enlem`,
`longitude`/`boylam`, any case), so a table without dates or magnitudes
works here even though the other panes cannot use it; rows without
coordinates are skipped.

- **Methods**:
  - *SOM*: a self-organising map of 1 × *Regions* units; every event joins
    the region of its nearest unit. A unit that attracts no event gives no
    region, so there can be fewer regions than asked for.
  - *SOM + Mean Shift*: a larger map (10 × 10 by default) first summarises
    the catalog, then Mean Shift groups the map's units by density and so
    decides the number of regions itself. *Mean Shift quantile* sets its
    bandwidth: a larger value gives fewer, larger regions.
  - *Training iterations* and *Random seed* apply to both. The same seed
    gives the same regions.
- **Maps**: *Area name* is the first line of the map titles. *Marker* adds a
  labelled point (a station, a city). The layers are the country outlines
  shipped with the application, the faults (the shapefile loaded at the top
  of the tab, or the shipped East Anatolian Fault traces when none is
  loaded), a *Provinces* polygon shapefile if you give one, and the
  *Cities* labels: the sixteen cities around the East Anatolian Fault by
  default, or your own CSV of `lat, lon, name` (an optional `align` column
  with `right` puts the label left of the point). Maps are 12 × 10 inches at
  the chosen dpi; tick *and SVG* for a vector copy (large for big catalogs).
- **Run** starts a job. The pane then lists every region (centre, latitude
  and longitude range, number of events, file) and shows the maps; choose
  the map above the picture. The map language is the application's.

The output folder receives the script's files and two more:

| file | content |
|---|---|
| `map_01_study_area_plain.png` | every event, no layers |
| `map_02_study_area_geographic.png` | every event over the boundaries and faults |
| `map_03_som.png`, `map_04_som_meanshift.png` | the regions of each method, with their centres |
| `som_region_N.csv`, `sommeanshift_region_N.csv` | the catalog lines of region N, every original column untouched |
| `regions_summary.csv` | the region table |
| `regions_run.json` | what was run: settings, the catalog's SHA-256, the layers used, the version |

The script's interactive HTML maps are not produced; the map in the pane
replaces them. The clustering is this application's own implementation of
the same two algorithms (the script uses MiniSom and scikit-learn), with its
own random numbers: for a seed the regions are reproducible here, but they
are not the Python script's regions event for event. Keep a paper's maps and
tables with the implementation that produced them. Map lines come from
Natural Earth and do not necessarily depict accepted national boundaries;
the maps say so.

## Indicators tab: catalog-based seismicity indicators

This tab works on the earthquake catalog alone, the way the
Catalog-Based-Indicators tool did: it slides a time window over the events
of a region and computes, for every window, the Gutenberg-Richter b- and
a-value, the magnitude deficit, the square-root energy release rate, η (the
scatter around the Gutenberg-Richter line), μ (the mean interval between
characteristic events), the mean magnitude and the elapsed time, then
smooths them and plots them with the M5+ events marked. It follows the
tested fork of that tool, so undefined values are blank rather than zero.

### Catalog and selection

- **Catalog**: an AFAD export (CSV or `.gz`) with `Date` in
  `dd/mm/YYYY HH:MM:SS`, `Latitude`, `Longitude` and `Magnitude`. *Describe*
  loads it, applies the selection below and reports what is left.
- **Events**: the *whole catalog*, the events within a *buffer* of the
  **East Anatolian Fault Zone** (its main strand from Karlıova to Antakya
  and the northern Sürgü-Çardak strand, from the GEM Global Active Faults
  Database, CC BY-SA 4.0, shipped with the application), the events near
  the traces of your own **GeoJSON** file (LineString features; a `strand`
  property lets you pick strands), or the events **within a circle**. The
  default is the EAFZ with a 30 km buffer.
- **From date** drops the events before a date, for instance before a
  network densified.
- **Homogenise magnitudes** maps the AFAD magnitudes before 2013-11-20 (Md,
  then the older Ml) onto the 2014-2019 ML/Mw practice by matching
  exceedance rates. The conversion shipped with the application was fitted
  on the declustered national catalog; *Fit…* refits it on a national AFAD
  catalog of your own and saves it as JSON. Mixing the periods without this
  puts an artificial regime change into every indicator (within 30 km of
  the EAFZ, Mc and the b-value both jump at the start of 2014).
- **Decluster** keeps only the Gardner-Knopoff mainshocks and singles. Most
  indicators then rest on far fewer events.

### Indicators

- **Completeness Mc**: events below it do not enter the b-value, a-value
  and η. *Estimate* gives the maximum-curvature estimate plus 0.2 on the
  selected events. The original analysis used 1.3 for the EAFZ; with the
  2014+ ML catalog use about 2.0.
- **Magnitude bin**: the catalog's rounding (0.1 for AFAD); the b-value
  uses Mc minus half a bin.
- **Window** and **Step** in days, weeks, months or years, as calendar
  durations (a month step from the 31st lands on the last day of the next
  month). The windows start at the first event and advance by the step while
  they fit before the last event; an event on a window's end belongs to the
  next window.
- **Half-Gauss σ**: the mean magnitude and the energy rate are also
  computed with weights rising towards the newest event.
- **Moving averages**: trailing averages over the given numbers of windows,
  one figure per average, or all of them *on one figure*.
- **Bollinger bands**: a trailing mean with bands at ± k sample standard
  deviations.
- **Risk label**: `Risk_Durumu` marks the windows that end within the given
  number of days before an event of at least the given magnitude ("3 aydan
  az", otherwise "3 aydan fazla").
- **Output root**, **Figures**: the run writes into
  `Deprem_Analiz_Grafikleri_<catalog>_<selection>_<window>_<step>_Dikdörtgen/`
  under the root, with PNG figures at the chosen dpi, or the table alone.

### Results

The run is a job (stop it from the jobs panel or the *Stop* button). Its
folder holds `<prefix>_pencereler.csv`, the window table with the columns
of the original tool (`Window_Index`, `x_label`, `Window_Start_Time`,
`Window_End_Time`, `T`, `b_value`, `a_value`, `Delta_M`, `eta`, `mu`,
`Event_Count`, `Mmax_obs`, `Mmean_rectangular`, `dE_sqrt_rectangular`,
`Mmean_half_gauss`, `dE_sqrt_half_gauss`, `Mmax_obs_first_5_to_6`,
`Mmax_obs_first_6_plus`, `Risk_Durumu`), the annual event counts and one
figure per indicator, moving average and Bollinger setting. The *Results*
pane shows the table and the figures of the last run; *Open an earlier
run…* shows any such folder.

The figures mark the window in which each M5-6 (purple star) and M6+
(orange star) event first appears. The table is readable by the Python
tools; numbers are written as pandas writes them.

## Early warning tab: ayzek

This tab runs **ayzek**, the earthquake early-warning pipeline: it reads
continuous three-component waveforms from several stations, detects an
earthquake at each station with a learned detector, declares an event when
the stations agree within the P travel time, estimates the magnitude from the
peak P displacement and locates the event. ayzek is a separate program
(GPL-3.0-or-later, by H. Oğuz Bolat, Sibel Çimen, Bülent Bolat and Tülay
Yıldırım, Yıldız Technical University; <https://doi.org/10.5281/zenodo.23672495>),
frozen at its version 1.0.0 and shipped beside the application with its
model weights, its licence and its source (`third_party/ayzek`). The tab
builds its command line and shows what it prints; nothing of the method is
reimplemented here. Please cite ayzek when you use its results.

The line at the top says which copy will run: the one shipped with the
application, a path you chose (*Choose executable...*), or one found on the
PATH; *Use the shipped copy* forgets a chosen path. The shipped copy's
`models/` folder is used unless you set another one.

### Replay

- **Input: Replay miniSEED files.** Add one file per station, each holding
  that station's HH? channels at 100 Hz. The station code is read from the
  records and its position from the station table, which lists the TU and
  KO networks; for another network give your own table (`code,lat,lon`).
- **Speed**: 1 is real time, 10 ten times faster, 0 as fast as the machine
  allows. **Detect from (UTC)** suppresses triggers before a time while the
  earlier data still warms up the filters.
- **AFAD catalog**: an AFAD export (CSV). With it, every alarm is compared
  with the catalogue and the final report counts correct, false and missed
  alarms; without it, alarms are reported but not scored.

### Live

- **Input: Live SeedLink stream.** Give the server as `HOST:PORT` and the
  streams as `NET.STA[,NET.STA...]`; every station needs coordinates in the
  station table. **Archive to** appends every record received to
  `DIR/STATION.mseed`, which replays later like any file. **Duration** stops
  the input after that many seconds; 0 runs until you press *Stop*.
- Live input is not available in the Windows build of ayzek (it needs POSIX
  sockets); replay files instead.

### Detection settings

The defaults are ayzek's frozen ones; a field reading *default* leaves the
option out. **Detector** chooses the 6 s window detector (three seeds), the
streaming onset transformer, or the STA/LTA and FilterPicker references.
**Stations to declare**, **Trigger threshold**, **Trigger windows**,
**Instant threshold** and **Release** are the trigger rule; **Location**
picks the transformer's geometry head or the P and S picks. **Assess every
alarm** judges each alarm from its S-P picks and writes `assess.csv` in the
output folder; **Write every window's detector probability** fills
`scores/` there. **Warning sites** (`NAME,LAT,LON`, one per line) add the
S-wave warning time at those places to the report. **Extra options** passes
anything else as on the command line (`ayzek --help` lists them;
`third_party/ayzek/README.md` explains them).

### Results

While the run goes, every tagged line ayzek prints (`ALARM`, `PLOC`, `PDMAG`,
`LOCATE`, `ASSESS`...) is a row of the table: the alarm number, its time
and the message. The full output, including the final report, is in the
log below it, and the line under the log summarises the report's counts:
alarms, how many match a catalogue event, catalogue events detected (and
detected before the S wave) and missed. *Open output folder* opens the
working directory of the run.

## Settings tab

**Interface** chooses the language. The **tools** section locates the
original command-line tools (`sismokaos`, `sfc`, `tdvms`) for anyone who
wants to run them beside the application: give the executable or the
project directory (a checkout with its `pyproject.toml`), and *Check* asks
the tool for its version. Nothing in the other tabs needs this; every tool
is built into the application. The `ayzek` entry is the exception: it is a
separate program, shown here as *shipped with the application*, and the
Early warning tab runs it.

## Where files live

| what | Linux | macOS | Windows |
|---|---|---|---|
| application settings (window layout, language, last folders) | `~/.config/sismokaos/sismokaos-gui.conf` | `~/Library/Preferences/com.sismokaos.sismokaos-gui.plist` | registry, `HKCU\Software\sismokaos\sismokaos-gui` |
| mailbox and defaults (`mailbox.env`, `settings.env`) | `~/.config/tdvms/` | `~/.config/tdvms/` | `%APPDATA%\tdvms\` |
| a study (`study.toml`, every run's results and `run.json`) | the study folder you chose | | |
| campaigns, ledgers, downloaded archives | the campaigns folder you chose | | |
| feature extraction results, labelled tables | the output directory you chose | | |
| classification projects (`project.toml`, `results/`) | the project folder you chose | | |
| indicator tables and figures (`Deprem_Analiz_Grafikleri_*/`) | the output root you chose (`~/indicators` by default) | | |
| region maps, per-region catalogs, `regions_run.json` | the output folder you chose (`~/regions` by default) | | |
| ayzek's assessment table and scores | the output folder you chose (your home folder when empty) | | |
| the shipped ayzek (executable, `models/`, licence) | inside the AppImage, `usr/share/sismokaos-gui/ayzek` | `sismokaos-gui.app/Contents/Resources/ayzek` | `ayzek\` beside the executable |

Campaign files are shared with the command-line `tdvms` tool, which uses the
same ledger and env files but a different lock file: never run both on the
same campaign at the same time.

## Troubleshooting

- **A run says the ledger is held by another process.** Another copy of the
  application or the command-line tool has it. Stop that one; if it crashed,
  the lock clears itself after a while or can be removed (`.qlock` beside the
  ledger).
- **No mail arrives.** *Mailbox… → Check login* must succeed; for Gmail use
  an app password and make sure IMAP is enabled. Requests go out from the
  plus-addresses of the *Address* field, so that mailbox must accept them.
- **Calibration fails with too few quiet windows.** Raise the minimum
  magnitude or lower the maximum distance so fewer events exclude time, use
  more data, or reuse a calibration from a longer run of the same station.
- **The station is not in the embedded list.** Type its latitude and
  longitude (*set the station position directly*) or point to your own
  station list CSV with code, latitude and longitude columns.
- **Mixed sample rates in the data directory.** One run handles one native
  rate; split the files by rate.
- **The update check cannot reach GitHub.** It needs outgoing HTTPS; behind a
  proxy or offline, update with the install command when you can.
- **Something else.** Copy the job's log (*Copy log* in the Jobs panel) and
  open an issue at
  <https://github.com/SismoKaos-Research/sismokaos-gui/issues>.

## Licence

Copyright (C) 2026 H. Oğuz Bolat.

sismokaos-gui is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version (GPL-3.0-or-later). It is distributed in the hope that it
will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. The full text is in
`LICENSE`. Releases up to and including v0.2.1 were published under the MIT
licence and remain available under it.

The source of every release is this repository at the release's tag, with
its submodules (`git clone --recursive`).

Third-party parts, each with a provenance note:

| Part | Licence | Note |
| --- | --- | --- |
| Qt 6 (linked dynamically) | LGPL v3 | <https://www.qt.io/licensing> |
| XGBoost and dmlc-core (linked statically) | Apache 2.0 | `third_party/XGBOOST-PROVENANCE.md` |
| libsvm | BSD 3-clause | `third_party/libsvm/PROVENANCE.md` |
| toml++ | MIT | `third_party/tomlplusplus/PROVENANCE.md` |
| miniz | MIT | `third_party/miniz/PROVENANCE.md` |
| Seismogenic fault sources of Turkey and its neighbours (EFSM20, clipped and simplified; a separate data file) | CC BY 4.0 | `data/faults/PROVENANCE.md` |
| East Anatolian Fault Zone strands (GEM GAF-DB; a separate data file, distributed alongside, not as part of, the program; remains CC BY-SA 4.0) | CC BY-SA 4.0 | `data/faults/PROVENANCE.md` |
| Country outlines (Natural Earth 5.1.1, clipped and simplified) | public domain | `src/analysis/data/PROVENANCE.md` |
| ayzek (a separate program run by the Early warning tab) | GPL-3.0-or-later | `third_party/AYZEK-PROVENANCE.md` |

ayzek is not linked into the application: it is shipped beside it with its
licence, and its source is the `third_party/ayzek` submodule.

The two fault files are data, not part of the program. They are installed
as files beside it (the `data` folder of each package, with `LICENSE.txt`
and `PROVENANCE.md`) and keep their own licences:

- **Fault lines on the maps**: the crustal fault sources of the European
  Fault-Source Model 2020. Basili R., Danciu L., Beauval C., et al. (2022),
  *European Fault-Source Model 2020 (EFSM20): online data on fault geometry
  and activity parameters*, Istituto Nazionale di Geofisica e Vulcanologia
  (INGV), <https://doi.org/10.13127/efsm20>, CC BY 4.0; clipped to Turkey
  and its neighbours, simplified and reduced in attributes. These are
  simplified fault sources of a hazard model, not an active fault map.
- **The EAFZ catalog zone**: fault sections of the GEM Global Active Faults
  Database (Styron and Pagani 2020,
  <https://github.com/GEMScienceTools/gem-global-active-faults>), CC BY-SA
  4.0. A modified copy of this file that you pass on stays under CC BY-SA
  4.0.
