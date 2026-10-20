#!/usr/bin/env bash
# Assemble a relocatable Linux bundle from an installed prefix.
# Usage: [LD_LIBRARY_PATH=<extra lib dirs>] make-bundle.sh <install-prefix>
set -euo pipefail

ROOT="${1:?usage: make-bundle.sh <install-prefix>}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

for exe in holos-server holos-cli; do
    [ -x "$ROOT/bin/$exe" ] || { echo "missing $ROOT/bin/$exe" >&2; exit 1; }
done

cp "$HERE/holos-tts.sh" "$HERE/download-models.sh" "$HERE/README.txt" "$ROOT/"
chmod +x "$ROOT/holos-tts.sh" "$ROOT/download-models.sh"
mkdir -p "$ROOT/lib" "$ROOT/models" "$ROOT/speakers"

rm -rf "$ROOT/include" "$ROOT/cmake" "$ROOT/share" "$ROOT/lib/cmake" "$ROOT/lib/pkgconfig"
find "$ROOT/bin" -maxdepth 1 -type f ! -name holos-server ! -name holos-cli -delete
find "$ROOT/lib" -maxdepth 1 -type f -name '*.a' -delete
rm -f "$ROOT"/drogon.*.log

libs="$(ldd "$ROOT/bin/holos-server" "$ROOT/bin/holos-cli" 2>/dev/null |
        awk '/=> \// {print $3}' | sort -u)"

for lib in $libs; do
    base="$(basename "$lib")"
    case "$base" in
        libc.so.*|libm.so.*|libdl.so.*|librt.so.*|libpthread.so.*|\
        libresolv.so.*|libnsl.so.*|libutil.so.*|ld-linux*.so.*)
            continue ;;
    esac
    cp -L "$lib" "$ROOT/lib/$base"
done

for exe in holos-server holos-cli; do
    if LD_LIBRARY_PATH="$ROOT/lib" ldd "$ROOT/bin/$exe" 2>/dev/null | grep -q "not found"; then
        echo "unresolved libraries for $exe:" >&2
        LD_LIBRARY_PATH="$ROOT/lib" ldd "$ROOT/bin/$exe" | grep "not found" >&2
        exit 1
    fi
done

echo "bundle ready: $ROOT ($(du -sh "$ROOT" | cut -f1), models excluded)"
