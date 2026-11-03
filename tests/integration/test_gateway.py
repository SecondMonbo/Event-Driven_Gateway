#!/usr/bin/env python3
"""End-to-end tests for the gateway.

The script starts a mock DeepSeek-compatible SSE server and one or more
gateway processes.  It intentionally uses only the Python standard library
so CI does not need pytest.
"""

import argparse
import http.client
import json
import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class MockDeepSeek(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length)
        try:
            request = json.loads(raw)
        except Exception:
            request = {}

        messages = request.get("messages", [])
        last_user = ""
        for message in reversed(messages):
            if message.get("role") == "user":
                last_user = message.get("content", "")
                break

        if "__slow__" in last_user:
            self._start_sse()
            self._send_content("first")
            time.sleep(3.0)
            self._send_done()
            return

        if "__tool__" in last_user and not any(
            m.get("role") == "tool" for m in messages
        ):
            self._start_sse()
            self._send_tool_call()
            self._send_done()
            return

        self._start_sse()
        self._send_content("tool-ok" if "__tool__" in last_user else "ok")
        self._send_done()

    def _start_sse(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()

    def _write_chunk(self, data: str):
        encoded = data.encode("utf-8")
        try:
            self.wfile.write(
                f"{len(encoded):x}\r\n".encode() + encoded + b"\r\n"
            )
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass

    def _send_content(self, text):
        event = (
            "data: "
            + json.dumps(
                {"choices": [{"delta": {"content": text}}]},
                ensure_ascii=False,
            )
            + "\n\n"
        )
        self._write_chunk(event)

    def _send_tool_call(self):
        event = (
            "data: "
            + json.dumps(
                {
                    "choices": [
                        {
                            "delta": {
                                "tool_calls": [
                                    {
                                        "index": 0,
                                        "id": "call_1",
                                        "function": {
                                            "name": "get_current_time",
                                            "arguments": "{}",
                                        },
                                    }
                                ]
                            }
                        }
                    ]
                }
            )
            + "\n\n"
        )
        self._write_chunk(event)

    def _send_done(self):
        self._write_chunk("data: [DONE]\n\n")
        try:
            self.wfile.write(b"0\r\n\r\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass


def wait_for_port(port, proc, log_path, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            log = Path(log_path).read_text(errors="replace")
            raise RuntimeError(f"gateway exited early:\n{log}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return
        except OSError:
            time.sleep(0.05)
    log = Path(log_path).read_text(errors="replace")
    raise RuntimeError(f"gateway did not listen on {port}:\n{log}")


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


class Gateway:
    def __init__(self, binary, mock_url, with_key=True):
        self.port = free_port()
        self.log = tempfile.NamedTemporaryFile(
            prefix="gateway-test-", suffix=".log", delete=False
        )
        self.log.close()

        env = os.environ.copy()
        env["GATEWAY_PORT"] = str(self.port)
        env["DEEPSEEK_BASE_URL"] = mock_url
        env["DEEPSEEK_MODEL"] = "test-model"
        if with_key:
            env["DEEPSEEK_API_KEY"] = "test-key"
        else:
            env.pop("DEEPSEEK_API_KEY", None)

        self.proc = subprocess.Popen(
            [str(binary)],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=open(self.log.name, "wb"),
        )
        try:
            wait_for_port(self.port, self.proc, self.log.name)
        except Exception:
            self.stop()
            raise

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=3)

    def read_log(self):
        return Path(self.log.name).read_text(errors="replace")


def http_request(port, method, path, headers=None, body=None, timeout=5):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=timeout)
    try:
        conn.request(method, path, body=body, headers=headers or {})
        response = conn.getresponse()
        response_body = response.read().decode("utf-8", errors="replace")
        return response.status, dict(response.getheaders()), response_body
    finally:
        conn.close()


def raw_request(port, data, read_timeout=3):
    with socket.create_connection(("127.0.0.1", port), timeout=read_timeout) as sock:
        sock.settimeout(read_timeout)
        sock.sendall(data)
        chunks = []
        while True:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            chunks.append(chunk)
        return b"".join(chunks)


def test_static(gateway):
    status, _headers, body = http_request(
        gateway.port, "GET", "/", {"Connection": "close"}
    )
    assert status == 200, status
    assert "Hello from Static File Server" in body, body

    status, _headers, _body = http_request(
        gateway.port, "GET", "/index.html?x=1", {"Connection": "close"}
    )
    assert status == 200, status

    response = raw_request(
        gateway.port,
        b"GET /../../etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n",
    )
    assert response.startswith(b"HTTP/1.1 403"), response[:200]

    status, headers, _body = http_request(
        gateway.port, "DELETE", "/", {"Connection": "close"}
    )
    assert status == 405, status
    assert "GET" in headers.get("Allow", ""), headers


def test_keep_alive(gateway):
    response = raw_request(
        gateway.port,
        (
            b"GET / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n"
            b"GET /index.html HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n"
        ),
        read_timeout=2,
    )
    assert response.count(b"HTTP/1.1 200 OK") >= 2, response[:500]


def test_chat_success(gateway):
    body = json.dumps({"session_id": "s1", "message": "hi"})
    status, headers, response = http_request(
        gateway.port,
        "POST",
        "/v1/chat",
        {"Content-Type": "application/json"},
        body,
        timeout=10,
    )
    assert status == 200, status
    assert headers.get("Transfer-Encoding") == "chunked", headers
    assert "event: content" in response, response
    assert "data: ok" in response, response
    assert "event: done" in response, response


def test_tool_call(gateway):
    body = json.dumps({"session_id": "tool", "message": "__tool__"})
    status, _headers, response = http_request(
        gateway.port,
        "POST",
        "/v1/chat",
        {"Content-Type": "application/json"},
        body,
        timeout=10,
    )
    assert status == 200, status
    assert "event: tool_result" in response, response
    assert "event: content" in response, response
    assert "event: done" in response, response


def test_heartbeat(gateway):
    with socket.create_connection(("127.0.0.1", gateway.port), timeout=5) as sock:
        sock.settimeout(5)
        sock.sendall(
            b"GET /events HTTP/1.1\r\nHost: x\r\n"
            b"Accept: text/event-stream\r\n\r\n"
        )
        data = b""
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            data += chunk
            if b"event: heartbeat" in data:
                break
    assert b"event: heartbeat" in data, data[:500]


def test_cancel(gateway):
    body = json.dumps({"session_id": "slow", "message": "__slow__"}).encode()
    with socket.create_connection(("127.0.0.1", gateway.port), timeout=5) as sock:
        sock.settimeout(5)
        request = (
            b"POST /v1/chat HTTP/1.1\r\nHost: x\r\n"
            b"Content-Type: application/json\r\n"
            + f"Content-Length: {len(body)}\r\n".encode()
            + b"\r\n"
            + body
        )
        sock.sendall(request)
        # read headers + first content, then simulate client disconnect
        sock.recv(4096)

    time.sleep(1.5)
    log = gateway.read_log()
    assert "canceled" in log, log

    # Worker must be free after cancellation.
    status, _headers, response = http_request(
        gateway.port,
        "POST",
        "/v1/chat",
        {"Content-Type": "application/json"},
        json.dumps({"session_id": "after", "message": "hi"}),
        timeout=10,
    )
    assert status == 200, status
    assert "event: done" in response, response


def test_limits(gateway):
    response = raw_request(
        gateway.port,
        b"GET / HTTP/1.1\r\nHost: x\r\nX-Big: "
        + b"a" * 9000
        + b"\r\n\r\n",
    )
    assert response.startswith(b"HTTP/1.1 431"), response[:200]

    # Send the declared oversized Content-Length with a small prefix.
    # The gateway should reject from the header before reading the body.
    response = raw_request(
        gateway.port,
        b"POST / HTTP/1.1\r\nHost: x\r\n"
        b"Content-Type: application/octet-stream\r\n"
        b"Content-Length: 2097152\r\n"
        b"Connection: close\r\n\r\n"
        + b"x" * 1024,
    )
    assert response.startswith(b"HTTP/1.1 413"), response[:200]


def test_no_api_key(mock_url):
    gateway = Gateway(BIN, mock_url, with_key=False)
    try:
        status, _headers, response = http_request(
            gateway.port,
            "POST",
            "/v1/chat",
            {"Content-Type": "application/json"},
            json.dumps({"session_id": "s1", "message": "hi"}),
            timeout=10,
        )
        assert status == 200, status
        assert "event: error" in response, response
        assert "event: done" in response, response
    finally:
        gateway.stop()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gateway", required=True)
    parser.add_argument("--www", required=True)
    args = parser.parse_args()

    global BIN
    BIN = Path(args.gateway).resolve()
    www = Path(args.www).resolve()

    # HttpChannel resolves ../www relative to the executable.
    expected_www = BIN.parent.parent / "www"
    if not expected_www.exists():
        expected_www.symlink_to(www, target_is_directory=True)

    mock = ThreadingHTTPServer(("127.0.0.1", 0), MockDeepSeek)
    mock.daemon_threads = True
    mock_url = f"http://127.0.0.1:{mock.server_address[1]}/v1/chat/completions"
    thread = threading.Thread(target=mock.serve_forever, daemon=True)
    thread.start()

    gateway = Gateway(BIN, mock_url, with_key=True)
    try:
        test_static(gateway)
        test_keep_alive(gateway)
        test_chat_success(gateway)
        test_tool_call(gateway)
        test_heartbeat(gateway)
        test_cancel(gateway)
        test_limits(gateway)
        test_no_api_key(mock_url)
    finally:
        gateway.stop()
        mock.shutdown()
        mock.server_close()

    print("integration tests passed")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"integration test failed: {exc}", file=sys.stderr)
        raise
