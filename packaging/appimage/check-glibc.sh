#!/usr/bin/env bash
set -euo pipefail

APPDIR="${1:?usage: check-glibc.sh <AppDir> [max-version]}"
MAX="${2:-2.35}"

command -v file >/dev/null 2>&1 || { echo "file not found" >&2; exit 1; }
command -v objdump >/dev/null 2>&1 || { echo "objdump not found" >&2; exit 1; }

fail=0
max_seen="0"

while IFS= read -r -d '' f; do
    file "$f" | grep -q "ELF" || continue
    versions="$(objdump -T "$f" 2>/dev/null |
        grep -o 'GLIBC_[0-9][0-9.]*' |
        sed 's/^GLIBC_//' |
        sort -V -u || true)"
    [ -n "$versions" ] || continue

    top="$(printf '%s\n' "$versions" | tail -n 1)"
    max_seen="$(printf '%s\n' "$max_seen" "$top" | sort -V | tail -n 1)"

    if [ "$(printf '%s\n' "$MAX" "$top" | sort -V | tail -n 1)" != "$MAX" ]; then
        echo "GLIBC_$top required by $f (allowed $MAX)" >&2
        fail=1
    fi
done < <(find "$APPDIR" -type f \( -perm -111 -o -name '*.so*' \) -print0)

if [ "$fail" -ne 0 ]; then
    echo "glibc check failed (max allowed $MAX)" >&2
    exit 1
fi

echo "glibc OK: max GLIBC_$max_seen (allowed $MAX)"
