"""
Baseline model training pipeline.
"""

from __future__ import annotations

import csv
import json
import os
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple

from .labeling import FocusLabel

try:
    import xgboost as xgb
except ImportError:  # pragma: no cover - optional dependency
    xgb = None


LABEL_VALUE_TO_INDEX = {
    int(FocusLabel.DISTRACTED): 0,
    int(FocusLabel.PSEUDO_PRODUCTIVE): 1,
    int(FocusLabel.PRODUCTIVE): 2,
    int(FocusLabel.DEEP_FOCUS): 3,
}


def default_feature_columns() -> List[str]:
    return [
        "seconds_since_session_start",
        "hour_of_day",
        "day_of_week",
        "minutes_since_last_break",
        "keystroke_count",
        "keystroke_rate",
        "keystroke_interval_mean",
        "keystroke_interval_std",
        "keystroke_interval_trend",
        "mouse_move_count",
        "mouse_distance_pixels",
        "mouse_speed_mean",
        "mouse_speed_std",
        "mouse_acceleration_mean",
        "mouse_click_count",
        "context_switches_30s",
        "context_switches_5min",
        "time_in_current_app",
        "unique_apps_5min",
        "idle_time_30s",
        "idle_event_count_5min",
        "longest_active_stretch_5min",
        "window_title_length",
        "window_title_changed_30s",
        "is_browser",
        "is_ide",
        "is_communication",
        "is_entertainment",
        "is_productivity",
        "focus_momentum",
        "is_pseudo_productive",
    ]


@dataclass
class Dataset:
    features: List[List[float]]
    labels: List[int]
    timestamps: List[float]


