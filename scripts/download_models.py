#!/usr/bin/env python3
"""Download HolosTTS native models from the public HF repo.

Default repo: Morkon/holos-native-models.
Default format is the multi-stage `holos-onnx` bundle:
  models/holos-onnx/{holos_onnx.json,encoder,prosody,decoder,decoder_window,source,voices.npz}
  models/stress.trie
  models/accentor-lite.onnx
  models/voice_encoder.onnx
  models/verbalizer/gemma-int8/...

Legacy single-graph layout (`--format legacy`):
  models/holos_cpu_int8.onnx
  models/vocab.json
  models/voices.json
  + stress.trie / accentor-lite.onnx / voice_encoder.onnx / verbalizer/...

Only files missing locally are fetched (adding a new model to the repo does
not re-download the rest); --force re-downloads everything. The default bundle
includes the Gemma-3-270M verbalizer, so a clean install starts with
`verbalizer: "neural+rule"`.

Verification is enabled by default: the bundle `SHA256SUMS` is checked, and the
voice catalog is validated (29 unique names, 256 finite float32 values each) in
both `voices.npz` and legacy `voices.json`. Use --no-verify to skip, or
--verify-only to check an existing directory without downloading anything.

Usage:
  python3 scripts/download_models.py [--out DIR] [--repo REPO]
      [--format legacy|bundle] [--force] [--with-m2m100]
      [--verify | --no-verify] [--verify-only]

Requires: pip install huggingface_hub (not needed for --verify-only).
Public repo — no token needed.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import json
import math
import os
import struct
import sys
import tempfile
import zipfile

EXPECTED_VOICES = 29
VOICE_DIM = 256

# Legacy format: repo_path -> local_path (relative to --out)
FLAT_MAP = {
    "stress/stress.trie": "stress.trie",
    "tts/vocab.json": "vocab.json",
    "tts/voices.json": "voices.json",
    "tts/holos_cpu_int8.onnx": "holos_cpu_int8.onnx",
    "accentor/accentor-lite.onnx": "accentor-lite.onnx",
    "tts/voice_encoder.onnx": "voice_encoder.onnx",
}

# Keep directory structure: repo_prefix -> local_prefix
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

# Bundle stress & accentor (same as legacy)
BUNDLE_FLAT_MAP_EXTRA = {
    "stress/stress.trie": "stress.trie",
    "accentor/accentor-lite.onnx": "accentor-lite.onnx",
    "tts/voice_encoder.onnx": "voice_encoder.onnx",
}


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_npy(path: str):
    """Minimal .npy reader (stdlib only). Returns (dtype, shape, data bytes)."""
    with open(path, "rb") as f:
        magic = f.read(6)
        if magic != b"\x93NUMPY":
            raise ValueError(f"{path}: not a .npy file")
        version = f.read(2)
        if len(version) != 2:
            raise ValueError(f"{path}: truncated .npy version")
        major = version[0]
        if major == 1:
            hlen = struct.unpack("<H", f.read(2))[0]
        elif major in (2, 3):
            hlen = struct.unpack("<I", f.read(4))[0]
        else:
            raise ValueError(f"{path}: unsupported .npy version {major}")
        header = f.read(hlen)
        try:
            meta = ast.literal_eval(header.decode("latin1"))
        except (SyntaxError, ValueError) as exc:
            raise ValueError(f"{path}: invalid .npy header: {exc}") from exc
        data = f.read()
    return meta.get("descr"), meta.get("shape"), data


def parse_unicode_npy(path: str, expected_shape: tuple[int, ...]) -> list[str]:
    dtype, shape, data = read_npy(path)
    if not isinstance(dtype, str) or not dtype.startswith(("<U", "|U")):
        raise ValueError(f"{path}: expected unicode dtype, got {dtype!r}")
    if tuple(shape) != expected_shape:
        raise ValueError(f"{path}: expected shape {expected_shape}, got {shape}")
    maxlen = int(dtype[2:])
    count = expected_shape[0]
    expected_bytes = count * maxlen * 4
    if len(data) != expected_bytes:
        raise ValueError(f"{path}: expected {expected_bytes} data bytes, got {len(data)}")
    out = []
    for i in range(count):
        chunk = data[i * maxlen * 4:(i + 1) * maxlen * 4]
        codepoints = struct.unpack(f"<{maxlen}I", chunk)
        out.append("".join(chr(cp) for cp in codepoints).split("\x00", 1)[0])
    return out


def parse_f32_npy(path: str, expected_shape: tuple[int, int]) -> list[list[float]]:
    dtype, shape, data = read_npy(path)
    if dtype not in ("<f4", "|f4"):
        raise ValueError(f"{path}: expected float32 dtype, got {dtype!r}")
    if tuple(shape) != expected_shape:
        raise ValueError(f"{path}: expected shape {expected_shape}, got {shape}")
    rows, cols = expected_shape
    expected_bytes = rows * cols * 4
    if len(data) != expected_bytes:
        raise ValueError(f"{path}: expected {expected_bytes} data bytes, got {len(data)}")
    flat = struct.unpack(f"<{rows * cols}f", data)
    values = [list(flat[i * cols:(i + 1) * cols]) for i in range(rows)]
    for r, row in enumerate(values):
        for v in row:
            if not math.isfinite(v):
                raise ValueError(f"{path}: non-finite value at row {r}")
    return values


def verify_voices_npz(path: str) -> None:
    if not os.path.isfile(path):
        raise FileNotFoundError(f"missing voices.npz: {path}")
    with zipfile.ZipFile(path) as zf, tempfile.TemporaryDirectory() as tmp_dir:
        names = set(zf.namelist())
        if names != {"names.npy", "values.npy"}:
            raise ValueError(
                f"{path}: expected exactly names.npy and values.npy, got {sorted(names)}"
            )
        zf.extract("names.npy", tmp_dir)
        zf.extract("values.npy", tmp_dir)
        voice_names = parse_unicode_npy(os.path.join(tmp_dir, "names.npy"),
                                        (EXPECTED_VOICES,))
        parse_f32_npy(os.path.join(tmp_dir, "values.npy"),
                      (EXPECTED_VOICES, VOICE_DIM))
    if len(set(voice_names)) != len(voice_names):
        raise ValueError(f"{path}: duplicate voice names")
    print(f"  OK {path}: {EXPECTED_VOICES} voices x {VOICE_DIM} float32")


def verify_voices_json(path: str) -> None:
    if not os.path.isfile(path):
        raise FileNotFoundError(f"missing voices.json: {path}")
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError(f"{path}: expected a JSON object")
    if len(data) != EXPECTED_VOICES:
        raise ValueError(f"{path}: expected {EXPECTED_VOICES} voices, got {len(data)}")
    for name, arr in data.items():
        if not isinstance(arr, list) or len(arr) != VOICE_DIM:
            raise ValueError(f"{path}: voice {name!r} must have {VOICE_DIM} floats")
        for v in arr:
            if not isinstance(v, (int, float)) or not math.isfinite(v):
                raise ValueError(f"{path}: voice {name!r} has a non-finite value")
    print(f"  OK {path}: {EXPECTED_VOICES} voices x {VOICE_DIM} floats")


def verify_sha256sums(manifest: str, base_dir: str) -> None:
    if not os.path.isfile(manifest):
        raise FileNotFoundError(f"missing SHA256SUMS: {manifest}")
    checked = 0
    with open(manifest, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 2:
                raise ValueError(f"{manifest}:{lineno}: expected '<sha256>  <file>'")
            digest, name = parts
            path = os.path.join(base_dir, name)
            if not os.path.isfile(path):
                raise FileNotFoundError(f"{manifest}: missing listed file {name}")
            actual = sha256_file(path)
            if actual != digest.lower():
                raise ValueError(
                    f"{path}: SHA256 mismatch (expected {digest}, got {actual})"
                )
            checked += 1
    if checked == 0:
        raise ValueError(f"{manifest}: no checksums found")
    print(f"  OK {manifest}: {checked} file(s)")


def verify_layout(out: str, fmt: str) -> None:
    print("Verifying model files ...")
    if fmt == "bundle":
        verify_sha256sums(os.path.join(out, "holos-onnx", "SHA256SUMS"),
                          os.path.join(out, "holos-onnx"))
        verify_voices_npz(os.path.join(out, "holos-onnx", "voices.npz"))
    voices_json = os.path.join(out, "voices.json")
    if os.path.isfile(voices_json):
        verify_voices_json(voices_json)


def local_relpath(src_rel: str, flat_map: dict[str, str],
                  tree_prefixes: dict[str, str]) -> str | None:
    if src_rel in flat_map:
        return flat_map[src_rel]
    for src_prefix, dst_prefix in tree_prefixes.items():
        if src_rel.startswith(src_prefix + "/"):
            return os.path.join(dst_prefix, src_rel[len(src_prefix) + 1:])
    return None


def tree_remote_files(remote_files: list[str], prefix: str) -> list[str]:
    return [f for f in remote_files if f.startswith(prefix + "/")]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="models")
    ap.add_argument("--repo", default="Morkon/holos-native-models")
    ap.add_argument("--token", default=os.environ.get("HF_TOKEN", ""))
    ap.add_argument("--force", action="store_true",
                    help="re-download even if models are already present")
    ap.add_argument("--with-m2m100", action="store_true",
                    help="also download legacy M2M100 verbalizer (verbalizer/onnx-int8)")
    ap.add_argument("--format", choices=["legacy", "bundle"], default="bundle",
                    help="model format to download: legacy (single ONNX) or bundle (holos-onnx)")
    ap.add_argument("--verify", dest="verify", action="store_true", default=True,
                    help="verify SHA256SUMS and voice catalog (default)")
    ap.add_argument("--no-verify", dest="verify", action="store_false",
                    help="skip verification")
    ap.add_argument("--verify-only", action="store_true",
                    help="only verify an existing model directory; do not download")
    args = ap.parse_args()

    if args.verify_only and not args.verify:
        print("ERROR: --verify-only conflicts with --no-verify", file=sys.stderr)
        return 1

    os.makedirs(args.out, exist_ok=True)

    if args.format == "legacy":
        flat_map = dict(FLAT_MAP)
        tree_prefixes = dict(TREE_PREFIXES)
        format_name = "legacy"
    else:  # bundle
        flat_map = {**BUNDLE_FLAT_MAP, **BUNDLE_FLAT_MAP_EXTRA}
        tree_prefixes = dict(TREE_PREFIXES)
        format_name = "bundle"

    if args.with_m2m100:
        tree_prefixes.update(M2M100_PREFIX)
        flat_map.pop("verbalizer/onnx-int8", None)

    if args.verify_only:
        try:
            verify_layout(args.out, args.format)
        except Exception as exc:
            print(f"ERROR: verification failed: {exc}", file=sys.stderr)
            return 1
        print("Verification passed.")
        return 0

    try:
        from huggingface_hub import list_repo_files, snapshot_download
    except ImportError:
        print("ERROR: huggingface_hub not installed. Run: pip install huggingface_hub",
              file=sys.stderr)
        return 1

    token = args.token or None
    try:
        remote_files = list_repo_files(args.repo, token=token)
    except Exception as exc:
        print(f"ERROR: cannot list {args.repo}: {exc}", file=sys.stderr)
        return 1

    missing_flat: list[str] = []
    repo_missing: list[str] = []
    for src_rel, dst_rel in flat_map.items():
        if src_rel not in remote_files:
            repo_missing.append(src_rel)
            print(f"  MISSING in repo: {src_rel}", file=sys.stderr)
            continue
        dst = os.path.join(args.out, dst_rel)
        if args.force or not (os.path.isfile(dst) and os.path.getsize(dst) > 0):
            missing_flat.append(src_rel)

    missing_tree: list[str] = []
    tree_missing_files: list[str] = []
    for src_prefix, dst_prefix in tree_prefixes.items():
        files = tree_remote_files(remote_files, src_prefix)
        if not files:
            repo_missing.append(src_prefix + "/")
            print(f"  MISSING tree in repo: {src_prefix}", file=sys.stderr)
            continue
        need = False
        for src_rel in files:
            dst_rel = os.path.join(dst_prefix, src_rel[len(src_prefix) + 1:])
            dst = os.path.join(args.out, dst_rel)
            if args.force or not (os.path.isfile(dst) and os.path.getsize(dst) > 0):
                need = True
                tree_missing_files.append(src_rel)
        if need:
            missing_tree.append(src_prefix + "/*")

    if repo_missing:
        print(f"ERROR: {args.repo} is missing required paths: {', '.join(repo_missing)}",
              file=sys.stderr)
        return 1

    if not missing_flat and not missing_tree:
        print(f"Models ({format_name}) already present in {args.out} — skip download (--force to redo).")
        if args.verify:
            try:
                verify_layout(args.out, args.format)
            except Exception as exc:
                print(f"ERROR: verification failed: {exc}", file=sys.stderr)
                return 1
        return 0

    allow_patterns = missing_flat + missing_tree
    total = len(missing_flat) + len(tree_missing_files)
    print(f"Downloading {total} missing file(s) from {args.repo} "
          f"({format_name}) -> {args.out} ...")
    cache_dir = os.path.join(args.out, ".hf_cache")
    try:
        snapshot_download(
            repo_id=args.repo,
            local_dir=cache_dir,
            token=token,
            allow_patterns=allow_patterns,
        )
    except Exception as exc:
        print(f"ERROR: download failed: {exc}", file=sys.stderr)
        return 1

    import shutil

    def move(src_rel: str) -> None:
        dst_rel = local_relpath(src_rel, flat_map, tree_prefixes)
        if dst_rel is None:
            print(f"  SKIP unexpected file: {src_rel}", file=sys.stderr)
            return
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

    for src_rel in missing_flat + tree_missing_files:
        if not src_rel.endswith(".lock"):
            move(src_rel)

    shutil.rmtree(cache_dir, ignore_errors=True)

    if args.verify:
        try:
            verify_layout(args.out, args.format)
        except Exception as exc:
            print(f"ERROR: verification failed: {exc}", file=sys.stderr)
            return 1

    out_abs = os.path.abspath(args.out)
    print("\nDone. Launch server with:")
    print(f"  HOLOS_MODELS_DIR={out_abs} \\")
    if args.format == "bundle":
        print(f"  HOLOS_ONNX={os.path.join(out_abs, 'holos-onnx')} \\")
    print(f"  HOLOS_VERBALIZER_DIR={os.path.join(out_abs, 'verbalizer', 'gemma-int8')} \\")
    print("  holos-server")
    if args.with_m2m100:
        print(f"\nLegacy M2M100: HOLOS_VERBALIZER_DIR={os.path.join(out_abs, 'verbalizer', 'onnx-int8')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
