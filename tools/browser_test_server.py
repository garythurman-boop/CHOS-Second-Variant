#!/usr/bin/env python3
"""Deterministic HTTP fixture server for CHOS Browser testing on a host computer.

Run on the host with: python3 tools/browser_test_server.py --bind 0.0.0.0 --port 8080
From QEMU user-mode networking, open http://10.0.2.2:8080/ in CHOS.
"""
from __future__ import annotations
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import time
from urllib.parse import urlsplit

INDEX = b"""<!doctype html>
<html><head><title>CHOS Browser Test</title></head><body>
<h1>CHOS Browser Test Fixture</h1>
<p>Use these links to verify clickable navigation and all supported redirect codes.</p>
<ul>
<li><a href="/local-page">Direct page</a></li>
<li><a href="/relative/index.html">Relative link test</a></li>
<li><a href="/redirect/301">Redirect 301</a></li>
<li><a href="/redirect/302">Redirect 302</a></li>
<li><a href="/redirect/303">Redirect 303</a></li>
<li><a href="/redirect/307">Redirect 307</a></li>
<li><a href="/redirect/308">Redirect 308</a></li>
<li><a href="/loop/a">Redirect loop (should stop safely)</a></li>
</ul>
<p>Enter /slow-close to exercise delayed TCP close and /wrong-content-type for HTML sniffing.</p>
</body></html>"""

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"
    server_version = "CHOSFixture/0.1"
    sys_version = ""

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"[{self.address_string()}] {fmt % args}", flush=True)

    def _send(self, status: int, body: bytes, content_type: str = "text/html; charset=utf-8",
              headers: dict[str, str] | None = None, delay_close: float = 0.0) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        for name, value in (headers or {}).items():
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        if delay_close:
            # Content-Length lets the browser finish rendering now, but the TCP
            # peer deliberately retains its sending socket to test bounded close.
            time.sleep(delay_close)

    def do_GET(self) -> None:  # noqa: N802
        path = urlsplit(self.path).path
        if path in ("/", "/index.html"):
            self._send(200, INDEX)
        elif path == "/local-page":
            self._send(200, b"<html><body><h1>Direct page loaded</h1><p>Link click succeeded.</p><ul><li>First</li><li>Second</li></ul><a href='/'>Back to the index</a></body></html>")
        elif path == "/relative/index.html":
            self._send(200, b"<html><body><h1>Relative base</h1><a href='sibling.html'>Open sibling page</a></body></html>")
        elif path == "/relative/sibling.html":
            self._send(200, b"<html><body><h1>Relative link succeeded</h1><a href='../'>Return to index</a></body></html>")
        elif path == "/redirect/https":
            self._send(302, b"", headers={"Location": "https://example.com/"})
        elif path.startswith("/redirect/"):
            try:
                status = int(path.rsplit("/", 1)[-1])
            except ValueError:
                status = 0
            if status not in (301, 302, 303, 307, 308):
                self._send(404, b"<h1>Fixture route not found</h1>")
            else:
                self._send(status, b"", headers={"Location": f"/redirect-target?via={status}"})
        elif path == "/redirect-target":
            q = urlsplit(self.path).query or "no query"
            self._send(200, f"<html><body><h1>Redirect succeeded</h1><p>{q}</p><a href='/'>Home</a></body></html>".encode())
        elif path == "/loop/a":
            self._send(302, b"", headers={"Location": "/loop/b"})
        elif path == "/loop/b":
            self._send(302, b"", headers={"Location": "/loop/a"})
        elif path == "/wrong-content-type":
            self._send(200, b"<html><body><h1>HTML sniff succeeded</h1><a href='/'>Home</a></body></html>", "text/plain")
        elif path == "/plain":
            self._send(200, b"Plain text page\nSecond line\nThird line", "text/plain; charset=utf-8")
        elif path == "/slow-close":
            self._send(200, b"<html><body><h1>Slow peer close</h1><p>This body is complete, but the peer holds the connection for 8 seconds.</p><a href='/local-page'>Navigate away now</a></body></html>", delay_close=8.0)
        elif path == "/slow-response":
            time.sleep(20.0)
            self._send(200, b"<html><body><h1>Slow response finally arrived</h1></body></html>")
        else:
            self._send(404, b"<html><body><h1>404 Fixture route not found</h1></body></html>")

    def do_HEAD(self) -> None:  # noqa: N802
        self._send(200, b"")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    print(f"CHOS browser fixture listening on http://{args.bind}:{args.port}/", flush=True)
    print("In QEMU user-mode networking, try http://10.0.2.2:8080/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopping fixture server.", flush=True)
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
