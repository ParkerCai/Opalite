# Opalite technical appendix

This appendix documents how the current code works, what was added for the CS5330 final project, and how to extend or evaluate the system without turning the browser client into a heavy app.

## 1. Repository layout

```text
opalite/
├── index.html
├── css/styles.css
├── js/
│   ├── app.js
│   ├── gemini-client.js
│   ├── camera.js
│   ├── audio-recorder.js
│   ├── audio-streamer.js
│   ├── cv/
│   │   ├── enhancement-pipeline.js
│   │   ├── frame-utils.js
│   │   ├── places365-client.js
│   │   ├── monodepth2-client.js
│   │   └── retinanet-client.js
│   ├── evaluation/
│   │   └── session-logger.js
│   └── worklets/
│       └── audio-processor.js
├── config.example.js
├── config.local.js            # gitignored, local only
├── docs/
│   ├── final_project_report.md
│   └── technical_appendix.md
└── evaluation/
    ├── README.md
    ├── run_eval.py
    ├── scenarios/
    ├── sample_runs/
    └── results/
```

The original hackathon app already had the live audio and image loop. The new work is concentrated in three places:

1. `js/cv/` for optional computer vision priors
2. `js/evaluation/` for session logging
3. `evaluation/` for scenario scoring and reproducible benchmark files

## 2. Baseline runtime pipeline

### 2.1 Startup path

When the user presses **Start** in `index.html`, `js/app.js` does the following:

1. Reads the Gemini API key from the hidden input or local storage.
2. Creates a `GeminiClient` instance with a navigation-focused system prompt.
3. Opens a WebSocket to the Gemini Live API.
4. Starts the Web Audio output pipeline.
5. Starts the camera with `getUserMedia`.
6. Starts the microphone capture pipeline with an `AudioWorklet`.
7. Acquires a screen wake lock.
8. Switches the UI from setup mode to session mode.

### 2.2 Streaming loop

The baseline streaming loop is deliberately simple.

- The camera captures a JPEG frame at **1 FPS**.
- `GeminiClient.sendImage()` pushes that frame as `realtimeInput`.
- The microphone captures **16 kHz mono PCM16** audio and sends chunks with `GeminiClient.sendAudio()`.
- Gemini returns text and **24 kHz PCM16** audio.
- `AudioStreamer` schedules PCM chunks using Web Audio timing so playback is smooth.

This is a good fit for a static web app because it avoids a custom backend and keeps latency low.

### 2.3 User interaction model

The current UI is minimal by design.

- **Tap zone**: captures a fresh frame and asks for a short navigation description tied to that exact image.
- **Mute button**: disables the mic without ending the session.
- **Flip button**: toggles between environment and user-facing cameras.
- **End button**: closes the session and cleans up media streams.

## 3. Baseline modules

### 3.1 `js/gemini-client.js`

`GeminiClient` is the transport layer. It is responsible for:

- opening the WebSocket connection
- sending the setup payload
- sending streamed audio and image chunks
- sending a paired image-plus-text request for tap-to-describe
- decoding server responses into text or raw audio bytes
- surfacing setup, interruption, turn-complete, and error callbacks

The class is small, but it is the system boundary between browser code and the Live API.

### 3.2 `js/camera.js`

`CameraManager` wraps `getUserMedia`, camera switching, and JPEG capture through an off-screen canvas. It standardizes the frame width, keeps compression under control, and returns base64 JPEG payloads that can be sent directly to Gemini or to the optional CV services.

### 3.3 `js/audio-recorder.js`

`AudioRecorder` captures microphone input with browser echo cancellation, noise suppression, and auto-gain enabled. The `AudioWorklet` emits PCM16 chunks that are converted to base64 and streamed out.

### 3.4 `js/audio-streamer.js`

`AudioStreamer` converts raw PCM16 bytes from Gemini into `Float32` samples, chunks them into 200 ms buffers, and schedules playback slightly ahead of real time. This buffer discipline matters. Without it, spoken output clicks, gaps, or drifts.

## 4. New CV enhancement pipeline

### 4.1 Design goal