def load_dataset(
    path: str,
    feature_columns: Optional[List[str]] = None,
    label_column: str = "label",
) -> Dataset:
    feature_columns = feature_columns or default_feature_columns()
    features: List[List[float]] = []
    labels: List[int] = []
    timestamps: List[float] = []

    with open(path, "r", newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            label_raw = (row.get(label_column) or "").strip()
            if label_raw == "":
                continue

            label_value = int(label_raw)
            if label_value not in LABEL_VALUE_TO_INDEX:
                continue

            vector = []
            for column in feature_columns:
                value = (row.get(column) or "0").strip()
                try:
                    vector.append(float(value))
                except ValueError:
                    vector.append(0.0)

            features.append(vector)
            labels.append(LABEL_VALUE_TO_INDEX[label_value])
            timestamps.append(float(row.get("timestamp") or 0))

    return Dataset(features=features, labels=labels, timestamps=timestamps)


def time_series_splits(n_samples: int, n_splits: int = 5) -> Iterable[Tuple[List[int], List[int]]]:
    if n_samples < 2:
        return []
    n_splits = max(1, n_splits)
    split_size = max(1, n_samples // (n_splits + 1))
    splits: List[Tuple[List[int], List[int]]] = []

    for i in range(n_splits):
        train_end = split_size * (i + 1)
        val_end = split_size * (i + 2)
        if train_end >= n_samples:
            break
        val_end = min(n_samples, val_end)
        train_idx = list(range(0, train_end))
        val_idx = list(range(train_end, val_end))
        if val_idx:
            splits.append((train_idx, val_idx))
    return splits


def _to_list(values):
    if hasattr(values, "tolist"):
        return values.tolist()
    return values


def _predictions_to_labels(predictions: List) -> List[int]:
    predictions = _to_list(predictions)
    if not predictions:
        return []
    first = predictions[0]
    if isinstance(first, (list, tuple)):
        return [int(max(range(len(row)), key=row.__getitem__)) for row in predictions]
    if hasattr(first, "__len__") and not isinstance(first, (int, float)):
        return [int(max(range(len(row)), key=row.__getitem__)) for row in predictions]
    return [int(x) for x in predictions]


def _accuracy_score(y_true: List[int], y_pred: List[int]) -> float:
    if not y_true:
        return 0.0
    correct = sum(1 for a, b in zip(y_true, y_pred) if a == b)
    return correct / len(y_true)


def precision_at_k(
    probas: List[List[float]],
    labels: List[int],
    target_index: int,
    k_fraction: float = 0.1,
) -> float:
    probas = _to_list(probas)
    if not probas:
        return 0.0
    k = max(1, int(len(probas) * k_fraction))
    ranked = sorted(range(len(probas)), key=lambda i: probas[i][target_index])
    top_k = ranked[-k:]
    hits = sum(1 for i in top_k if labels[i] == target_index)
    return hits / len(top_k)


def recall_for_class(
    probas: List[List[float]],
    labels: List[int],
    target_index: int,
    threshold: float = 0.7,
) -> float:
    probas = _to_list(probas)
    positives = [i for i, label in enumerate(labels) if label == target_index]
    if not positives:
        return 0.0
    hits = sum(1 for i in positives if probas[i][target_index] >= threshold)
    return hits / len(positives)


class MajorityClassifier:
    def __init__(self) -> None:
        self.majority_index: Optional[int] = None

    def fit(self, labels: List[int]) -> "MajorityClassifier":
        counts: Dict[int, int] = {}
        for label in labels:
            counts[label] = counts.get(label, 0) + 1
        self.majority_index = max(counts, key=counts.get)
        return self

    def predict_proba(self, features: List[List[float]]) -> List[List[float]]:
        if self.majority_index is None:
            raise RuntimeError("MajorityClassifier not fit")
        probas = []
        for _ in features:
            row = [0.0, 0.0, 0.0, 0.0]
            row[self.majority_index] = 1.0
            probas.append(row)
        return probas

    def to_dict(self) -> Dict[str, int]:
        if self.majority_index is None:
            raise RuntimeError("MajorityClassifier not fit")
        return {"type": "majority", "majority_index": int(self.majority_index)}


@dataclass
class TrainingResult:
    model: object
    metrics: Dict[str, float]


def train_baseline(
    dataset: Dataset,
    backend: str = "auto",
    n_splits: int = 5,
) -> TrainingResult:
    if not dataset.features:
        raise ValueError("dataset is empty")

    if backend == "auto":
        backend = "xgboost" if xgb is not None else "majority"

    if backend == "xgboost":
        if xgb is None:
            raise RuntimeError("xgboost is not installed")

        classes = sorted(set(dataset.labels))
        label_to_class = {label: idx for idx, label in enumerate(classes)}
        class_to_label = {idx: label for label, idx in label_to_class.items()}

        if len(classes) < 2:
            raise ValueError("Need at least two classes to train XGBoost")

        X_train = dataset.features
        y_train = [label_to_class[label] for label in dataset.labels]

        model = xgb.XGBClassifier(
            max_depth=6,
            learning_rate=0.1,
            n_estimators=100,
            objective="multi:softprob",
            num_class=len(classes),
            eval_metric="mlogloss",
        )
        model.fit(X_train, y_train)

        probas_raw = model.predict_proba(dataset.features)
        probas_raw = _to_list(probas_raw)
        probas = []
        for row in probas_raw:
            full = [0.0, 0.0, 0.0, 0.0]
            for idx, prob in enumerate(row):
                original_label = class_to_label[idx]
                full[original_label] = prob
            probas.append(full)
    elif backend == "majority":
        model = MajorityClassifier().fit(dataset.labels)
        probas = model.predict_proba(dataset.features)
    else:
        raise ValueError(f"Unknown backend: {backend}")

    distracted_index = LABEL_VALUE_TO_INDEX[int(FocusLabel.DISTRACTED)]
    metrics = {
        "precision_at_10pct": precision_at_k(probas, dataset.labels, distracted_index, 0.1),
        "recall_distracted": recall_for_class(probas, dataset.labels, distracted_index, 0.7),
    }

    return TrainingResult(model=model, metrics=metrics)


def save_model(model: object, path: str) -> None:
    if hasattr(model, "save_model"):
        model.save_model(path)
        return

    if hasattr(model, "to_dict"):
        payload = model.to_dict()
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle)
        return

    raise ValueError("Unsupported model type for saving")
