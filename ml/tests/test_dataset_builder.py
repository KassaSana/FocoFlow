import csv
import os
import tempfile
import unittest

from ml.dataset_builder import (
    join_features_with_labels,
    read_features_csv,
    read_labels_csv,
    write_labeled_csv,
)
from ml.features import FeatureVector, write_features_csv
from ml.labeling import FocusLabel, LabelRecord, LabelSource


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


class TestDatasetBuilder(unittest.TestCase):
    def test_join_with_labels(self) -> None:
        features = [make_feature(10.0, 5), make_feature(20.0, 6), make_feature(40.0, 7)]
        labels = [
            LabelRecord(
                timestamp=25.0,
                label=FocusLabel.PRODUCTIVE,
                source=LabelSource.HOTKEY,
                session_id="s1",
                notes="",
            )
        ]

        with tempfile.TemporaryDirectory() as tmp:
            feature_path = os.path.join(tmp, "features.csv")
            label_path = os.path.join(tmp, "labels.csv")
            output_path = os.path.join(tmp, "dataset.csv")

            write_features_csv(feature_path, features)
            with open(label_path, "w", newline="", encoding="utf-8") as handle:
                writer = csv.writer(handle)
                writer.writerow(["timestamp", "label", "source", "session_id", "notes"])
                writer.writerow([25.0, int(FocusLabel.PRODUCTIVE), "HOTKEY", "s1", ""])

            feature_rows = read_features_csv(feature_path)
            label_rows = read_labels_csv(label_path)
            joined = join_features_with_labels(feature_rows, label_rows, label_window_seconds=20)

            self.assertEqual(len(joined), 2)
            self.assertEqual(joined[0]["label"], str(int(FocusLabel.PRODUCTIVE)))

            count = write_labeled_csv(output_path, joined)
            self.assertEqual(count, 2)

    def test_keep_unlabeled(self) -> None:
        features = [make_feature(10.0, 5)]
        labels = []

        with tempfile.TemporaryDirectory() as tmp:
            feature_path = os.path.join(tmp, "features.csv")
            write_features_csv(feature_path, features)

            feature_rows = read_features_csv(feature_path)
            joined = join_features_with_labels(feature_rows, labels, keep_unlabeled=True)

            self.assertEqual(len(joined), 1)
            self.assertEqual(joined[0]["label"], "")


if __name__ == "__main__":
    unittest.main()
