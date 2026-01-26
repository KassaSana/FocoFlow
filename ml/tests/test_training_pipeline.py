import os
import tempfile
import unittest

from ml.features import FeatureVector, write_features_csv
from ml.dataset_builder import join_features_with_labels, read_features_csv, write_labeled_csv
from ml.labeling import FocusLabel, LabelRecord, LabelSource
from ml.training_pipeline import load_dataset, train_baseline


def make_feature(timestamp: float, keystrokes: int) -> FeatureVector:
    return FeatureVector(
        timestamp=timestamp,
        seconds_since_session_start=0,
        hour_of_day=9,
        day_of_week=1,
        minutes_since_last_break=0,
        keystroke_count=keystrokes,
        keystroke_rate=1.0,
        keystroke_interval_mean=0.5,
        keystroke_interval_std=0.1,
        keystroke_interval_trend=0.0,
        mouse_move_count=0,
        mouse_distance_pixels=0.0,
        mouse_speed_mean=0.0,
        mouse_speed_std=0.0,
        mouse_acceleration_mean=0.0,
        mouse_click_count=0,
        context_switches_30s=0,
        context_switches_5min=0,
        time_in_current_app=0,
        unique_apps_5min=1,
        idle_time_30s=0.0,
        idle_event_count_5min=0,
        longest_active_stretch_5min=10,
        window_title_length=10,
        window_title_changed_30s=False,
        is_browser=False,
        is_ide=True,
        is_communication=False,
        is_entertainment=False,
        is_productivity=False,
        focus_momentum=0.0,
        productivity_category="Building",
        is_pseudo_productive=False,
        recent_event_sequence=[1, 1, 1],
    )


class TestTrainingPipeline(unittest.TestCase):
    def test_train_baseline_majority(self) -> None:
        features = [make_feature(10.0, 5), make_feature(20.0, 6), make_feature(30.0, 7)]
        labels = [
            LabelRecord(25.0, FocusLabel.PRODUCTIVE, LabelSource.HOTKEY, "s1"),
            LabelRecord(35.0, FocusLabel.DISTRACTED, LabelSource.HOTKEY, "s1"),
        ]

        with tempfile.TemporaryDirectory() as tmp:
            feature_path = os.path.join(tmp, "features.csv")
            dataset_path = os.path.join(tmp, "dataset.csv")

            write_features_csv(feature_path, features)
            feature_rows = read_features_csv(feature_path)
            labeled_rows = join_features_with_labels(feature_rows, labels, label_window_seconds=15)
            write_labeled_csv(dataset_path, labeled_rows)

            dataset = load_dataset(dataset_path)
            result = train_baseline(dataset, backend="majority")

            self.assertIn("precision_at_10pct", result.metrics)
            self.assertIn("recall_distracted", result.metrics)


if __name__ == "__main__":
    unittest.main()
