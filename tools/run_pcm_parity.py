import sys
import numpy as np

sys.path.insert(0, '/app')
from app.tts_pipeline import _generate_part, _resolve_voice

# Читає фонемні частини (одна на рядок), генерує аудіо через Python ORT,
# дампить float32. Порівнюється з C++ dump (парність phoneme->audio).
phon_file = sys.argv[1]
f32_file = sys.argv[2]

parts = [l.rstrip('\n') for l in open(phon_file, encoding='utf-8') if l.strip()]
voice_arr = _resolve_voice("Speaker_0")
chunks = [_generate_part(p, voice_arr, 1.0) for p in parts]
audio = np.concatenate(chunks) if chunks else np.zeros(0, dtype=np.float32)
audio.astype(np.float32).tofile(f32_file)
print("py parts=%d samples=%d" % (len(parts), audio.shape[0]), file=sys.stderr)
