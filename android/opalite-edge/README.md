# Opalite Edge (Phase 3)

Android app that turns a D435i + phone into a handheld obstacle-aware edge device. Depth and color come from librealsense over USB-OTG. Geometry (three-sector free-space analysis), sonar spatial audio (miniaudio), and the Gemma-4-via-Ollama Brain client are the same C++ modules the Windows build uses, compiled into `libopaliteedge.so` via NDK.

## Architecture

- Java `MainActivity` owns the librealsense `Pipeline` and a single capture thread. Frames are aligned depth→color with a `HoleFillingFilter` applied so the two panes share a frustum and the parallax shadow next to close objects is filled in.
- Each depth frame is handed to `analyzeFreeSpaceNative`. Results drive:
  - `sonarUpdateNative` — continuous stereo hum, louder as clearance drops, panned per sector. Live L / C / R amplitude is polled back at 20 Hz via `sonarGetAmpsNative` and rendered as three ProgressBar meters next to the top-down pane.
  - Android `TextToSpeech` — one-shot "Blocked at X meters" when center crosses the threshold (3 s cooldown).
- Four live panes mirror the Windows layout: Color and Depth on the top row, Top-Down (X-Z occupancy) and Sonar controls on the second row. The Color pane has L / C / R ROI overlays and a "fwd X.XX m" text readout; the Top-Down pane has clearance-colored cone wedges for the three sectors.
- A full-width 400 dp **Describe** button pinned to the bottom. A tap JPEG-encodes the latest color frame and calls `askBrainNative` on a worker thread; the text response is spoken via TTS. Haptic feedback: a short 30 ms buzz on touch-down, a longer 150 ms buzz on release.
- **Long-press** the Describe button to record a spoken question with Android `SpeechRecognizer`. The transcript either (a) matches a local voice command (see below) and mutates app state directly, or (b) is appended to the Brain prompt so you can ask things like "what's on the desk in front of me" and get a spoken reply.
- Runtime settings exposed: Sonar on/off switch, volume / pitch / falloff sliders, Speak-response switch, and a collapsible debug panel with the Brain host field + rolling log.

## Voice commands (long-press Describe)

Recognized phrases are matched before the Brain call. If the phrase doesn't match a command, the transcript is sent to Gemma as the Describe prompt.

| Phrase | Effect |
| --- | --- |
| "turn off sonar" / "sonar off" / "mute sonar" | Switch Sonar off. |
| "turn on sonar" / "sonar on" / "unmute sonar" | Switch Sonar on. |
| "turn off voice" / "voice off" / "stop talking" | Disable TTS (Speak response off). |
| "turn on voice" / "voice on" | Enable TTS. |

## Build prerequisites

- Android Studio Iguana or later.
- **Pin Gradle JDK to 17** (`Settings → Build Tools → Gradle → Gradle JDK` → Temurin/JetBrains JDK 17). The project stays on Gradle 7.5.1 + AGP 7.4.2, which refuses to run on Java ≥19.
- NDK 26.x installed (`SDK Manager → SDK Tools → NDK (Side by side)`). CMake 3.22.1 is bundled with the NDK.
- `librealsense.aar` lives under `app/libs/` (already vendored).

## Runtime prerequisites

- USB-OTG cable that provides both USB-3 bandwidth and enough power for the D435i (>400 mA). Most generic dongles are USB-2; depth will either be slow or fail outright on those.
- Phone on the same Wi-Fi network as a machine running `ollama serve` with `gemma4:e2b` pulled, if you want the Describe button to work. The Ollama server must bind to `0.0.0.0` (set `OLLAMA_HOST=0.0.0.0:11434` on the host) so the phone can reach it over LAN. Sonar, TTS obstacle alerts, and voice commands all run fully offline.

## Permissions

On first launch the app requests `CAMERA` + `RECORD_AUDIO` in a single dialog. `VIBRATE` is a normal permission granted at install time. The app also registers a USB-attach intent-filter so plugging the D435i back in brings Opalite to the foreground and auto-retries the pipeline without a manual relaunch.

## Layout of shared C++

The NDK module compiles three sources from the shared desktop tree:

- `src/free_space.cpp` — the OpenCV-free raw-pointer core; Windows keeps using the same file with its `cv::Mat` overload compiled in under `-DOPALITE_USE_OPENCV`.
- `src/sonar.cpp` — miniaudio auto-selects AAudio/OpenSL on NDK; no code changes.
- `src/brain_client.cpp` — same core, the JPEG bytes arrive pre-encoded from Java instead of via `cv::imencode`.

The JNI entry points are all in `app/src/main/cpp/jni_bridge.cpp`.

## Running

1. Plug the D435i into the phone via USB-OTG. Android shows a "device attached" prompt; tap "OK" / "Always use this app" so the USB permission grants. The capture loop auto-starts.
2. Plug in headphones; the Sonar hum will otherwise come out of the phone's speaker.
3. Walk toward a wall: the hum grows louder and pulses faster; the L / C / R meters track live amplitude; at <0.8 m the phone says "Blocked at 0.6 meters."
4. **Tap** Describe for an on-demand VLM caption of what's ahead.
5. **Long-press** Describe, say a question or a voice command, release. The transcript either flips a setting locally or is forwarded to Gemma and the reply spoken.
6. Expand the debug panel ("Show debug") to edit the Brain host (`http://<pc-ip>:11434`) — it persists across launches via `SharedPreferences`.

## Latency log

Every Brain round-trip is appended on-device to `/storage/emulated/0/Android/data/com.opalite.edge/files/brain_latency.csv` with the same schema the desktop build uses (`wall_ms, roundtrip_ms, ok, mode`). Pull with:

```bash
adb pull /storage/emulated/0/Android/data/com.opalite.edge/files/brain_latency.csv data/brain_latency_android.csv
```

## Troubleshooting

- No depth when you plug the camera in → cable is likely USB-2. Test with Intel's reference test app before suspecting code.
- `libopaliteedge.so` fails to load → NDK not installed, or `abiFilters` in `app/build.gradle` doesn't match the phone's ABI. Default is `arm64-v8a`.
- Describe returns `ERROR: no response` → Brain host unreachable. Confirm `curl -X POST http://<pc-ip>:11434/api/generate -d '{"model":"gemma4:e2b","prompt":"hi","stream":false}'` works from the phone's Wi-Fi network; also check that Ollama was started with `OLLAMA_HOST=0.0.0.0:11434` and that the host firewall allows inbound 11434.
- Long-press records nothing / "voice: no results" → no on-device recognizer available. Install Google's Speech Services from the Play Store, or grant the app mic permission if the first dialog was denied.
