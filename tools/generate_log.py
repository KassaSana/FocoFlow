"""Generate a synthetic event log compatible with the C++ engine schema.

Used to seed the demo flow on platforms without the Windows capture engine.
"""

import argparse
import os
import struct
import time

HEADER_STRUCT = struct.Struct("<IIQQQ4Q")
EVENT_STRUCT = struct.Struct("<QII24sI16sI")
MAGIC = 0x4C47464E
VERSION = 1

DEFAULT_OUTPUT = os.path.join("samples", "events_demo.bin")


def create_dummy_log(filename, num_events=100):
    header_size = HEADER_STRUCT.size
    event_size = EVENT_STRUCT.size
    file_size = header_size + num_events * event_size

    # magic, version, write_offset, event_count, file_size, padding...
    header = HEADER_STRUCT.pack(
        MAGIC,
        VERSION,
        file_size,  # write_offset points to end of file
        num_events,
        file_size,
        0, 0, 0, 0
    )

    with open(filename, "wb") as f:
        f.write(header)

        start_time = int(time.time() * 1_000_000)

        for i in range(num_events):
            timestamp = start_time + i * 100_000  # 100ms apart
            event_type = 3  # MOUSE_MOVE
            process_id = 1234
            app_name = b"code.exe"
            window_handle = 5678
            data_raw = b"\x00" * 16
            reserved = 0

            event = EVENT_STRUCT.pack(
                timestamp,
                event_type,
                process_id,
                app_name,
                window_handle,
                data_raw,
                reserved
            )
            f.write(event)

    print(f"Created {filename} with {num_events} events")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--output", "-o", default=DEFAULT_OUTPUT,
                        help=f"Output path (default: {DEFAULT_OUTPUT})")
    parser.add_argument("--events", "-n", type=int, default=100,
                        help="Number of synthetic events to write")
    args = parser.parse_args()

    parent = os.path.dirname(args.output)
    if parent:
        os.makedirs(parent, exist_ok=True)
    create_dummy_log(args.output, num_events=args.events)


if __name__ == "__main__":
    main()
