# sismokaos-gui

A desktop application for the SismoKaos workflow: downloading continuous
seismic data from AFAD/TDVMS and FDSN services, extracting statistical and
chaotic features from it, relating them to the earthquake catalog and the
active faults around a station with publication figures, and (coming)
classifying the features. Everything
runs inside the application; no Python, Qt or other installation is needed.
Turkish and English, switchable while running. Linux, Windows and macOS.

For building from source, the code layout and how to add a tab, see
[DEVELOPING.md](DEVELOPING.md).

## Contents

1. [Install and update](#install-and-update)
2. [The window](#the-window)
3. [Data tab: TDVMS campaigns and FDSN pulls](#data-tab-tdvms-campaigns-and-fdsn-pulls)
4. [Features tab: preprocessing and feature extraction](#features-tab-preprocessing-and-feature-extraction)
5. [Analysis tab: earthquakes, faults, labels and figures](#analysis-tab-earthquakes-faults-labels-and-figures)
6. [Settings tab](#settings-tab)
7. [Where files live](#where-files-live)
8. [Troubleshooting](#troubleshooting)

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

The tabs along the top are the tools: **Data**, **Features**, **Analysis**,
**Classification** (a placeholder until the classification port lands), and
**Settings**.

The **Jobs** panel at the bottom lists everything the application runs: a
download campaign, a calibration, a feature extraction. Each job shows its
state, progress, elapsed time and, when selected, its live log. *Cancel*
stops a job cooperatively: it finishes the step it is on and writes nothing
half-done. *Copy log* puts the log on the clipboard for a bug report. The
panel can be hidden and shown from *View → Jobs*; it reappears when a job
starts.

**Language.** *View → Language* switches between Türkçe and English
immediately; the choice is remembered.

Closing the window while jobs run asks first; confirming cancels them.

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

## Analysis tab: earthquakes, faults, labels and figures

This tab takes a feature table and puts it next to the earthquake catalog
and the active faults, the way the SCAP platform did: which events happened
near the station and when, which fault is closest, a label per window, and
the figures.

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

## Settings tab

**Interface** chooses the language. The **tools** section is for the two
command-line tools not yet ported (`sismokaos`, `sfc`): give the executable
or the project directory (a checkout with its `pyproject.toml`), and *Check*
asks the tool for its version. Nothing in the Data or Features tabs needs
this.

## Where files live

| what | Linux | macOS | Windows |
|---|---|---|---|
| application settings (window layout, language, last folders) | `~/.config/sismokaos/sismokaos-gui.conf` | `~/Library/Preferences/com.sismokaos.sismokaos-gui.plist` | registry, `HKCU\Software\sismokaos\sismokaos-gui` |
| mailbox and defaults (`mailbox.env`, `settings.env`) | `~/.config/tdvms/` | `~/.config/tdvms/` | `%APPDATA%\tdvms\` |
| campaigns, ledgers, downloaded archives | the campaigns folder you chose | | |
| feature extraction results, labelled tables | the output directory you chose | | |

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

MIT, see `LICENSE`. Qt is used under the LGPL v3; miniz under MIT
(`third_party/miniz/PROVENANCE.md`).
