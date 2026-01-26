import os
import tempfile
import unittest

from ml.features import FeatureVector
from ml.inference_server import (
    HeuristicModel,
    MajorityModel,
    feature_to_vector,
    load_model,
    scores_from_probas,
)
from ml.training_pipeline import default_feature_columns


def make_feature() -> FeatureVector:
    return FeatureVector(
        timestamp=100.0,
        seconds_since_session_start=10,
        hour_of_day=9,
        day_of_week=2,
        minutes_since_last_break=3,
        keystroke_count=20,
        keystroke_rate=2.5,
        keystroke_interval_mean=0.4,
        keystroke_interval_std=0.1,
        keystroke_interval_trend=0.02,
        mouse_move_count=5,
        mouse_distance_pixels=120.0,
        mouse_speed_mean=50.0,
        mouse_speed_std=5.0,
        mouse_acceleration_mean=2.0,
        mouse_click_count=1,
        context_switches_30s=2,
        context_switches_5min=4,
        time_in_current_app=90,
        unique_apps_5min=2,
        idle_time_30s=1.0,
        idle_event_count_5min=0,
        longest_active_stretch_5min=120,
        window_title_length=12,
        window_title_changed_30s=True,
        is_browser=False,
        is_ide=True,
        is_communication=False,
        is_entertainment=False,
        is_productivity=False,
        focus_momentum=0.4,
        productivity_category="Building",
        is_pseudo_productive=False,
        recent_event_sequence=[1, 2, 3],
    )


class TestInferenceServer(unittest.TestCase):
    def test_feature_to_vector(self) -> None:
        feature = make_feature()
        columns = default_feature_columns()
        vector = feature_to_vector(feature, columns)
        self.assertEqual(len(vector), len(columns))
        self.assertEqual(vector[0], float(feature.seconds_since_session_start))
        self.assertEqual(vector[4], float(feature.keystroke_count))
        self.assertEqual(vector[23], 1.0)

    def test_scores_from_probas(self) -> None:
        scores = scores_from_probas([0.7, 0.1, 0.1, 0.1])
        self.assertAlmostEqual(scores.distraction_risk, 0.7)
        self.assertGreater(scores.focus_score, 25.0)
        self.assertLessEqual(scores.focus_score, 100.0)

    def test_heuristic_model(self) -> None:
        model = HeuristicModel(default_feature_columns())
        probas = model.predict_proba([[0.0] * len(default_feature_columns())])
        self.assertEqual(len(probas), 1)
        self.assertEqual(len(probas[0]), 4)
        self.assertAlmostEqual(sum(probas[0]), 1.0, places=4)

    def test_load_majority_model(self) -> None:
        columns = default_feature_columns()
        handle = tempfile.NamedTemporaryFile(mode="w", suffix=".json", delete=False)
        try:
            handle.write('{"type": "majority", "majority_index": 2}')
            handle.close()
            model = load_model(handle.name, columns)

            self.assertIsInstance(model, MajorityModel)
            probas = model.predict_proba([[1.0] * len(columns)])
            self.assertEqual(probas[0][2], 1.0)
        finally:
            try:
                os.unlink(handle.name)
            except OSError:
                pass


if __name__ == "__main__":
    unittest.main()