import os
import struct
import tempfile
import unittest

from ml.event_log_reader import EventLogReader
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


class TestEventLogReader(unittest.TestCase):
    def test_read_events(self) -> None:
        events = [
            build_event_bytes(EventType.KEY_PRESS, "code.exe"),
            build_event_bytes(EventType.MOUSE_CLICK, "chrome.exe"),
        ]
        header = build_header_bytes(len(events), LogHeader.STRUCT.size + len(events) * EventRecord.STRUCT.size)

        with tempfile.NamedTemporaryFile(delete=False) as handle:
            handle.write(header)
            for event in events:
                handle.write(event)
            path = handle.name

        try:
            reader = EventLogReader(path)
            header_out = reader.read_header()
            self.assertEqual(header_out.event_count, 2)

            records = reader.read_events()
            self.assertEqual(len(records), 2)
            self.assertEqual(records[0].event_type, EventType.KEY_PRESS)
            self.assertEqual(records[0].app_name, "code.exe")
            self.assertEqual(records[1].event_type, EventType.MOUSE_CLICK)
            self.assertEqual(records[1].app_name, "chrome.exe")
        finally:
            os.remove(path)

    def test_invalid_header_magic(self) -> None:
        bad_header = struct.pack("<IIQQQ4Q", 0x0, 1, 64, 0, 64, 0, 0, 0, 0)
        with tempfile.NamedTemporaryFile(delete=False) as handle:
            handle.write(bad_header)
            path = handle.name

        try:
            reader = EventLogReader(path)
            with self.assertRaises(ValueError):
                reader.read_header()
        finally:
            os.remove(path)


if __name__ == "__main__":
    unittest.main()
