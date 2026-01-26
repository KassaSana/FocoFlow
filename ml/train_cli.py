"""
CLI wrapper for training the baseline model from CSV files.
"""

from __future__ import annotations

import argparse
import json
import os
import tempfile
from typing import List, Optional

from .dataset_builder import (
    join_features_with_labels,
    read_features_csv,
    read_labels_csv,
    write_labeled_csv,
)
from .training_pipeline import load_dataset, save_model, train_baseline


def run_training(
    dataset_path: Optional[str],
    features_path: Optional[str],
    labels_path: Optional[str],
    output_dataset_path: Optional[str],
    output_model_path: Optional[str],
    output_metrics_path: Optional[str],
    label_window_seconds: int,
    backend: str,
    n_splits: int,
    feature_columns: Optional[List[str]],
    label_column: str,
) -> dict:
    temp_path = None
    if dataset_path is None:
        if not features_path or not labels_path:
            raise ValueError("Provide --dataset or both --features-csv and --labels-csv.")

        feature_rows = read_features_csv(features_path)
        label_rows = read_labels_csv(labels_path)
        joined = join_features_with_labels(
            feature_rows,
            label_rows,
            label_window_seconds=label_window_seconds,
        )

        if output_dataset_path is None:
            temp = tempfile.NamedTemporaryFile(delete=False, suffix=".csv")
            output_dataset_path = temp.name
            temp.close()
            temp_path = output_dataset_path

        write_labeled_csv(output_dataset_path, joined)
        dataset_path = output_dataset_path

    try:
        dataset = load_dataset(dataset_path, feature_columns=feature_columns, label_column=label_column)
        result = train_baseline(dataset, backend=backend, n_splits=n_splits)
        if output_model_path:
            save_model(result.model, output_model_path)
        if output_metrics_path:
            os.makedirs(os.path.dirname(output_metrics_path) or ".", exist_ok=True)
            with open(output_metrics_path, "w", encoding="utf-8") as handle:
                json.dump(result.metrics, handle, indent=2)
        return result.metrics
    finally:
        if temp_path and os.path.exists(temp_path):
            os.remove(temp_path)


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train Neural Focus baseline model.")
    parser.add_argument("--dataset", dest="dataset_path", help="Path to labeled dataset CSV.")
    parser.add_argument("--features-csv", dest="features_path", help="Path to features CSV.")
    parser.add_argument("--labels-csv", dest="labels_path", help="Path to labels CSV.")
    parser.add_argument("--output-dataset", dest="output_dataset_path", help="Write joined dataset CSV here.")
    parser.add_argument("--output-model", dest="output_model_path", help="Write trained model here.")
    parser.add_argument("--output-metrics", dest="output_metrics_path", help="Write metrics JSON here.")
    parser.add_argument("--label-window-seconds", type=int, default=300)
    parser.add_argument("--backend", choices=["auto", "xgboost", "majority"], default="auto")
    parser.add_argument("--splits", type=int, default=5)
    parser.add_argument("--feature-columns", default=None)
    parser.add_argument("--label-column", default="label")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    feature_columns = None
    if args.feature_columns:
        feature_columns = [c.strip() for c in args.feature_columns.split(",") if c.strip()]

    metrics = run_training(
        dataset_path=args.dataset_path,
        features_path=args.features_path,
        labels_path=args.labels_path,
        output_dataset_path=args.output_dataset_path,
        output_model_path=args.output_model_path,
        output_metrics_path=args.output_metrics_path,
        label_window_seconds=args.label_window_seconds,
        backend=args.backend,
        n_splits=args.splits,
        feature_columns=feature_columns,
        label_column=args.label_column,
    )

    print("Training metrics:")
    for key, value in metrics.items():
        print(f"- {key}: {value:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
