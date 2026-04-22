# Opalite Phase 1 — Testing Procedures

Manual validation steps that cost minutes to run and produce numbers
suitable for citing in the class report's Experiments & Results section.

> The older `evaluation/` harness (`run_eval.py`, `scenarios/`, `sample_runs/`,
> `results/`) targets the pre-pivot Gemini-based web app and is not wired into
> Phase 1. Treat it as historical until the Phase 2 model-side work lands.

## Depth-alignment sanity check

Confirms that color and `rs2::align`'d depth share pixel coordinates — the
invariant that the top-down deprojection and the free-space sector ROIs
both silently depend on.

1. Place a flat poster or notebook at a measured distance (e.g., **1.00 m**)
   directly in front of the camera, filling the center beam.
2. Launch `bin/opalite.exe`. Click **Save frame**.
3. Open `data/saved_frames/<timestamp>_color.png` and
   `<timestamp>_depth.png`. Load the depth with
   `cv::imread(path, cv::IMREAD_UNCHANGED)` to preserve 16-bit mm.
4. Pick a pixel on the target in the color image; read the same `(u, v)`
   in the depth image. Divide the stored value by 1000 to compare against
   your measured distance.
5. **Pass criterion:** within **±30 mm at 1 m** is acceptable for the
   D435i's active-stereo depth. Larger drift points at a broken
   configuration (wrong intrinsics, missing `rs2::align`, etc.).

The live Controls `Range: min - max m` readout should also bracket your
measured distance while the object fills the frame.

## Latency check

1. Launch `bin/opalite.exe` and let it run for at least 30 s of normal use.
2. Close the app. Open `data/latency.csv`.
3. The expected result for the current Phase 1 pipeline on the dev PC is
   **median ~9 ms, p95 ~10 ms**. Sustained deviation (median > 20 ms)
   indicates the top-down deproject loop or a texture upload has
   regressed.

Note: this metric is processing-pipeline time (poll-success to
end-of-render). End-to-end sensor-photon-to-screen includes the camera's
~33 ms frame period and GPU present — add ~50 ms to any pipeline number
quoted in the report.
