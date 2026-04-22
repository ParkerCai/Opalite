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

### Keyboard

- **Q / ESC / Quit button** — exit cleanly.

### Controls pane (live)

- **Save frame** — write timestamped `YYYYMMDD_HHMMSS_{color.png,depth.png}` into `data/saved_frames/`. Depth PNG is 16-bit millimetres (use `cv::IMREAD_UNCHANGED`).
- **Free space** — analyzer toggle and sliders:
  - `blocked (m)` — anything inside this distance flags a sector as blocked. Default 0.50.
  - `horizon (m)` — distance at which a sector's clearance score saturates at 1.0. Default 3.0.
  - `span` — total horizontal image fraction covered by the three sectors. Default 0.95 (~65° FoV cone).
  - `beam` — width of the narrow forward-hazard center sector. Default 0.25 (~17° beam).
- **Top-down** — bird's-eye occupancy map sliders:
  - `extent (m)` — half-width (X) and full-depth (Z) of the world grid. Default 5.
  - `cell (m)` — world metres per output pixel. Default 0.02.
  - `min Z (m)` — near-plane cutoff; depth closer than this is ignored. Default 0.20.

The Controls pane also shows live render/camera FPS, median & p95 pipeline latency, stream resolutions, min/max depth in frame, per-sector near-depth + clearance score, current forward distance, and the suggested direction.

## Free-space analyzer notes

- **Near-depth = 5th percentile of valid pixels per sector**, not raw min. A single noisy pixel can't move a sector's reading.
- **Minimum-support gate** (`minValidPixels = 200`): sectors with fewer valid samples are "insufficient data" and are never flagged blocked.
- **Sticky-center policy** (`sideBiasM = 0.25 m`): if the center reading is usable and not blocked, stay on CENTER unless a side's near-depth beats center's by at least 0.25 m. Keeps guidance calm rather than flipping over centimetre-scale deltas.

## Testing

Manual validation procedures (depth-alignment sanity check, latency spot-check) live in [evaluation/TESTING.md](evaluation/TESTING.md).

## Repo layout

```
src/         C++ sources and src/CMakeLists.txt (FetchContent + find_package)
include/     project headers
data/        saved frame pairs and runtime artefacts (gitignored contents)
bin/         build output (gitignored)
build/       CMake build trees (gitignored)
docs/        project proposal and demo script
```

## Known limitations (Phase 1)

- **No height filtering** — the top-down map bins ceiling, wall, and torso-height pixels into the same (X, Z) cells, so overhead obstructions can blob together with floor-plane ones. Y-range gating is Phase 2.
- **Single-frame analyzer** — no temporal smoothing. Moving the camera quickly produces a brief wobble in the Suggested direction.
- **D435i depth floor** ~0.28 m — objects closer than that show up as invalid (zero) pixels, not near-range obstacles.
- **Reflective / transparent surfaces** produce depth holes; with min-support gating these reduce to "insufficient data" rather than false alarms, but the sector is effectively blind in that direction.
- **No semantic classification** — geometry-only. "Chair vs sofa vs wall" is out of scope for Phase 1 by design; semantics land in Phase 2 on DGX Spark.
