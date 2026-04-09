#!/usr/bin/env python3
"""
ModCraft Action Proxy — HTTP → TCP bridge with Swagger UI.

Connects to the game server as a player client, then exposes every
ActionProposal type as a documented REST endpoint.

Swagger UI:  http://localhost:8088/docs
OpenAPI JSON: http://localhost:8088/openapi.json

Usage:
    pip install fastapi uvicorn
    python3 tools/action_proxy.py                     # defaults: server localhost:7777, proxy :8088
    python3 tools/action_proxy.py --server-port 7778 --proxy-port 8099
"""

import argparse
import re
import struct
import socket
import threading
import time
import sys
from pathlib import Path
from typing import Optional

try:
    from fastapi import FastAPI, HTTPException
    from fastapi.middleware.cors import CORSMiddleware
    from pydantic import BaseModel, Field
    import uvicorn
except ImportError:
    print("Missing dependencies. Run:  pip install fastapi uvicorn", file=sys.stderr)
    sys.exit(1)

# ── Protocol constants (must match net_protocol.h / action.h) ─────────────────

C_ACTION         = 0x0001
C_HELLO          = 0x0003
PROTOCOL_VERSION = 2
ENTITY_NONE      = 0

S_WELCOME        = 0x1001
S_ENTITY         = 0x1002
S_REMOVE         = 0x1004
S_ERROR          = 0x100B

# ActionProposal::Type enum order (must match action.h)
TYPE_MOVE            = 0
TYPE_RELOCATE        = 1
TYPE_CONVERT  = 2
TYPE_INTERACT  = 3
TYPE_RELOAD_BEHAVIOR = 4

# ── Binary helpers (mirrors net_protocol.h WriteBuffer) ──────────────────────

def _str(s: str) -> bytes:
    b = s.encode("utf-8")
    return struct.pack("<I", len(b)) + b

def _vec3(x, y, z) -> bytes:
    return struct.pack("<fff", float(x), float(y), float(z))

def _ivec3(x, y, z) -> bytes:
    return struct.pack("<iii", int(x), int(y), int(z))

def _bool(v) -> bytes:
    return struct.pack("<B", 1 if v else 0)

def _u32(v) -> bytes:
    return struct.pack("<I", int(v))

def _i32(v) -> bytes:
    return struct.pack("<i", int(v))

def _f32(v) -> bytes:
    return struct.pack("<f", float(v))


def _parse_entity_state(payload: bytes) -> Optional[dict]:
    """Parse S_ENTITY payload → dict with id, type_id, x, y, z, hp, max_hp, props.

    Mirrors net_protocol.h::deserializeEntityState.  Returns None on parse error.
    """
    try:
        o = 0
        def ru32():
            nonlocal o; v = struct.unpack_from("<I", payload, o)[0]; o += 4; return v
        def ri32():
            nonlocal o; v = struct.unpack_from("<i", payload, o)[0]; o += 4; return v
        def rf32():
            nonlocal o; v = struct.unpack_from("<f", payload, o)[0]; o += 4; return v
        def rstr():
            nonlocal o
            n = ru32(); s = payload[o:o+n].decode("utf-8", errors="replace"); o += n; return s
        def rbool():
            nonlocal o; v = payload[o] != 0; o += 1; return v

        eid     = ru32()
        type_id = rstr()
        x, y, z = rf32(), rf32(), rf32()
        rf32(); rf32(); rf32()   # velocity
        rf32(); rf32()           # yaw, pitch
        rbool()                  # on_ground
        rstr()                   # goal_text
        rstr()                   # character_skin
        hp      = ri32()
        max_hp  = ri32()
        props   = {}
        for _ in range(ru32()):
            k = rstr(); v = rstr()
            props[k] = v
        return {"id": eid, "type_id": type_id,
                "x": x, "y": y, "z": z,
                "hp": hp, "max_hp": max_hp, "props": props}
    except Exception:
        return None


