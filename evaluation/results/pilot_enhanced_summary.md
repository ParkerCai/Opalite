# Opalite evaluation summary: pilot_enhanced

- Model: gemini-2.5-flash-native-audio-latest + CV priors
- Scenarios: 6
- Hazard recall: 1.00
- Hazard precision: 1.00
- Object recall: 1.00
- Direction recall: 0.88
- OCR keyword recall: 1.00
- Scene recall: 0.80
- Mean latency: 1525.0 ms
- Mean response length: 7.7 words

## Per-scenario breakdown

| Scenario | Task | Hazard | Object | Direction | OCR | Scene | Latency (ms) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| hallway-clear | indoor_navigation | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1360 |
| stairs-down | hazard_detection | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1480 |
| crosswalk-traffic | outdoor_navigation | 1.00 | 1.00 | 1.00 | 1.00 | 0.67 | 1660 |
| exit-sign | ocr_navigation | 1.00 | 1.00 | 1.00 | 1.00 | 1.00 | 1380 |
| crowded-sidewalk | dynamic_obstacles | 1.00 | 1.00 | 0.50 | 1.00 | 0.67 | 1720 |
| cafeteria-menu | daily_living | 1.00 | 1.00 | 1.00 | 1.00 | 0.67 | 1550 |
