# Opalite Edge (Phase 3)

Android app that turns a D435i + phone into a handheld obstacle-aware edge device. Depth and color come from librealsense over USB-OTG. Geometry (three-sector free-space analysis), sonar spatial audio (miniaudio), and the Gemma-4-via-Ollama Brain client are the same C++ modules the Windows build uses, compiled into `libopaliteedge.so` via NDK.

## Architecture

- Java `MainActivity` owns the librealsense `Pipeline` and a single capture thread.
- Each depth frame is handed to `analyzeFreeSpaceNative`. Results drive:
  - `sonarUpdateNative` — continuous stereo hum, louder as clearance drops, panned per sector.
  - Android `TextToSpeech` — one-shot "Blocked at X meters" when center crosses the threshold (3 s cooldown).
- A Describe button JPEG-encodes the latest color frame and calls `askBrainNative` on a worker thread; the text response is spoken via TTS.
- Only the "Speak" switch, volume slider, and Brain host field are user-facing runtime settings.

## Build prerequisites

- Android Studio Iguana or later.
- **Pin Gradle JDK to 17** (`Settings → Build Tools → Gradle → Gradle JDK` → Temurin/JetBrains JDK 17). The project stays on Gradle 7.5.1 + AGP 7.4.2, which refuses to run on Java ≥19.
- NDK 26.x installed (`SDK Manager → SDK Tools → NDK (Side by side)`). CMake 3.22.1 is bundled with the NDK.
- `librealsense.aar` lives under `app/libs/` (already vendored).

## Runtime prerequisites

- USB-OTG cable that provides both USB-3 bandwidth and enough power for the D435i (>400 mA). Most generic dongles are USB-2; depth will either be slow or fail outright on those.
- Phone on the same Wi-Fi network as a machine running `ollama serve` with `gemma4:e2b` pulled, if you want the Describe button to work. Sonar and TTS obstacle alerts run fully offline.

## Layout of shared C++

The NDK module compiles three sources from the shared desktop tree:

- `src/free_space.cpp` — the OpenCV-free raw-pointer core; Windows keeps using the same file with its `cv::Mat` overload compiled in under `-DOPALITE_USE_OPENCV`.
- `src/sonar.cpp` — miniaudio auto-selects AAudio/OpenSL on NDK; no code changes.
- `src/brain_client.cpp` — same core, the JPEG bytes arrive pre-encoded from Java instead of via `cv::imencode`.

The JNI entry points are all in `app/src/main/cpp/jni_bridge.cpp`.

## Running

1. Plug the D435i into the phone via USB-OTG. Android shows a "device attached" prompt; tap "OK" / "Always use this app" so the USB permission grants.
2. Launch Opalite Edge. Plug in headphones; the Sonar hum will otherwise come out of the phone's speaker.
3. Tap **Start**. The capture loop begins and the free-space line updates (`L 0.81  C 0.62  R 0.94  dir CENTER  fwd 1.80m`).
4. Walk toward a wall: the hum grows louder and pulses faster; at <0.8 m the phone says "Blocked at 0.6 meters."
5. Tap **Describe what's ahead** for an on-demand VLM caption. Configure the Brain host to `http://<pc-ip>:11434` once; it's remembered per launch.

## Troubleshooting

- No depth when you plug the camera in → cable is likely USB-2. Test with Intel's reference test app before suspecting code.
- `libopaliteedge.so` fails to load → NDK not installed, or `abiFilters` in `app/build.gradle` doesn't match the phone's ABI. Default is `arm64-v8a`.
- Describe returns `ERROR: no response` → Brain host unreachable. Confirm `curl -X POST http://<pc-ip>:11434/api/generate -d '{"model":"gemma4:e2b","prompt":"hi","stream":false}'` works from the phone's Wi-Fi network; also check Windows Firewall.
