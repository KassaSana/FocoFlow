"""
Export a trained XGBoost model to ONNX for the Rust runtime (optional `onnx` feature).

Usage:
  python -m ml.export_onnx --model-path artifacts/model.json --output artifacts/model.onnx
"""

from __future__ import annotations

import argparse
import json
import os
import sys

from .training_pipeline import default_feature_columns


def export_xgboost_to_onnx(model_path: str, output_path: str) -> None:
    try:
        import xgboost as xgb
        from skl2onnx import convert_sklearn
        from skl2onnx.common.data_types import FloatTensorType
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError(
            "Install xgboost and skl2onnx to export ONNX: pip install xgboost skl2onnx onnx"
        ) from exc

    model = xgb.XGBClassifier()
    model.load_model(model_path)
    n_features = len(default_feature_columns())
    initial_type = [("input", FloatTensorType([None, n_features]))]
    onnx_model = convert_sklearn(model, initial_types=initial_type)
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    with open(output_path, "wb") as handle:
        handle.write(onnx_model.SerializeToString())


def main() -> None:
    parser = argparse.ArgumentParser(description="Export Snapback model to ONNX")
    parser.add_argument("--model-path", required=True)
    parser.add_argument("--output", default="artifacts/model.onnx")
    args = parser.parse_args()

    if not os.path.exists(args.model_path):
        print(f"model not found: {args.model_path}", file=sys.stderr)
        sys.exit(1)

    with open(args.model_path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
    if isinstance(payload, dict) and payload.get("type") == "majority":
        print("majority stub models cannot be exported to ONNX", file=sys.stderr)
        sys.exit(2)

    export_xgboost_to_onnx(args.model_path, args.output)
    print(f"exported ONNX model to {args.output}")


if __name__ == "__main__":
    main()
