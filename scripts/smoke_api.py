#!/usr/bin/env python3
"""Smoke test for a running HolosTTS server (healthz + synth + clone + delete).

Usage: python3 scripts/smoke_api.py [base_url] [expected_version]
"""
import base64
import io
import json
import math
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import wave

BASE = (sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:8004").rstrip("/")
EXPECT_VERSION = sys.argv[2] if len(sys.argv) > 2 else None
NAME = "Smoke Portable"


def request(path, payload=None, method=None, allow_404=False):
    data = None
    headers = {}
    if payload is not None:
        data = json.dumps(payload).encode()
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(BASE + path, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=180) as resp:
            raw = resp.read()
            ctype = resp.headers.get("Content-Type", "")
            if "json" in ctype:
                return json.loads(raw)
            return raw
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        if allow_404 and e.code == 404:
            return {"status": "not_found"}
        raise SystemExit(f"HTTP {e.code} on {method or 'GET'} {path}: {body}")


def wav_5s():
    buf = io.BytesIO()
    rate = 16000
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for i in range(rate * 5):
            frames += struct.pack("<h", int(12000 * math.sin(2 * math.pi * 220 * i / rate)))
        w.writeframes(bytes(frames))
    return buf.getvalue()


def wait_ready(timeout=300):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            return request("/healthz")
        except SystemExit:
            time.sleep(2)
        except Exception:
            time.sleep(2)
    raise SystemExit("server is not ready")


h = wait_ready()
assert h.get("status") == "ok", h
assert h.get("cloning") is True, f"cloning disabled: {h}"
if EXPECT_VERSION:
    assert h.get("version") == EXPECT_VERSION, f"version {h.get('version')} != {EXPECT_VERSION}"
print(f"healthz OK: version={h.get('version')} voices={h.get('voices')}")

synth = request("/api/synthesize", {"text": "Привіт, це тест портативної збірки."})
assert synth["duration_sec"] > 0.5, synth
assert len(base64.b64decode(synth["audio_base64"])) > 10000
print(f"synth OK: {synth['duration_sec']:.2f}s voice={synth['voice']}")

request("/api/voices/" + urllib.parse.quote(NAME), method="DELETE", allow_404=True)
saved = request("/api/voices", {
    "name": NAME,
    "audio_base64": base64.b64encode(wav_5s()).decode(),
})
assert saved.get("voice") == NAME or saved.get("name") == NAME, saved
print(f"clone OK: {NAME}")

synth2 = request("/api/synthesize", {"text": "Клонований голос працює.", "voice": NAME})
assert synth2["voice"] == NAME, synth2
assert synth2["duration_sec"] > 0.2
print(f"cloned synth OK: {synth2['duration_sec']:.2f}s")

request("/api/voices/" + urllib.parse.quote(NAME), method="DELETE", allow_404=True)
print("delete OK")
print("SMOKE PASSED")
