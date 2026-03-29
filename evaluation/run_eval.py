#!/usr/bin/env python3
"""Score Opalite evaluation runs against annotated scenario expectations.

Usage:
  python3 evaluation/run_eval.py \
    --scenarios evaluation/scenarios/cs5330_navigation_core.json \
    --run evaluation/sample_runs/pilot_enhanced.json
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from statistics import mean


def load_json(path: Path):
    with path.open() as f:
        return json.load(f)


def as_set(values):
    return {str(v).lower() for v in values or []}


def safe_div(num, den):
    return num / den if den else 0.0


def count_words(text: str) -> int:
    return len([token for token in text.strip().split() if token])


def evaluate(scenarios, run):
    scenario_map = {item["id"]: item for item in scenarios}

    totals = {
        "hazard_expected": 0,
        "hazard_matched": 0,
        "hazard_predicted": 0,
        "hazard_true_positive": 0,
        "objects_expected": 0,
        "objects_matched": 0,
        "directions_expected": 0,
        "directions_matched": 0,
        "text_expected": 0,
        "text_matched": 0,
        "scene_expected": 0,
        "scene_matched": 0,
    }

    rows = []
    latencies = []
    word_counts = []

    for result in run.get("results", []):
        scenario = scenario_map.get(result["scenarioId"])
        if not scenario:
            raise ValueError(f"Scenario not found: {result['scenarioId']}")

        expected = scenario.get("expected", {})
        hazard_expected = as_set(expected.get("hazards"))
        object_expected = as_set(expected.get("objects"))
        direction_expected = as_set(expected.get("directions"))
        text_expected = as_set(expected.get("textKeywords"))
        scene_expected = as_set(expected.get("sceneLabels"))

        hazard_pred = as_set(result.get("hazards"))
        object_pred = as_set(result.get("objects"))
        direction_pred = as_set(result.get("directions"))
        text_pred = as_set(result.get("textKeywords"))
        scene_pred = as_set(result.get("sceneLabels"))

        hazard_match = hazard_expected & hazard_pred
        object_match = object_expected & object_pred
        direction_match = direction_expected & direction_pred
        text_match = text_expected & text_pred
        scene_match = scene_expected & scene_pred

        totals["hazard_expected"] += len(hazard_expected)
        totals["hazard_matched"] += len(hazard_match)
        totals["hazard_predicted"] += len(hazard_pred)
        totals["hazard_true_positive"] += len(hazard_match)
        totals["objects_expected"] += len(object_expected)
        totals["objects_matched"] += len(object_match)
        totals["directions_expected"] += len(direction_expected)
        totals["directions_matched"] += len(direction_match)
        totals["text_expected"] += len(text_expected)
        totals["text_matched"] += len(text_match)
        totals["scene_expected"] += len(scene_expected)
        totals["scene_matched"] += len(scene_match)

        latency_ms = result.get("latencyMs")
        if isinstance(latency_ms, (int, float)):
            latencies.append(float(latency_ms))

        word_counts.append(count_words(result.get("responseText", "")))

        rows.append({
            "scenario_id": result["scenarioId"],
            "task": scenario.get("task"),
            "hazard_recall": safe_div(len(hazard_match), len(hazard_expected)) if hazard_expected else 1.0,
            "object_recall": safe_div(len(object_match), len(object_expected)) if object_expected else 1.0,
            "direction_recall": safe_div(len(direction_match), len(direction_expected)) if direction_expected else 1.0,
            "text_recall": safe_div(len(text_match), len(text_expected)) if text_expected else 1.0,
            "scene_recall": safe_div(len(scene_match), len(scene_expected)) if scene_expected else 1.0,
            "latency_ms": latency_ms,
            "response_text": result.get("responseText", ""),
        })

    summary = {
        "run_name": run.get("meta", {}).get("runName", "unnamed_run"),
        "model": run.get("meta", {}).get("model", "unknown"),
        "num_scenarios": len(rows),
        "hazard_recall": safe_div(totals["hazard_matched"], totals["hazard_expected"]),
        "hazard_precision": safe_div(totals["hazard_true_positive"], totals["hazard_predicted"]),
        "object_recall": safe_div(totals["objects_matched"], totals["objects_expected"]),
        "direction_recall": safe_div(totals["directions_matched"], totals["directions_expected"]),
        "text_keyword_recall": safe_div(totals["text_matched"], totals["text_expected"]),
        "scene_recall": safe_div(totals["scene_matched"], totals["scene_expected"]),
        "mean_latency_ms": mean(latencies) if latencies else None,
        "mean_words_per_response": mean(word_counts) if word_counts else None,
    }

    return summary, rows


def render_markdown(summary, rows):
    lines = []
    lines.append(f"# Opalite evaluation summary: {summary['run_name']}")
    lines.append("")
    lines.append(f"- Model: {summary['model']}")
    lines.append(f"- Scenarios: {summary['num_scenarios']}")
    lines.append(f"- Hazard recall: {summary['hazard_recall']:.2f}")
    lines.append(f"- Hazard precision: {summary['hazard_precision']:.2f}")
    lines.append(f"- Object recall: {summary['object_recall']:.2f}")
    lines.append(f"- Direction recall: {summary['direction_recall']:.2f}")
    lines.append(f"- OCR keyword recall: {summary['text_keyword_recall']:.2f}")
    lines.append(f"- Scene recall: {summary['scene_recall']:.2f}")
    if summary['mean_latency_ms'] is not None:
        lines.append(f"- Mean latency: {summary['mean_latency_ms']:.1f} ms")
    if summary['mean_words_per_response'] is not None:
        lines.append(f"- Mean response length: {summary['mean_words_per_response']:.1f} words")
    lines.append("")
    lines.append("## Per-scenario breakdown")
    lines.append("")
    lines.append("| Scenario | Task | Hazard | Object | Direction | OCR | Scene | Latency (ms) |")
    lines.append("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |")

    for row in rows:
        lines.append(
            f"| {row['scenario_id']} | {row['task']} | {row['hazard_recall']:.2f} | {row['object_recall']:.2f} | "
            f"{row['direction_recall']:.2f} | {row['text_recall']:.2f} | {row['scene_recall']:.2f} | {row['latency_ms'] or ''} |"
        )

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenarios", required=True, type=Path)
    parser.add_argument("--run", required=True, type=Path)
    parser.add_argument("--json", action="store_true", help="Emit JSON instead of markdown")
    args = parser.parse_args()

    scenarios = load_json(args.scenarios)
    run = load_json(args.run)
    summary, rows = evaluate(scenarios, run)

    if args.json:
        print(json.dumps({"summary": summary, "rows": rows}, indent=2))
    else:
        print(render_markdown(summary, rows))


if __name__ == "__main__":
    main()
