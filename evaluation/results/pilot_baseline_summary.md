# Opalite evaluation summary: pilot_baseline

- Model: gemini-2.5-flash-native-audio-latest
- Scenarios: 6
- Hazard recall: 0.43
- Hazard precision: 1.00
- Object recall: 0.50
- Direction recall: 0.62
- OCR keyword recall: 0.60
- Scene recall: 0.40
- Mean latency: 1448.3 ms
- Mean response length: 5.5 words

## Per-scenario breakdown

| Scenario | Task | Hazard | Object | Direction | OCR | Scene | Latency (ms) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| hallway-clear | indoor_navigation | 1.00 | 1.00 | 1.00 | 1.00 | 0.50 | 1240 |
| stairs-down | hazard_detection | 0.50 | 0.00 | 0.50 | 1.00 | 0.50 | 1410 |
| crosswalk-traffic | outdoor_navigation | 0.50 | 0.00 | 1.00 | 1.00 | 0.33 | 1580 |
| exit-sign | ocr_navigation | 1.00 | 1.00 | 1.00 | 1.00 | 0.50 | 1320 |
| crowded-sidewalk | dynamic_obstacles | 0.33 | 0.00 | 0.00 | 1.00 | 0.33 | 1650 |
| cafeteria-menu | daily_living | 1.00 | 0.50 | 1.00 | 0.50 | 0.33 | 1490 |
