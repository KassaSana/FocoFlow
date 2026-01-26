"""
ZeroMQ subscriber for live event ingestion.
"""

from __future__ import annotations

import sys
from dataclasses import dataclass, field
from typing import Optional

from .event_schema import EventRecord

try:
    import zmq
except ImportError:  # pragma: no cover - handled by tests
    zmq = None


def default_zmq_endpoint() -> str:
    if sys.platform.startswith("win"):
        return "tcp://127.0.0.1:5560"
    return "ipc:///tmp/neurofocus_events"


@dataclass
class ZmqSubscriberConfig:
    endpoint: str = field(default_factory=default_zmq_endpoint)
    rcvhwm: int = 10000


class ZmqEventSubscriber:
    def __init__(self, config: ZmqSubscriberConfig = ZmqSubscriberConfig()) -> None:
        if zmq is None:
            raise RuntimeError("pyzmq is required for ZmqEventSubscriber")
        self.config = config
        self._context = zmq.Context()
        self._socket = None

    def start(self) -> None:
        if self._socket is not None:
            return
        self._socket = self._context.socket(zmq.SUB)
        self._socket.setsockopt(zmq.SUBSCRIBE, b"")
        self._socket.setsockopt(zmq.RCVHWM, self.config.rcvhwm)
        self._socket.connect(self.config.endpoint)

    def close(self) -> None:
        if self._socket is not None:
            self._socket.close(0)
            self._socket = None
        self._context.term()

    def recv_event(self, timeout_ms: int = 0) -> Optional[EventRecord]:
        if self._socket is None:
            raise RuntimeError("subscriber not started")
        if timeout_ms >= 0:
            events = self._socket.poll(timeout_ms)
            if (events & zmq.POLLIN) == 0:
                return None
        data = self._socket.recv()
        if len(data) != EventRecord.STRUCT.size:
            return None
        return EventRecord.from_bytes(data)
