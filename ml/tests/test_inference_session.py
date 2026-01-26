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


class SessionCaptureHandler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        payload = None
        if body:
            try:
                payload = json.loads(body.decode("utf-8"))
            except (json.JSONDecodeError, UnicodeDecodeError):
                payload = None

        if self.path == "/api/sessions/start":
            self.server.session_goal = payload.get("goal") if isinstance(payload, dict) else None
            response = {"sessionId": "session-123", "goal": self.server.session_goal}
            data = json.dumps(response).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)
            return

        if self.path == "/api/predictions":
            self.server.prediction = payload
            self.send_response(200)
            self.end_headers()
            return

        self.send_response(404)
        self.end_headers()

    def log_message(self, format: str, *args) -> None:
        return


@unittest.skipIf(zmq is None, "pyzmq not installed")
class TestInferenceSessionStart(unittest.TestCase):
    def test_session_goal_starts_session(self) -> None:
        server = HTTPServer(("127.0.0.1", 0), SessionCaptureHandler)
        server.session_goal = None
        server.prediction = None
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()

        port = server.server_address[1]
        endpoint = "tcp://127.0.0.1:5563"

        ctx = zmq.Context()
        pub = ctx.socket(zmq.PUB)
        pub.bind(endpoint)

        try:
            config = InferenceConfig(
                backend_url=f"http://127.0.0.1:{port}",
                session_id="fallback-session",
                zmq_endpoint=endpoint,
                prediction_interval_seconds=0.0,
                poll_interval_ms=50,
                model_path=None,
                log_predictions=False,
                dry_run=False,
                max_predictions=1,
                session_goal="Test focus goal",
            )

            inference_thread = threading.Thread(target=run_inference, args=(config,), daemon=True)
            inference_thread.start()

            time.sleep(0.1)
            payload = build_event_bytes(EventType.KEY_PRESS, "code.exe")
            for _ in range(20):
                pub.send(payload)
                time.sleep(0.05)
                if server.prediction is not None:
                    break

            inference_thread.join(timeout=1.0)

            self.assertEqual(server.session_goal, "Test focus goal")
            self.assertIsNotNone(server.prediction)
            self.assertEqual(server.prediction.get("sessionId"), "session-123")
        finally:
            server.shutdown()
            server.server_close()
            server_thread.join(timeout=1.0)
            pub.close(0)
            ctx.term()


if __name__ == "__main__":
    unittest.main()
