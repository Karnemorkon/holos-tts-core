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
  python3 scripts/download_models.py [--out DIR] [--repo REPO] [--format legacy|bundle]

Requires: pip install huggingface_hub
Public repo — no token needed.
"""

from __future__ import annotations

import argparse
import os
import sys

# Legacy format: repo_path -> local_path (relative to --out)
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

# Bundle format (holos-onnx-int8): repo_path -> local_path
BUNDLE_FLAT_MAP = {
    "holos-onnx-int8/holos_onnx.json": "holos-onnx/holos_onnx.json",
    "holos-onnx-int8/encoder.onnx": "holos-onnx/encoder.onnx",
    "holos-onnx-int8/prosody.onnx": "holos-onnx/prosody.onnx",
    "holos-onnx-int8/decoder.onnx": "holos-onnx/decoder.onnx",
    "holos-onnx-int8/decoder_window.onnx": "holos-onnx/decoder_window.onnx",
    "holos-onnx-int8/source.onnx": "holos-onnx/source.onnx",
    "holos-onnx-int8/voices.npz": "holos-onnx/voices.npz",
    "holos-onnx-int8/SHA256SUMS": "holos-onnx/SHA256SUMS",
    "holos-onnx-int8/README.md": "holos-onnx/README.md",
}

# Bundle verbalizer (same as legacy)
BUNDLE_TREE_PREFIXES = {
    "verbalizer/gemma-int8": "verbalizer/gemma-int8",
}

# Bundle stress & accentor (same as legacy)
BUNDLE_FLAT_MAP_EXTRA = {
    "stress/stress.trie": "stress.trie",
    "accentor/accentor-lite.onnx": "accentor-lite.onnx",
    "tts/voice_encoder.onnx": "voice_encoder.onnx",
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default="models")
    ap.add_argument("--repo", default="Morkon/holos-native-models")
    ap.add_argument("--token", default=os.environ.get("HF_TOKEN", ""))
    ap.add_argument("--force", action="store_true",
                    help="re-download even if models are already present")
    ap.add_argument("--with-m2m100", action="store_true",
                    help="also download legacy M2M100 verbalizer (verbalizer/onnx-int8)")
    ap.add_argument("--format", choices=["legacy", "bundle"], default="legacy",
                    help="model format to download: legacy (single ONNX) or bundle (holos-onnx)")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)

    if args.format == "legacy":
        flat_map = {
            "stress/stress.trie": "stress.trie",
            "tts/vocab.json": "vocab.json",
            "tts/voices.json": "voices.json",
            "tts/holos_cpu_int8.onnx": "holos_cpu_int8.onnx",
            "accentor/accentor-lite.onnx": "accentor-lite.onnx",
            "tts/voice_encoder.onnx": "voice_encoder.onnx",
        }
        tree_prefixes = {"verbalizer/gemma-int8": "verbalizer/gemma-int8"}
        if args.with_m2m100:
            flat_map["verbalizer/onnx-int8"] = "verbalizer/onnx-int8"
        format_name = "legacy"
    else:  # bundle
        flat_map = {
            "holos-onnx-int8/holos_onnx.json": "holos-onnx/holos_onnx.json",
            "holos-onnx-int8/encoder.onnx": "holos-onnx/encoder.onnx",
            "holos-onnx-int8/prosody.onnx": "holos-onnx/prosody.onnx",
            "holos-onnx-int8/decoder.onnx": "holos-onnx/decoder.onnx",
            "holos-onnx-int8/decoder_window.onnx": "holos-onnx/decoder_window.onnx",
            "holos-onnx-int8/source.onnx": "holos-onnx/source.onnx",
            "holos-onnx-int8/voices.npz": "holos-onnx/voices.npz",
            "holos-onnx-int8/SHA256SUMS": "holos-onnx/SHA256SUMS",
            "holos-onnx-int8/README.md": "holos-onnx/README.md",
            "stress/stress.trie": "stress.trie",
            "accentor/accentor-lite.onnx": "accentor-lite.onnx",
            "tts/voice_encoder.onnx": "voice_encoder.onnx",
        }
        tree_prefixes = {"verbalizer/gemma-int8": "verbalizer/gemma-int8"}
        format_name = "bundle"

    try:
        from huggingface_hub import list_repo_files, snapshot_download
    except ImportError:
        print("ERROR: huggingface_hub not installed. Run: pip install huggingface_hub", file=sys.stderr)
        return 1

    def local_relpath(src_rel: str) -> str | None:
        if src_rel in flat_map:
            return flat_map[src_rel]
        for src_prefix, dst_prefix in tree_prefixes.items():
            if src_rel.startswith(src_prefix + "/"):
                return os.path.join(dst_prefix, src_rel[len(src_prefix) + 1:])
        return None

    remote_files = list_repo_files("Morkon/holos-native-models", token=os.environ.get("HF_TOKEN") or None)
    wanted: list[str] = []
    for src_rel in flat_map:
        if src_rel in remote_files:
            wanted.append(src_rel)
        else:
            print(f"  MISSING in repo: {src_rel}", file=sys.stderr)

    # Idempotent: only download missing files
    missing = []
    for src_rel in flat_map:
        dst = os.path.join(args.out, flat_map[src_rel])
        if args.force or not (os.path.isfile(dst) and os.path.getsize(dst) > 0):
            missing.append(src_rel)

    if not missing:
        print(f"Models ({format_name}) already present in {args.out} — skip download (--force to redo).")
        return 0

    print(f"Downloading {len(missing)} missing file(s) from Morkon/holos-native-models ({format_name}) -> {args.out} ...")
    cache_dir = os.path.join(args.out, ".hf_cache")
    snapshot_download(
        "Morkon/holos-native-models",
        local_dir=os.path.join(args.out, ".hf_cache"),
        token=os.environ.get("HF_TOKEN") or None,
        allow_patterns=missing,
    )

    import shutil

    def move(src_rel: str) -> None:
        if src_rel in flat_map:
            dst_rel = flat_map[src_rel]
        else:
            # verbalizer/gemma-int8/*
            dst_rel = os.path.join("verbalizer/gemma-int8", src_rel.split("/", 1)[1])
        src = os.path.join(args.out, ".hf_cache", src_rel)
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

    shutil.rmtree(os.path.join(args.out, ".hf_cache"), ignore_errors=True)

    print("\nDone. Launch server with:")
    if args.format == "legacy":
        print(f"  HOLOS_MODELS_DIR=$PWD/{args.out} HOLOS_VERBALIZER_DIR=$PWD/{args.out}/verbalizer/gemma-int8 \\")
        print("  holos-server")
    else:
        print(f"  HOLOS_MODELS_DIR=$PWD/{args.out} HOLOS_VERBALIZER_DIR=$PWD/{args.out}/verbalizer/gemma-int8 \\")
        print("  HOLOS_ONNX=$PWD/{args.out}/holos-onnx holos-server")
    return 0


if __name__ == "__main__":
    sys.exit(main())