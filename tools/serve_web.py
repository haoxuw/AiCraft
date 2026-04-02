#!/usr/bin/env python3
"""Local web server for AgentWorld WASM build with required CORS headers."""

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
    # Auto-detect the HTML file
    html = 'agentworld.html' if os.path.exists('agentworld.html') else 'aicraft.html'
    print(f'\n  AgentWorld Web: http://localhost:{port}/{html}\n')
    server.serve_forever()
