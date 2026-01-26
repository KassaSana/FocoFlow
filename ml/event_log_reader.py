"""
Read memory-mapped event logs produced by the C++ engine.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, List, Optional

from .event_schema import EventRecord, LogHeader


@dataclass
class EventLogReader:
    path: str

    def read_header(self) -> LogHeader:
        with open(self.path, "rb") as handle:
            data = handle.read(LogHeader.STRUCT.size)
        header = LogHeader.from_bytes(data)
        header.validate()
        return header

    def iter_events(self, limit: Optional[int] = None) -> Iterable[EventRecord]:
        header = self.read_header()
        max_events = header.event_count
        if limit is not None:
            max_events = min(max_events, limit)

        with open(self.path, "rb") as handle:
            handle.seek(LogHeader.STRUCT.size)
            for _ in range(max_events):
                chunk = handle.read(EventRecord.STRUCT.size)
                if len(chunk) < EventRecord.STRUCT.size:
                    break
                yield EventRecord.from_bytes(chunk)

    def read_events(self, limit: Optional[int] = None) -> List[EventRecord]:
        return list(self.iter_events(limit=limit))
