#!/usr/bin/env python3
"""
Simple HTTP server with no-cache headers for development
"""

import http.server
import socketserver

PORT = 3000

class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        super().end_headers()

class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True

with ReusableTCPServer(("", PORT), NoCacheHandler) as httpd:
    print(f"LNK-22 Web Client running at http://localhost:{PORT}")
    print("Press Ctrl+C to stop")
    httpd.serve_forever()
