import csv
import os
import tempfile
import unittest

from ml.features import FeatureVector, append_feature_csv, write_features_csv


def make_feature(seed: int) -> FeatureVector:
    return FeatureVector(
        timestamp=1000.0 + seed,
        seconds_since_session_start=seed,
        hour_of_day=10,
        day_of_week=1,
        minutes_since_last_break=5,
        keystroke_count=seed + 1,
        keystroke_rate=3.5,
        keystroke_interval_mean=0.2,
        keystroke_interval_std=0.1,
        keystroke_interval_trend=-0.05,
        mouse_move_count=seed + 2,
        mouse_distance_pixels=120.0,
        mouse_speed_mean=200.0,
        mouse_speed_std=50.0,
        mouse_acceleration_mean=20.0,
        mouse_click_count=2,
        context_switches_30s=1,
        context_switches_5min=2,
        time_in_current_app=30,
        unique_apps_5min=3,
        idle_time_30s=0.0,
        idle_event_count_5min=0,
        longest_active_stretch_5min=300,
        window_title_length=12,
        window_title_changed_30s=False,
        is_browser=False,
        is_ide=True,
        is_communication=False,
        is_entertainment=False,
        is_productivity=False,
        focus_momentum=75.0,
        productivity_category="Building",
        is_pseudo_productive=False,
        recent_event_sequence=[1, 2, 3],
    )


class TestFeatureExport(unittest.TestCase):
    def test_write_features_csv(self) -> None:
        features = [make_feature(1), make_feature(2)]
        with tempfile.NamedTemporaryFile(delete=False) as handle:
            path = handle.name

        try:
            count = write_features_csv(path, features)
            self.assertEqual(count, 2)

            with open(path, "r", newline="", encoding="utf-8") as handle:
                reader = csv.reader(handle)
                rows = list(reader)

            self.assertEqual(rows[0], FeatureVector.headers())
            self.assertEqual(len(rows) - 1, 2)
        finally:
            os.remove(path)

    def test_append_feature_csv(self) -> None:
        with tempfile.NamedTemporaryFile(delete=False) as handle:
            path = handle.name

        try:
            append_feature_csv(path, make_feature(1))
            append_feature_csv(path, make_feature(2))

            with open(path, "r", newline="", encoding="utf-8") as handle:
                reader = csv.reader(handle)
                rows = list(reader)

            self.assertEqual(rows[0], FeatureVector.headers())
            self.assertEqual(len(rows), 3)
        finally:
            os.remove(path)


if __name__ == "__main__":
    unittest.main()
