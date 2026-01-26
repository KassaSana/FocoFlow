import os
import tempfile
import unittest
from typing import List

from ml.event_schema import EventRecord, EventType, LogHeader
from ml.metrics_report import collect_metrics


def build_event(timestamp_us: int, event_type: EventType, app_name: str) -> bytes:
    app_raw = app_name.encode("utf-8")[:24].ljust(24, b"\x00")
    data_raw = b"\x00" * 16
    return EventRecord.STRUCT.pack(
        timestamp_us,
        int(event_type),
        1234,
        app_raw,
        0,
        data_raw,
        0,
    )


def write_log(path: str, events: List[bytes]) -> None:
    header_size = LogHeader.STRUCT.size
    event_size = EventRecord.STRUCT.size
    write_offset = header_size + len(events) * event_size
    file_size = write_offset
    header = LogHeader.STRUCT.pack(
        LogHeader.MAGIC,
        LogHeader.VERSION,
        write_offset,
        len(events),
        file_size,
        0,
        0,
        0,
        0,
    )

    with open(path, "wb") as handle:
        handle.write(header)
        for event in events:
            handle.write(event)


class TestMetricsReport(unittest.TestCase):
    def test_collect_metrics(self) -> None:
        event_a = build_event(1_000_000, EventType.KEY_PRESS, "code.exe")
        event_b = build_event(2_000_000, EventType.MOUSE_CLICK, "chrome.exe")

        handle = tempfile.NamedTemporaryFile(delete=False)
        try:
            handle.close()
            write_log(handle.name, [event_a, event_b])

            metrics = collect_metrics(handle.name, benchmark_features=True)
            self.assertEqual(metrics.total_events, 2)
            self.assertAlmostEqual(metrics.duration_seconds, 1.0, places=3)
            self.assertEqual(metrics.unique_apps, 2)
            self.assertEqual(metrics.event_type_counts.get("KEY_PRESS"), 1)
            self.assertEqual(metrics.event_type_counts.get("MOUSE_CLICK"), 1)
            self.assertIsNotNone(metrics.feature_throughput_eps)
        finally:
            try:
                os.unlink(handle.name)
            except OSError:
                pass


if __name__ == "__main__":
    unittest.main()
