# Opalite

Local-first spatial assistant for indoor navigation and assistive perception,
built around an Intel RealSense D435i depth camera.

Phase 1 baseline: a native C++ GUI on Windows that captures aligned
color and depth, renders a top-down occupancy map, and uses depth
geometry to score left / center / right free space and suggest a
direction.

Phase 2 adds two optional feedback layers on top of the Phase 1
geometry baseline, both running locally:

- **Sonar** — continuous stereo audio that pulses and pans by sector
  clearance. No network. Closer obstacles → louder and faster hum.
- **Brain** — on-demand visual-language caption from a local Gemma 4
  VLM via Ollama. Geometry stays authoritative for distance and
  direction; Gemma only names the object.

Phase 3 ports the whole pipeline to an Android handheld. The D435i
plugs into the phone over USB-OTG; geometry, sonar, and the Brain HTTP
client are the same C++ modules compiled into an NDK `.so`. Output is
the phone's speakers/headphones (sonar + TTS) plus a 4-pane preview
mirroring the Windows layout. Brain inference still round-trips to
Ollama on the LAN. See [android/opalite-edge/README.md](android/opalite-edge/README.md).

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

**For the optional Brain pane (Phase 2B):**

- **Ollama** running locally, reachable at `http://localhost:11434`
  (install from [ollama.com](https://ollama.com)).
- **Gemma 4 E2B** pulled: `ollama pull gemma4:e2b` (~7 GB, multimodal).
  Other tags work by editing `BrainConfig::model`.

The Brain pane is an overlay: the rest of the app runs identically
whether or not Ollama is reachable.

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

Current status: live four-pane layout (Color and Depth on top,
Top-Down and Brain below, Controls strip at the bottom), per-frame
free-space analyzer with clearance scores and suggested direction,
timestamped frame save, rolling pipeline-latency meter + CSV log,
stereo sonar audio feedback layer, and an on-demand Gemma 4 VLM
caption pane with both structured and free-form query modes.

## Controls

### Keyboard

- **Q / ESC / Quit button** — exit cleanly.
- **M** — toggle Sonar mute (fades smoothly, no click).
- **SPACE** — fire a Brain query (Ask). Edge-detected + state-gated
  so a held key only issues one request per press.

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
- **Sonar** — stereo-audio feedback layer (can be left on in the background):
  - `on` checkbox and `M` hotkey both toggle mute. Fade is smooth.
  - `vol` — master volume. Default 0.25.
  - `pitch` — carrier frequency in Hz. Default 110 (low hum).
  - `falloff` — amplitude exponent. Higher = quieter mid-range, steeper
    onset close-up. Default 4 (quartic).
  - Per-sector L / C / R amplitude meters.

### Brain pane (Phase 2B)

Semantic overlay driven by a local Gemma 4 VLM through Ollama. The
caption appears in the bottom-right pane; geometry and sonar keep
running live during the 1–3 s forward pass.

- **Mode radio**
  - **Structured** (default) — Gemma emits `{"main_object":"<noun>"}`
    via JSON-schema-constrained decoding. The app composes a
    deterministic sentence from the object name + the geometry layer's
    closest sector + sticky-center direction. Example: `Person on the
    left at 0.65 m, go right.`
  - **Freeform** — Gemma writes the whole sentence. Useful for prompt
    iteration and comparison.
- **Editable prompt** — multiline text box, separate buffer per mode.
- **Ask button** / **SPACE** — fire a request. Status line shows
  `requesting... N.N s` while pending, `done (N.NN s, median M.MM s
  over K)` when it returns, or `failed (N ms)` on error.
- **Raw JSON** is shown muted below the composed sentence so the split
  between VLM recognition and sensor-driven wording is explicit.
- **Brain latency CSV** — every request is appended to
  `data/brain_latency.csv` with `wall_ms, roundtrip_ms, ok, mode`.

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

- **No height filtering** — the top-down map bins ceiling, wall, and torso-height pixels into the same (X, Z) cells, so overhead obstructions can blob together with floor-plane ones.
- **Single-frame analyzer** — no temporal smoothing. Moving the camera quickly produces a brief wobble in the Suggested direction.
- **D435i depth floor** ~0.28 m — objects closer than that show up as invalid (zero) pixels, not near-range obstacles.
- **Reflective / transparent surfaces** produce depth holes; with min-support gating these reduce to "insufficient data" rather than false alarms, but the sector is effectively blind in that direction.
- **No object recognition in the real-time loop** — Phase 1 runs at
  the camera's 30 FPS using depth geometry only.

## Known limitations (Phase 2)

- **Brain round-trip is ~2–3 s** for `gemma4:e2b` on the dev PC — fine
  for demos, too slow for continuous guidance. Per-frame captions
  would require a much smaller model or on-device inference (Phase 3).
- **No TTS yet** — captions are text-only. Windows SAPI / Android
  `TextToSpeech` slot in during Phase 3.
- **Sonar cadence is global** — all three sectors share the same
  carrier frequency and falloff shape. Per-sector timbre (e.g.
  different pitch per side) could improve discrimination.

## Phase 3 — Android edge device

The phone is the compute node. D435i plugs into the phone via USB-OTG;
the Java `MainActivity` owns the librealsense pipeline and hands every
depth/color frame into `libopaliteedge.so`, which is the same
`free_space.cpp` / `sonar.cpp` / `brain_client.cpp` the Windows build
uses, bridged through a thin `jni_bridge.cpp`. `miniaudio` picks
AAudio or OpenSL on the NDK side and drives the phone's speaker /
headphones directly.

Shipped tonight:

- **Four-pane preview** — Color + Depth (top), Top-Down + Sonar
  column (bottom). Color has L / C / R ROI overlays and a live
  `fwd X.XX m` readout; Top-Down has clearance-colored cone wedges
  per sector. Depth uses a turbo-ish colormap; color is
  depth→color-aligned with a `HoleFillingFilter` so the two panes
  share a frustum.
- **Live L / C / R sonar amplitude meters** polled back from the
  NDK sonar at 20 Hz.
- **Single 400 dp "Describe" button** pinned to the bottom with
  rounded bottom corners matching the phone's display radius.
  Short haptic on press-down, longer haptic on release so a blind
  user gets tactile confirmation without looking.
- **Long-press Describe → voice capture** via Android
  `SpeechRecognizer`. The transcript is first matched against
  local voice commands (`turn off sonar`, `voice off`, etc.); if
  none match, it's forwarded to Gemma as the prompt and the reply
  is spoken back via TTS.
- **Threshold-cross TTS** — one-shot "Blocked at X meters" when
  the center sector goes clear → blocked, with a 3 s cooldown.
- **Immersive fullscreen + dark theme + Opalite logo**. USB-attach
  intent auto-resumes the pipeline; `CAMERA` + `RECORD_AUDIO`
  permissions are requested in a single combined dialog.
- **Collapsible debug panel** with a persisted Brain host field
  (`SharedPreferences`) and a rolling log. Every Brain round-trip
  is appended to an on-device `brain_latency.csv` with the same
  schema as the desktop log.

See [android/opalite-edge/README.md](android/opalite-edge/README.md)
for build, runtime, and troubleshooting details.

### Known limitations (Phase 3)

- **Brain still requires LAN reachability** to a host running
  `ollama serve` with `gemma4:e2b`. On-device Gemma 4 via
  AICore / ML Kit Prompt API is planned for a follow-up —
  expected to eliminate the LAN dependency and shave the
  round-trip further.
- **USB-OTG quality is the single biggest variable**. USB-2
  dongles drop depth to <15 fps or fail outright; budget a
  known-good cable before any demo.
