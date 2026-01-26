import time
import unittest

from ml.event_schema import EventRecord, EventType

try:
    import zmq
except ImportError:
    zmq = None

from ml.zmq_subscriber import ZmqEventSubscriber, ZmqSubscriberConfig


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


@unittest.skipIf(zmq is None, "pyzmq not installed")
class TestZmqSubscriber(unittest.TestCase):
    def test_receive_event(self) -> None:
        endpoint = "tcp://127.0.0.1:5560"
        ctx = zmq.Context()
        pub = ctx.socket(zmq.PUB)
        pub.bind(endpoint)

        subscriber = ZmqEventSubscriber(ZmqSubscriberConfig(endpoint=endpoint))
        subscriber.start()

        # Slow joiner protection.
        time.sleep(0.05)

        pub.send(build_event_bytes(EventType.KEY_PRESS, "code.exe"))

        received = None
        for _ in range(10):
            received = subscriber.recv_event(timeout_ms=100)
            if received is not None:
                break

        subscriber.close()
        pub.close(0)
        ctx.term()

        self.assertIsNotNone(received)
        self.assertEqual(received.event_type, EventType.KEY_PRESS)
        self.assertEqual(received.app_name, "code.exe")


if __name__ == "__main__":
    unittest.main()
