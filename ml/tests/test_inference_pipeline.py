import json
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer

from ml.event_schema import EventRecord, EventType
from ml.inference_server import InferenceConfig, run_inference

try:
    import zmq
except ImportError:
    zmq = None


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


class PredictionCaptureHandler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        try:
            payload = json.loads(body.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            payload = None

        self.server.payload = payload
        self.send_response(200)
        self.end_headers()

    def log_message(self, format: str, *args) -> None:
        return


@unittest.skipIf(zmq is None, "pyzmq not installed")
class TestInferencePipeline(unittest.TestCase):
    def test_prediction_published_to_backend(self) -> None:
        server = HTTPServer(("127.0.0.1", 0), PredictionCaptureHandler)
        server.payload = None
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()

        port = server.server_address[1]
        endpoint = "tcp://127.0.0.1:5562"

        ctx = zmq.Context()
        pub = ctx.socket(zmq.PUB)
        pub.bind(endpoint)

        try:
            config = InferenceConfig(
                backend_url=f"http://127.0.0.1:{port}",
                session_id="test-session",
                zmq_endpoint=endpoint,
                prediction_interval_seconds=0.0,
                poll_interval_ms=50,
                model_path=None,
                log_predictions=False,
                dry_run=False,
                max_predictions=1,
            )

            inference_thread = threading.Thread(target=run_inference, args=(config,), daemon=True)
            inference_thread.start()

            time.sleep(0.1)
            payload = build_event_bytes(EventType.KEY_PRESS, "code.exe")
            for _ in range(20):
                pub.send(payload)
                time.sleep(0.05)
                if server.payload is not None:
                    break

            inference_thread.join(timeout=1.0)

            self.assertIsNotNone(server.payload)
            self.assertEqual(server.payload.get("sessionId"), "test-session")
            self.assertIn("focusScore", server.payload)
            self.assertIn("distractionRisk", server.payload)
        finally:
            server.shutdown()
            server.server_close()
            server_thread.join(timeout=1.0)
            pub.close(0)
            ctx.term()


if __name__ == "__main__":
    unittest.main()
