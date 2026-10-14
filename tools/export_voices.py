#!/usr/bin/env python3
"""Одноразовий експорт голосів із voices.pt (PyTorch) у voices.json для C++ ядра.

PyTorch-формат .pt не читається у C++, тому конвертуємо пресети ембеддінгів
[256] у простий JSON. Зберігається у volume моделей (HOLOS_MODELS_DIR).

Використання (у контейнері Python-сервісу):
    python3 tools/export_voices.py --pt /path/voices.pt --out /app/models/voices.json
"""
import argparse
import json
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--pt", required=True, help="шлях до voices.pt")
    ap.add_argument("--out", required=True, help="шлях вихідного voices.json")
    args = ap.parse_args()

    import torch

    voices = torch.load(args.pt, map_location="cpu", weights_only=True)
    out = {}
    for name, emb in voices.items():
        arr = emb.detach().cpu().float().reshape(-1).tolist()
        if len(arr) != 256:
            print(f"warn: {name} має {len(arr)} вимірів (очікувалось 256)", file=sys.stderr)
        out[name] = arr

    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False)
    print(f"експортовано {len(out)} голосів -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
