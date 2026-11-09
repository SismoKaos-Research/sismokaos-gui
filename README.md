# sismokaos-gui

A desktop front end for the sismokaos tools, with the tools themselves being
ported into it one by one so that nothing outside the application is needed:

| tool | repository | status in the GUI |
|---|---|---|
| `tdvms` | tdvms-pull | **ported to C++** (`src/tdvms`): campaigns, ledger, portal, mail, archives, coverage, FDSN pulls run in-process |
| `sismokaos` | Sismokaos-featureExtract | still driven as a process (download, preprocess, chaotic feature extraction from config.json) |
| `sfc` | seismic-features-classification | still driven as a process (projects, feature extraction, classification runs, reports) |

Every piece of work, ported or external, is a *job*: it appears in the jobs
panel with its live output, progress, elapsed time and exit status, and can be
cancelled. External tools are `ProcessJob`s around QProcess; ported code runs
as `TaskJob`s in worker threads.

Native C++20 with Qt 6 Widgets and one vendored C library (miniz, for ZIP
archives; `third_party/miniz/PROVENANCE.md`). Turkish and English, switchable
at run time.

## The tdvms port

`src/tdvms` is a module-for-module port of tdvms-pull, the AFAD/TDVMS
download campaign tool. It reads and writes the same files as the Python
tool: the JSON-lines ledger (`campaigns/<name>/ledger.jsonl` or
`<name>_ledger.jsonl`), `mailbox.env` and `settings.env` in the config
directory, the `.coverage.json` cache, and the FDSN day ledger. A campaign
started with one tool can be continued with the other, but not at the same
time: the two use different lock files.

| file | what it ports |
|---|---|
| `ledger`, `plan`, `campaign`, `slots`, `disk`, `settings`, `history`, `report` | the ledger and its states, window planning, campaign layouts, the address pool and its lock, the disk guard, credentials and defaults, station history, `tdvms status` |
| `mail`, `imap` | the MIME reader and classifier (Link, NoData, Foreign, Unrelated), a synchronous IMAP client with BODY.PEEK |
| `http`, `portal` | blocking HTTP behind an interface (fakes in the tests), the portal's typed outcomes |
| `fetch`, `coverage` | download and verification of archives, filing, miniSEED header scanning and gap reports |
| `supervisor`, `runner` | the reap/reclaim/retire/submit cycle, the campaign and queue loops as TaskJob work, the Watch and ntfy/desktop notifications |
| `fdsn`, `catalog` | whole-day pulls from an FDSN service, the embedded station and event catalogs |

The tests under `tests/tst_tdvms_*.cpp` mirror the Python suite case by case.

## Build

Needs CMake 3.21+, a C++20 compiler and Qt 6.5+ (Widgets, LinguistTools; Test
for the tests).

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
./build/src/app/sismokaos-gui
```

On Windows, `windeployqt build/src/app/sismokaos-gui.exe` collects the Qt
libraries next to the executable.

## Where the tools are found

For each tool, in this order: the executable path set on the Settings tab; the
`PATH`; `<project dir>/.venv/bin/<tool>` (`.venv\Scripts\<tool>.exe` on
Windows) when a project directory is set; `uv run --project <project dir>
<tool>` when that directory has a `pyproject.toml` and `uv` is installed. The
Settings tab shows which one applies and can ask the tool for its version.

## Layout

```
src/jobs/      Job (QProcess + progress parsing), JobModel, JobsPanel, ProgressParser
src/tools/     ToolDesc, ToolRegistry: descriptors and resolution of the CLIs
src/tdvms/     the C++ port of tdvms-pull (library, no GUI)
third_party/   vendored libraries, each with a PROVENANCE.md
src/core/      Module interface, AppContext, ModuleRegistry, I18n
src/modules/   one directory per GUI module (tab), each its own CMake target
               (tdvms is the first real one; settings; placeholders)
src/app/       MainWindow and main()
tests/         QtTest suites: jobs, progress, tool resolution, the tdvms port
translations/  sismokaos-gui_tr.ts, sismokaos-gui_en.ts
```

### Adding a module

1. Create `src/modules/<name>/` with a `CMakeLists.txt` like
   `modules/placeholder/CMakeLists.txt` (a static library appended to
   `SKG_MODULE_TARGETS` and `SKG_TRANSLATED_TARGETS`), and add it to
   `src/modules/CMakeLists.txt`.
2. Subclass `skg::Module` (`src/core/module.h`): `id()`, `title()`,
   `description()`, `requiredTools()`, `createWidget()`. The widget starts
   tools through `ctx.tools->makeSpec(...)` and `ctx.jobs->add(...)`.
3. Register it in `src/app/main.cpp` with `modules.emplace<YourModule>(...)`.
   Tabs appear in registration order.

User-visible strings go through `tr()`; widgets re-read them in
`changeEvent(QEvent::LanguageChange)`.

### Porting a tool

A ported tool is a library under `src/<tool>/` with no Qt Widgets in it,
tested on its own, plus a module under `src/modules/<tool>/` that drives it.
Long work is wrapped as `TaskJob` work (`jobs/taskjob.h`): it gets a
`TaskContext` for logging, progress and cooperative cancellation, and may
block on the network since it has its own thread. Keep file formats
compatible with the original tool so the two can share data.

### Adding a tool

One entry in `kTools` (`src/tools/toolregistry.cpp`): id, display name,
homepage, version arguments, progress parser name, environment to set. If the
tool prints progress in a new format, add a `ProgressParser` subclass in
`src/jobs/progress.h` and name it in `makeProgressParser`.

Progress formats understood today: `[i/n]` at the start of a line
(Sismokaos-featureExtract), and `@sfc {json}` events (`sfc --progress json`,
set through `SEISMIC_PROGRESS=json`).

## Translations

```sh
cmake --build build --target update_translations   # lupdate: sources -> translations/*.ts
```

Edit `translations/sismokaos-gui_tr.ts` (Qt Linguist or a text editor); the
`.qm` files are built and embedded automatically.

## Licence

MIT, see `LICENSE`. Qt is used under the LGPL v3.
