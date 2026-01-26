"""
Binary schema helpers for Neural Focus events and log headers.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
from typing import ClassVar


class EventType(IntEnum):
    UNKNOWN = 0
    KEY_PRESS = 1
    KEY_RELEASE = 2
    MOUSE_MOVE = 3
    MOUSE_CLICK = 4
    MOUSE_WHEEL = 5
    WINDOW_FOCUS_CHANGE = 6
    WINDOW_TITLE_CHANGE = 7
    WINDOW_MINIMIZE = 8
    WINDOW_MAXIMIZE = 9
    IDLE_START = 10
    IDLE_END = 11
    SCREEN_LOCK = 12
    SCREEN_UNLOCK = 13


@dataclass(frozen=True)
class LogHeader:
    magic: int
    version: int
    write_offset: int
    event_count: int
    file_size: int

    STRUCT: ClassVar[struct.Struct] = struct.Struct("<IIQQQ4Q")
    MAGIC: ClassVar[int] = 0x4C47464E  # "NFGL"
    VERSION: ClassVar[int] = 1

    @classmethod
    def from_bytes(cls, data: bytes) -> "LogHeader":
        if len(data) < cls.STRUCT.size:
            raise ValueError("header too small")
        unpacked = cls.STRUCT.unpack_from(data)
        magic, version, write_offset, event_count, file_size, *_ = unpacked
        return cls(magic, version, write_offset, event_count, file_size)

    def validate(self) -> None:
        if self.magic != self.MAGIC:
            raise ValueError("invalid log header magic")
        if self.version != self.VERSION:
            raise ValueError("unsupported log header version")
        if self.write_offset < self.STRUCT.size:
            raise ValueError("invalid write offset")


@dataclass(frozen=True)
class EventRecord:
    timestamp_us: int
    event_type: EventType
    process_id: int
    app_name: str
    window_handle: int
    data_raw: bytes
    reserved: int

    STRUCT: ClassVar[struct.Struct] = struct.Struct("<QII24sI16sI")

    @classmethod
    def from_bytes(cls, data: bytes) -> "EventRecord":
        if len(data) < cls.STRUCT.size:
            raise ValueError("event record too small")
        unpacked = cls.STRUCT.unpack_from(data)
        timestamp_us, event_type, process_id, app_raw, window_handle, data_raw, reserved = unpacked
        app_name = app_raw.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
        return cls(
            timestamp_us=timestamp_us,
            event_type=EventType(event_type),
            process_id=process_id,
            app_name=app_name,
            window_handle=window_handle,
            data_raw=data_raw,
            reserved=reserved,
        )
