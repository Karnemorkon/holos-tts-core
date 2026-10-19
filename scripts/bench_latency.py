#!/usr/bin/env python3
"""TTFB/RTF бенчмарк для holos-tts-core.

Вимірює (клієнтські) TTFB/тотальний час/RTF для трьох шляхів:
  - POST /api/synthesize          (non-stream; бере серверні timings з JSON)
  - POST /v1/audio/speech stream  (TTFB = перший байт PCM16)
  - WS   /api/ws/synthesize       (TTFB = перший бінарний фрейм)

Сервер мусить бути запущений з HOLOS_METRICS=1, щоб у відповідях були
timings; без нього скрипт все одно міряє клієнтські метрики.

Приклад:
  python3 scripts/bench_latency.py --url http://127.0.0.1:8005 --runs 3
  python3 scripts/bench_latency.py --cold-container holos-core-bench
"""
import argparse
import asyncio
import json
import re
import statistics
import subprocess
import time
import urllib.request

TEXTS = [
    ("short", "Привіт, як справи?"),
    ("medium", "Сьогодні гарна погода, тому ми вирішили прогулятися парком і послухати спів птахів."),
    ("digits", "Зустріч призначена на 12 березня 2026 року о 15:30, квиток коштує 250 гривень."),
    ("long", "Українська мова має багату історію. Перші писемні пам'ятки датуються "
             "одинадцятим століттям. Сучасна літературна мова формувалася протягом століть, "
             "увібравши в себе народні говірки та запозичення з інших мов. Сьогодні нею "
             "розмовляють мільйони людей у всьому світі, а 2026 року вона впевнено звучить "
             "у цифрових сервісах, зокрема у синтезі мовлення."),
]
MODES = ["rule", "neural"]
SAMPLE_RATE = 24000


def post_json(url, payload, timeout=300):
    data = json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    return urllib.request.urlopen(req, timeout=timeout)


def measure_nonstream(base, text, mode):
    t0 = time.perf_counter()
    with post_json(base + "/api/synthesize", {
        "text": text, "voice": "Speaker_0", "auto_verbalize": mode,
    }) as r:
        body = json.loads(r.read())
    total_ms = (time.perf_counter() - t0) * 1000.0
    audio_s = body["duration_sec"]
    return {
        "ttfb_ms": None,
        "total_ms": total_ms,
        "audio_s": audio_s,
        "rtf": total_ms / 1000.0 / audio_s if audio_s else None,
        "timings": body.get("timings"),
    }


def measure_stream(base, text, mode):
    t0 = time.perf_counter()
    with post_json(base + "/v1/audio/speech", {
        "input": text, "voice": "Speaker_0", "response_format": "pcm",
        "stream": True, "auto_verbalize": mode,
    }) as r:
        first = r.read(1)
        ttfb_ms = (time.perf_counter() - t0) * 1000.0
        nbytes = len(first)
        while True:
            chunk = r.read(65536)
            if not chunk:
                break
            nbytes += len(chunk)
    total_ms = (time.perf_counter() - t0) * 1000.0
    audio_s = nbytes / 2 / SAMPLE_RATE
    return {
        "ttfb_ms": ttfb_ms,
        "total_ms": total_ms,
        "audio_s": audio_s,
        "rtf": total_ms / 1000.0 / audio_s if audio_s else None,
        "timings": None,
    }


async def ws_once(ws_url, text, mode):
    import websockets  # noqa: WPS433 (опційна залежність)
    t0 = time.perf_counter()
    async with websockets.connect(ws_url + "/api/ws/synthesize", max_size=None) as ws:
        await ws.send(json.dumps({
            "text": text, "voice": "Speaker_0", "auto_verbalize": mode,
        }))
        ttfb_ms = None
        nbytes = 0
        while True:
            msg = await ws.recv()
            if isinstance(msg, (bytes, bytearray)):
                if ttfb_ms is None:
                    ttfb_ms = (time.perf_counter() - t0) * 1000.0
                nbytes += len(msg)
            else:
                body = json.loads(msg)
                if body.get("error"):
                    raise RuntimeError(body["error"])
                total_ms = (time.perf_counter() - t0) * 1000.0
                audio_s = body.get("duration_sec") or (nbytes / 2 / SAMPLE_RATE)
                return {
                    "ttfb_ms": ttfb_ms,
                    "total_ms": total_ms,
                    "audio_s": audio_s,
                    "rtf": total_ms / 1000.0 / audio_s if audio_s else None,
                    "timings": body.get("timings"),
                }


def measure_ws(base, text, mode):
    ws_url = base.replace("https://", "wss://").replace("http://", "ws://")
    return asyncio.run(ws_once(ws_url, text, mode))


