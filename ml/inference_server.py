"""
Real-time inference bridge from ZeroMQ events to the backend prediction API.
"""

from __future__ import annotations

import argparse
import json
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Iterable, List, Optional

from .features import FeatureExtractor, FeatureVector
from .training_pipeline import default_feature_columns
from .zmq_subscriber import ZmqEventSubscriber, ZmqSubscriberConfig, default_zmq_endpoint

try:
    import xgboost as xgb
except ImportError:  # pragma: no cover - optional dependency
    xgb = None


FOCUS_LEVELS = [25.0, 50.0, 75.0, 100.0]


@dataclass(frozen=True)
class PredictionScores:
    focus_score: float
    distraction_risk: float


def clamp(value: float, min_value: float, max_value: float) -> float:
    return max(min_value, min(max_value, value))


def feature_to_vector(feature: FeatureVector, columns: Iterable[str]) -> List[float]:
    values: List[float] = []
    for column in columns:
        raw = getattr(feature, column, 0.0)
        if isinstance(raw, bool):
            values.append(1.0 if raw else 0.0)
        elif raw is None:
            values.append(0.0)
        else:
            try:
                values.append(float(raw))
            except (TypeError, ValueError):
                values.append(0.0)
    return values


def scores_from_probas(probas: List[float]) -> PredictionScores:
    if len(probas) != 4:
        padded = (probas + [0.0, 0.0, 0.0, 0.0])[:4]
        total = sum(padded) or 1.0
        probas = [value / total for value in padded]
    distraction_risk = clamp(float(probas[0]), 0.0, 1.0)
    focus_score = sum(level * probas[index] for index, level in enumerate(FOCUS_LEVELS))
    return PredictionScores(focus_score=focus_score, distraction_risk=distraction_risk)


class HeuristicModel:
    def __init__(self, columns: List[str]) -> None:
        self._index = {name: idx for idx, name in enumerate(columns)}

    def _value(self, row: List[float], name: str, default: float = 0.0) -> float:
        idx = self._index.get(name)
        if idx is None or idx >= len(row):
            return default
        return float(row[idx])

    def predict_proba(self, features: List[List[float]]) -> List[List[float]]:
        results: List[List[float]] = []
        for row in features:
            context_switches = self._value(row, "context_switches_30s")
            idle_time = self._value(row, "idle_time_30s")
            keystroke_rate = self._value(row, "keystroke_rate")
            time_in_app = self._value(row, "time_in_current_app")
            is_entertainment = self._value(row, "is_entertainment")
            is_communication = self._value(row, "is_communication")

            risk = 0.0
            risk += min(1.0, context_switches / 4.0) * 0.3
            risk += min(1.0, idle_time / 8.0) * 0.3
            risk += (1.0 - min(1.0, keystroke_rate / 4.0)) * 0.2
            risk += (1.0 - min(1.0, time_in_app / 120.0)) * 0.1
            risk += (1.0 if is_entertainment else 0.0) * 0.05
            risk += (1.0 if is_communication else 0.0) * 0.05
            risk = clamp(risk, 0.0, 1.0)

            remaining = 1.0 - risk
            results.append(
                [
                    risk,
                    remaining * 0.2,
                    remaining * 0.5,
                    remaining * 0.3,
                ]
            )
        return results


class MajorityModel:
    def __init__(self, majority_index: int) -> None:
        self.majority_index = majority_index

    def predict_proba(self, features: List[List[float]]) -> List[List[float]]:
        probas: List[List[float]] = []
        for _ in features:
            row = [0.0, 0.0, 0.0, 0.0]
            row[self.majority_index] = 1.0
            probas.append(row)
        return probas


def load_model(model_path: Optional[str], columns: List[str]) -> object:
    if not model_path:
        return HeuristicModel(columns)

    try:
        with open(model_path, "r", encoding="utf-8") as handle:
            payload = json.load(handle)
    except (OSError, json.JSONDecodeError):
        payload = None

    if isinstance(payload, dict) and payload.get("type") == "majority":
        return MajorityModel(int(payload.get("majority_index", 0)))

    if xgb is None:
        raise RuntimeError("xgboost is not installed, cannot load model")

    model = xgb.XGBClassifier()
    model.load_model(model_path)
    return model


