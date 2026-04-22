# Opalite

Local-first spatial assistant for indoor navigation and assistive perception,
built around an Intel RealSense D435i depth camera.

Phase 1 target: a native C++ GUI on Windows that captures live color + depth,
shows a top-down bird's-eye view, and flags forward-cone obstacles.

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

Current status: Phase 1 scaffold only. Run prints a single startup line and
exits. See `.claude/plans/i-m-starting-a-fresh-whimsical-wilkes.md` (Google-
Drive-level reference, may not be present in the repo) for the full Phase 1
plan.

## Controls

(Placeholder — filled in once the ImGui shell lands in step 3.)

## Repo layout

```
src/         C++ sources and the real CMakeLists.txt
include/     project headers
data/        saved frame pairs and runtime artifacts
bin/         build output (gitignored)
build/       CMake build trees (gitignored)
docs/        project proposal, demo script, references
```
