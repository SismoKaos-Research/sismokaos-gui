# sismokaos-gui

A desktop front end for the sismokaos command-line tools:

| tool | repository | what it does |
|---|---|---|
| `sismokaos` | Sismokaos-featureExtract | download, preprocess, chaotic feature extraction from config.json |
| `sfc` | seismic-features-classification | projects, feature extraction, classification runs, reports |
| `tdvms` | tdvms-pull | AFAD/TDVMS download campaigns |

The GUI never imports their code. It runs them as processes, hands them their
own config files and command lines, and reads what they print and write. Every
run is a *job*: it appears in the jobs panel with its live output, progress,
elapsed time and exit status, and can be cancelled.

Native C++20 with Qt 6 Widgets. Turkish and English, switchable at run time.

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
src/core/      Module interface, AppContext, ModuleRegistry, I18n
src/modules/   one directory per GUI module (tab), each its own CMake target
src/app/       MainWindow and main()
tests/         QtTest suites for jobs, progress parsing, tool resolution
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
