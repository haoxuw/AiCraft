#!/usr/bin/env python3
"""Test the block query pipeline — verifies blocks (including trees) are sent to clients."""

import socket, struct, time, sys, os

HOST = "127.0.0.1"
PORT = int(os.environ.get("PORT", "7777"))

S_WELCOME = 0x1001
S_ENTITY = 0x1002
S_CHUNK = 0x1003

def main():
    print(f"Connecting to {HOST}:{PORT}...")
    s = socket.socket()
    try:
        s.connect((HOST, PORT))
    except ConnectionRefusedError:
        print("ERROR: No server. Run: make server")
        return 1

    s.settimeout(5)

    # Read all data for 3 seconds
    data = b""
    t0 = time.time()
    while time.time() - t0 < 3:
        try:
            d = s.recv(65536)
            if d:
                data += d
            else:
                break
        except socket.timeout:
            break
    s.close()

    print(f"Received {len(data)} bytes")

    # Parse messages
    off = 0
    chunks = {}
    spawn = None
    while off + 8 <= len(data):
        msg_type, length = struct.unpack_from("<II", data, off)
        off += 8
        if off + length > len(data):
            break
        payload = data[off:off+length]
        off += length

        if msg_type == S_WELCOME:
            pid = struct.unpack_from("<I", payload, 0)[0]
            sx, sy, sz = struct.unpack_from("<fff", payload, 4)
            spawn = (sx, sy, sz)
            print(f"Welcome! Player={pid}, spawn=({sx:.0f},{sy:.0f},{sz:.0f})")

        elif msg_type == S_CHUNK:
            p = 0
            cx = struct.unpack_from("<i", payload, p)[0]; p += 4
            cy = struct.unpack_from("<i", payload, p)[0]; p += 4
            cz = struct.unpack_from("<i", payload, p)[0]; p += 4
            blocks = []
            for i in range(16*16*16):
                bid = struct.unpack_from("<I", payload, p)[0]; p += 4
                blocks.append(bid)
            chunks[(cx,cy,cz)] = blocks

    print(f"Parsed {len(chunks)} chunks")

    if not chunks:
        print("ERROR: No chunks!")
        return 1

    # Count block types
    counts = {}
    for pos, blocks in chunks.items():
        for bid in blocks:
            if bid > 0:
                counts[bid] = counts.get(bid, 0) + 1

    print(f"\nBlock types (by ID):")
    for bid, cnt in sorted(counts.items(), key=lambda x: -x[1]):
        print(f"  ID {bid:3d}: {cnt:6d} blocks")

    # Find non-ground blocks (likely trees = wood + leaves)
    # Ground blocks are the most common ones
    ground_ids = {bid for bid, cnt in counts.items() if cnt > 1000}
    print(f"\nGround IDs (>1000): {ground_ids}")

    tree_blocks = []
    for (cx,cy,cz), blocks in chunks.items():
        for i, bid in enumerate(blocks):
            if bid > 0 and bid not in ground_ids:
                lx = i % 16
                lz = (i // 16) % 16
                ly = i // 256
                wx = cx*16 + lx
                wy = cy*16 + ly
                wz = cz*16 + lz
                tree_blocks.append((wx,wy,wz,bid))

    print(f"\nNon-ground blocks: {len(tree_blocks)}")
    by_id = {}
    for wx,wy,wz,bid in tree_blocks:
        by_id.setdefault(bid, []).append((wx,wy,wz))

    for bid, positions in sorted(by_id.items()):
        print(f"\n  ID {bid}: {len(positions)} blocks (likely {'wood' if len(positions) < 500 else 'leaves/other'})")
        for p in positions[:8]:
            print(f"    ({p[0]:4d}, {p[1]:3d}, {p[2]:4d})")
        if len(positions) > 8:
            print(f"    ... +{len(positions)-8} more")

    print("\nDone.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
