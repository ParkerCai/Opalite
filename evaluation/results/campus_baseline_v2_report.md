# Opalite evaluation summary: campus_baseline_v2

- Model: gemini-2.5-flash-exp
- Scenarios: 6
- Hazard recall: 0.43
- Hazard precision: 1.00
- Object recall: 0.88
- Direction recall: 0.50
- OCR keyword recall: 1.00
- Scene recall: 0.40
- Mean latency: 1381.7 ms
- Mean response length: 13.5 words

## Per-scenario breakdown

| Scenario | Task | Hazard | Object | Direction | OCR | Scene | Latency (ms) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| hallway-clear | indoor_navigation | 1.00 | 1.00 | 0.50 | 1.00 | 0.50 | 1180 |
| stairs-down | hazard_detection | 0.50 | 1.00 | 0.50 | 1.00 | 0.50 | 1350 |
| crosswalk-traffic | outdoor_navigation | 0.50 | 1.00 | 1.00 | 1.00 | 0.33 | 1420 |
| exit-sign | ocr_navigation | 1.00 | 1.00 | 1.00 | 1.00 | 0.50 | 1290 |
| crowded-sidewalk | dynamic_obstacles | 0.33 | 1.00 | 0.00 | 1.00 | 0.33 | 1580 |
| cafeteria-menu | daily_living | 1.00 | 0.50 | 1.00 | 1.00 | 0.33 | 1470 |
