import unittest

from ml.labeling import FocusLabel, LabelSource, Labeler


class TestLabeling(unittest.TestCase):
    def test_label_recording(self) -> None:
        labeler = Labeler(session_id="s1", goal="Focus work")
        labeler.start_session(10.0)

        labeler.add_label(
            label=FocusLabel.PRODUCTIVE,
            source=LabelSource.HOTKEY,
            timestamp=25.0,
            notes="Felt good",
        )

        labeler.end_session(60.0)

        labels = labeler.labels
        self.assertEqual(len(labels), 1)
        self.assertEqual(labels[0].label, FocusLabel.PRODUCTIVE)
        self.assertEqual(labels[0].source, LabelSource.HOTKEY)
        self.assertEqual(labels[0].session_id, "s1")
        self.assertEqual(labeler.session.start_ts, 10.0)
        self.assertEqual(labeler.session.end_ts, 60.0)


if __name__ == "__main__":
    unittest.main()
