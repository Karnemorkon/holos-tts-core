#!/usr/bin/env python3
"""Генерує golden-фікстури для C++ тестів voice encoder (клонування голосу).

Запуск (потрібні onnxruntime + numpy; напр. контейнер holos-hybrid):
  python tools/gen_voice_encoder_golden.py \\
      --onnx /tmp/ve-export/voice_encoder_int8.onnx \\
      --golden tests/voice_encoder_golden.txt \\
      --npy tests/voice_npy_fixture.npy

Сигнал мусить збігатися з tests/test_voice_encoder.cpp:
  x[i] = 0.35*sin(2*pi*173*i/24000)
       + 0.2*sin(2*pi*431*i/24000)*(0.5+0.5*sin(2*pi*2.7*i/24000))
"""
import argparse

import numpy as np
import onnxruntime as ort

SR = 24000
DURATION_SEC = 3.0


def make_signal() -> np.ndarray:
    n = int(SR * DURATION_SEC)
    i = np.arange(n, dtype=np.float64)
    x = (
        0.35 * np.sin(2 * np.pi * 173.0 * i / SR)
        + 0.2 * np.sin(2 * np.pi * 431.0 * i / SR)
        * (0.5 + 0.5 * np.sin(2 * np.pi * 2.7 * i / SR))
    )
    return x.astype(np.float32)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--onnx", required=True, help="voice_encoder(.int8).onnx")
    ap.add_argument("--golden", default="tests/voice_encoder_golden.txt")
    ap.add_argument("--npy", default="tests/voice_npy_fixture.npy")
    args = ap.parse_args()

    sess = ort.InferenceSession(
        args.onnx, ort.SessionOptions(), providers=["CPUExecutionProvider"]
    )
    inp = sess.get_inputs()[0]
    print("onnx input:", inp.name, inp.shape, inp.type)

    wave = make_signal().reshape(1, -1)
    out = sess.run(None, {inp.name: wave})[0].reshape(-1)
    assert out.shape == (256,), out.shape

    with open(args.golden, "w", encoding="utf-8") as f:
        for v in out:
            f.write("%.9g\n" % float(v))

    fixture = np.arange(256, dtype=np.float32) / np.float32(256) - np.float32(0.5)
    np.save(args.npy, fixture)

    print("golden:", args.golden, "->", out.shape)
    print("npy fixture:", args.npy, fixture.shape, fixture.dtype)


if __name__ == "__main__":
    main()
