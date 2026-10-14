import sys
import numpy as np

a = np.fromfile(sys.argv[1], dtype=np.float32)
b = np.fromfile(sys.argv[2], dtype=np.float32)
n = min(len(a), len(b))
a, b = a[:n], b[:n]
if n == 0:
    print("EMPTY cpp=%d py=%d" % (len(a), len(b)))
    sys.exit(1)
diff = np.abs(a - b)
corr = float(np.corrcoef(a, b)[0, 1]) if n > 1 else float('nan')
print("cpp_samples=%d py_samples=%d n=%d maxdiff=%.6g meandiff=%.6g corr=%.8f"
      % (len(a), len(b), n, float(diff.max()), float(diff.mean()), corr))
