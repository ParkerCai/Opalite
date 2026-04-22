# Opalite

Local-first spatial assistant for indoor navigation and assistive perception,
built around an Intel RealSense D435i depth camera.

Phase 1 target: a native C++ GUI on Windows that captures aligned color
and depth, renders a top-down occupancy map, and uses depth geometry to
score left / center / right free space and suggest a direction.

## Prerequisites (Windows)

- **Intel RealSense SDK 2.0** installed to `C:\Program Files\RealSense SDK 2.0`
  (default installer location). Installer: [librealsense releases](https://github.com/IntelRealSense/librealsense/releases).
- **OpenCV 4.x** with `OpenCV_DIR` environment variable pointing at the
  directory containing `OpenCVConfig.cmake` (e.g. `C:\opencv_build\build\install`).
- **CMake 3.24+**.
- **Visual Studio 2022** with the Desktop C++ workload (provides MSVC + the
  `Visual Studio 17 2022` CMake generator).
- **Intel RealSense D435i** plugged into a **USB 3** port (blue-tab or "SS" labelled).

If the RealSense SDK is installed somewhere other than the default path, pass
`-DREALSENSE_SDK_DIR=<path>` at configure time.

## Build

```bash
cmake --preset x64-release
cmake --build build/x64-release --config Release
```

The executable lands in `bin/opalite.exe`, with `realsense2.dll` copied
alongside so there's no PATH setup required at runtime.

## Run

```bash
./bin/opalite.exe
```

Current status: live three-pane preview (full-width top-down, color +
aligned depth, Controls strip), per-frame free-space analyzer with
clearance scores and suggested direction, timestamped frame save,
rolling pipeline-latency meter + CSV log.

## Controls

- **Q / ESC / Quit button** — exit.
- **Save frame** — write timestamped color + 16-bit depth PNGs to `data/saved_frames/`.
- **Free space** — toggle the analyzer; sliders for `blocked (m)` (red-flag distance) and `horizon (m)` (full-clear distance).
- **Top-down** — sliders for world `extent (m)`, `cell (m)` resolution, and `min Z (m)` near-plane cutoff.

The Controls pane also shows live render/camera FPS, median & p95 pipeline latency, stream resolutions, depth range, and per-sector clearance.

## Repo layout

```
src/         C++ sources and the real CMakeLists.txt
include/     project headers
data/        saved frame pairs and runtime artifacts
bin/         build output (gitignored)
build/       CMake build trees (gitignored)
docs/        project proposal, demo script, references
```
