#!/usr/bin/env python3
"""Local web server for CivCraft WASM build with required CORS headers."""

import sys
import os
from http.server import HTTPServer, SimpleHTTPRequestHandler

class CORSHandler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        super().end_headers()

if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    directory = sys.argv[2] if len(sys.argv) > 2 else '.'
    os.chdir(directory)
    server = HTTPServer(('', port), CORSHandler)
    candidates = ['civcraft.html']
    html = next((f for f in candidates if os.path.exists(f)), candidates[0])
    ws_port = 7779
    print(f"""
  CivCraft Web Client: http://localhost:{port}/{html}

  To join a LAN game, you need a WebSocket proxy (websockify) running
  on the server machine so the browser can reach the TCP game server:

    pip install websockify          # one-time setup
    websockify {ws_port} localhost:7777   # proxy WS:{ws_port} → TCP:7777

  Then in the web client, enter the server address as:
    host: <server-ip>   port: {ws_port}
""")
    server.serve_forever()