class BackendPublisher:
    def __init__(self, base_url: str, timeout: float = 2.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def start_session(self, goal: str) -> Optional[str]:
        payload = {"goal": goal}
        body = json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            f"{self.base_url}/api/sessions/start",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                raw = response.read()
        except (urllib.error.URLError, urllib.error.HTTPError):
            return None

        try:
            data = json.loads(raw.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            return None

        session_id = data.get("sessionId") if isinstance(data, dict) else None
        if isinstance(session_id, str) and session_id:
            return session_id
        return None

    def send_prediction(self, session_id: str, scores: PredictionScores) -> bool:
        payload = {
            "sessionId": session_id,
            "focusScore": scores.focus_score,
            "distractionRisk": scores.distraction_risk,
        }
        body = json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            f"{self.base_url}/api/predictions",
            data=body,
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                response.read()
            return True
        except (urllib.error.URLError, urllib.error.HTTPError):
            return False


@dataclass
class InferenceConfig:
    backend_url: str
    session_id: str
    zmq_endpoint: str
    prediction_interval_seconds: float
    poll_interval_ms: int
    model_path: Optional[str]
    log_predictions: bool
    dry_run: bool
    max_predictions: Optional[int] = None
    session_goal: Optional[str] = None


def run_inference(config: InferenceConfig) -> None:
    columns = default_feature_columns()
    model = load_model(config.model_path, columns)
    subscriber = ZmqEventSubscriber(ZmqSubscriberConfig(endpoint=config.zmq_endpoint))
    extractor = FeatureExtractor()
    publisher = BackendPublisher(config.backend_url)
    session_id = config.session_id
    if config.session_goal:
        started_session = publisher.start_session(config.session_goal)
        if started_session:
            session_id = started_session
            if config.log_predictions:
                print(f"started session {session_id}")
        elif config.log_predictions:
            print("session start failed; using provided session id")

    last_prediction_ts = 0.0
    predictions_sent = 0

    subscriber.start()
    try:
        while True:
            event = subscriber.recv_event(timeout_ms=config.poll_interval_ms)
            if event is None:
                continue

            feature = extractor.update(event)
            now = feature.timestamp
            if now - last_prediction_ts < config.prediction_interval_seconds:
                continue

            vector = feature_to_vector(feature, columns)
            probas = model.predict_proba([vector])
            scores = scores_from_probas(probas[0])
            extractor.update_focus_score(scores.focus_score / 100.0)
            last_prediction_ts = now

            if config.log_predictions:
                print(
                    f"focus={scores.focus_score:.1f} risk={scores.distraction_risk:.2f}"
                    f" session={session_id}"
                )

            if config.dry_run:
                predictions_sent += 1
                if config.max_predictions is not None and predictions_sent >= config.max_predictions:
                    break
                continue

            published = publisher.send_prediction(session_id, scores)
            if config.log_predictions and not published:
                print("prediction publish failed")
            predictions_sent += 1
            if config.max_predictions is not None and predictions_sent >= config.max_predictions:
                break
    except KeyboardInterrupt:
        print("inference stopped")
    finally:
        subscriber.close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Stream live predictions to the backend")
    parser.add_argument("--backend-url", default="http://localhost:8080")
    parser.add_argument("--session-id", default="live-session")
    parser.add_argument("--session-goal", default=None)
    parser.add_argument("--zmq-endpoint", default=default_zmq_endpoint())
    parser.add_argument("--prediction-interval", type=float, default=10.0)
    parser.add_argument("--poll-ms", type=int, default=500)
    parser.add_argument("--model-path", default=None)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    config = InferenceConfig(
        backend_url=args.backend_url,
        session_id=args.session_id,
        session_goal=args.session_goal,
        zmq_endpoint=args.zmq_endpoint,
        prediction_interval_seconds=args.prediction_interval,
        poll_interval_ms=args.poll_ms,
        model_path=args.model_path,
        log_predictions=not args.quiet,
        dry_run=args.dry_run,
    )
    run_inference(config)


if __name__ == "__main__":
    main()
