#!/usr/bin/env bash
# Download the default HolosTTS model set (~0.6 GB) from Hugging Face.
# No Python required; only curl. Already present files are skipped.
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${HOLOS_MODELS_DIR:-$DIR/models}"
REPO="${HOLOS_HF_REPO:-Morkon/holos-native-models}"
BASE="https://huggingface.co/$REPO/resolve/main"

FILES=(
    "stress/stress.trie stress.trie"
    "tts/vocab.json vocab.json"
    "tts/voices.json voices.json"
    "tts/holos_cpu_int8.onnx holos_cpu_int8.onnx"
    "tts/voice_encoder.onnx voice_encoder.onnx"
    "accentor/accentor-lite.onnx accentor-lite.onnx"
    "verbalizer/gemma-int8/model.onnx verbalizer/gemma-int8/model.onnx"
    "verbalizer/gemma-int8/tokenizer.model verbalizer/gemma-int8/tokenizer.model"
    "verbalizer/gemma-int8/config.json verbalizer/gemma-int8/config.json"
    "verbalizer/gemma-int8/GEMMA-NOTICE.txt verbalizer/gemma-int8/GEMMA-NOTICE.txt"
)

command -v curl >/dev/null 2>&1 || {
    echo "Потрібен curl. Встановіть його (apt install curl) і повторіть." >&2
    exit 1
}

mkdir -p "$OUT"

for entry in "${FILES[@]}"; do
    src="${entry%% *}"
    dst="${entry#* }"
    out="$OUT/$dst"

    if [ -s "$out" ]; then
        echo "  skip  $dst"
        continue
    fi

    mkdir -p "$(dirname "$out")"
    echo "  fetch $dst"
    curl -fL --retry 3 --retry-delay 2 -o "$out.part" "$BASE/$src"
    mv "$out.part" "$out"
done

echo
echo "Готово. Запуск: $DIR/holos-tts.sh"