The class project needed explicit computer vision structure, but the original app was intentionally lightweight. I did not want to replace the browser client with a local PyTorch stack or a bundled native runtime. The compromise is a modular adapter layer.

`js/cv/enhancement-pipeline.js` manages optional CV modules that run through external HTTP endpoints. Each adapter is small and only expects structured JSON back. That keeps the browser client stable while still allowing serious CV models to plug in.

### 4.2 `js/cv/frame-utils.js`

This file contains helper functions shared by the CV adapters:

- base64 JPEG to `Blob`
- JSON POST with timeout and abort support
- basic geometry helpers for detection boxes
- formatting and set utilities

### 4.3 `js/cv/places365-client.js`

This adapter expects a scene-classification endpoint that accepts a base64 JPEG and returns either:

```json
{
  "scene": "hallway",
  "confidence": 0.82,
  "indoorOutdoor": "indoor",
  "topK": [
    {"label": "hallway", "score": 0.82},
    {"label": "staircase", "score": 0.09}
  ]
}
```

The adapter normalizes the response into:

- best scene label
- confidence score
- indoor or outdoor prior
- optional top-k list

That summary becomes short context text like `scene hallway (0.82), likely indoor`.

### 4.4 `js/cv/monodepth2-client.js`

This adapter expects a depth service to return compact, decision-ready values rather than a full depth map. The browser client does not need a tensor. It needs a walking hint.

Expected response shape:

```json
{
  "nearestDistanceM": 0.9,
  "centerDistanceM": 1.4,
  "freeSpaceDirection": "left",
  "confidence": 0.74,
  "obstacleRisk": "high"
}
```

The adapter converts that into text such as:

- nearest obstacle 0.9 m
- forward depth 1.4 m
- more open space on the left
- risk high

This is a deliberately coarse interface. It is enough for the live assistant and easy to evaluate.

### 4.5 `js/cv/retinanet-client.js`

This adapter expects standard detection outputs.

```json
{
  "detections": [
    {"label": "person", "score": 0.94, "bbox": [0.32, 0.18, 0.58, 0.94]},
    {"label": "bench", "score": 0.72, "bbox": [0.60, 0.45, 0.96, 0.92]}
  ]
}
```

Boxes are assumed to be normalized `xyxy` coordinates. The adapter then identifies likely path obstacles with a simple rule:

- box center lies near the middle of the frame
- box center lies in the lower half of the frame
- the box is large enough to matter

This is not full path planning. It is a good enough heuristic to separate “object exists” from “object may block the user.”

### 4.6 `js/cv/enhancement-pipeline.js`

`CVEnhancementPipeline` is the orchestrator. It:

- instantiates enabled adapters
- rate-limits analysis with `analysisIntervalMs`
- runs enabled modules in parallel with `Promise.allSettled`
- stores the latest combined analysis
- converts structured outputs into one compact summary string
- builds tap-to-describe prompts that include CV priors

The summary is injected into the live session only when it changes, which keeps context notes from spamming the model.

## 5. Changes made in `js/app.js`

The main app file now does more than just stream to Gemini.

### 5.1 CV integration

At session start, `app.js` creates:

- `new CVEnhancementPipeline(window.OPALITE_CV || { enabled: false })`
- `new SessionLogger(window.OPALITE_EVAL || { enabled: true })`

During the 1 FPS camera loop:

1. Capture frame.
2. Send frame to Gemini.
3. Optionally run the CV enhancement pipeline.
4. If the summary changes, send a short context note to Gemini.
5. If Monodepth2 reports high risk, vibrate strongly.

During tap-to-describe:

1. Capture a fresh frame.
2. Run CV analysis if needed.
3. Build a prompt that includes the priors only if they match the image.
4. Send image and prompt together as one turn.

### 5.2 Small bug fix in the original code

The original `app.js` assigned `client.onText` twice and `client.onTurnComplete` twice. That meant the hazard-word vibration logic was overwritten by the later console logger. The rewritten file merges those callbacks into single handlers, so the text-based hazard vibration now works as intended.

### 5.3 Logging hooks

