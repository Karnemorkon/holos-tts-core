#!/usr/bin/env python3
"""Переконвертувати M2M100-вербалізатор у stored-int8 CT2 (економія RAM/диску).

Без зміни якості: поточний сервіс уже використовує compute_type=int8 (динамічна
квантизація при завантаженні). Збережений int8 дає той самий числовий шлях, але:
  - диск:    1.94 ГБ (fp32)  -> ~470 МБ (int8)
  - RAM:     ~1980 МБ        -> ~940 МБ   (перевірено, вихід ідентичний)

Використання (у контейнері з ctranslate2 + transformers + torch):
    python tools/convert_verbalizer_int8.py [OUTPUT_DIR]

Потім увімкніть папку як модель вербалізатора (див. app/verbalizer.py MODEL_PATH).
"""
import os
import subprocess
import sys

SRC_REPO = "skypro1111/m2m100-ukr-verbalization"  # оригінальна HF (safetensors)
COPY_FILES = [
    "vocab.json",
    "special_tokens_map.json",
    "added_tokens.json",
    "sentencepiece.bpe.model",
]


def main() -> int:
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "/models/ct2-int8-verbalizer"
    if os.path.exists(out_dir):
        print(f"output dir exists, use --force semantics: {out_dir}", file=sys.stderr)
        return 1
    cmd = [
        "ct2-transformers-converter",
        "--model", SRC_REPO,
        "--output_dir", out_dir,
        "--quantization", "int8",
        "--copy_files", *COPY_FILES,
    ]
    print("running:", " ".join(cmd))
    subprocess.run(cmd, check=True)
    size = os.path.getsize(os.path.join(out_dir, "model.bin"))
    print(f"done. model.bin = {size / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
