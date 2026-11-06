"""Run: python3 subnormal_stealing/serve.py"""

import errno
import secrets
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent
SIZE = 8


def victim_html(pixels):
    """One SVG unit is one CSS pixel"""
    """8x8 black then insert rect white pixels"""
    rects = ''.join(
        f'<rect x="{i % SIZE}" y="{i // SIZE}" width="1" height="1" fill="white"/>'
        for i, value in enumerate(pixels) if value
    )
    return f'''<!doctype html><meta charset="utf-8"><title>Victim fixture</title>
<style>html,body{{margin:0;overflow:hidden;background:black}}svg{{display:block}}</style>
<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8" viewBox="0 0 8 8"
shape-rendering="crispEdges"><rect width="8" height="8" fill="black"/>{rects}</svg>
<script>parent.postMessage('fixture-ready', 'http://127.0.0.1:8000');</script>'''.encode()


def handler(role, pixels):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            path = urlsplit(self.path).path
            content_type = 'text/html; charset=utf-8'
            if role == 'victim' and path == '/':
                body = victim_html(pixels)
            elif role == 'attacker' and path in ('/', '/attacker.js'):
                filename = 'index.html' if path == '/' else 'attacker.js'
                body = (ROOT / filename).read_bytes()
                if path == '/attacker.js':
                    content_type = 'text/javascript; charset=utf-8'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            # no headers: embedding allowed, reads are not
            self.end_headers()
            self.wfile.write(body)

    return Handler


def main():
    pixels = [0] * 32 + [1] * 32    # the secret
    secrets.SystemRandom().shuffle(pixels) # half black half white pixels
    servers = []
    try:
        for port, role in ((8000, 'attacker'), (8001, 'victim')):
            try:
                servers.append(ThreadingHTTPServer(('127.0.0.1', port), handler(role, pixels)))
            except OSError as error:
                if error.errno == errno.EADDRINUSE:
                    raise SystemExit(f'Port {port} is already in use. Stop the other server with Ctrl+C, then retry.') from None
                raise
        for server in servers:
            threading.Thread(target=server.serve_forever, daemon=True).start()
        print('Attacker: http://127.0.0.1:8000', flush=True)
        print('Victim:   http://127.0.0.1:8001', flush=True)
        print('Ctrl+C to stop. Restart creates a new pattern.', flush=True)
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        for server in servers:
            server.server_close()


if __name__ == '__main__':
    main()
