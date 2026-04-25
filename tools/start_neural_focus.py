"""
Cross-platform launcher for the FocoFlow stack.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def resolve_maven_wrapper(backend_dir: Path) -> list[str] | None:
    if os.name == "nt":
        wrapper = backend_dir / "mvnw.cmd"
    else:
        wrapper = backend_dir / "mvnw"
    if wrapper.exists():
        return [str(wrapper)]
    return None


def resolve_maven_command() -> list[str] | None:
    mvn = shutil.which("mvn")
    if mvn:
        return [mvn]

    m2_home = os.environ.get("M2_HOME")
    if m2_home:
        candidate = Path(m2_home) / "bin" / "mvn.cmd"
        if candidate.exists():
            return [str(candidate)]

    if os.name == "nt":
        choco_root = os.environ.get("ChocolateyInstall", "C:\\ProgramData\\chocolatey")
        candidate = Path(choco_root) / "bin" / "mvn.cmd"
        if candidate.exists():
            return [str(candidate)]

    return None


def resolve_docker_compose(root: Path) -> list[str] | None:
    compose_path = root / "docker-compose.yml"
    if not compose_path.exists():
        return None
    if shutil.which("docker"):
        return ["docker", "compose", "-f", str(compose_path)]
    if shutil.which("docker-compose"):
        return ["docker-compose", "-f", str(compose_path)]
    return None


def resolve_npm() -> list[str] | None:
    npm = shutil.which("npm")
    if npm:
        return [npm]
    if os.name == "nt":
        npm_cmd = shutil.which("npm.cmd")
        if npm_cmd:
            return [npm_cmd]
    return None


def start_process(command: list[str], cwd: Path) -> None:
    subprocess.Popen(command, cwd=str(cwd))


def main() -> None:
    parser = argparse.ArgumentParser(description="Start the FocoFlow stack")
    parser.add_argument("--backend-url", default="http://localhost:8080")
    parser.add_argument("--session-id", default="live-session")
    parser.add_argument("--session-goal", default="")
    parser.add_argument("--zmq-endpoint", default="tcp://127.0.0.1:5560")
    parser.add_argument("--engine-path", default="")
    parser.add_argument("--log-path", default="")
    parser.add_argument("--replay-sleep-ms", type=int, default=0)
    parser.add_argument("--replay-startup-delay-ms", type=int, default=100)
    parser.add_argument("--no-backend", action="store_true")
    parser.add_argument("--no-inference", action="store_true")
    parser.add_argument("--no-frontend", action="store_true")
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    backend_dir = root / "backend"
    frontend_dir = root / "frontend"
    default_engine = root / "core" / "build" / "bin" / "neurofocus_engine.exe"

    print("Starting FocoFlow stack...")

    if not args.no_backend:
        mvn_wrapper = resolve_maven_wrapper(backend_dir)
        mvn_cmd = mvn_wrapper or resolve_maven_command()
        if mvn_cmd:
            start_process(mvn_cmd + ["spring-boot:run"], backend_dir)
            label = "mvnw" if mvn_wrapper else "mvn"
            print(f"Started backend ({label} spring-boot:run)")
        else:
            compose = resolve_docker_compose(root)
            if compose:
                subprocess.run(compose + ["up", "-d", "backend"], check=False)
                print("Started backend (docker compose up -d backend)")
            else:
                print("Backend not started (Maven/Docker not found).")

    if not args.no_inference:
        cmd = [
            sys.executable,
            "-m",
            "ml.inference_server",
            "--backend-url",
            args.backend_url,
            "--session-id",
            args.session_id,
            "--zmq-endpoint",
            args.zmq_endpoint,
        ]
        if args.session_goal:
            cmd += ["--session-goal", args.session_goal]
        start_process(cmd, root)
        print("Started inference bridge")

    if args.engine_path:
        engine = Path(args.engine_path)
        if engine.exists():
            start_process([str(engine)], root)
            print(f"Started event engine: {engine}")
        else:
            print(f"Engine path not found: {engine}")
    elif not args.log_path and default_engine.exists():
        start_process([str(default_engine)], root)
        print(f"Started event engine: {default_engine}")

    if args.log_path:
        log_path = Path(args.log_path)
        if log_path.exists():
            cmd = [
                sys.executable,
                "-m",
                "ml.event_replay",
                "--log-path",
                str(log_path),
                "--endpoint",
                args.zmq_endpoint,
                "--startup-delay-ms",
                str(max(0, args.replay_startup_delay_ms)),
            ]
            if args.replay_sleep_ms > 0:
                cmd += ["--sleep-ms", str(args.replay_sleep_ms)]
            start_process(cmd, root)
            print(f"Started event replay: {log_path}")
        else:
            print(f"Log path not found: {log_path}")

    if not args.no_frontend:
        npm_cmd = resolve_npm()
        if npm_cmd and (frontend_dir / "package.json").exists():
            start_process(npm_cmd + ["run", "dev"], frontend_dir)
            print("Started frontend (npm run dev)")
        else:
            print("Frontend not started (npm missing or package.json absent).")

    print("Done. Use --no-frontend/--no-backend/--no-inference to skip components.")


if __name__ == "__main__":
    main()
