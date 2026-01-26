"""
Join feature rows with label records for training datasets.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional

from .labeling import FocusLabel, LabelRecord, LabelSource

LABEL_HEADERS = ["timestamp", "label", "source", "session_id", "notes"]


@dataclass(frozen=True)
class FeatureRow:
    timestamp: float
    row: Dict[str, str]


def read_features_csv(path: str) -> List[FeatureRow]:
    rows: List[FeatureRow] = []
    with open(path, "r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for raw in reader:
            timestamp = float(raw.get("timestamp", "0") or 0)
            rows.append(FeatureRow(timestamp=timestamp, row=dict(raw)))
    return rows


def _parse_label(value: str) -> FocusLabel:
    value = value.strip()
    if value.isdigit() or (value.startswith("-") and value[1:].isdigit()):
        return FocusLabel(int(value))
    return FocusLabel[value]


def _parse_source(value: str) -> LabelSource:
    value = value.strip()
    if value.isdigit():
        return LabelSource(int(value))
    return LabelSource[value]


def read_labels_csv(path: str) -> List[LabelRecord]:
    labels: List[LabelRecord] = []
    with open(path, "r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for raw in reader:
            labels.append(
                LabelRecord(
                    timestamp=float(raw.get("timestamp", "0") or 0),
                    label=_parse_label(raw.get("label", "0")),
                    source=_parse_source(raw.get("source", "MANUAL")),
                    session_id=raw.get("session_id", ""),
                    notes=raw.get("notes", ""),
                )
            )
    return labels


def join_features_with_labels(
    feature_rows: Iterable[FeatureRow],
    label_rows: Iterable[LabelRecord],
    label_window_seconds: int = 300,
    keep_unlabeled: bool = False,
) -> List[Dict[str, str]]:
    labels = sorted(label_rows, key=lambda record: record.timestamp)
    labeled: List[Dict[str, str]] = []

    label_index = 0

    for feature in sorted(feature_rows, key=lambda row: row.timestamp):
        while label_index < len(labels) and labels[label_index].timestamp < feature.timestamp:
            label_index += 1

        next_label: Optional[LabelRecord] = None
        if label_index < len(labels):
            next_label = labels[label_index]

        if next_label is None:
            if keep_unlabeled:
                row = dict(feature.row)
                row.update(
                    {
                        "label": "",
                        "label_source": "",
                        "label_session_id": "",
                        "label_notes": "",
                    }
                )
                labeled.append(row)
            continue

        if next_label.timestamp - feature.timestamp > label_window_seconds:
            if keep_unlabeled:
                row = dict(feature.row)
                row.update(
                    {
                        "label": "",
                        "label_source": "",
                        "label_session_id": "",
                        "label_notes": "",
                    }
                )
                labeled.append(row)
            continue

        row = dict(feature.row)
        row.update(
            {
                "label": str(int(next_label.label)),
                "label_source": next_label.source.name,
                "label_session_id": next_label.session_id,
                "label_notes": next_label.notes,
            }
        )
        labeled.append(row)

    return labeled


def write_labeled_csv(path: str, rows: Iterable[Dict[str, str]]) -> int:
    rows = list(rows)
    if not rows:
        return 0
    headers = list(rows[0].keys())
    count = 0
    with open(path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=headers)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
            count += 1
    return count
