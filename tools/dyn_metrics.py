"""Dynamics metrics on several time scales (voiced frames only) + sibilant balance; used to calibrate v1.2."""
import numpy as np, importlib.util, os
from scipy import signal
from scipy.ndimage import uniform_filter1d
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sa", os.path.join(here, "suno_analyze.py")); sa = importlib.util.module_from_spec(spec); spec.loader.exec_module(sa)
SR = sa.SR

def spreads(x):
    m = x.mean(1); out = {}
    for ms in (50, 100, 400):
        w = int(ms / 1000 * SR); b = 2400
        bl = 10 * np.log10(np.maximum(uniform_filter1d(m ** 2, b)[b // 2::b], 0) + 1e-20)      # 50 ms blocks
        top = np.percentile(bl, 95); k = max(1, ms // 50); vals = []
        for i in range(0, len(bl) - k, k):
            seg = bl[i:i + k]
            if (seg > top - 15).all(): vals.append(10 * np.log10(np.mean(10 ** (seg / 10))))
        vals = np.array(vals)
        out[ms] = float(np.percentile(vals, 90) - np.percentile(vals, 10)) if len(vals) > 10 else np.nan
    return out

def sibilance(x, thr=-6.0, minlen=6, segs=None, return_segs=False):
    m = x.mean(1); h = 480
    hf = signal.sosfilt(signal.butter(3, 4000, 'highpass', fs=SR, output='sos'), m)
    E = lambda v: 10 * np.log10(np.maximum(uniform_filter1d(v ** 2, h)[::h], 0) + 1e-20)
    full, hfe = E(m), E(hf); ratio = hfe - full; top = np.percentile(full, 95)
    s = (ratio > thr) & (full > top - 30)
    find = segs is None
    if find: segs = []
    i = 0
    while find and i < len(s):
        if s[i]:
            j = i
            while j < len(s) and s[j]: j += 1
            if j - i >= minlen: segs.append((i, j))
            i = j
        else: i += 1
    v = np.array([np.max(full[i:j]) - np.median(full[max(0, i - 25):i - 3]) for i, j in segs if i > 28])
    v = v[np.isfinite(v) & (np.abs(v) < 60)]
    res = (float(np.median(v)), float(np.percentile(v, 90)), len(v)) if len(v) else (np.nan, np.nan, 0)
    return (res, segs) if return_segs else res

def onset_prom(x):
    M = x.mean(1); h = 96
    fr = np.lib.stride_tricks.sliding_window_view(M, h * 2)[::h]; e = 10 * np.log10((fr ** 2).mean(1) + 1e-20); top = np.percentile(e, 95)
    pr = []; i = 25
    while i < len(e) - 80:
        if e[i] > top - 25 and e[i] - e[i - 10] > 8:
            pr.append(e[i:i + 15].max() - np.median(e[i + 25:i + 75])); i += 40
        else: i += 1
    pr = np.array(pr); return (float(np.median(pr)), float(np.percentile(pr, 90))) if len(pr) else (np.nan, np.nan)

def report(label, x, segs=None):
    sp = spreads(x); sb = sibilance(x, segs=segs); op = onset_prom(x); d = sa.Measure(x).dynamics()
    print(f"{label:30s} spread 50/100/400ms {sp[50]:4.1f}/{sp[100]:4.1f}/{sp[400]:4.1f} | crest {d['crest_db']:4.1f} | onset med/p90 {op[0]:+4.1f}/{op[1]:+4.1f} | sib vs vowel med/p90 {sb[0]:+5.1f}/{sb[1]:+5.1f} (n={sb[2]})")
    return dict(sp=sp, sib=sb, onset=op, crest=d['crest_db'])
