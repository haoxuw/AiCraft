"""Minimal ModCraft TCP protocol client for E2E test observation.

Connects to a modcraft-server, sends C_HELLO to join as an observer player,
and receives entity/inventory/time broadcasts. Does NOT render or simulate —
just tracks server state for test assertions.

Wire format: [u32 msg_type][u32 payload_length][payload bytes]
"""

import socket
import struct
import time
import uuid

# Message types (must match src/shared/net_protocol.h)
C_HELLO         = 0x0003
S_WELCOME       = 0x1001
S_ENTITY        = 0x1002
S_REMOVE        = 0x1004
S_TIME          = 0x1005
S_BLOCK         = 0x1006
S_INVENTORY     = 0x1007
S_CHUNK         = 0x1003
S_CHUNK_Z       = 0x100F
S_CHUNK_EVICT   = 0x100E
S_CHUNK_INFO    = 0x1010
S_CHUNK_INFO_DELTA = 0x1011
S_ASSIGN_ENTITY = 0x1008
S_REVOKE_ENTITY = 0x1009
S_RELOAD_BEHAVIOR = 0x100A
S_ERROR         = 0x100B
S_PROXIMITY     = 0x1012

PROTOCOL_VERSION = 2


class ReadBuffer:
    """Sequential binary reader matching C++ ReadBuffer."""
    def __init__(self, data: bytes):
        self._data = data
        self._pos = 0

    def remaining(self):
        return len(self._data) - self._pos

    def _read(self, n):
        end = self._pos + n
        if end > len(self._data):
            raise ValueError(f"ReadBuffer underflow: need {n}, have {self.remaining()}")
        chunk = self._data[self._pos:end]
        self._pos = end
        return chunk

    def u8(self):  return struct.unpack_from("<B", self._read(1))[0]
    def u16(self): return struct.unpack_from("<H", self._read(2))[0]
    def u32(self): return struct.unpack_from("<I", self._read(4))[0]
    def i32(self): return struct.unpack_from("<i", self._read(4))[0]
    def f32(self): return struct.unpack_from("<f", self._read(4))[0]
    def bool_(self): return self.u8() != 0

    def vec3(self):
        return (self.f32(), self.f32(), self.f32())

    def ivec3(self):
        return (self.i32(), self.i32(), self.i32())

    def string(self):
        length = self.u32()
        if self._pos + length > len(self._data):
            length = 0
        s = self._data[self._pos:self._pos + length].decode("utf-8", errors="replace")
        self._pos += length
        return s


class EntityState:
    """Parsed S_ENTITY payload."""
    __slots__ = ("id", "type", "position", "velocity", "yaw", "on_ground",
                 "goal_text", "character_skin", "hp", "max_hp", "owner",
                 "move_target", "move_speed", "props")

    def __repr__(self):
        return (f"Entity(id={self.id}, type={self.type}, "
                f"pos=({self.position[0]:.1f},{self.position[1]:.1f},{self.position[2]:.1f}), "
                f"hp={self.hp}, goal={self.goal_text!r})")


def parse_entity_state(rb: ReadBuffer) -> EntityState:
    """Parse EntityState matching C++ deserializeEntityState."""
    es = EntityState()
    es.id = rb.u32()
    es.type = rb.string()
    es.position = rb.vec3()
    es.velocity = rb.vec3()
    es.yaw = rb.f32()
    es.on_ground = rb.bool_()
    es.goal_text = rb.string()
    es.character_skin = rb.string()
    es.hp = rb.i32()
    es.max_hp = rb.i32()
    es.owner = rb.i32() if rb.remaining() > 0 else 0
    es.move_target = rb.vec3() if rb.remaining() >= 12 else (0, 0, 0)
    es.move_speed = rb.f32() if rb.remaining() >= 4 else 0.0
    prop_count = rb.u32() if rb.remaining() >= 4 else 0
    es.props = {}
    for _ in range(prop_count):
        if rb.remaining() < 8:
            break
        k = rb.string()
        v = rb.string()
        es.props[k] = v
    return es


class InventoryState:
    """Parsed S_INVENTORY payload."""
    __slots__ = ("entity_id", "items")

    def count(self, item_id):
        return self.items.get(item_id, 0)