def serialize_action(p: dict) -> bytes:
    """
    Exact mirror of serializeAction() in src/shared/net_protocol.h.
    All fields are always written; the server ignores irrelevant ones.
    """
    buf  = _u32(p["type"])
    buf += _u32(p["actor_id"])
    # Move ────────────────────────────────────────────────────────────────────
    buf += _vec3(p.get("vel_x", 0), p.get("vel_y", 0), p.get("vel_z", 0))
    buf += _bool(p.get("jump", False))
    buf += _bool(p.get("fly", False))
    buf += _f32(p.get("jump_velocity", 17.0))
    buf += _f32(p.get("look_pitch", 0.0))
    buf += _f32(p.get("look_yaw",   0.0))
    buf += _str(p.get("goal_text",  ""))
    # Relocate ────────────────────────────────────────────────────────────────
    buf += _u32(p.get("from_entity",  ENTITY_NONE))
    buf += _u32(p.get("to_entity",    ENTITY_NONE))
    buf += _bool(p.get("to_ground",   False))
    buf += _str(p.get("item_id",      ""))
    buf += _i32(p.get("item_count",   1))
    buf += _str(p.get("equip_slot",   ""))
    # Convert ─────────────────────────────────────────────────────────────────
    buf += _str(p.get("from_item",    ""))
    buf += _i32(p.get("from_count",   1))
    buf += _str(p.get("to_item",      ""))
    buf += _i32(p.get("to_count",     1))
    buf += _ivec3(p.get("block_x", 0), p.get("block_y", 0), p.get("block_z", 0))
    buf += _bool(p.get("convert_from_block",   False))
    buf += _bool(p.get("convert_to_block",     False))
    buf += _bool(p.get("convert_direct",       True))
    buf += _u32(p.get("convert_from_entity",   ENTITY_NONE))
    # ReloadBehavior ──────────────────────────────────────────────────────────
    buf += _str(p.get("behavior_source", ""))
    return buf


def send_msg(sock: socket.socket, msg_type: int, payload: bytes) -> None:
    sock.sendall(struct.pack("<II", msg_type, len(payload)) + payload)


# ── TCP connection manager ────────────────────────────────────────────────────

