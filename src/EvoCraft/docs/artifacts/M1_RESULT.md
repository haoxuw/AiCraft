# M1 — Handshake Artifact

Completed: 2026-04-13. Per plan §11, M1 is shippable when the Godot client
receives framed messages from `evocraft-server` over TCP.

## What's live

- `evocraft-server` (C++20, POSIX sockets, single-threaded poll loop).
- `evocraft` Godot 4 project at `src/EvoCraft/godot/`.
- Wire format: `u32 payloadLen` · `u16 msgType` · body. Little-endian.
- Messages: `S_HELLO` (proto version + server name), `S_TICK` (u64 tick, f32 simTime).
- `make game GAME=evocraft` → server + interactive Godot client.
- `make test_e2e GAME=evocraft` → server + headless Godot, exits 0 after 3 ticks.

## Verification

See [`m1_handshake_client.log`](m1_handshake_client.log) — Godot client's view
of 10 consecutive ticks. Matching server-side log in
[`m1_handshake_server.log`](m1_handshake_server.log).

Independent wire-format verification via raw Python client also passed
(`S_HELLO proto=1 name='evocraft-server M1'` + 3 S_TICK frames).

## Known limitations (by design for M1)

- Server ignores all inbound bytes — `C_*` messages land at M2.
- Port collision on pre-running server produces a confusing success (Godot
  connects to the old server). Mitigated by `pkill -x evocraft-server` in
  the `test_e2e` target.
- Tick rate is 1Hz — deliberately slow for readability. M3 drops to 30Hz.

## Next: M2

Scene composition (floor caustics, water ceiling, kelp sway, fog, camera
fixed at `(0, 0, 11)`). No cells yet.
