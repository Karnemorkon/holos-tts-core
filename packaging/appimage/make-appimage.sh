#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"

BUNDLE="${1:?usage: make-appimage.sh <bundle-dir> <models-dir> [output]}"
MODELS="${2:?usage: make-appimage.sh <bundle-dir> <models-dir> [output]}"
VERSION="${VERSION:-$("$BUNDLE/bin/holos-cli" version)}"
OUT="${3:-$REPO_ROOT/dist/holos-tts-core-${VERSION}-x86_64.AppImage}"
APPDIR="${APPDIR:-$REPO_ROOT/build/appimage/AppDir}"
APPIMAGETOOL="${APPIMAGETOOL:-$REPO_ROOT/build/appimage/appimagetool-x86_64.AppImage}"
APPIMAGETOOL_URL="${APPIMAGETOOL_URL:-https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage}"

for exe in holos-server holos-cli; do
    [ -x "$BUNDLE/bin/$exe" ] || { echo "missing $BUNDLE/bin/$exe" >&2; exit 1; }
done

required=(
    holos_cpu_int8.onnx
    voice_encoder.onnx
    stress.trie
    vocab.json
    voices.json
    accentor-lite.onnx
    verbalizer/gemma-int8/model.onnx
    verbalizer/gemma-int8/tokenizer.model
    verbalizer/gemma-int8/config.json
    verbalizer/gemma-int8/GEMMA-NOTICE.txt
)
for f in "${required[@]}"; do
    [ -s "$MODELS/$f" ] || { echo "missing model: $MODELS/$f" >&2; exit 1; }
done

[ -f "$REPO_ROOT/LICENSE" ] || { echo "missing $REPO_ROOT/LICENSE" >&2; exit 1; }

rm -rf "$APPDIR"
mkdir -p \
    "$APPDIR/usr/bin" \
    "$APPDIR/usr/lib" \
    "$APPDIR/usr/share/holos-tts/models" \
    "$APPDIR/usr/share/applications" \
    "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
    "$APPDIR/usr/share/doc/holos-tts"

cp -a "$BUNDLE/bin/." "$APPDIR/usr/bin/"
cp -a "$BUNDLE/lib/." "$APPDIR/usr/lib/"
cp -a "$MODELS/." "$APPDIR/usr/share/holos-tts/models/"
find "$APPDIR/usr/share/holos-tts/models" -type f -name '*.part' -delete

cp "$HERE/AppRun" "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"
cp "$HERE/holos-tts.desktop" "$APPDIR/holos-tts.desktop"
cp "$HERE/holos-tts.desktop" "$APPDIR/usr/share/applications/holos-tts.desktop"
cp "$HERE/holos-tts.png" "$APPDIR/holos-tts.png"
cp "$HERE/holos-tts.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/holos-tts.png"
ln -sf holos-tts.png "$APPDIR/.DirIcon"

cp "$REPO_ROOT/LICENSE" "$APPDIR/usr/share/doc/holos-tts/LICENSE"
cp "$MODELS/verbalizer/gemma-int8/GEMMA-NOTICE.txt" "$APPDIR/usr/share/doc/holos-tts/GEMMA-NOTICE.txt"

if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$APPDIR/holos-tts.desktop"
fi

"$HERE/check-glibc.sh" "$APPDIR" 2.35

if [ ! -x "$APPIMAGETOOL" ]; then
    mkdir -p "$(dirname "$APPIMAGETOOL")"
    echo "downloading appimagetool 1.9.1..."
    curl -fL --retry 3 --retry-delay 2 -o "$APPIMAGETOOL" "$APPIMAGETOOL_URL"
    chmod +x "$APPIMAGETOOL"
fi

mkdir -p "$(dirname "$OUT")"
rm -f "$OUT"
ARCH=x86_64 VERSION="$VERSION" APPIMAGE_EXTRACT_AND_RUN=1 \
    "$APPIMAGETOOL" --no-appstream "$APPDIR" "$OUT"

echo
echo "AppImage: $OUT"
du -h "$OUT"
sha256sum "$OUT"