def parse_inventory(rb: ReadBuffer) -> InventoryState:
    inv = InventoryState()
    inv.entity_id = rb.u32()
    n = rb.u32()
    inv.items = {}
    for _ in range(n):
        item_id = rb.string()
        count = rb.i32()
        if count > 0:
            inv.items[item_id] = count
    # Equipment: [u8 count][{str slot, str id}...]
    if rb.remaining() >= 1:
        equip_count = rb.u8()
        for _ in range(equip_count):
            if rb.remaining() < 8:
                break
            rb.string()  # slot name
            rb.string()  # item id
    return inv


class ObserverClient:
    """TCP client that connects to modcraft-server and tracks game state."""

    def __init__(self):
        self.sock = None
        self.player_id = 0
        self.spawn_pos = (0, 0, 0)
        self.entities = {}       # id -> EntityState
        self.inventories = {}    # id -> InventoryState
        self.world_time = 0.0
        self._recv_buf = b""

    def connect(self, host, port, timeout=10.0):
        """Connect and send C_HELLO. Returns True on S_WELCOME."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(timeout)
        self.sock.connect((host, port))
        self.sock.setblocking(False)

        # Send C_HELLO: [u32 version][str uuid][str name][str creature_type]
        payload = b""
        payload += struct.pack("<I", PROTOCOL_VERSION)
        client_uuid = str(uuid.uuid4())
        payload += self._encode_string(client_uuid)
        payload += self._encode_string("test_observer")
        payload += self._encode_string("base:villager")
        self._send_msg(C_HELLO, payload)

        # Wait for S_WELCOME
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            msgs = self._recv_messages(timeout=0.1)
            for msg_type, data in msgs:
                if msg_type == S_WELCOME:
                    rb = ReadBuffer(data)
                    self.player_id = rb.u32()
                    self.spawn_pos = rb.vec3()
                    return True
                # Process other messages that arrive before welcome
                self._handle_message(msg_type, data)
        return False

    def poll(self, duration=0.1):
        """Receive and process messages for up to `duration` seconds."""
        msgs = self._recv_messages(timeout=duration)
        for msg_type, data in msgs:
            self._handle_message(msg_type, data)

    def poll_until(self, condition, timeout=60.0, poll_interval=0.2):
        """Poll until condition() returns True or timeout expires.
        Returns True if condition met, False on timeout.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.poll(duration=poll_interval)
            if condition():
                return True
        return False

    def get_entity(self, eid):
        return self.entities.get(eid)

    def get_inventory(self, eid):
        return self.inventories.get(eid)

    def find_entities_by_type(self, type):
        return [e for e in self.entities.values() if e.type == type]

    def disconnect(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None

    # ── Internal ──────────────────────────────────────────────

    def _handle_message(self, msg_type, data):
        rb = ReadBuffer(data)
        if msg_type == S_ENTITY:
            es = parse_entity_state(rb)
            self.entities[es.id] = es
        elif msg_type == S_REMOVE:
            eid = rb.u32()
            self.entities.pop(eid, None)
            self.inventories.pop(eid, None)
        elif msg_type == S_INVENTORY:
            inv = parse_inventory(ReadBuffer(data))
            self.inventories[inv.entity_id] = inv
        elif msg_type == S_TIME:
            self.world_time = rb.f32()
        # Ignore chunk/block/other messages

    def _send_msg(self, msg_type, payload):
        header = struct.pack("<II", msg_type, len(payload))
        self.sock.sendall(header + payload)

    def _encode_string(self, s):
        encoded = s.encode("utf-8")
        return struct.pack("<I", len(encoded)) + encoded

    def _recv_messages(self, timeout=0.1):
        """Non-blocking receive. Returns list of (msg_type, payload_bytes)."""
        import select
        messages = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = max(0.001, deadline - time.monotonic())
            try:
                ready, _, _ = select.select([self.sock], [], [], remaining)
            except (ValueError, OSError):
                break
            if not ready:
                break
            try:
                chunk = self.sock.recv(65536)
            except (BlockingIOError, ConnectionError):
                break
            if not chunk:
                break
            self._recv_buf += chunk

        # Parse complete messages from buffer
        while len(self._recv_buf) >= 8:
            msg_type, length = struct.unpack_from("<II", self._recv_buf)
            if len(self._recv_buf) < 8 + length:
                break
            payload = self._recv_buf[8:8 + length]
            self._recv_buf = self._recv_buf[8 + length:]
            messages.append((msg_type, payload))
        return messages
