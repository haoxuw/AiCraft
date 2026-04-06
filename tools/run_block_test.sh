#!/bin/bash
# Test block data pipeline: start server, connect, check blocks
set -e

PORT=9876
BINARY="./build/modcraft-server"

# Kill any stale servers
pkill -9 -f "modcraft-server" 2>/dev/null || true
sleep 1

# Start server
$BINARY --port $PORT &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"
sleep 3

# Run Python test
python3 -u -c "
import socket, struct, time

s = socket.socket()
s.connect(('127.0.0.1', $PORT))
time.sleep(2)
s.settimeout(2)

data = b''
try:
    while True:
        d = s.recv(65536)
        if d:
            data += d
        else:
            break
except:
    pass

print(f'Received: {len(data)} bytes')

off = 0
chunks = 0
types = {}
while off + 8 <= len(data):
    t, l = struct.unpack_from('<II', data, off)
    off += 8
    if off + l > len(data):
        break
    if t == 0x1001:
        pid = struct.unpack_from('<I', data, off)[0]
        sx, sy, sz = struct.unpack_from('<fff', data, off+4)
        print(f'WELCOME: player={pid} spawn=({sx:.0f},{sy:.0f},{sz:.0f})')
    elif t == 0x1003:
        chunks += 1
        for i in range(4096):
            bid = struct.unpack_from('<I', data, off+12+i*4)[0]
            if bid > 0:
                types[bid] = types.get(bid, 0) + 1
    off += l

print(f'Chunks: {chunks}')
print(f'Block types found:')
for bid, cnt in sorted(types.items()):
    print(f'  BlockId {bid}: {cnt} blocks')
s.close()
"

# Stop server
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null || true
echo "Done."
