#!/usr/bin/env python3
"""Regression tests for the deterministic runtime HTTP fixture."""
from __future__ import annotations
import http.client
import threading
import sys
from pathlib import Path
from http.server import ThreadingHTTPServer
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.browser_test_server import Handler


def request(port: int, path: str) -> tuple[int, dict[str, str], bytes]:
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=2)
    conn.request("GET", path)
    response = conn.getresponse()
    body = response.read()
    result = response.status, {k.lower(): v for k, v in response.getheaders()}, body
    conn.close()
    return result


def main() -> None:
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    port = server.server_address[1]
    try:
        status, headers, body = request(port, "/")
        assert status == 200 and "text/html" in headers["content-type"]
        for code in (301, 302, 303, 307, 308):
            status, headers, body = request(port, f"/redirect/{code}")
            assert status == code and headers["location"] == f"/redirect-target?via={code}"
            assert headers["content-length"] == "0" and body == b""
        status, headers, _ = request(port, "/loop/a")
        assert status == 302 and headers["location"] == "/loop/b"
        status, headers, _ = request(port, "/redirect/https")
        assert status == 302 and headers["location"] == "https://example.com/"
        status, headers, body = request(port, "/wrong-content-type")
        assert status == 200 and headers["content-type"].startswith("text/plain")
        assert body.lstrip().lower().startswith(b"<html>")
        status, headers, body = request(port, "/relative/index.html")
        assert status == 200 and b"href='sibling.html'" in body
        status, headers, body = request(port, "/relative/sibling.html")
        assert status == 200 and b"Relative link succeeded" in body
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    print("Browser HTTP fixture: index, links, all redirect codes, loops and misleading content type passed")

if __name__ == "__main__":
    main()
