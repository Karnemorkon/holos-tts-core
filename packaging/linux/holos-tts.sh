#!/usr/bin/env bash
# Portable launcher for holos-tts-core (no Docker).
# Models are expected in ./models (run download-models.sh once).
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export HOLOS_MODELS_DIR="${HOLOS_MODELS_DIR:-$DIR/models}"
export HOLOS_VERBALIZER_DIR="${HOLOS_VERBALIZER_DIR:-$HOLOS_MODELS_DIR/verbalizer/gemma-int8}"
export HOLOS_SPEAKERS_DIR="${HOLOS_SPEAKERS_DIR:-$DIR/speakers}"
export HOLOS_VOICE_ENCODER="${HOLOS_VOICE_ENCODER:-$HOLOS_MODELS_DIR/voice_encoder.onnx}"
export HOLOS_HOST="${HOLOS_HOST:-127.0.0.1}"
export HOLOS_PORT="${HOLOS_PORT:-8004}"
export LD_LIBRARY_PATH="$DIR/lib:${LD_LIBRARY_PATH:-}"

if [ ! -f "$HOLOS_MODELS_DIR/holos_cpu_int8.onnx" ]; then
    echo "Моделі не знайдено у $HOLOS_MODELS_DIR" >&2
    echo "Запустіть один раз: $DIR/download-models.sh" >&2
    exit 1
fi

mkdir -p "$HOLOS_SPEAKERS_DIR"
URL="http://$HOLOS_HOST:$HOLOS_PORT/"

if [ "${HOLOS_OPEN_BROWSER:-1}" != "0" ]; then
    (
        for _ in $(seq 1 120); do
            if command -v curl >/dev/null 2>&1 &&
               curl -sf --max-time 2 "$URL/healthz" >/dev/null 2>&1; then
                break
            fi
            sleep 1
        done
        if command -v xdg-open >/dev/null 2>&1; then
            xdg-open "$URL" >/dev/null 2>&1 || true
        fi
    ) &
fi

echo "HolosTTS: $URL  (Ctrl+C — зупинити)"
exec "$DIR/bin/holos-server"
