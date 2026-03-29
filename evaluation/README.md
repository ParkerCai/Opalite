# Opalite evaluation framework

This folder turns Opalite into a reproducible CS5330 project instead of just a demo. The goal is to measure whether the assistant helps with safe navigation and scene understanding, not just whether it sounds good.

## What is measured

The current rubric tracks five CV-facing behaviors:

1. **Hazard recall**: does the system mention stairs, traffic, or other urgent obstacles?
2. **Object recall**: does it identify navigation-relevant objects such as doors, signs, handrails, benches, or counters?
3. **Directional recall**: does it communicate left, right, ahead, or other motion cues correctly?
4. **OCR keyword recall**: does it recover useful text like EXIT or menu prices?
5. **Scene recall**: does it place the user in the right context, such as hallway, sidewalk, or cafeteria?

Latency and response length are tracked too, because assistive output has to arrive fast and stay short.

## Files

- `scenarios/cs5330_navigation_core.json`: core six-scenario benchmark for Opalite
- `sample_runs/pilot_baseline.json`: illustrative pilot log without CV prior modules
- `sample_runs/pilot_enhanced.json`: illustrative pilot log with CV prior modules
- `run_eval.py`: standard-library scorer that produces markdown or JSON summaries

## Run the scorer

```bash
python3 evaluation/run_eval.py \
  --scenarios evaluation/scenarios/cs5330_navigation_core.json \
  --run evaluation/sample_runs/pilot_enhanced.json
```

To emit machine-readable output:

```bash
python3 evaluation/run_eval.py \
  --scenarios evaluation/scenarios/cs5330_navigation_core.json \
  --run evaluation/sample_runs/pilot_enhanced.json \
  --json
```

## Logging a real Opalite session

The browser app now exposes a small logger through `window.OpaliteEvaluation`.

From the browser console you can export a session log:

```js
window.OpaliteEvaluation.summary()
window.OpaliteEvaluation.downloadJSON('my-campus-run.json', {
  operator: 'Parker',
  location: 'NEU campus hallway',
  condition: 'indoor daylight'
})
```

That gives you a timestamped event log with frame sends, AI text, audio chunks, latency samples, and CV prior notes. For the current class deliverable, the scorer expects an annotated scenario-level file like the two examples in `sample_runs/`. The event log is the raw material you use to build those annotations.

## Suggested evaluation protocol

1. Record short clips for each scenario class: hallway, stairs, crosswalk, exit sign, crowded sidewalk, menu board.
2. Run **baseline Opalite** with CV priors disabled.
3. Run **enhanced Opalite** with the three optional modules enabled.
4. Save logs from both runs.
5. Annotate each scenario with expected hazards, objects, directions, scene labels, and text keywords.
6. Score both runs with `run_eval.py` and compare hazard recall, OCR recall, and latency.

## Notes on honesty

The sample run files in this folder are illustrative pilot annotations so the framework is easy to inspect right now. They are not a substitute for a real user study. For the final class submission, Parker should replace them with runs collected on campus or in controlled indoor scenes.
