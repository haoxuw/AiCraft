"""Test harness — launches civcraft-server, waits for ready, connects observer.

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

# Locate the build directory relative to repo root.
# __file__ = <root>/src/tests/behavior_scenario_validation/harness.py → 4 dirnames.
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
BUILD_DIR = os.path.join(REPO_ROOT, "build")
SERVER_BIN = os.path.join(BUILD_DIR, "civcraft-server")
CLIENT_BIN = os.path.join(BUILD_DIR, "civcraft-ui-vk")


def _find_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class GameHarness:
    """Manages a civcraft-server process and an observer TCP connection."""

    def __init__(self, template=2, seed=100, port=None, spawn_agent_host=False):
        self.template = template
        self.seed = seed
        self.port = port or _find_free_port()
        self.server_proc = None
        self.agent_host_proc = None
        self.spawn_agent_host = spawn_agent_host
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

        ready_path = f"/tmp/civcraft_ready_{self.port}"
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
        server_log = f"/tmp/civcraft_test_server_{self.port}.log"
        self._server_log_path = server_log
        self._server_log_f = open(server_log, "wb")
        self.server_proc = subprocess.Popen(
            cmd,
            cwd=BUILD_DIR,
            stdout=self._server_log_f,
            stderr=subprocess.STDOUT,
        )

        # Wait for ready signal
        deadline = time.monotonic() + 15.0
        while time.monotonic() < deadline:
            if self.server_proc.poll() is not None:
                self._server_log_f.close()
                with open(server_log, "rb") as f:
                    out = f.read().decode(errors="replace")
                raise RuntimeError(f"Server exited early (code={self.server_proc.returncode}):\n{out}")
            if os.path.exists(ready_path):
                break
            time.sleep(0.1)
        else:
            self.stop()
            raise TimeoutError("Server did not become ready within 15s")

        # Small delay for TCP listener to fully start
        time.sleep(0.3)

        # Spawn agent-host BEFORE observer when requested. Seats are assigned
        # in connection order, and `make game`'s reproducible wedge bug is a
        # seat-1 phenomenon (village stamp closer to forest, tighter packing).
        # Observer-first would give the agent-host seat 2, where villagers
        # spawn too far from trees to reproduce the bug in a short test run.
        #
        # Vulkan client bootstrap (GPU init + Python + chunk prep) is slower
        # than Python observer connect, so we wait for the server log to
        # confirm the agent-host claimed seat=1 before letting observer in.
        if self.spawn_agent_host:
            if not os.path.isfile(CLIENT_BIN):
                self.stop()
                raise FileNotFoundError(
                    f"Client binary not found: {CLIENT_BIN}\n"
                    f"Run: cmake --build build -j$(nproc)"
                )
            agent_log = f"/tmp/civcraft_agent_host_{self.port}.log"
            self.agent_host_proc = subprocess.Popen(
                [CLIENT_BIN, "--log-only",
                 "--host", "127.0.0.1", "--port", str(self.port)],
                cwd=BUILD_DIR,
                stdout=open(agent_log, "wb"),
                stderr=subprocess.STDOUT,
            )
            # Wait for agent-host to finish its handshake (S_READY sent)
            # before we let observer in. If observer connects during Client 1's
            # chunk-prep burst, the server is too busy to answer S_WELCOME and
            # observer times out.
            deadline = time.monotonic() + 60.0
            saw_seat = False
            while time.monotonic() < deadline:
                if self.agent_host_proc.poll() is not None:
                    self.stop()
                    raise RuntimeError("Agent-host exited before handshake")
                try:
                    with open(server_log, "rb") as f:
                        data = f.read()
                        if not saw_seat and b"claimed seat=1" in data:
                            saw_seat = True
                        if saw_seat and b"S_READY sent" in data:
                            break
                except FileNotFoundError:
                    pass
                time.sleep(0.2)
            else:
                self.stop()
                raise TimeoutError("Agent-host handshake did not complete within 60s")

        # Connect observer last so it lands in a later seat.
        if not self.observer.connect("127.0.0.1", self.port, timeout=30.0):
            self.stop()
            raise ConnectionError("Observer failed to receive S_WELCOME")

    def stop(self):
        """Kill server and all spawned agent processes."""
        self.observer.disconnect()
        if self.agent_host_proc and self.agent_host_proc.poll() is None:
            self.agent_host_proc.send_signal(signal.SIGTERM)
            try:
                self.agent_host_proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.agent_host_proc.kill()
                self.agent_host_proc.wait()
        if self.server_proc and self.server_proc.poll() is None:
            # Send SIGTERM (server saves and shuts down gracefully)
            self.server_proc.send_signal(signal.SIGTERM)
            try:
                self.server_proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.server_proc.kill()
                self.server_proc.wait()
        if getattr(self, "_server_log_f", None):
            try:
                self._server_log_f.close()
            except Exception:
                pass

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
