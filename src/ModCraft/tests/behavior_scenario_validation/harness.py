"""Test harness — launches modcraft-server, waits for ready, connects observer.

Usage:
    with GameHarness(template=2, seed=100) as game:
        game.wait_for_entities(timeout=30)
        # ... assertions on game.observer.entities ...
"""

import os
import signal
import socket
import subprocess
import time

from .protocol import ObserverClient

# Locate the build directory relative to repo root
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
BUILD_DIR = os.path.join(REPO_ROOT, "build")
SERVER_BIN = os.path.join(BUILD_DIR, "modcraft-server")


def _find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class GameHarness:
    """Manages a modcraft-server process and an observer TCP connection."""

    def __init__(self, template=2, seed=100, port=None):
        self.template = template
        self.seed = seed
        self.port = port or _find_free_port()
        self.server_proc = None
        self.observer = ObserverClient()

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()

    def start(self):
        """Launch server, wait for ready file, connect observer."""
        if not os.path.isfile(SERVER_BIN):
            raise FileNotFoundError(
                f"Server binary not found: {SERVER_BIN}\n"
                f"Run: cmake --build build -j$(nproc)"
            )

        ready_path = f"/tmp/modcraft_ready_{self.port}"
        # Clean stale ready file
        try:
            os.remove(ready_path)
        except FileNotFoundError:
            pass

        # Launch server
        cmd = [
            SERVER_BIN,
            "--port", str(self.port),
            "--template", str(self.template),
            "--seed", str(self.seed),
        ]
        self.server_proc = subprocess.Popen(
            cmd,
            cwd=BUILD_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )

        # Wait for ready signal
        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            if self.server_proc.poll() is not None:
                out = self.server_proc.stdout.read().decode(errors="replace")
                raise RuntimeError(f"Server exited early (code={self.server_proc.returncode}):\n{out}")
            if os.path.exists(ready_path):
                break
            time.sleep(0.1)
        else:
            self.stop()
            raise TimeoutError("Server did not become ready within 15s")

        # Small delay for TCP listener to fully start
        time.sleep(0.3)

        # Connect observer
        if not self.observer.connect("127.0.0.1", self.port, timeout=10.0):
            self.stop()
            raise ConnectionError("Observer failed to receive S_WELCOME")

    def stop(self):
        """Kill server and all spawned agent processes."""
        self.observer.disconnect()
        if self.server_proc and self.server_proc.poll() is None:
            # Send SIGTERM (server saves and shuts down gracefully)
            self.server_proc.send_signal(signal.SIGTERM)
            try:
                self.server_proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.server_proc.kill()
                self.server_proc.wait()

    def wait_for_entities(self, min_count=2, timeout=30.0):
        """Poll until at least min_count entities are visible."""
        ok = self.observer.poll_until(
            lambda: len(self.observer.entities) >= min_count,
            timeout=timeout,
        )
        if not ok:
            raise TimeoutError(
                f"Expected >= {min_count} entities, got {len(self.observer.entities)} "
                f"after {timeout}s: {list(self.observer.entities.values())}"
            )

    def wait_for_type(self, type, timeout=30.0):
        """Poll until at least one entity of the given type appears."""
        ok = self.observer.poll_until(
            lambda: any(e.type == type for e in self.observer.entities.values()),
            timeout=timeout,
        )
        if not ok:
            types = {e.type for e in self.observer.entities.values()}
            raise TimeoutError(
                f"No entity of type {type!r} after {timeout}s. "
                f"Visible types: {types}"
            )
