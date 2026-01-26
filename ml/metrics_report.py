"""
Generate basic metrics from a recorded event log.
"""

from __future__ import annotations

import argparse
import json
import time
from collections import Counter
from dataclasses import dataclass
from typing import Dict, Optional

from .event_log_reader import EventLogReader
from .features import FeatureExtractor


@dataclass(frozen=True)
class Metrics:
    log_path: str
    total_events: int
    duration_seconds: float
    events_per_second: float
    unique_apps: int
    event_type_counts: Dict[str, int]
    feature_throughput_eps: Optional[float]


def collect_metrics(
    log_path: str,
    limit: Optional[int] = None,
    benchmark_features: bool = False,
) -> Metrics:
    reader = EventLogReader(log_path)
    type_counts: Counter = Counter()
    unique_apps = set()
    first_ts: Optional[int] = None
    last_ts: Optional[int] = None
    total_events = 0

    extractor = FeatureExtractor() if benchmark_features else None
    start_ts = time.perf_counter() if benchmark_features else None

    for event in reader.iter_events(limit=limit):
        total_events += 1
        if first_ts is None:
            first_ts = event.timestamp_us
        last_ts = event.timestamp_us
        type_counts[event.event_type] += 1
        if event.app_name:
            unique_apps.add(event.app_name)
        if extractor is not None:
            extractor.update(event)

    duration_seconds = 0.0
    if total_events > 1 and first_ts is not None and last_ts is not None:
        duration_seconds = max(0.0, (last_ts - first_ts) / 1_000_000.0)

    events_per_second = 0.0
    if duration_seconds > 0:
        events_per_second = total_events / duration_seconds

    feature_throughput_eps = None
    if benchmark_features and start_ts is not None:
        elapsed = max(1e-9, time.perf_counter() - start_ts)
        feature_throughput_eps = total_events / elapsed

    event_type_counts = {event_type.name: count for event_type, count in type_counts.items()}

    return Metrics(
        log_path=log_path,
        total_events=total_events,
        duration_seconds=duration_seconds,
        events_per_second=events_per_second,
        unique_apps=len(unique_apps),
        event_type_counts=event_type_counts,
        feature_throughput_eps=feature_throughput_eps,
    )


def render_report(metrics: Metrics) -> str:
    lines = [
        f"log_path: {metrics.log_path}",
        f"total_events: {metrics.total_events}",
        f"duration_seconds: {metrics.duration_seconds:.2f}",
        f"events_per_second: {metrics.events_per_second:.2f}",
        f"unique_apps: {metrics.unique_apps}",
        "event_type_breakdown:",
    ]

    for name, count in sorted(
        metrics.event_type_counts.items(), key=lambda item: item[1], reverse=True
    ):
        lines.append(f"  {name}: {count}")

    if metrics.feature_throughput_eps is not None:
        lines.append(f"feature_extraction_eps: {metrics.feature_throughput_eps:.2f}")

    return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Summarize event log metrics")
    parser.add_argument("--log-path", required=True)
    parser.add_argument("--limit", type=int, default=None)
    parser.add_argument("--benchmark-features", action="store_true")
    parser.add_argument("--output-json", default=None)
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    metrics = collect_metrics(
        log_path=args.log_path,
        limit=args.limit,
        benchmark_features=args.benchmark_features,
    )
    print(render_report(metrics))

    if args.output_json:
        payload = {
            "log_path": metrics.log_path,
            "total_events": metrics.total_events,
            "duration_seconds": metrics.duration_seconds,
            "events_per_second": metrics.events_per_second,
            "unique_apps": metrics.unique_apps,
            "event_type_counts": metrics.event_type_counts,
            "feature_throughput_eps": metrics.feature_throughput_eps,
        }
        with open(args.output_json, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2)


if __name__ == "__main__":
    main()
