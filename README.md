# AnimeAn

AnimeAn is a Qt desktop application for animation and vector drawing
experiments. It includes optional Python support through `pybind11`, so scripts
can inspect and edit the C++ scene model and use vector geometry helpers.

## Main Features

- Qt Widgets + OpenGLWidgets drawing interface.
- `AnimeModel` scene/xsheet data model.
- `AnimeVectorLogic` vector geometry algorithms.
- Embedded Python and optional standalone Python extension module.
- Python ExtraTool hooks for vector algorithm prototyping.
- Windows deployment target: `deploy_AnimeAn`.

## Project Layout

- `main.cpp`: application entry point.
- `mainwindow.*`: main window and dock/panel orchestration.
- `openglwidget.*`: drawing widget and tool event handling.
- `algorithm/animemodel.*`: core scene, layer, frame, asset, cell, vector image model.
- `algorithm/vectorlogic.*`: vector path and geometry algorithms.
- `pythonbind/python_bindings.cpp`: low-level Python bindings.
- `pythonbind/animemodel.py`: high-level Python wrapper.
- `pyfile/`: embedded Python scripts, hooks, and ExtraTool definitions.
- `build_scripts/agent_build.ps1`: release build and deploy verification script.

## Build

The project uses CMake.

### Windows / Qt

```powershell
cmake -S . -B build
cmake --build build --config Release
```

### Python Support

Embedded Python support is enabled by default. The project first looks for the
runtime in:

- `tools/python312`

To build the optional standalone Python extension module:

```powershell
cmake -S . -B build -DANIMEAN_BUILD_PYTHON_MODULE=ON
cmake --build build --config Release
```

## Deploy Verification

For agent-side release verification, use:

```powershell
PowerShell -ExecutionPolicy Bypass -File ".\build_scripts\agent_build.ps1"
```

The expected final log line is:

```text
===== AGENT BUILD DONE EXIT_CODE=0 =====
```

## Python Entry Points

Python binding and ExtraTool documentation:

- English design and API reference: `pybind_readme.md`
- Chinese summary and development notes: `python_bind_chinese_readme.md`
- OpenToonz parser notes in English and Chinese: `opentoonz_tools/README.md`

Minimal usage:

```python
from animemodel import AnimeModel

model = AnimeModel()
model.initialize(layer_count=2, frame_count=24)
model.add_polyline([(0, 0), (100, 80)])
```

ExtraTool algorithm development currently works best as a post-processing vector
workflow: select an ExtraTool, draw with the native Pen tool, receive hook events
in Python, process the current stroke or cell, write vector results back, then
refresh the widget.

## License

This repository includes third-party dependency `external/pybind11`. Its license
follows the upstream project; see the files in that directory for details.
