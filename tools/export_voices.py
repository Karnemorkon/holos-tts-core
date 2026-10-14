"""Export voices.pt -> voices.json (C++ core loads JSON, no torch).

voices.pt is a dict {speaker_name: float32[256]} (HolosTTS.encode voice embedding).
Run inside the hybrid container (torch + HF cache available):

    python tools/export_voices.py /app/models/voices.json
"""
import json
import sys

import torch
from huggingface_hub import hf_hub_download

MODEL_REPO = "patriotyk/HolosTTS"


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "/app/models/voices.json"
    pt = hf_hub_download(repo_id=MODEL_REPO, filename="voices.pt")
    voices = torch.load(pt, map_location="cpu", weights_only=True)
    out = {name: [float(x) for x in emb.reshape(-1).tolist()] for name, emb in voices.items()}
    for name, vec in out.items():
        assert len(vec) == 256, f"{name}: expected 256 dims, got {len(vec)}"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False)
    print(f"exported {len(out)} voices -> {out_path}")


if __name__ == "__main__":
    main()
