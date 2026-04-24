# Opalite — CS5330 Final Project Submission

## Authors

- **Parker Cai** — NUID 002529785

## Project description

Opalite is a real-time assistive perception system for blind and low-vision users built around an Intel RealSense D435i RGB-D camera. The pipeline is local-first: depth geometry decides direction and distance, a continuous stereo "sonar" turns sector clearance into spatial audio, and an on-demand local visual-language model (Gemma 4 via Ollama) labels what is in front of the user. Geometry stays authoritative for safety-critical signals; the VLM only contributes object names and general scene description. The same C++ core compiles into a Windows desktop GUI, an Android NDK library, and a phone app that drives the D435i over USB-OTG.

The main claim of the project is that pairing a fast depth-geometry analyzer with a narrowly scoped local VLM gives the best of both: geometry runs every frame and owns distance and direction; the VLM is consulted on demand to name objects and describe scenes. The geometry side stays fast and verifiable; the VLM side stays flexible and interactive.

## Demo video

Latest version of the demo video (Google Drive, "Anyone with the link" view access):

**[https://drive.google.com/file/d/1X_PbR2FerQNciU3VgEspeMiEBj2biSt4/view?usp=sharing](https://drive.google.com/file/d/1X_PbR2FerQNciU3VgEspeMiEBj2biSt4/view?usp=sharing)**

The link is stable: the file is updated in place via Drive's "Manage versions" feature, so this URL always points to the most recent demo. Latest known upload: see `Opalite_demo.mp4` modification time in Drive.

## Source code

The code is bundled with this submission and also lives at:

**[https://github.com/ParkerCai/Opalite](https://github.com/ParkerCai/Opalite)**

### Repository layout (relevant pieces)

```
Opalite/
├── docs/
│   ├── final_project_report.md        # source markdown for the report
│   ├── figures/                        # IEEE figures (system block, sector geometry, GUI screenshot)
│   ├── demo_shot_list.md               # demo storyboard for the video
│   └── pdr_demo_description_and_script.md
├── include/                            # Phase 1/2 C++ headers (RealSense, free-space, sonar, brain, top-down)
├── src/                                # Phase 1/2 C++ implementations
├── android/opalite-edge/               # Phase 3 Android port (NDK reuses the same C++ sources)
├── opalite-webapp/                     # Archived hackathon web prototype (NOT part of the final submission)
├── evaluation/
│   ├── TESTING.md                      # manual validation procedures (depth alignment, latency, Brain RT)
│   └── results/                        # historical scenario results from the pre-pivot prototype
├── submission/
│   ├── README.md                       # this file
│   └── final_project_report.pdf        # IEEE 2-column PDF of the report
└── README.md                           # project README (build/setup for development)
```

## Datasets

There is no static dataset for this submission. Evaluation is against live D435i streams in controlled indoor scenes (see `evaluation/TESTING.md` for the procedures). Latency telemetry is recorded to `data/latency.csv` and `data/brain_latency.csv` during normal use.

## Notes

- The IEEE-format report PDF is `submission/final_project_report.pdf`. The markdown source is `docs/final_project_report.md`.
- The `opalite-webapp/` directory is an earlier hackathon prototype (Gemini Live API + browser camera). It is preserved in the repo as historical context but is **not** part of the CS5330 final project submission. The submitted system is the C++ desktop GUI + Android port described in the report.
