#!/usr/bin/env python3
"""Download HolosTTS native models from HF repo Morkon/holos-native-models.

Lays files out to match holos-server defaults (HOLOS_MODELS_DIR layout):
  models/stress.trie
  models/vocab.json
  models/voices.json
  models/holos_cpu_int8.onnx
  models/accentor-lite.onnx
  models/voice_encoder.onnx
  models/verbalizer/gemma-int8/...

Only files missing locally are fetched (adding a new model to the repo does
not re-download the rest); --force re-downloads everything.

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
    "tts/voice_encoder.onnx": "voice_encoder.onnx",
}

# Keep directory structure
TREE_PREFIXES = {
    "verbalizer/gemma-int8": "verbalizer/gemma-int8",
}

# Legacy backend (rollback / A-B): --with-m2m100
M2M100_PREFIX = {"verbalizer/onnx-int8": "verbalizer/onnx-int8"}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--token", default=os.environ.get("HF_TOKEN", ""))
    ap.add_argument("--force", action="store_true",
                    help="re-download even if models are already present")
    ap.add_argument("--with-m2m100", action="store_true",
                    help="also download legacy M2M100 verbalizer (verbalizer/onnx-int8)")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    tree_prefixes = dict(TREE_PREFIXES)
    if args.with_m2m100:
        tree_prefixes.update(M2M100_PREFIX)

    try:
        from huggingface_hub import list_repo_files, snapshot_download
    except ImportError:
        print("ERROR: huggingface_hub not installed. Run: pip install huggingface_hub", file=sys.stderr)
        return 1

    def local_relpath(src_rel: str) -> str | None:
        if src_rel in FLAT_MAP:
            return FLAT_MAP[src_rel]
        for src_prefix, dst_prefix in tree_prefixes.items():
            if src_rel.startswith(src_prefix + "/"):
                return os.path.join(dst_prefix, src_rel[len(src_prefix) + 1:])
        return None

    remote_files = list_repo_files(args.repo, token=args.token or None)
    wanted: list[str] = []
    for src_rel in FLAT_MAP:
        if src_rel in remote_files:
            wanted.append(src_rel)
        else:
            print(f"  MISSING in repo: {src_rel}", file=sys.stderr)
    for prefix in tree_prefixes:
        wanted += sorted(f for f in remote_files if f.startswith(prefix + "/"))

    # Ідемпотентність (compose auto-init): тягнемо лише те, чого немає локально.
    missing = []
    for src_rel in wanted:
        dst = os.path.join(args.out, local_relpath(src_rel))
        if args.force or not (os.path.isfile(dst) and os.path.getsize(dst) > 0):
            missing.append(src_rel)

    if not missing:
        print(f"Models already present in {args.out} — skip download (--force to redo).")
        return 0

    print(f"Downloading {len(missing)} missing file(s) from {args.repo} -> {args.out} ...")
    cache_dir = os.path.join(args.out, ".hf_cache")
    snapshot_download(
        args.repo,
        local_dir=cache_dir,
        token=args.token or None,
        allow_patterns=missing,
    )

    import shutil

    def move(src_rel: str) -> None:
        dst_rel = local_relpath(src_rel)
        src = os.path.join(cache_dir, src_rel)
        dst = os.path.join(args.out, dst_rel)
        if not os.path.exists(src):
            print(f"  MISSING after download: {src_rel}", file=sys.stderr)
            return
        os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
        if os.path.exists(dst):
            os.remove(dst)
        os.replace(src, dst)
        size_mb = os.path.getsize(dst) / 1e6
        print(f"  {dst_rel}  ({size_mb:.1f} MB)")

    for src_rel in missing:
        if not src_rel.endswith(".lock"):
            move(src_rel)

    shutil.rmtree(cache_dir, ignore_errors=True)

    print("\nDone. Launch server with:")
    print(f"  HOLOS_MODELS_DIR=$PWD/{args.out} HOLOS_VERBALIZER_DIR=$PWD/{args.out}/verbalizer/gemma-int8 \\")
    print("  holos-server")
    return 0


if __name__ == "__main__":
    sys.exit(main())
