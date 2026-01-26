"""
Replay event logs to a ZeroMQ PUB socket for local demos.
"""

from __future__ import annotations

import argparse
import time
from dataclasses import dataclass
from typing import Iterable, Optional

from .event_schema import EventRecord, LogHeader
from .zmq_subscriber import default_zmq_endpoint

try:
    import zmq
except ImportError:  # pragma: no cover - optional dependency
    zmq = None


@dataclass(frozen=True)
class ReplayConfig:
    log_path: str
    endpoint: str
    limit: Optional[int]
    sleep_ms: int
    startup_delay_ms: int
    loop: bool


def iter_event_bytes(path: str, limit: Optional[int] = None) -> Iterable[bytes]:
    with open(path, "rb") as handle:
        header_bytes = handle.read(LogHeader.STRUCT.size)
        header = LogHeader.from_bytes(header_bytes)
        header.validate()
        max_events = header.event_count
        if limit is not None:
            max_events = min(max_events, limit)

        handle.seek(LogHeader.STRUCT.size)
        for _ in range(max_events):
            chunk = handle.read(EventRecord.STRUCT.size)
            if len(chunk) < EventRecord.STRUCT.size:
                break
            yield chunk


def run_replay(config: ReplayConfig) -> None:
    if zmq is None:
        raise RuntimeError("pyzmq is required to replay events")

    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.bind(config.endpoint)

    time.sleep(max(0, config.startup_delay_ms) / 1000.0)

    try:
        while True:
            for payload in iter_event_bytes(config.log_path, limit=config.limit):
                socket.send(payload)
                if config.sleep_ms > 0:
                    time.sleep(config.sleep_ms / 1000.0)
            if not config.loop:
                break
    except KeyboardInterrupt:
        print("replay stopped")
    finally:
        socket.close(0)
        context.term()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Replay event logs into ZeroMQ")
    parser.add_argument("--log-path", required=True)
    parser.add_argument("--endpoint", default=default_zmq_endpoint())
    parser.add_argument("--limit", type=int, default=None)
    parser.add_argument("--sleep-ms", type=int, default=0)
    parser.add_argument("--startup-delay-ms", type=int, default=100)
    parser.add_argument("--loop", action="store_true")
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()
    config = ReplayConfig(
        log_path=args.log_path,
        endpoint=args.endpoint,
        limit=args.limit,
        sleep_ms=max(0, args.sleep_ms),
        startup_delay_ms=max(0, args.startup_delay_ms),
        loop=args.loop,
    )
    run_replay(config)


if __name__ == "__main__":
    main()
