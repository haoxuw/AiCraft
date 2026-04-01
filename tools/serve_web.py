#!/usr/bin/env python3
"""Local web server for AiCraft WASM build with required CORS headers."""

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
    print(f'\n  AiCraft Web: http://localhost:{port}/aicraft.html\n')
    server.serve_forever()