class Connection:
    """
    Maintains a single persistent TCP connection to the game server.
    Connects as a GUI player client (C_HELLO), receives its entity ID
    from S_WELCOME, then forwards C_ACTION messages on demand.
    """

    def __init__(self):
        self._sock: Optional[socket.socket] = None
        self._lock = threading.Lock()
        self._connected = False
        self.entity_id: Optional[int] = None
        self.server_errors: list[str] = []
        # Live entity table: entity_id → {type_id, x, y, z, hp, max_hp, props}
        self._entities: dict[int, dict] = {}
        self._entity_lock = threading.Lock()

    # ── public ───────────────────────────────────────────────────────────────

    @property
    def connected(self) -> bool:
        return self._connected

    def connect(self, host: str, port: int) -> None:
        with self._lock:
            if self._connected:
                return
            try:
                s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                s.settimeout(5.0)
                s.connect((host, port))
                s.settimeout(None)
                self._sock = s
                self._connected = True
            except OSError as exc:
                raise RuntimeError(f"Cannot connect to {host}:{port} — {exc}") from exc

        # C_HELLO: [u32 version][str uuid][str name][str skin]
        payload  = _u32(PROTOCOL_VERSION)
        payload += _str("swagger-proxy")
        payload += _str("SwaggerProxy")
        payload += _str("")
        send_msg(self._sock, C_HELLO, payload)

        # Background receiver thread — keeps socket alive, captures entity_id
        threading.Thread(target=self._recv_loop, daemon=True).start()

        # Wait up to 5 s for S_WELCOME
        deadline = time.time() + 5.0
        while self.entity_id is None and time.time() < deadline:
            time.sleep(0.05)
        if self.entity_id is None:
            raise RuntimeError("No S_WELCOME received — is the game server running?")

    def send_action(self, proposal: dict) -> None:
        if not self._connected:
            raise RuntimeError("Not connected to game server. Call POST /connect first.")
        payload = serialize_action(proposal)
        with self._lock:
            send_msg(self._sock, C_ACTION, payload)

    def resolve_actor(self, actor_id: Optional[int]) -> int:
        """Return actor_id if set, otherwise fall back to this client's entity."""
        if actor_id is not None and actor_id != 0:
            return actor_id
        if self.entity_id is None:
            raise RuntimeError("Not connected — no entity assigned yet.")
        return self.entity_id

    # ── private ──────────────────────────────────────────────────────────────

    def _recv_loop(self) -> None:
        try:
            while True:
                raw = self._recv_exact(8)
                msg_type, length = struct.unpack("<II", raw)
                payload = self._recv_exact(length) if length else b""
                self._handle(msg_type, payload)
        except Exception:
            self._connected = False

    def entity_snapshot(self) -> dict[int, dict]:
        """Return a shallow copy of the live entity table (thread-safe)."""
        with self._entity_lock:
            return dict(self._entities)

    def _handle(self, msg_type: int, payload: bytes) -> None:
        if msg_type == S_WELCOME:
            # [u32 entityId][vec3 spawn]
            self.entity_id = struct.unpack_from("<I", payload, 0)[0]
        elif msg_type == S_ENTITY:
            parsed = _parse_entity_state(payload)
            if parsed:
                with self._entity_lock:
                    self._entities[parsed["id"]] = parsed
        elif msg_type == S_REMOVE:
            eid = struct.unpack_from("<I", payload, 0)[0]
            with self._entity_lock:
                self._entities.pop(eid, None)
        elif msg_type == S_ERROR:
            # [u32 entityId][str message]
            eid = struct.unpack_from("<I", payload, 0)[0]
            slen = struct.unpack_from("<I", payload, 4)[0]
            msg = payload[8 : 8 + slen].decode("utf-8", errors="replace")
            entry = f"entity {eid}: {msg}"
            self.server_errors.append(entry)
            if len(self.server_errors) > 50:
                self.server_errors = self.server_errors[-50:]

    def _recv_exact(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Server closed the connection")
            buf += chunk
        return buf


# ── Global connection (one per proxy process) ─────────────────────────────────

conn = Connection()

# ── FastAPI setup ─────────────────────────────────────────────────────────────

app = FastAPI(
    title="ModCraft Action API",
    description=(
        "HTTP → TCP proxy for the ModCraft game server.\n\n"
        "**Read vs Write**\n"
        "- `GET` endpoints are **read-only** — they return state without sending anything to the server.\n"
        "- `POST /action/*` endpoints are **write actions** — they send an `ActionProposal` over TCP "
        "and the server validates and executes them.\n\n"
        "The server accepts exactly **four action types**: `Move`, `Relocate`, `Convert`, `Interact`.  "
        "All gameplay compiles down to these four primitives.\n\n"
        "**Note on block queries**: within Python behaviors `get_block(x, y, z)` is a "
        "read-only query against the agent's local chunk cache — it never touches the server. "
        "This proxy does not expose a block-query endpoint because the proxy does not cache chunk data.\n\n"
        "**Workflow**\n"
        "1. `POST /connect` — connect to the game server (one-time)\n"
        "2. `GET /status` — get your assigned entity ID\n"
        "3. Call any `POST /action/*` endpoint — leave `actor_id = 0` to act as your own entity\n\n"
        "**Material value rule**: `Convert` is rejected if `to_item` value × `to_count` "
        "exceeds `from_item` value × `from_count`. Use `to_item = \"\"` to destroy.\n\n"
        "**Attack shorthand**: `Convert` with `convert_from_entity = <target>`, "
        "`from_item = \"hp\"`, `to_item = \"\"`."
    ),
    version="1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Pydantic request models ───────────────────────────────────────────────────

class ConnectRequest(BaseModel):
    host: str = Field("127.0.0.1", description="Game server host")
    port: int = Field(7777,        description="Game server port")

class MoveRequest(BaseModel):
    actor_id: int = Field(0,   description="Entity to move (0 = your own entity)")
    vel_x:    float = Field(0, description="Desired X velocity (m/s)")
    vel_y:    float = Field(0, description="Desired Y velocity (m/s)")
    vel_z:    float = Field(0, description="Desired Z velocity (m/s)")
    jump:     bool  = Field(False, description="True = jump this tick")
    fly:      bool  = Field(False, description="True = fly (creative / admin mode)")
    look_yaw: float = Field(0, description="Camera yaw in degrees")
    look_pitch: float = Field(0, description="Camera pitch in degrees")
    goal_text: str  = Field("", description="Text shown above entity head")

class RelocateRequest(BaseModel):
    actor_id:    int  = Field(0,   description="Entity performing the action (0 = own)")
    from_entity: int  = Field(0,   description="Source entity ID (item on ground, chest) — 0 = none")
    to_entity:   int  = Field(0,   description="Destination entity ID (chest) — 0 = own inventory")
    to_ground:   bool = Field(False, description="True = drop item as world entity at actor's feet")
    item_id:     str  = Field("",  description="Item type string e.g. 'base:wood'")
    item_count:  int  = Field(1,   description="Number of items to move")
    equip_slot:  str  = Field("",  description="Non-empty = equip to slot ('head','chest','offhand',…)")

class ConvertRequest(BaseModel):
    actor_id:    int  = Field(0,      description="Entity performing the action (0 = own)")
    from_item:   str  = Field(...,    description="Source item type, 'hp', or block type string")
    from_count:  int  = Field(1,      description="Amount to consume")
    to_item:     str  = Field("",     description="Result item type or 'hp'. Empty string = destroy")
    to_count:    int  = Field(1,      description="Amount to produce")
    block_x:     int  = Field(0,      description="Block position X (required if from/to_block is true)")
    block_y:     int  = Field(0,      description="Block position Y")
    block_z:     int  = Field(0,      description="Block position Z")
    convert_from_block: bool = Field(False, description="True = consume the world block at block_x/y/z")
    convert_to_block:   bool = Field(False, description="True = place result item as a world block")
    convert_direct:     bool = Field(True,  description="False = spawn result as a dropped item entity")
    convert_from_entity: int = Field(0,     description="Non-zero = act on that entity's HP or inventory (attack, heal target)")

class InteractRequest(BaseModel):
    actor_id: int = Field(0, description="Entity performing the action (0 = own)")
    block_x:  int = Field(..., description="Block X coordinate")
    block_y:  int = Field(..., description="Block Y coordinate")
    block_z:  int = Field(..., description="Block Z coordinate")

class ReloadBehaviorRequest(BaseModel):
    actor_id:        int  = Field(0,   description="Entity whose behavior to replace (0 = own)")
    entity_id:       int  = Field(0,   description="Target entity ID (overrides actor_id if non-zero)")
    behavior_source: str  = Field(..., description="Full Python source code of the new behavior")

# ── Endpoints ─────────────────────────────────────────────────────────────────

@app.post(
    "/connect",
    summary="Connect to the game server",
    tags=["Connection"],
)
def connect(req: ConnectRequest):
    """
    Open a TCP connection to the game server and perform the client handshake.
    Returns the assigned player entity ID.
    Call this once before issuing any action.
    """
    try:
        conn.connect(req.host, req.port)
        return {"status": "connected", "entity_id": conn.entity_id, "host": req.host, "port": req.port}
    except RuntimeError as exc:
        raise HTTPException(status_code=503, detail=str(exc))


@app.get(
    "/status",
    summary="Connection status and entity ID",
    tags=["Connection"],
)
def status():
    """Returns whether the proxy is connected and what entity ID was assigned."""
    return {
        "connected":        conn.connected,
        "entity_id":        conn.entity_id,
        "recent_errors":    conn.server_errors[-10:],
    }


@app.post(
    "/action/move",
    summary="Move — set entity velocity",
    tags=["Actions"],
)
def action_move(req: MoveRequest):
    """
    Velocity-based movement. The server clamps to the entity's `walk_speed`.

    Send every tick for smooth movement, or once for a single nudge.
    Set `jump=true` to jump. For Creatures control, set `actor_id` to the Creatures's entity ID.
    """
    try:
        p = req.model_dump()
        p["type"] = TYPE_MOVE
        p["actor_id"] = conn.resolve_actor(req.actor_id or None)
        conn.send_action(p)
        return {"sent": "Move", "actor_id": p["actor_id"]}
    except RuntimeError as exc:
        raise HTTPException(status_code=409, detail=str(exc))


@app.post(
    "/action/relocate",
    summary="Relocate — move items between inventories",
    tags=["Actions"],
)
def action_relocate(req: RelocateRequest):
    """
    Moves items between any two inventory references without creating new value.

    **Pick up item on ground**: set `from_entity` to the item entity ID.

    **Drop item**: set `to_ground=true`, `item_id`, and `item_count`.

    **Transfer to chest**: set `to_entity` to the chest entity ID.

    **Equip item**: set `item_id` and `equip_slot` (e.g. `"head"`, `"chest"`, `"offhand"`).
    """
    try:
        p = req.model_dump()
        p["type"] = TYPE_RELOCATE
        p["actor_id"] = conn.resolve_actor(req.actor_id or None)
        conn.send_action(p)
        return {"sent": "Relocate", "actor_id": p["actor_id"]}
    except RuntimeError as exc:
        raise HTTPException(status_code=409, detail=str(exc))


@app.post(
    "/action/convert",
    summary="Convert — transform items, break/place blocks, attack, heal",
    tags=["Actions"],
)
def action_convert(req: ConvertRequest):
    """
    The general transformation primitive. Material value must not increase.

    **Break block** → `from_item="base:wood"`, `convert_from_block=true`, `block_x/y/z`,
    `to_item="base:log"`, `convert_direct=false` (drop item entity).

    **Place block** → `from_item="base:stone"`, `convert_to_block=true`, `block_x/y/z`,
    `to_item="base:stone"`.

    **Use potion** → `from_item="base:potion"`, `from_count=1`, `to_item="hp"`, `to_count=4`.

    **Attack** → `convert_from_entity=<target_id>`, `from_item="hp"`, `from_count=<damage>`,
    `to_item=""` (destroy HP).

    **Lay egg** → `from_item="hp"`, `from_count=2`, `to_item="base:egg"`, `to_count=1`,
    `convert_direct=false`.
    """
    try:
        p = req.model_dump()
        p["type"] = TYPE_CONVERT
        p["actor_id"] = conn.resolve_actor(req.actor_id or None)
        conn.send_action(p)
        return {"sent": "Convert", "actor_id": p["actor_id"]}
    except RuntimeError as exc:
        raise HTTPException(status_code=409, detail=str(exc))


@app.post(
    "/action/interact",
    summary="Interact — toggle any interactive object (door, TNT, button)",
    tags=["Actions"],
)
def action_interact(req: InteractRequest):
    """
    Toggles the state of an interactive block.

    - **Door / DoorOpen** → opens or closes
    - **TNT** → lights the fuse
    - **Button** → pulses
    """
    try:
        p = req.model_dump()
        p["type"] = TYPE_INTERACT
        p["actor_id"] = conn.resolve_actor(req.actor_id or None)
        conn.send_action(p)
        return {"sent": "Interact", "actor_id": p["actor_id"]}
    except RuntimeError as exc:
        raise HTTPException(status_code=409, detail=str(exc))


@app.post(
    "/action/reload_behavior",
    summary="ReloadBehavior — hot-swap an Creatures's Python behavior",
    tags=["Actions"],
)
def action_reload_behavior(req: ReloadBehaviorRequest):
    """
    Replaces the Python behavior source code for the target entity without
    restarting the agent process.

    Set `entity_id` to the Creatures's entity ID. The server forwards the new
    source to the agent client that owns that entity.
    """
    try:
        p = req.model_dump()
        p["type"] = TYPE_RELOAD_BEHAVIOR
        target = req.entity_id if req.entity_id else req.actor_id
        p["actor_id"] = conn.resolve_actor(target or None)
        conn.send_action(p)
        return {"sent": "ReloadBehavior", "actor_id": p["actor_id"]}
    except RuntimeError as exc:
        raise HTTPException(status_code=409, detail=str(exc))


# ── Artifact catalog (loaded dynamically) ────────────────────────────────────
#
# Source of truth:
#   src/shared/material_values.h  — material values (default 1.0 if absent)
#   artifacts/blocks/**/*.py      — block definitions (variable `blocks`)
#   artifacts/items/**/*.py       — item definitions  (variable `item`)
#   artifacts/living/**/*.py      — living defs       (variable `living`)
#
# "hp" is a special virtual type: 1 HP = 1.0 material value.

_REPO_ROOT = Path(__file__).parent.parent

_SPECIAL = [
    {
        "id":             "hp",
        "name":           "Hit Points",
        "kind":           "special",
        "category":       "biological",
        "material_value": 1.0,
        "notes": (
            "Virtual type used in Convert actions to represent entity health. "
            "1 HP = 1.0 material value. Allows: eat apple (apple→hp, value 2→2), "
            "attack (hp→destroy, value decreases). "
            "New value enters the world only via HP regeneration and chunk generation."
        ),
    },
]


def _load_material_values() -> dict:
    """Parse src/shared/material_values.h → {type_id: float}."""
    values = {}
    header = _REPO_ROOT / "src" / "shared" / "material_values.h"
    try:
        for m in re.finditer(r'\{"([^"]+)",\s*([\d.]+)f\}', header.read_text()):
            values[m.group(1)] = float(m.group(2))
    except (OSError, ValueError):
        pass
    return values


def _load_artifact_catalog():
    """Load blocks, items, creatures from artifact files.

    Returns (blocks, items, creatures) — each entry augmented with
    material_value from material_values.h (default 1.0 if not listed).
    """
    mat = _load_material_values()

    def augment(entry: dict) -> dict:
        e = dict(entry)
        e.setdefault("material_value", mat.get(e.get("id", ""), 1.0))
        return e

    blocks = []
    for path in sorted((_REPO_ROOT / "artifacts" / "blocks").rglob("*.py")):
        ns: dict = {}
        try:
            exec(path.read_text(), ns)  # noqa: S102
        except Exception:
            continue
        for b in ns.get("blocks", []):
            blocks.append(augment(b))

    items = []
    for path in sorted((_REPO_ROOT / "artifacts" / "items").rglob("*.py")):
        ns = {}
        try:
            exec(path.read_text(), ns)  # noqa: S102
        except Exception:
            continue
        if "item" in ns:
            items.append(augment(ns["item"]))

    creatures = []
    for path in sorted((_REPO_ROOT / "artifacts" / "creatures").rglob("*.py")):
        ns = {}
        try:
            exec(path.read_text(), ns)  # noqa: S102
        except Exception:
            continue
        if "creature" in ns:
            creatures.append(augment(ns["creature"]))

    return blocks, items, creatures


_BLOCKS, _ITEMS, _CREATURES = _load_artifact_catalog()


@app.get(
    "/metadata",
    summary="All built-in object types with material values and live world counts",
    tags=["World Queries"],
)
def metadata():
    """
    Returns the complete catalog of built-in game objects, their intrinsic
    **material values**, and **live counts** of entities currently tracked on
    the server.

    ## Material value system

    Every object has an intrinsic material value per unit.  The server enforces
    conservation: a `Convert` action is rejected if
    `value(to_item) × to_count > value(from_item) × from_count`.

    Reference: **1 base:dirt = 1.0**.  1 HP = 1.0 (same scale).

    New value enters the world only via:
    - HP regeneration (creatures regen health over time)
    - Chunk generation (terrain blocks appear when new chunks load)
    - Plant growth (wheat crops, future tree regrowth)

    ## Live counts

    `world_count` is populated for entity types (creatures, dropped items,
    players) from the live S_ENTITY / S_REMOVE stream received since connecting.
    It is `null` for block types — counting blocks requires decoding chunk data
    (block IDs in S_CHUNK are uint32 registry indices, not string IDs), which
    this proxy does not do.

    Dropped items (`base:item_entity`) are broken down by `item_type` prop in
    the `dropped_items` field.
    """
    entities = conn.entity_snapshot()

    # Count live entities by type_id
    type_counts: dict[str, int] = {}
    dropped: dict[str, int] = {}
    for e in entities.values():
        tid = e["type_id"]
        type_counts[tid] = type_counts.get(tid, 0) + 1
        if tid == "base:item_entity":
            item_type = e["props"].get("item_type", "unknown")
            dropped[item_type] = dropped.get(item_type, 0) + int(e["props"].get("count", 1))

    def with_count(entry: dict, kind: str) -> dict:
        row = dict(entry)
        row["kind"] = kind
        if kind in ("creature", "special"):
            row["world_count"] = type_counts.get(entry["id"], 0)
        else:
            row["world_count"] = None  # blocks: not tracked from proxy
        return row

    return {
        "material_value_reference": "1 base:dirt = 1.0  |  1 hp = 1.0",
        "value_conservation_rule": (
            "Convert is rejected if value(to_item)*to_count > value(from_item)*from_count. "
            "Entries marked '(default)' in notes are not in the explicit table; "
            "server assigns 1.0."
        ),
        "world_count_note": (
            "world_count is live for creatures/players/dropped items (from S_ENTITY stream). "
            "null for blocks (uint32 chunk IDs not decoded by proxy). "
            "Counts reset to 0 on reconnect until server re-sends all entity states."
        ),
        "special_concepts": [with_count(e, "special") for e in _SPECIAL],
        "blocks":           [with_count(b, "block")   for b in _BLOCKS],
        "items":            [with_count(i, "item")     for i in _ITEMS],
        "creatures":        [with_count(c, "creature") for c in _CREATURES],
        "dropped_items_on_ground": dropped,
    }


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ModCraft Action Proxy with Swagger UI")
    parser.add_argument("--server-host", default="127.0.0.1", help="Game server host (default: 127.0.0.1)")
    parser.add_argument("--server-port", default=7777, type=int, help="Game server port (default: 7777)")
    parser.add_argument("--proxy-port",  default=8088, type=int, help="HTTP proxy port (default: 8088)")
    parser.add_argument("--auto-connect", action="store_true",
                        help="Connect to game server immediately on startup")
    args = parser.parse_args()

    if args.auto_connect:
        try:
            conn.connect(args.server_host, args.server_port)
            print(f"[Proxy] Connected to {args.server_host}:{args.server_port} "
                  f"— entity ID: {conn.entity_id}")
        except RuntimeError as exc:
            print(f"[Proxy] Auto-connect failed: {exc}  (use POST /connect later)")

    print(f"\n  Swagger UI:   http://localhost:{args.proxy_port}/docs")
    print(f"  OpenAPI JSON: http://localhost:{args.proxy_port}/openapi.json\n")

    uvicorn.run(app, host="0.0.0.0", port=args.proxy_port, log_level="warning")


if __name__ == "__main__":
    main()
