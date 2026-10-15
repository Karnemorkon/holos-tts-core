#!/usr/bin/env python3
"""Download HolosTTS native models from HF repo Morkon/holos-native-models.

Lays files out to match holos-server defaults (HOLOS_MODELS_DIR layout):
  models/stress.trie
  models/vocab.json
  models/voices.json
  models/holos_cpu_int8.onnx
  models/accentor-lite.onnx
  models/verbalizer/onnx-int8/...

Usage:
  python3 scripts/download_models.py [--out DIR] [--repo REPO] [--patterns P ...]

Requires: pip install huggingface_hub
Public repo — no token needed.
"""

from __future__ import annotations

import argparse
import os
import sys

DEFAULT_REPO = "Morkon/holos-native-models"
DEFAULT_OUT = "models"

# repo_path -> local_path (relative to --out)
FLAT_MAP = {
    "stress/stress.trie": "stress.trie",
    "tts/vocab.json": "vocab.json",
    "tts/voices.json": "voices.json",
    "tts/holos_cpu_int8.onnx": "holos_cpu_int8.onnx",
    "accentor/accentor-lite.onnx": "accentor-lite.onnx",
}

# Keep directory structure
TREE_PREFIXES = {
    "verbalizer/onnx-int8": "verbalizer/onnx-int8",
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--token", default=os.environ.get("HF_TOKEN", ""))
    args = ap.parse_args()

    try:
        from huggingface_hub import snapshot_download
    except ImportError:
        print("ERROR: huggingface_hub not installed. Run: pip install huggingface_hub", file=sys.stderr)
        return 1

    os.makedirs(args.out, exist_ok=True)

    print(f"Downloading {args.repo} -> {args.out} ...")
    cache_dir = os.path.join(args.out, ".hf_cache")
    snapshot_download(
        args.repo,
        local_dir=cache_dir,
        token=args.token or None,
        allow_patterns=[
            "stress/stress.trie",
            "tts/vocab.json",
            "tts/voices.json",
            "tts/holos_cpu_int8.onnx",
            "accentor/accentor-lite.onnx",
            "verbalizer/onnx-int8/*",
        ],
    )

    def move(src_rel: str, dst_rel: str) -> None:
        src = os.path.join(cache_dir, src_rel)
        dst = os.path.join(args.out, dst_rel)
        if not os.path.exists(src):
            print(f"  MISSING in repo: {src_rel}", file=sys.stderr)
            return
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        if os.path.exists(dst):
            os.remove(dst)
        os.replace(src, dst)
        size_mb = os.path.getsize(dst) / 1e6
        print(f"  {dst_rel}  ({size_mb:.1f} MB)")

    for src_rel, dst_rel in FLAT_MAP.items():
        move(src_rel, dst_rel)

    for src_prefix, dst_prefix in TREE_PREFIXES.items():
        src_root = os.path.join(cache_dir, src_prefix)
        if not os.path.isdir(src_root):
            print(f"  MISSING dir in repo: {src_prefix}/", file=sys.stderr)
            continue
        for root, _dirs, files in os.walk(src_root):
            for f in files:
                if f.endswith(".lock"):
                    continue
                src_abs = os.path.join(root, f)
                rel = os.path.relpath(src_abs, cache_dir)
                dst_abs = os.path.join(args.out, dst_prefix, os.path.relpath(src_abs, src_root))
                os.makedirs(os.path.dirname(dst_abs), exist_ok=True)
                if os.path.exists(dst_abs):
                    os.remove(dst_abs)
                os.replace(src_abs, dst_abs)
                print(f"  {os.path.relpath(dst_abs, args.out)}  ({os.path.getsize(dst_abs)/1e6:.1f} MB)")

    import shutil
    shutil.rmtree(cache_dir, ignore_errors=True)

    print("\nDone. Launch server with:")
    print(f"  HOLOS_MODELS_DIR=$PWD/{args.out} HOLOS_VERBALIZER_DIR=$PWD/{args.out}/verbalizer/onnx-int8 \\")
    print("  holos-server")
    return 0


if __name__ == "__main__":
    sys.exit(main())
