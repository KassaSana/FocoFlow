import os
import tempfile
import unittest

from ml.event_replay import iter_event_bytes
from ml.event_schema import EventRecord, EventType, LogHeader


def build_event_bytes(event_type: int, app_name: str) -> bytes:
    app_raw = app_name.encode("utf-8")[:24].ljust(24, b"\x00")
    data_raw = b"\x00" * 16
    return EventRecord.STRUCT.pack(
        123456789,
        event_type,
        4242,
        app_raw,
        0x1234,
        data_raw,
        0,
    )


def build_header_bytes(event_count: int, file_size: int) -> bytes:
    write_offset = LogHeader.STRUCT.size + event_count * EventRecord.STRUCT.size
    return LogHeader.STRUCT.pack(
        LogHeader.MAGIC,
        LogHeader.VERSION,
        write_offset,
        event_count,
        file_size,
        0,
        0,
        0,
        0,
    )


class TestEventReplay(unittest.TestCase):
    def test_iter_event_bytes(self) -> None:
        events = [
            build_event_bytes(EventType.KEY_PRESS, "code.exe"),
            build_event_bytes(EventType.MOUSE_CLICK, "chrome.exe"),
        ]
        header = build_header_bytes(
            len(events),
            LogHeader.STRUCT.size + len(events) * EventRecord.STRUCT.size,
        )

        with tempfile.NamedTemporaryFile(delete=False) as handle:
            handle.write(header)
            for event in events:
                handle.write(event)
            path = handle.name

        try:
            payloads = list(iter_event_bytes(path))
            self.assertEqual(payloads, events)
        finally:
            os.remove(path)

    def test_iter_event_bytes_limit(self) -> None:
        events = [
            build_event_bytes(EventType.KEY_PRESS, "code.exe"),
            build_event_bytes(EventType.MOUSE_CLICK, "chrome.exe"),
        ]
        header = build_header_bytes(
            len(events),
            LogHeader.STRUCT.size + len(events) * EventRecord.STRUCT.size,
        )

        with tempfile.NamedTemporaryFile(delete=False) as handle:
            handle.write(header)
            for event in events:
                handle.write(event)
            path = handle.name

        try:
            payloads = list(iter_event_bytes(path, limit=1))
            self.assertEqual(len(payloads), 1)
            self.assertEqual(payloads[0], events[0])
        finally:
            os.remove(path)


if __name__ == "__main__":
    unittest.main()