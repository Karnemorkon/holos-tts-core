#!/usr/bin/env python3
"""Export the official HolosTTS VoiceEncoder to ONNX (voice cloning).

The upstream encoder is a PyTorch module (holos.models.VoiceEncoder). This tool
exports a single-graph wrapper that takes a mono 24 kHz waveform and returns a
256-d voice embedding. Two upstream constructs cannot be exported as-is and are
replaced with numerically identical alternatives:

  * torchaudio MelSpectrogram calls torch.stft(return_complex=True), which the
    opset-17 exporter rejects. The mel front-end is rebuilt from the module's
    own window/filterbanks using return_complex=False.
  * torchaudio Conformer builds a padding mask with int(max(lengths).item());
    that bakes the frame count into the graph. The server always encodes one
    full utterance (batch=1, lengths == full sequence), where the original mask
    is all ones, so the mask path is dropped.

Usage (inside the hybrid image, which has torch/torchaudio/holos):
  python3 tools/export_voice_encoder.py \
      --weights /models/tts/voice_encoder.pt \
      --out /models/tts/voice_encoder.onnx \
      --quantize-out /models/tts/voice_encoder_int8.onnx \
      --samples /tmp/clone-samples

Requires: torch, torchaudio, holos, onnx, onnxruntime, soundfile, librosa,
numpy.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import sys
import time

import numpy as np
import torch
import torch.nn.functional as F


def load_encoder(weights: str):
    from holos.models import VoiceEncoder

    enc = VoiceEncoder(weights_path=weights, device="cpu")
    enc.eval()
    return enc


def patch_sdpa_for_export() -> None:
    """Lower scaled_dot_product_attention to matmul/softmax for the exporter.

    The legacy opset-17 exporter has no symbolic for aten::scaled_dot_product_
    attention. The conformer only ever calls it with attn_mask=None, dropout=0,
    is_causal=False, so the math equivalent is exact.
    """
    orig = torch.nn.functional.scaled_dot_product_attention

    def sdpa(
        query,
        key,
        value,
        attn_mask=None,
        dropout_p=0.0,
        is_causal=False,
        scale=None,
        enable_gqa=False,
    ):
        if attn_mask is not None or is_causal or enable_gqa or dropout_p:
            return orig(
                query,
                key,
                value,
                attn_mask=attn_mask,
                dropout_p=dropout_p,
                is_causal=is_causal,
                scale=scale,
                enable_gqa=enable_gqa,
            )
        factor = scale if scale is not None else 1.0 / (query.size(-1) ** 0.5)
        attn = torch.matmul(query * factor, key.transpose(-2, -1))
        return torch.matmul(torch.softmax(attn, dim=-1), value)

    torch.nn.functional.scaled_dot_product_attention = sdpa


class MelExport(torch.nn.Module):
    """MelSpectrogram replica that survives ONNX export.

    Mirrors torchaudio's Spectrogram: reflect-pad the signal by n_fft//2, run a
    centerless STFT with the module's own window, take the power spectrum, then
    contract with the HTK filterbank. Only the complex-vs-real output form
    differs from upstream, which is exactly the exporter limitation.
    """

    def __init__(self, mel):
        super().__init__()
        sp = mel.spectrogram
        self.register_buffer("holos_mel_window", sp.window.detach().clone())
        self.register_buffer(
            "holos_mel_fb", mel.mel_scale.fb.detach().clone()
        )  # [n_freq, n_mels]
        self.n_fft = int(sp.n_fft)
        self.hop = int(sp.hop_length)
        self.win = int(sp.win_length)
        self.pad = self.n_fft // 2
        self.pad_mode = str(sp.pad_mode)
        self.power = float(sp.power)
        self.normalized = bool(sp.normalized)

    def forward(self, wave):
        x = F.pad(wave, (self.pad, self.pad), mode=self.pad_mode)
        spec = torch.stft(
            x,
            n_fft=self.n_fft,
            hop_length=self.hop,
            win_length=self.win,
            window=self.holos_mel_window,
            center=False,
            normalized=self.normalized,
            onesided=True,
            return_complex=False,
        )  # [B, n_fft//2+1, T, 2]
        power = spec.pow(2).sum(dim=-1)
        if self.power != 2.0:
            power = power.pow(self.power / 2.0)
        return torch.matmul(self.holos_mel_fb.transpose(0, 1), power)


class VoiceEncoderExport(torch.nn.Module):
    """wave [1, N] @ 24 kHz -> voice [1, 256]."""

    def __init__(self, enc, max_sec: float = 30.0):
        super().__init__()
        self.to_mel = MelExport(enc.to_mel)
        self.style_encoder = enc.style_encoder
        self.prosodic_style_encoder = enc.prosodic_style_encoder
        self.crop_len = (
            3300 if max_sec is None else min(int(max_sec * 24000 // 300), 3300)
        )

    @staticmethod
    def _encode(enc, mel):
        x = enc.mel_proj(mel)
        x = x.transpose(-1, -2)
        x = enc.proj_norm(x)

        x = x.transpose(0, 1)
        for layer in enc.conformer_pre.conformer_layers:
            x = layer(x, None)
        for layer in enc.conformer_body.conformer_layers:
            x = layer(x, None)
        x = x.transpose(0, 1)

        w = enc.pooling.attn(x)
        w = torch.softmax(w, dim=1)
        x = (x * w).sum(dim=1)
        return enc.out(x)

    def forward(self, wave):
        mel = self.to_mel(wave)  # [1, 80, T]
        mel = (torch.log(1e-5 + mel) + 4.0) / 4.0
        frames = mel.shape[-1] - mel.shape[-1] % 2  # upstream even trim
        mel = mel[:, :, :frames]

        crop = torch.sym_min(frames, self.crop_len)
        acoustic = self._encode(self.style_encoder, mel)
        prosodic = self._encode(
            self.prosodic_style_encoder, mel[:, :, :crop]
        )
        return torch.cat([acoustic, prosodic], dim=-1)


def read_wave_24k(path: str) -> np.ndarray:
    import librosa
    import soundfile as sf

    wave, sr = sf.read(path)
    if wave.ndim > 1:
        wave = wave[:, 0].squeeze()
    wave = np.asarray(wave, dtype=np.float32)
    if sr != 24000:
        wave = librosa.resample(wave, orig_sr=sr, target_sr=24000)
    return np.asarray(wave, dtype=np.float32)


def cosine(a: np.ndarray, b: np.ndarray) -> float:
    a = a.reshape(-1).astype(np.float64)
    b = b.reshape(-1).astype(np.float64)
    denom = np.linalg.norm(a) * np.linalg.norm(b)
    return float(np.dot(a, b) / denom) if denom else float("nan")


def check_mel(enc, wrapper, samples: list[str]) -> float:
    """Custom mel front-end vs upstream MelSpectrogram (pre-log)."""
    worst = 0.0
    for path in samples[:3]:
        wave = torch.from_numpy(read_wave_24k(path))[None, :]
        with torch.inference_mode():
            ref = enc.to_mel(wave)
            got = wrapper.to_mel(wave)
        diff = float((ref - got).abs().max())
        rel = diff / max(float(ref.abs().max()), 1e-9)
        worst = max(worst, rel)
        print(f"[mel] {os.path.basename(path)}: max_abs={diff:.3e} rel={rel:.3e}")
    return worst


def export_fp32(wrapper, out: str, opset: int, dynamo: bool) -> None:
    example = torch.zeros(1, 24000 * 5, dtype=torch.float32)
    with torch.inference_mode():
        torch.onnx.export(
            wrapper,
            (example,),
            out,
            input_names=["wave"],
            output_names=["voice"],
            dynamic_axes={"wave": {0: "batch", 1: "samples"}},
            dynamic_shapes={"wave": {1: "samples"}},
            opset_version=opset,
            do_constant_folding=True,
            dynamo=dynamo,
        )
    inline_external_data(out)
    print(f"[export] fp32 -> {out} ({os.path.getsize(out) / 1e6:.1f} MB)")


def inline_external_data(path: str) -> None:
    """Fold a `.onnx.data` sidecar produced by the dynamo exporter back in.

    The C++ server (and HF uploader) expects one self-contained file; 192 MB is
    well below the protobuf inline limit.
    """
    import onnx

    data_file = path + ".data"
    if not os.path.exists(data_file):
        return
    model = onnx.load(path)
    onnx.save_model(model, path, save_as_external_data=False)
    os.remove(data_file)
    print(f"[export] inlined external data from {data_file}")


def validate(enc, wrapper, onnx_path: str, samples: list[str], label: str) -> dict:
    import onnxruntime as ort

    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    rows = []
    for path in samples:
        wave = read_wave_24k(path)
        with torch.inference_mode():
            ref = enc(path).numpy()  # upstream end-to-end reference
            self_ = wrapper(torch.from_numpy(wave)[None, :]).numpy()
        got = sess.run(["voice"], {"wave": wave[None, :]})[0]
        rows.append(
            {
                "file": os.path.basename(path),
                "sec": round(len(wave) / 24000, 2),
                "cos": cosine(ref, got),
                "cos_vs_wrapper": cosine(self_, got),
                "max_abs": float(np.max(np.abs(ref - got))),
            }
        )
    cos = [r["cos"] for r in rows]
    print(
        f"[validate:{label}] n={len(rows)} cos min={min(cos):.6f} "
        f"mean={sum(cos) / len(cos):.6f}"
    )
    for r in rows:
        print(
            f"  {r['file']:<24} {r['sec']:6.2f}s  cos={r['cos']:.6f} "
            f"graph={r['cos_vs_wrapper']:.6f} max_abs={r['max_abs']:.2e}"
        )
    return {"rows": rows, "cos_min": min(cos), "cos_mean": sum(cos) / len(cos)}


def quantize_int8(src: str, dst: str) -> None:
    import onnx
    from onnxruntime.quantization import QuantType, quantize_dynamic

    model = onnx.load(src)
    exclude = []
    for node in model.graph.node:
        if node.op_type in ("MatMul", "Gemm") and any(
            "holos_mel_fb" in name for name in node.input[1:]
        ):
            exclude.append(node.name)
    if exclude:
        print(f"[quant] excluding mel filterbank matmul: {exclude}")

    # Dynamo emits value_info annotations that onnx strict shape inference
    # rejects (a stale dim on one reshape); drop them so quantization can run.
    del model.graph.value_info[:]
    tmp = src + ".clean.onnx"
    onnx.save(model, tmp)
    try:
        quantize_dynamic(
            tmp,
            dst,
            weight_type=QuantType.QInt8,
            per_channel=True,
            op_types_to_quantize=["MatMul", "Gemm"],
            nodes_to_exclude=exclude,
            extra_options={"MatMulConstBOnly": True},
        )
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
    inline_external_data(dst)
    print(f"[quant] int8 -> {dst} ({os.path.getsize(dst) / 1e6:.1f} MB)")


def latency(onnx_path: str, seconds: list[int], runs: int = 3) -> dict:
    import onnxruntime as ort

    sess = ort.InferenceSession(onnx_path, providers=["CPUExecutionProvider"])
    out = {}
    for sec in seconds:
        wave = np.random.RandomState(0).randn(1, 24000 * sec).astype(np.float32)
        sess.run(None, {"wave": wave})  # warmup
        times = []
        for _ in range(runs):
            t0 = time.perf_counter()
            sess.run(None, {"wave": wave})
            times.append((time.perf_counter() - t0) * 1000)
        out[sec] = round(float(np.median(times)), 1)
        print(f"[latency] {onnx_path}: {sec}s audio -> {out[sec]} ms")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--weights", required=True)
    ap.add_argument("--out", required=True, help="fp32 ONNX output")
    ap.add_argument("--quantize-out", default="", help="optional int8 ONNX output")
    ap.add_argument("--samples", default="", help="directory with WAV samples")
    ap.add_argument("--opset", type=int, default=18)
    ap.add_argument("--no-dynamo", action="store_true")
    ap.add_argument("--max-sec", type=float, default=30.0)
    ap.add_argument("--report", default="", help="optional JSON report path")
    args = ap.parse_args()

    patch_sdpa_for_export()
    enc = load_encoder(args.weights)
    wrapper = VoiceEncoderExport(enc, max_sec=args.max_sec).eval()

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    samples = (
        sorted(glob.glob(os.path.join(args.samples, "*.wav")))
        if args.samples
        else []
    )
    report = {"fp32_size": 0, "samples": samples, "crop_len": wrapper.crop_len}
    if samples:
        report["mel_rel_err"] = check_mel(enc, wrapper, samples)

    export_fp32(wrapper, args.out, args.opset, dynamo=not args.no_dynamo)
    report["fp32_size"] = os.path.getsize(args.out)
    if samples:
        report["fp32"] = validate(enc, wrapper, args.out, samples, "fp32")
    report["fp32_latency_ms"] = latency(args.out, [5, 15, 30])

    if args.quantize_out:
        quantize_int8(args.out, args.quantize_out)
        report["int8_size"] = os.path.getsize(args.quantize_out)
        if samples:
            report["int8"] = validate(
                enc, wrapper, args.quantize_out, samples, "int8"
            )
        report["int8_latency_ms"] = latency(args.quantize_out, [5, 15, 30])

    if args.report:
        with open(args.report, "w", encoding="utf-8") as f:
            json.dump(report, f, ensure_ascii=False, indent=2)
        print(f"[report] {args.report}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