`app.js` also logs:

- session start and stop
- status transitions
- frame sends
- AI text events
- audio chunk receipts
- CV analysis summaries
- context-note injections
- camera switches and mic toggles

## 6. Evaluation logging

### 6.1 `js/evaluation/session-logger.js`

The browser-side logger is intentionally simple. It keeps events in memory and exposes three console helpers:

```js
window.OpaliteEvaluation.summary()
window.OpaliteEvaluation.exportJSON({ operator: 'Parker' })
window.OpaliteEvaluation.downloadJSON('run.json', { location: 'campus hallway' })
```

Each event contains:

- Unix timestamp in milliseconds
- ISO timestamp
- event type
- payload

The logger also records a rough response latency by pairing the next AI text event with the previous frame-send event.

### 6.2 Why the logger matters

The evaluation script does not consume raw event logs directly yet. Instead, the logs make it possible to annotate scenario-level runs cleanly. That is a sensible split for a student project:

- raw event log for traceability
- compact scenario file for scoring and comparison

## 7. Offline scorer

### 7.1 `evaluation/run_eval.py`

This script uses only the Python standard library. It loads:

- a scenario file with expected hazards, objects, directions, scene labels, and OCR keywords
- a run file with predicted outputs and latency values

It then computes:

- hazard recall and precision
- object recall
- direction recall
- OCR keyword recall
- scene recall
- mean latency
- mean words per response

The script can emit either markdown or JSON.

### 7.2 Example command

```bash
python3 evaluation/run_eval.py \
  --scenarios evaluation/scenarios/cs5330_navigation_core.json \
  --run evaluation/sample_runs/pilot_enhanced.json
```

### 7.3 Why scenario-level scoring was chosen

A frame-level metric would sound more “CV-like,” but it would not match the product goal. Users need a useful spoken decision, not a perfect box on every frame. The scenario unit is a better fit because it captures whether the system communicated the right safety fact.

## 8. Configuration

### 8.1 Local config

`config.local.js` is gitignored. For safe sharing and setup, `config.example.js` was added.

The current config shape is:

```js
window.OPALITE_API_KEY = 'PASTE_YOUR_KEY_HERE';

window.OPALITE_CV = {
  enabled: false,
  analysisIntervalMs: 2500,
  sendContextNotes: true,
  places365: {
    enabled: false,
    endpoint: 'http://localhost:8001/places365'
  },
  monodepth2: {
    enabled: false,
    endpoint: 'http://localhost:8001/monodepth2'
  },
  retinanet: {
    enabled: false,
    endpoint: 'http://localhost:8001/retinanet'
  }
};

window.OPALITE_EVAL = {
  enabled: true
};
```

### 8.2 Why endpoint-based modules

This keeps the browser client:

- dependency-free
- portable across phones and laptops
- easy to deploy as static hosting
- compatible with multiple backend implementations later

The endpoints could be served by Flask, FastAPI, Hugging Face Inference Endpoints, Vertex AI custom inference, or any thin HTTP wrapper around the chosen models.

## 9. What still needs real implementation work

The project now has a proper integration scaffold, but some parts are still adapters rather than end-to-end deployed services.

1. **Model serving**: Places365, Monodepth2, and RetinaNet endpoints still need actual deployment.
2. **Temporal smoothing**: CV priors are currently per-frame summaries. A tracked state across several frames would reduce jitter.
3. **Text-region priors**: the app still relies on Gemini for OCR. A text detector could make sign and menu reading more stable.
4. **Direction fusion**: free-space direction from depth and object location from RetinaNet could be fused more explicitly instead of only being turned into text.
5. **Real data collection**: the sample benchmark is illustrative. Real logged campus scenes should replace the current example runs.

## 10. Suggested next step after this appendix

If there is time before the final submission, the highest-value improvement is not another code refactor. It is collecting two small real datasets:

- indoor campus navigation clips
- outdoor sidewalk and crosswalk clips

Run those once with priors off and once with priors on. Score both with `run_eval.py`. That would turn the current report from a strong prototype writeup into a more defensible empirical project.
