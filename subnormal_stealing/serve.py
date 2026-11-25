"""Run: python3 subnormal_stealing/serve.py"""

import errno
import json
import secrets
import socketserver
import threading
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent
SIZE = 8
RESULTS_LOG = ROOT / 'results_log.jsonl'

class ThreadingHTTPServer(socketserver.ThreadingMixIn, HTTPServer):
    daemon_threads = True


def victim_html(pixels):
    """One SVG unit is one CSS pixel"""
    """8x8 black then insert rect white pixels"""
    rects = ''.join(
        f'<rect x="{i % SIZE}" y="{i // SIZE}" width="1" height="1" fill="white"/>'
        for i, value in enumerate(pixels) if value
    )
    return f'''<!doctype html><meta charset="utf-8"><title>Victim</title>
<style>html,body{{margin:0;overflow:hidden;background:black}}svg{{display:block}}</style>
<svg xmlns="http://www.w3.org/2000/svg" width="8" height="8" viewBox="0 0 8 8"
shape-rendering="crispEdges"><rect width="8" height="8" fill="black"/>{rects}</svg>
<script>parent.postMessage('fixture-ready', 'http://127.0.0.1:8000');</script>'''.encode()


def handler(role, pixels, run_id):
    class Handler(BaseHTTPRequestHandler):
        def _send(self, body, content_type='text/html; charset=utf-8', status=200):
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urlsplit(self.path).path
            if role == 'victim' and path == '/':
                self._send(victim_html(pixels))
            elif role == 'victim' and path == '/truth.json':
                body = json.dumps({'size': SIZE, 'pixels': pixels}).encode()
                self._send(body, content_type='application/json; charset=utf-8')
            elif role == 'attacker' and path in ('/', '/attacker.js'):
                filename = 'index.html' if path == '/' else 'attacker.js'
                body = (ROOT / filename).read_bytes()
                if path == '/':
                    body = body.replace(b'__RUN_ID__', run_id.encode())
                content_type = ('text/javascript; charset=utf-8' if path == '/attacker.js'
                                 else 'text/html; charset=utf-8')
                self._send(body, content_type=content_type)
            else:
                self.send_error(404)

        def do_POST(self):
            path = urlsplit(self.path).path
            if role == 'attacker' and path == '/results':
                length = int(self.headers.get('Content-Length', 0))
                raw = self.rfile.read(length) if length else b''
                try:
                    record = json.loads(raw.decode('utf-8'))
                except (ValueError, UnicodeDecodeError):
                    self.send_error(400, 'Invalid JSON')
                    return
                record['receivedAt'] = datetime.now(timezone.utc).isoformat()
                record['runId'] = run_id
                with RESULTS_LOG.open('a', encoding='utf-8') as f:
                    f.write(json.dumps(record) + '\n')
                self._send(b'{"ok":true}', content_type='application/json; charset=utf-8')
            else:
                self.send_error(404)

        def log_message(self, fmt, *args):
            pass  

    return Handler


def main():
    pixels = [0] * 32 + [1] * 32    # the secret
    secrets.SystemRandom().shuffle(pixels)  # half black half white pixels
    run_id = secrets.token_hex(8)
    truth_path = ROOT / f'truth_{run_id}.json'
    truth_path.write_text(json.dumps({'runId': run_id, 'size': SIZE, 'pixels': pixels}), encoding='utf-8')
    servers = []
    try:
        for port, role in ((8000, 'attacker'), (8001, 'victim')):
            try:
                servers.append(ThreadingHTTPServer(('127.0.0.1', port), handler(role, pixels, run_id)))
            except OSError as error:
                if error.errno == errno.EADDRINUSE:
                    raise SystemExit(f'Port {port} is already in use. Stop the other server with Ctrl+C, then retry.') from None
                raise
        for server in servers:
            threading.Thread(target=server.serve_forever, daemon=True).start()
        print('Attacker: http://127.0.0.1:8000', flush=True)
        print('Victim:   http://127.0.0.1:8001', flush=True)
        print(f'Run ID:   {run_id}', flush=True)
        print(f'Truth file:  {truth_path}', flush=True)
        print(f'Results log: {RESULTS_LOG}', flush=True)
        print('Ctrl+C to stop. Restart creates a new pattern and a new run ID.', flush=True)
        threading.Event().wait()
    except KeyboardInterrupt:
        pass
    finally:
        for server in servers:
            server.server_close()


if __name__ == '__main__':
    main()
