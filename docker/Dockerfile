# syntax=docker/dockerfile:1.6
# Multi-stage build for holos-tts-core (C++20, ONNX Runtime + CTranslate2 + Drogon).
# Builder compiles; runtime is a slim image with only the binary + shared libs.

ARG ORT_VERSION=1.31.0

# ---------------------------------------------------------------------------
# Stage 1: builder
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder
ARG ORT_VERSION
ARG TARGETARCH

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git ca-certificates curl \
        pkg-config zlib1g-dev libssl-dev uuid-dev libcares2-dev \
        libmp3lame-dev libvorbis-dev libogg-dev \
        libsentencepiece-dev \
        python3 \
    && rm -rf /var/lib/apt/lists/*

# ONNX Runtime prebuilt (x86_64 / aarch64) -> /opt/ort
RUN set -eux; \
    case "$TARGETARCH" in \
      amd64) ORT_ARCH="x64" ;; \
      arm64) ORT_ARCH="aarch64" ;; \
      *) echo "unsupported arch: $TARGETARCH" >&2; exit 1 ;; \
    esac; \
    curl -fsSL -o /tmp/ort.tgz \
      "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-linux-${ORT_ARCH}-${ORT_VERSION}.tgz"; \
    mkdir -p /opt/ort; tar -xzf /tmp/ort.tgz -C /opt/ort --strip-components=1; rm /tmp/ort.tgz

WORKDIR /src
COPY . .

# Build: full native core (ORT + server + neural verbalizer).
RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DHOLOS_FETCH_DEPS=ON \
        -DHOLOS_WITH_ORT=ON \
        -DHOLOS_WITH_CT2=OFF \
        -DHOLOS_WITH_SERVER=ON \
        -DHOLOS_WITH_VERBALIZER=ON \
        -DHOLOS_BUILD_TESTS=OFF \
        -DHOLOS_ORT_ROOT=/opt/ort \
    && cmake --build build --parallel \
    && cmake --install build --prefix /opt/holos

# ---------------------------------------------------------------------------
# Stage 2: runtime
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgomp1 libmp3lame0 libvorbis0a libvorbisenc2 libogg0 ca-certificates curl \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /opt/holos /opt/holos
COPY --from=builder /opt/ort/lib/libonnxruntime.so* /opt/holos/lib/

ENV PATH="/opt/holos/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/holos/lib:${LD_LIBRARY_PATH}"

# Models are mounted at runtime (volume holos_models_cache -> /app/models).
# HOLOS_VERBALIZER_DIR: neural verbalizer (M2M100 ONNX int8); empty -> rule-only.
ENV HOLOS_MODELS_DIR=/app/models
ENV HOLOS_VERBALIZER_DIR=/app/models/verbalizer/onnx-int8
ENV HOLOS_PORT=8004
EXPOSE 8004

ENTRYPOINT ["holos-server"]
