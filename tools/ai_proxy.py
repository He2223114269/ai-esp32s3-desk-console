"""Small OpenAI-compatible proxy for the ODES ESP32 assistant.

Secrets are read from environment variables and are never written to disk.
This server is intended for a trusted local network, not direct Internet use.
"""

from __future__ import annotations

import json
import hmac
import os
import threading
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


HOST = os.environ.get("ODES_PROXY_HOST", "0.0.0.0")
PORT = int(os.environ.get("ODES_PROXY_PORT", "8765"))
DEVICE_TOKEN = os.environ.get("ODES_DEVICE_TOKEN", "")
API_KEY = os.environ.get("OPENAI_API_KEY", "")
API_BASE = os.environ.get(
    "OPENAI_BASE_URL", "https://open.bigmodel.cn/api/paas/v4"
).rstrip("/")
MODEL = os.environ.get("OPENAI_MODEL", "glm-4.5-flash")
MAX_TURNS = 8
MAX_MESSAGE_CHARS = 1000

history: list[dict[str, str]] = []
history_lock = threading.Lock()


class Handler(BaseHTTPRequestHandler):
    server_version = "ODESProxy/1.0"

    def log_message(self, fmt: str, *args: object) -> None:
        print("[proxy] " + fmt % args)

    def send_json(self, code: int, payload: dict[str, object]) -> None:
        body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path != "/health":
            self.send_json(404, {"error": "not found"})
            return
        self.send_json(200, {"ok": True, "model_configured": bool(API_KEY)})

    def do_POST(self) -> None:
        if self.path == "/reset":
            if not self.authorized():
                self.send_json(401, {"error": "unauthorized"})
                return
            with history_lock:
                history.clear()
            self.send_json(200, {"ok": True})
            return

        if self.path != "/chat":
            self.send_json(404, {"error": "not found"})
            return
        if not self.authorized():
            self.send_json(401, {"error": "unauthorized"})
            return
        if not API_KEY:
            self.send_plain(503, "Set OPENAI_API_KEY on the computer running this proxy.")
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length < 2 or length > 8192:
                raise ValueError("invalid request size")
            payload = json.loads(self.rfile.read(length))
            message = payload.get("message", "")
            if not isinstance(message, str) or not message.strip():
                raise ValueError("message is empty")
            if len(message) > MAX_MESSAGE_CHARS:
                raise ValueError("message is too long")
        except (ValueError, json.JSONDecodeError) as exc:
            self.send_plain(400, str(exc))
            return

        with history_lock:
            messages = [
                {
                    "role": "system",
                    "content": (
                        "You are ODES, a concise and practical assistant. "
                        "Reply in English using plain ASCII because the device "
                        "screen does not have a Chinese font. Keep the answer "
                        "under 350 characters. Do not use markdown tables."
                    ),
                },
                *history[-MAX_TURNS * 2 :],
                {"role": "user", "content": message},
            ]
            try:
                answer = self.ask_model(messages)
            except (OSError, TimeoutError, ValueError, urllib.error.URLError) as exc:
                self.send_plain(502, f"AI provider error: {exc}")
                return
            history.extend(
                [{"role": "user", "content": message}, {"role": "assistant", "content": answer}]
            )
            del history[:-MAX_TURNS * 2]

        self.send_plain(200, answer)

    def authorized(self) -> bool:
        if not DEVICE_TOKEN:
            return True
        supplied = self.headers.get("Authorization", "")
        expected = f"Bearer {DEVICE_TOKEN}"
        return hmac.compare_digest(supplied, expected)

    def send_plain(self, code: int, message: str) -> None:
        body = message.encode("utf-8")[:1600]
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    @staticmethod
    def ask_model(messages: list[dict[str, str]]) -> str:
        payload = json.dumps(
            {"model": MODEL, "messages": messages, "temperature": 0.5, "max_tokens": 180}
        ).encode("utf-8")
        request = urllib.request.Request(
            f"{API_BASE}/chat/completions",
            data=payload,
            headers={
                "Authorization": f"Bearer {API_KEY}",
                "Content-Type": "application/json",
            },
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=35) as response:
                result = json.loads(response.read(65536))
        except urllib.error.HTTPError as exc:
            detail = exc.read(1024).decode("utf-8", "replace")
            raise ValueError(f"HTTP {exc.code}: {detail}") from exc
        content = result["choices"][0]["message"]["content"]
        if isinstance(content, list):
            content = "".join(part.get("text", "") for part in content)
        if not isinstance(content, str) or not content.strip():
            raise ValueError("provider returned an empty answer")
        return content.strip().encode("ascii", "replace").decode("ascii")


def main() -> None:
    if not API_KEY:
        print("OPENAI_API_KEY is not set. The proxy will start, but chat requests will return 503.")
    print(f"ODES AI proxy listening on http://{HOST}:{PORT}")
    print(f"Model: {MODEL}; API: {API_BASE}")
    print("Keep this service on a trusted LAN; do not expose port 8765 to the Internet.")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()


if __name__ == "__main__":
    main()