def poll_health(base, timeout=120.0):
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < timeout:
        try:
            with urllib.request.urlopen(base + "/healthz", timeout=2) as r:
                if r.status == 200:
                    return (time.perf_counter() - t0) * 1000.0
        except Exception:
            time.sleep(0.01)
    raise TimeoutError("server did not become healthy")


def startup_logs(container):
    try:
        out = subprocess.run(
            ["docker", "logs", container], capture_output=True, text=True, timeout=30
        )
        text = (out.stdout or "") + (out.stderr or "")
        return [
            int(m) for m in re.findall(r"\[startup\].*\+(\d+)ms", text)
        ]
    except Exception:
        return []


def med(runs, key):
    vals = [r[key] for r in runs if r.get(key) is not None]
    return statistics.median(vals) if vals else None


def fmt(v, digits=0):
    if v is None:
        return "—"
    return f"{v:.{digits}f}"


def run_group(base, path_name, fn, text_id, text, mode, runs, warmup):
    results = []
    for _ in range(warmup):
        fn(base, text, mode)
    for _ in range(runs):
        results.append(fn(base, text, mode))
    timings = {}
    for r in results:
        t = r.get("timings")
        if t:
            for key in ("vb_ms", "phon_ms", "ort_ms", "parts"):
                timings.setdefault(key, []).append(t.get(key))
    return {
        "path": path_name,
        "text": text_id,
        "mode": mode,
        "runs": results,
        "ttfb_ms": med(results, "ttfb_ms"),
        "total_ms": med(results, "total_ms"),
        "audio_s": med(results, "audio_s"),
        "rtf": med(results, "rtf"),
        "vb_ms": statistics.median(timings["vb_ms"]) if timings.get("vb_ms") else None,
        "phon_ms": statistics.median(timings["phon_ms"]) if timings.get("phon_ms") else None,
        "ort_ms": statistics.median(timings["ort_ms"]) if timings.get("ort_ms") else None,
        "parts": int(statistics.median(timings["parts"])) if timings.get("parts") else None,
    }


def print_table(rows, cold_ms=None, cold_logs=None):
    if cold_ms is not None:
        print(f"Cold start (restart -> healthz 200): {cold_ms:.0f} ms")
    if cold_logs:
        print("Startup phases: " + ", ".join(f"+{v}ms" for v in cold_logs))
    print()
    print("| path | text | mode | parts | TTFB ms | total ms | audio s | RTF | vb ms | phon ms | ort ms |")
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    for r in rows:
        print(
            "| {path} | {text} | {mode} | {parts} | {ttfb} | {total} | {audio} | {rtf} | "
            "{vb} | {phon} | {ort} |".format(
                path=r["path"], text=r["text"], mode=r["mode"],
                parts=r["parts"] if r["parts"] is not None else "—",
                ttfb=fmt(r["ttfb_ms"]),
                total=fmt(r["total_ms"]),
                audio=fmt(r["audio_s"], 2),
                rtf=fmt(r["rtf"], 3),
                vb=fmt(r["vb_ms"]), phon=fmt(r["phon_ms"]), ort=fmt(r["ort_ms"]),
            )
        )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--url", default="http://127.0.0.1:8004")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--cold-container", default=None,
                    help="docker-контейнер: перезапустити і заміряти cold start")
    ap.add_argument("--json-out", default=None)
    ap.add_argument("--paths", default="nonstream,stream,ws")
    args = ap.parse_args()
    base = args.url.rstrip("/")
    paths = [p.strip() for p in args.paths.split(",") if p.strip()]
    bench_fns = {
        "nonstream": ("/api/synthesize", measure_nonstream),
        "stream": ("/v1/audio/speech (stream)", measure_stream),
        "ws": ("/api/ws/synthesize", measure_ws),
    }

    cold_ms = None
    cold_logs = []
    if args.cold_container:
        print(f"Cold start: docker restart {args.cold_container} ...", flush=True)
        subprocess.run(["docker", "restart", args.cold_container],
                       check=True, capture_output=True)
        cold_ms = poll_health(base)
        cold_logs = startup_logs(args.cold_container)

    # Перший запит після старту (ORT warmup) — окремо.
    first = measure_nonstream(base, TEXTS[0][1], "rule")
    print(f"First request after start (warmup): {first['total_ms']:.0f} ms")

    rows = []
    for path_name in paths:
        label, fn = bench_fns[path_name]
        for text_id, text in TEXTS:
            for mode in MODES:
                print(f"  {label} | {text_id} | {mode} ...", flush=True)
                rows.append(run_group(base, label, fn, text_id, text, mode,
                                      args.runs, args.warmup))
    print()
    print_table(rows, cold_ms, cold_logs)

    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            json.dump({
                "cold_start_ms": cold_ms,
                "startup_phases_ms": cold_logs,
                "first_request_ms": first["total_ms"],
                "rows": rows,
            }, f, ensure_ascii=False, indent=2)
        print(f"\nJSON: {args.json_out}")


if __name__ == "__main__":
    main()
