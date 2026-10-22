#!/usr/bin/env python3
"""Suno Chain analyzer.

Measures a wet Suno vocal stem and writes a preset (.json) for the Suno Chain plugin.
Reverb/delay/ducking/width parameters are fitted in closed loop: a pseudo-dry version of the
stem (singing kept, gaps muted) is rendered through the plugin's own DSP core (render_cli), the
render is measured with exactly the same code, and parameters are corrected until the
measurements match the original.

usage: suno_analyze.py vocal.(wav|mp3) --out preset.json [--name NAME] [--renderer path] [--iters 6]
"""
import argparse, json, os, subprocess, sys, tempfile
import numpy as np
from scipy import signal
from scipy.ndimage import uniform_filter1d

SR = 48000
CENTERS = [50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500,
           3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000]
H5 = 240  # 5 ms hop


def load(path):
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-ac", "2", "-ar", str(SR), "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).astype(np.float64)


def db(v):
    return 20 * np.log10(np.maximum(np.abs(v), 1e-12))


def env5(v):
    fr = np.lib.stride_tricks.sliding_window_view(v, H5 * 2)[::H5]
    return uniform_filter1d(10 * np.log10((fr ** 2).mean(1) + 1e-20), 4)


def norm_curve(c):
    c = np.asarray(c, float)
    sel = [i for i, f in enumerate(CENTERS) if 250 <= f <= 4000]
    return c - c[sel].mean()


def band_curve(P, f):
    out = []
    for c in CENTERS:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        s = (f >= lo) & (f <= hi)
        out.append(10 * np.log10(P[s].mean() + 1e-30) if s.any() else -200)
    return np.array(out)


# ----------------------------------------------------------------------------------------------
SING_SMOOTH_MS = 120   # >0: fill singing gaps / drop singing islands shorter than this (stems where mid-side flickers at 12 dB)
SING_RANGE_DB = 30.0   # singing = within this of the loud parts; lowered for stems whose reverb tails are mostly mono
EXCLUDE = []   # (start_s, end_s) ranges left out of every measurement (backing vocals, FX, bleed): --exclude


def excl_mask(n, hop_s):
    """True for frames (hop_s apart) that fall in an excluded range."""
    m = np.zeros(n, bool)
    for a, b in EXCLUDE:
        m[max(0, int(a / hop_s)):max(0, min(n, int(np.ceil(b / hop_s))))] = True
    return m


def silence_excluded(x):
    """Copy with excluded ranges silenced (for the plugin's own analyzers in render_cli)."""
    if not EXCLUDE: return x
    y = x.copy()
    for a, b in EXCLUDE: y[int(a * SR):int(b * SR)] = 0
    return y


class Measure:
    """All measurements used both for the Suno stem and for renders (identical code path)."""

    def __init__(self, x):
        self.L, self.R = x[:, 0], x[:, 1]
        self.M = (self.L + self.R) / 2
        self.S = (self.L - self.R) / 2
        self.em, self.es = env5(self.M), env5(self.S)
        n = len(self.em)
        top = np.percentile(self.em, 95)
        self.top = top
        self.sing = ((self.em - self.es) > 12) & (self.em > top - SING_RANGE_DB)
        self.ex = excl_mask(n, H5 / SR)
        if SING_SMOOTH_MS > 0:
            w = max(1, int(SING_SMOOTH_MS / 5))
            def fill(mask, val):   # runs of (not val) shorter than w, between val runs, become val
                out = mask.copy(); i = 0
                while i < n:
                    if out[i] != val:
                        j = i
                        while j < n and out[j] != val: j += 1
                        if i > 0 and j < n and j - i < w: out[i:j] = val
                        i = j
                    else: i += 1
                return out
            self.sing = fill(fill(self.sing, True), False)
        self.sing &= ~self.ex
        # offsets: >=400 ms singing followed by >=150 ms non-singing
        self.offs, i = [], 0
        while i < n:
            if self.sing[i]:
                j = i
                while j < n and self.sing[j]: j += 1
                k = j
                while k < n and not self.sing[k]: k += 1
                if j - i >= 80 and k - j >= 30 and not self.ex[i:k].any(): self.offs.append((i, j, k))
                i = k
            else:
                i += 1
        # onsets after (near) silence: the only places where the wet arrival is visible,
        # because elsewhere the previous phrase's tail masks it
        self.ons = []
        for i in range(60, n - 120):
            if (self.em[i] - self.em[i - 6] > 12 and self.em[i] > top - 15
                    and self.em[i - 40:i - 6].max() < top - 20 and not self.ex[i - 40:i + 120].any()):
                if not self.ons or i - self.ons[-1] > 100: self.ons.append(i)

    # ---- spectrum / dynamics (dry-side measurements) ----
    def target_curve(self, voiced=False):
        """Long-term spectrum of the loud frames. voiced=True (v2.1 VOWEL EQ): only frames with >= 70 % of their
        energy under 1.5 kHz (sung vowels), the same rule as the plugin's Learner::frameVoiced."""
        nf = 4096
        frames = np.lib.stride_tricks.sliding_window_view(self.M, nf)[::nf]
        lv = 10 * np.log10((frames ** 2).mean(1) + 1e-20)
        ok = ~excl_mask(len(frames), nf / SR)
        sel = (lv > np.percentile(lv[ok], 95) - 25) & ok
        w = np.hanning(nf)
        f = np.fft.rfftfreq(nf, 1 / SR)
        Pf = np.abs(np.fft.rfft(frames[sel] * w, axis=1)) ** 2
        if voiced:
            B = np.array([[Pf[i][(f >= c / 2 ** (1 / 6)) & (f <= c * 2 ** (1 / 6))].mean() for c in CENTERS] for i in range(len(Pf))])
            wgt = np.array(CENTERS, float)
            share = (B[:, np.array(CENTERS) < 1500] * wgt[np.array(CENTERS) < 1500]).sum(1) / ((B * wgt).sum(1) + 1e-30)
            Pf = Pf[share >= 0.7]
        P = Pf.mean(0)
        return norm_curve(band_curve(P, f))

    def dynamics(self):
        w = int(0.05 * SR)
        fr = np.lib.stride_tricks.sliding_window_view(self.M, w)[::w]
        r = 10 * np.log10((fr ** 2).mean(1) + 1e-20)
        p = db(np.abs(fr).max(1))
        exd = excl_mask(len(r), 0.05)
        r = np.where(exd, -200.0, r)            # excluded blocks count as silence (never 'voiced')
        a = r > np.percentile(r[~exd], 95) - 20
        # phrase-level consistency: 400 ms windows whose 8 x 50 ms blocks are all voiced (> p95 - 15 dB)
        top = np.percentile(r[~exd], 95); l4 = []
        for i in range(0, len(r) - 8, 8):
            blk = r[i:i + 8]
            if (blk > top - 15).all(): l4.append(10 * np.log10(np.mean(10 ** (blk / 10))))
        l4 = np.array(l4) if len(l4) >= 5 else np.array([0.0, 0.0])
        return dict(crest_db=float(np.median(p[a] - r[a])),
                    level_dbfs=float(np.median(r[a])),
                    spread400_db=float(np.percentile(l4, 90) - np.percentile(l4, 10)))

    def deess(self, freq=5500.0):
        sos = signal.butter(2, freq, 'highpass', fs=SR, output='sos')
        hf = signal.sosfilt(sos, self.M)
        e = lambda v: uniform_filter1d(v ** 2, 240)[::H5]
        ratio = 10 * np.log10((e(hf) + 1e-20) / (e(self.M) + 1e-20))
        m = min(len(ratio), len(self.sing))
        return float(np.percentile(ratio[:m][self.sing[:m]], 90))

    # ---- wet-side measurements ----
    def wet_arrival_ms(self):
        if len(self.ons) < 1: return None
        A = np.array([self.es[o - 20:o + 120] for o in self.ons if o + 120 < len(self.es)])
        ma = np.median(A, 0)
        base, peak = np.median(ma[:20]), np.max(ma[40:120])
        half = base + 0.5 * (peak - base)
        idx = np.where(ma[20:] >= half)[0]
        return float(idx[0] * 5) if len(idx) else None

    def offset_stats(self):
        """Wet swell after the vocal stops. Short pauses (< 360 ms) and long pauses (>= 400 ms) are kept
        apart: in short pauses a slow duck release cannot fully open, so mixing them hides the depth."""
        rises, wets, rel, rs, rl, tpk = [], [], [], [], [], []
        for i, j, k in self.offs:
            pre_s = np.median(self.es[max(i, j - 20):j])
            post = self.es[j + 4:min(k, j + 160)]          # up to 800 ms: Suno's swell is slow
            if len(post) < 8: continue
            r = post.max() - pre_s
            rises.append(r)
            dry = np.median(self.em[max(i, j - 60):j])
            wets.append(post.max() - dry)
            tgt = pre_s + 0.9 * (post.max() - pre_s)
            t = np.where(post >= tgt)[0]
            rel.append((t[0] + 4) * 5 if len(t) else np.nan)
            gap = (k - j) * 5
            if gap < 360: rs.append(r)
            elif gap >= 400: rl.append(r); tpk.append((np.argmax(post) + 4) * 5)
        if not rises: return None
        return dict(rise_db=float(np.median(rises)), wet_re_dry_db=float(np.median(wets)),
                    rise_time_ms=float(np.nanmedian(rel)), n=len(rises),
                    rise_short_db=float(np.median(rs)) if rs else None, rise_long_db=float(np.median(rl)) if rl else None,
                    peak_time_long_ms=float(np.median(tpk)) if tpk else None, n_short=len(rs), n_long=len(rl))

    def early_side_db(self):
        """Side relative to Mid 20-150 ms after onsets that follow real silence (before the reverb arrives):
        how much width layer is present at the very start of a section."""
        if not self.ons: return None
        v = [np.median(self.es[o + 4:o + 30] - self.em[o + 4:o + 30]) for o in self.ons if o + 30 < len(self.es)]
        return float(np.median(v)) if v else None

    def singing_width_db(self):
        """Side relative to Mid while singing (reverb under the voice + width layer)."""
        return float(np.median((self.es - self.em)[self.sing])) if self.sing.sum() > 100 else None

    def coh_profile(self):
        """Dry-correlated side component (width layer): excess coherence between S and M over a
        time-shifted baseline, power-weighted. Returns broadband value, octave-band profile,
        K-ratio (coherence over 340 ms vs 43 ms windows; lower = more detune/modulation) and bloom
        ratio (first 100 ms of phrases vs later)."""
        N, hop = 1024, 512
        f, t, ZM = signal.stft(self.M, SR, nperseg=N, noverlap=N - hop, boundary=None, padded=False)
        _, _, ZS = signal.stft(self.S, SR, nperseg=N, noverlap=N - hop, boundary=None, padded=False)
        sing = np.array([self.sing[min(len(self.sing) - 1, int(tt / 0.005))] for tt in t])
        ZM0 = np.roll(ZM, 300, axis=1)
        def ex_map(K, lo, hi):
            sel = (f >= lo) & (f < hi)
            sm = lambda a: uniform_filter1d(a, K, axis=1)
            Syy = sm(np.abs(ZS[sel]) ** 2)
            c = np.abs(sm(ZS[sel] * np.conj(ZM[sel]))) ** 2 / (sm(np.abs(ZM[sel]) ** 2) * Syy + 1e-20)
            c0 = np.abs(sm(ZS[sel] * np.conj(ZM0[sel]))) ** 2 / (sm(np.abs(ZM0[sel]) ** 2) * Syy + 1e-20)
            w = np.abs(ZM[sel]) ** 2
            return ((c - c0) * w).sum(0) / (w.sum(0) + 1e-20)
        ex8 = ex_map(8, 200, 4000)
        out = dict(broad=float(np.median(ex8[sing])))
        out["bands"] = {str(c): float(np.median(ex_map(8, c / np.sqrt(2), c * np.sqrt(2))[sing])) for c in (250, 500, 1000, 2000, 4000)}
        k4, k32 = np.median(ex_map(4, 200, 4000)[sing]), np.median(ex_map(32, 200, 4000)[sing])
        out["k_ratio"] = float(k32 / k4) if k4 > 1e-4 else None
        # bloom: time since phrase start
        tss = np.full(len(t), np.nan); i = 0
        while i < len(t):
            if sing[i] and (i == 0 or not sing[i - 1]):
                j = i
                while j < len(t) and sing[j]: tss[j] = t[j] - t[i]; j += 1
                i = j
            else: i += 1
        early, late = ex8[(tss >= 0) & (tss < 0.1)], ex8[tss >= 0.2]
        out["bloom_ratio"] = float(np.median(early) / np.median(late)) if len(early) > 20 and np.median(late) > 1e-3 else None
        return out

    def band_width(self, mask_kind="sing"):
        """Side-minus-mid (dB) in three bands (<300, 300-4k, >4k; LR4 filters, as the plugin's band split and meter),
        on the singing frames or in the pauses. These are the bands of the multiband width controls."""
        if not hasattr(self, "_bands"):
            lr4 = lambda f, kind: np.vstack([signal.butter(2, f, kind, fs=SR, output='sos')] * 2)   # Linkwitz-Riley 4th order, as the plugin
            lp, hp, lp2, hp2 = lr4(300, 'lowpass'), lr4(300, 'highpass'), lr4(4000, 'lowpass'), lr4(4000, 'highpass')
            def split(v):
                h = signal.sosfilt(hp, v); return [signal.sosfilt(lp, v), signal.sosfilt(lp2, h), signal.sosfilt(hp2, h)]
            self._bands = (split(self.M), split(self.S))
        mk = self.sing if mask_kind == "sing" else self.gap_mask()
        if mk.sum() < 40: return None
        g = np.repeat(mk, H5)[:len(self.M)]; g = np.pad(g, (0, len(self.M) - len(g))).astype(bool)
        bm, bs = self._bands
        return [float(10 * np.log10((bs[b][g] ** 2).mean() / ((bm[b][g] ** 2).mean() + 1e-30) + 1e-30)) for b in range(3)]

    def band_width_fit(self, mask_kind="sing"):
        """Side-minus-mid (dB) on sharp FFT bands that each lie inside one band of the plugin's split, away
        from the crossovers: low 100-300 Hz, mid 400-3000 Hz, high 5-12 kHz. Used for fitting (no leakage
        between bands, so each control moves only its own number)."""
        if not hasattr(self, "_stft"):
            f, t, ZM = signal.stft(self.M, SR, nperseg=2048, noverlap=1536, boundary=None, padded=False)
            _, _, ZS = signal.stft(self.S, SR, nperseg=2048, noverlap=1536, boundary=None, padded=False)
            self._stft = (f, t, np.abs(ZM) ** 2, np.abs(ZS) ** 2)
        f, t, PM, PS = self._stft
        mk = self.sing if mask_kind == "sing" else self.gap_mask()
        idx = np.clip((t / 0.005).astype(int), 0, len(mk) - 1); sel = mk[idx]
        if sel.sum() < 10: return None
        out = []
        for lo, hi in ((100, 300), (400, 3000), (5000, 12000)):
            b = (f >= lo) & (f < hi)
            out.append(float(10 * np.log10(PS[b][:, sel].sum() / (PM[b][:, sel].sum() + 1e-30) + 1e-30)))
        return out

    def gap_mask(self):
        g = np.zeros(len(self.em), bool)
        for i, j, k in self.offs:
            g[j + 12:min(k, j + 80)] = True
        return g & (self.em > self.top - 60)

    def width_db(self):
        g = self.gap_mask()
        return float(np.median(self.es[g] - self.em[g])) if g.sum() > 20 else None

    def wet_curve(self):
        g = self.gap_mask()
        gm = np.repeat(g, H5)[:len(self.S)]
        gm = np.pad(gm, (0, len(self.S) - len(gm)))
        sm = np.repeat(self.sing, H5)[:len(self.S)]
        sm = np.pad(sm, (0, len(self.S) - len(sm)))
        if gm.sum() < SR * 0.5: return None
        f, Pg = signal.welch(self.S[gm.astype(bool)], SR, nperseg=4096)
        _, Ps = signal.welch(self.M[sm.astype(bool)], SR, nperseg=4096)
        c = band_curve(Pg, f) - band_curve(Ps, f)
        c = np.convolve(np.pad(c, 1, mode='edge'), [0.25, 0.5, 0.25], 'valid')  # half-octave smoothing
        return norm_curve(c)

    def gap_decay(self, start_ms=230):
        """Wet decay rate (500-4k, L+R power) in gaps after the duck release and predelay feed.
        Returns median slope in dB/s over all usable gaps (>= 150 ms of window)."""
        if not hasattr(self, "_emb"):
            sos = signal.butter(3, (500, 4000), 'bandpass', fs=SR, output='sos')
            l, r = signal.sosfiltfilt(sos, self.L), signal.sosfiltfilt(sos, self.R)
            self._emb = env5(np.sqrt((l ** 2 + r ** 2) / 2))
        e, sl = self._emb, []
        for i, j, k in self.offs:
            # start after the wet swell has peaked (duck released), at least start_ms after the vocal stops
            pk = j + 4 + int(np.argmax(e[j + 4:min(k, j + 160)])) if min(k, j + 160) > j + 4 else j
            s0, s1 = max(j + start_ms // 5, pk), min(k, j + 400)
            if s1 - s0 < 30: continue
            t = np.arange(s1 - s0) * 0.005
            sl.append(np.polyfit(t, e[s0:s1], 1)[0])
        if len(sl) < 2: return None
        return dict(slope_db_s=float(np.median(sl)), n=len(sl), all=[round(float(v), 1) for v in sl])

    def delay_detect(self):
        """Look for repeating echoes: periodic peaks in the autocorrelation of the wet envelope in gaps."""
        if len(self.offs) < 4: return None
        acs = []
        for i, j, k in self.offs:
            if k - j < 120: continue
            seg = self.es[j:k] - np.linspace(self.es[j], self.es[k - 1], k - j)
            seg -= seg.mean()
            ac = np.correlate(seg, seg, 'full')[len(seg) - 1:]
            if ac[0] <= 0: continue
            acs.append(np.pad(ac / ac[0], (0, 400))[:400])
        if len(acs) < 3: return None
        ac = np.mean(acs, 0)
        pk, pr = signal.find_peaks(ac[16:], prominence=0.15)
        if not len(pk): return None
        T = (pk[np.argmax(pr['prominences'])] + 16) * 5
        return dict(time_ms=float(T), prominence=float(pr['prominences'].max()))


# ----------------------------------------------------------------------------------------------
def biquad_db(kind, f0, q, gain_db, f):
    w0 = 2 * np.pi * f0 / SR
    al = np.sin(w0) / (2 * q)
    c = np.cos(w0)
    if kind == 'hp':
        b = [(1 + c) / 2, -(1 + c), (1 + c) / 2]; a = [1 + al, -2 * c, 1 - al]
    elif kind == 'lp':
        b = [(1 - c) / 2, 1 - c, (1 - c) / 2]; a = [1 + al, -2 * c, 1 - al]
    else:
        A = 10 ** (gain_db / 40)
        b = [1 + al * A, -2 * c, 1 - al * A]; a = [1 + al / A, -2 * c, 1 - al / A]
    _, hh = signal.freqz(b, a, worN=np.asarray(f), fs=SR)
    return 20 * np.log10(np.abs(hh) + 1e-12)


def fit_wet_eq(curve):
    f = np.array(CENTERS, float)
    sel = (f >= 100) & (f <= 12500)
    best = None
    for hp in [80, 120, 160, 200, 250, 300, 350, 400, 500, 630]:
        h1 = biquad_db('hp', hp, 0.9, 0, f)
        for lp in [2500, 3150, 4000, 4500, 5000, 5600, 6300, 8000, 10000, 14000]:
            h2 = biquad_db('lp', lp, 0.8, 0, f)
            for bf in [1000, 1600, 2000, 2500, 3150]:
                for bg in [-3, -1.5, 0, 1.5, 3, 4.5, 6]:
                    m = norm_curve(h1 + h2 + biquad_db('pk', bf, 1.0, bg, f))
                    err = np.mean((m[sel] - curve[sel]) ** 2)
                    if best is None or err < best[0]: best = (err, hp, lp, bf, bg)
    return dict(wetHpf=best[1], wetLpf=best[2], wetBumpHz=best[3], wetBumpDb=best[4], rms_err_db=float(np.sqrt(best[0])))


# ----------------------------------------------------------------------------------------------
def render(renderer, params, dry, tmp):
    pfile = os.path.join(tmp, "p.txt")
    with open(pfile, "w") as fh:
        for k, v in params.items():
            if isinstance(v, (list, tuple)): v = ",".join(f"{x:.4f}" for x in v)
            elif isinstance(v, bool): v = int(v)
            fh.write(f"{k}={v}\n")
    fin, fout = os.path.join(tmp, "in.f32"), os.path.join(tmp, "out.f32")
    dry.astype(np.float32).tofile(fin)
    subprocess.run([renderer, pfile, fin, fout, str(SR)], check=True, capture_output=True)
    return np.fromfile(fout, dtype=np.float32).reshape(-1, 2).astype(np.float64)


def sib_curve_of(renderer, x):
    """Sibilant spectrum re vowel (2k..16k), measured by the plugin's own analyzer (render_cli 'sib' mode)."""
    if not os.path.exists(renderer): return None, 0
    with tempfile.TemporaryDirectory() as t:
        open(os.path.join(t, "p.txt"), "w").close(); x.astype(np.float32).tofile(os.path.join(t, "i.f32"))
        r = subprocess.run([renderer, os.path.join(t, "p.txt"), os.path.join(t, "i.f32"), os.path.join(t, "o.f32"), str(SR), "sib"], capture_output=True, text=True)
    v = r.stderr.split("sibcurve:")[-1].split()
    n = int(v[0])
    return ([round(float(c), 2) for c in v[1:11]] if n >= 3 else None), n


def meter_of(renderer, x):
    """The plugin's own WIDTH meter (render_cli 'meter' mode) over the whole file: targets for the WIDTH graph."""
    if not os.path.exists(renderer): return None
    with tempfile.TemporaryDirectory() as t:
        open(os.path.join(t, "p.txt"), "w").close(); x.astype(np.float32).tofile(os.path.join(t, "i.f32"))
        r = subprocess.run([renderer, os.path.join(t, "p.txt"), os.path.join(t, "i.f32"), os.path.join(t, "o.f32"), str(SR), "meter"], capture_output=True, text=True)
    v = [float(a) for a in r.stderr.split("meter:")[-1].split()[:6]]
    return dict(singing=[round(a, 2) for a in v[:3]], pauses=[round(a, 2) for a in v[3:]]) if len(v) == 6 else None


def wet_measures(m, ons=None):
    if ons is not None: m.ons = list(ons)      # renders are measured at the Suno section starts
    o = m.offset_stats() or {}
    return dict(arrival=m.wet_arrival_ms(), rise=o.get("rise_db"), wet=o.get("wet_re_dry_db"),
                rise_time=o.get("rise_time_ms"), rise_short=o.get("rise_short_db"), rise_long=o.get("rise_long_db"),
                peak_long=o.get("peak_time_long_ms"), width=m.width_db(), curve=m.wet_curve(), decay=m.gap_decay(),
                sing_width=m.singing_width_db(), early_side=m.early_side_db(), coh=m.coh_profile(),
                bw_sing=m.band_width("sing"), bw_gap=m.band_width("gap"),
                bwf_sing=m.band_width_fit("sing"), bwf_gap=m.band_width_fit("gap"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input"); ap.add_argument("--out", required=True); ap.add_argument("--name")
    ap.add_argument("--renderer", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build_cli", "render_cli"))
    ap.add_argument("--iters", type=int, default=6)
    ap.add_argument("--exclude", default="", help="time ranges to leave out of all measurements, seconds: '54.8-57.8,79.5-89.8'")
    ap.add_argument("--keep-character", help="previous preset (.json): keep its reverb character (decay, tone, predelay) and fit only "
                                              "levels, ducking and width; use when the decay estimate is unreliable (few long pauses)")
    a = ap.parse_args()

    x = load(a.input)
    if a.exclude:
        EXCLUDE[:] = [tuple(float(v) for v in r.split("-")) for r in a.exclude.split(",") if r.strip()]
    xs = silence_excluded(x)
    global SING_RANGE_DB, SING_SMOOTH_MS
    m = Measure(x)
    if len(m.offs) < 8:   # still too few phrase ends: smooth more
        SING_SMOOTH_MS = 200; m = Measure(x)
    print(f"singing gate: smooth {SING_SMOOTH_MS} ms, range {SING_RANGE_DB} dB -> {len(m.offs)} phrase ends", file=sys.stderr)
    log = lambda *s: print(*s, file=sys.stderr)
    log(f"duration {len(x)/SR:.1f}s, offsets {len(m.offs)}, onsets {len(m.ons)}")

    curve = m.target_curve()
    curve_v = m.target_curve(voiced=True)
    dyn = m.dynamics()
    deess_t = m.deess()
    import importlib.util as _iu
    _sp = _iu.spec_from_file_location("dm", os.path.join(os.path.dirname(os.path.abspath(__file__)), "dyn_metrics.py")); _dm = _iu.module_from_spec(_sp); _sp.loader.exec_module(_dm)
    sib_med, sib_p90, sib_n = _dm.sibilance(xs)
    T = wet_measures(m)
    dly = m.delay_detect()
    sib_curve, sib_curve_n = sib_curve_of(a.renderer, xs)
    width_meter = meter_of(a.renderer, xs)
    log("target:", {k: (v if not isinstance(v, np.ndarray) else "curve") for k, v in T.items()})

    # ---- initial guesses from direct measurement ----
    P = dict(amount=1.0, eqAmount=1.0, compAmount=0.0, compTotalGrDb=6.0, targetCrestDb=round(dyn["crest_db"], 2),
             comp1Ratio=4, comp1AttackMs=2, comp1ReleaseMs=50, comp2Ratio=4, comp2AttackMs=30, comp2ReleaseMs=300, targetSpreadDb=round(dyn["spread400_db"], 2),
             deessFreq=4000, deessTargetDb=-6.0, deessRatio=6.0, deessMaxDb=20, satDriveDb=6, satMix=0.15,
             delayOn=False, delayMs=375, delayFeedback=0.25, delayLevelDb=-18, delayHpf=300, delayLpf=5000, delayPingPong=True,
             reverbOn=True, predelayMs=max(0.0, (T["arrival"] or 190) - 10),
             rt60=float(np.clip(-60 / T["decay"]["slope_db_s"], 1.0, 8.0)) if T["decay"] and T["decay"]["slope_db_s"] < -1 else 3.0,
             rt60LowMul=1.0, rt60HighMul=0.7, crossoverHz=1500, size=1.0, modDepth=0.5, diffusion=0.75, diffusionSize=2.0,
             earlyMs=22, earlyLevelDb=-100,
             width=1.0, reverbLevelDb=-5.0, duckDb=max(3.0, T["rise_long"] or T["rise"] or 8), duckAttackMs=5,
             duckReleaseMs=max(60.0, 0.8 * (T["peak_long"] or 200)),
             widthOn=True, widthLevelDb=-28.0, widthDelayMs=1.0, widthShiftHz=1.0, widthTiltDb=0.0, widthHpf=200.0, widthLpf=16000.0,
             widthBloomDb=12.0, widthBloomMs=200.0,
             xoverLowHz=300.0, xoverHighHz=4000.0, revBandDb=[0.0, 0.0, 0.0], layerBandDb=[0.0, 0.0, 0.0], delayBandDb=[0.0, 0.0, 0.0])
    P.update(wetHpf=400.0, wetLpf=14000.0, wetBumpHz=3150.0, wetBumpDb=0.0)   # used when the pauses are too short to measure the reverb tone
    P.update({k: v for k, v in fit_wet_eq(T["curve"]).items() if k != "rms_err_db"} if T["curve"] is not None else {})
    if dly and dly["prominence"] > 0.3:
        P.update(delayOn=True, delayMs=dly["time_ms"])
    keep = None
    if a.keep_character:
        kr = json.load(open(a.keep_character))["reverb"]
        keep = dict(rt60=kr["rt60_s"], rt60LowMul=kr["rt60_low_mul"], rt60HighMul=kr["rt60_high_mul"], predelayMs=kr["predelay_ms"],
                    wetHpf=kr["hpf_hz"], wetLpf=kr["lpf_hz"], wetBumpHz=kr["bump_hz"], wetBumpDb=kr["bump_db"],
                    modDepth=min(0.5, kr.get("mod_depth", 0.5)), diffusion=kr.get("diffusion", 0.75), diffusionSize=kr.get("diffusion_size", 2.0))
        P.update(keep); log("keeping reverb character from", a.keep_character, keep)

    # ---- pseudo-dry: Suno mid with non-singing parts muted (smooth) ----
    g = np.repeat(m.sing.astype(float), H5)[:len(x)]
    g = np.pad(g, (0, len(x) - len(g)))
    g = uniform_filter1d(g, int(0.01 * SR))
    dry = np.stack([m.M * g, m.M * g], 1)

    def logit(v): v = float(np.clip(v, 1e-3, 0.95)); return np.log(v / (1 - v))
    def errors(T, R):
        e = dict(arrival=(T["arrival"] or 0) - (R["arrival"] or 0),
                 rise_long=(T["rise_long"] or 0) - (R["rise_long"] or 0),
                 rise_short=(T["rise_short"] or 0) - (R["rise_short"] or 0),
                 sing_width=(T["sing_width"] or 0) - (R["sing_width"] or 0),
                 gap_width=(T["width"] or 0) - (R["width"] or 0),
                 rt=((T["decay"] or {}).get("slope_db_s") or -1) / ((R["decay"] or {}).get("slope_db_s") or -1),
                 layer=logit(T["coh"]["broad"]) - logit(R["coh"]["broad"]),
                 k_ratio=(T["coh"]["k_ratio"] or 0) - (R["coh"]["k_ratio"] or 0),
                 early_side=(T["early_side"] or 0) - (R["early_side"] or 0),
                 bloom=(T["coh"]["bloom_ratio"] or 0) - (R["coh"]["bloom_ratio"] or 0))
        return e
    def score(e):
        return ((e["arrival"] / 20) ** 2 + (e["rise_long"] / 1.5) ** 2 + (e["rise_short"] / 1.5) ** 2 + (e["sing_width"] / 1.0) ** 2
                + (e["gap_width"] / 1.5) ** 2 + ((np.log(e["rt"]) / np.log(1.3)) ** 2 if e["rt"] > 0 else 25)
                + (e["layer"] / 0.3) ** 2 + (e["k_ratio"] / 0.08) ** 2 + (e["early_side"] / 4.0) ** 2 + (e["bloom"] / 0.1) ** 2)

    hist = []
    if not os.path.exists(a.renderer):
        log("renderer not found, skipping closed-loop fit"); a.iters = 0
    # Stage A target for the reverb: Suno's side while singing minus the part explained by the width layer
    TA = dict(T)
    if T["sing_width"] is not None:
        TA["sing_width"] = T["sing_width"] + 10 * np.log10(max(0.05, 1 - T["coh"]["broad"]))
    def scoreA(e):
        # with --keep-character the decay is fixed, so its (noisy, few pauses) error must not pick the iteration
        rt_term = 0.0 if a.keep_character else ((np.log(e["rt"]) / np.log(1.3)) ** 2 if e["rt"] > 0 else 25)
        return ((e["arrival"] / 20) ** 2 + (e["rise_long"] / 1.5) ** 2 + (e["rise_short"] / 1.5) ** 2 + (e["sing_width"] / 1.0) ** 2
                + (e["gap_width"] / 1.5) ** 2 + rt_term)
    def scoreB(e):
        return (e["layer"] / 0.3) ** 2 + (e["k_ratio"] / 0.08) ** 2 + (e["bloom"] / 0.1) ** 2 + (e["early_side"] / 3.0) ** 2
    with tempfile.TemporaryDirectory() as tmp:
        # ---------------- stage A: reverb + ducking (width layer off) ----------------
        best = None
        itA = a.iters
        for it in range(itA):
            y = render(a.renderer, dict(P, widthOn=False), dry, tmp)
            R = wet_measures(Measure(y), m.ons); err = errors(TA, R); sc_ = scoreA(err)
            hist.append({"stage": "A", **{k: round(float(v), 3) for k, v in err.items()}})
            if best is None or sc_ < best[0]: best = (sc_, dict(P))
            log(f"A{it}: score {sc_:6.2f} " + "  ".join(f"{k} {err[k]:+.2f}" for k in ("arrival", "rise_long", "rise_short", "sing_width", "gap_width", "rt")))
            P["predelayMs"] = float(np.clip(P["predelayMs"] + 0.7 * err["arrival"], 0, 500))
            P["duckDb"] = float(np.clip(P["duckDb"] + 0.5 * err["rise_long"], 0, 24))
            P["duckReleaseMs"] = float(np.clip(P["duckReleaseMs"] * 10 ** (-0.04 * (err["rise_short"] - 0.5 * err["rise_long"])), 30, 1500))
            P["reverbLevelDb"] = float(np.clip(P["reverbLevelDb"] + 0.5 * (err["sing_width"] + 0.5 * err["rise_long"]), -40, 8))
            P["width"] = float(np.clip(P["width"] * 10 ** (0.7 * err["gap_width"] / 20), 0.0, 1.6))
            if T["decay"] and R["decay"] and err["rt"] > 0:
                P["rt60"] = float(np.clip(P["rt60"] / np.clip(err["rt"], 0.8, 1.25) ** 0.6, 0.8, 8))
            if T["curve"] is not None and R["curve"] is not None:
                f = np.array(CENTERS, float)
                cur = norm_curve(biquad_db('hp', P["wetHpf"], 0.9, 0, f) + biquad_db('lp', P["wetLpf"], 0.8, 0, f)
                                 + biquad_db('pk', P["wetBumpHz"], 1.0, P["wetBumpDb"], f))
                P.update({k: v for k, v in fit_wet_eq(cur + 0.5 * (T["curve"] - R["curve"])).items() if k != "rms_err_db"})
            if keep: P.update(keep)
        if itA: P = best[1]; log(f"stage A best score {best[0]:.2f}")
        # ---------------- stage A2: reverb width per band, from the pauses (only reverb there) ----------------
        if itA and T["bwf_gap"] is not None:
            bestA2 = None
            for it in range(5):
                y = render(a.renderer, dict(P, widthOn=False), dry, tmp)
                R = wet_measures(Measure(y), m.ons)
                if R["bwf_gap"] is None: break
                e = np.array(T["bwf_gap"]) - np.array(R["bwf_gap"])
                hist.append({"stage": "A2", "band_err": [round(float(v), 2) for v in e]})
                log(f"A2{it}: gap band width err " + " ".join(f"{v:+.2f}" for v in e) + f"  bands {np.round(P['revBandDb'], 2)}")
                if bestA2 is None or np.abs(e).sum() < bestA2[0]: bestA2 = (float(np.abs(e).sum()), list(P["revBandDb"]))
                P["revBandDb"] = [float(np.clip(g + 0.9 * d, -30, 12)) for g, d in zip(P["revBandDb"], e)]
            if bestA2: P["revBandDb"] = bestA2[1]
        # ---------------- stage B: width layer on top of the fixed reverb ----------------
        best = None
        itB = max(0, a.iters // 2 + 2) if a.iters else 0
        for it in range(itB):
            y = render(a.renderer, P, dry, tmp)
            R = wet_measures(Measure(y), m.ons); err = errors(T, R); sc_ = scoreB(err)
            hist.append({"stage": "B", **{k: round(float(v), 3) for k, v in err.items()}})
            if best is None or sc_ < best[0]: best = (sc_, dict(P))
            log(f"B{it}: score {sc_:6.2f} " + "  ".join(f"{k} {err[k]:+.2f}" for k in ("layer", "k_ratio", "bloom", "sing_width", "early_side")))
            P["widthLevelDb"] = float(np.clip(P["widthLevelDb"] + 4.0 * err["layer"], -45, -8))
            tb, rb = T["coh"]["bands"], R["coh"]["bands"]
            lo_err = np.log((tb["250"] + 1e-3) / (tb["1000"] + 1e-3)) - np.log((rb["250"] + 1e-3) / (rb["1000"] + 1e-3))
            hi_err = np.log((tb["4000"] + 1e-3) / (tb["1000"] + 1e-3)) - np.log((rb["4000"] + 1e-3) / (rb["1000"] + 1e-3))
            P["widthHpf"] = float(np.clip(P["widthHpf"] * np.exp(-0.5 * lo_err), 60, 800))
            P["widthTiltDb"] = float(np.clip(P["widthTiltDb"] + 4.0 * hi_err, -6, 15))
            if T["coh"]["k_ratio"] and R["coh"]["k_ratio"]:
                # more frequency shift = faster phase rotation = lower coherence over long windows
                P["widthShiftHz"] = float(np.clip(P["widthShiftHz"] * np.exp(-3.0 * err["k_ratio"]), 0.05, 8.0))
            if T["coh"]["bloom_ratio"] and R["coh"]["bloom_ratio"]:
                P["widthBloomMs"] = float(np.clip(P["widthBloomMs"] * np.exp(-2.0 * err["bloom"]), 30, 800))
            if T["early_side"] is not None and R["early_side"] is not None:
                P["widthBloomDb"] = float(np.clip(P["widthBloomDb"] - 0.6 * err["early_side"], 0, 40))
        if itB: P = best[1]; log(f"stage B best score {best[0]:.2f}")
        # ---------------- stage C: trim the reverb level so the width while singing matches with the layer on ----------------
        for it in range(2 if a.iters else 0):
            y = render(a.renderer, P, dry, tmp)
            R = wet_measures(Measure(y), m.ons); err = errors(T, R)
            log(f"C{it}: sing_width {err['sing_width']:+.2f}  rise_long {err['rise_long']:+.2f}")
            P["reverbLevelDb"] = float(np.clip(P["reverbLevelDb"] + 0.9 * err["sing_width"], -40, 8))
        # ---------------- stages E-H: width per band while singing ----------------
        # The layer's level and colour are fitted on the coherence (stage B: the same measurement on Suno and on
        # the render). Its mid band stays as fitted there; the reverb level sets the total width of the mid band
        # (E); in the low and high bands the reverb's width is fixed by the pauses (A2), so the layer fills the
        # rest (F). Then the bloom (G) and the ducking depth (H) are re-checked.
        if a.iters and T["bwf_sing"] is not None:
            # low band of the layer: its share of Suno's low side from the coherence (the rest of Suno's low side is
            # uncorrelated: reverb and stem-separation residue, which a correlated layer must not imitate - pushed
            # harder it beats audibly against the voice)
            c_low = float(np.clip(T["coh"]["bands"]["250"], 0.03, 0.6))
            for it in range(2):
                Rl = wet_measures(Measure(render(a.renderer, dict(P, reverbOn=False), dry, tmp)), m.ons)["bwf_sing"]
                if Rl is None: break
                e0 = T["bwf_sing"][0] + 10 * np.log10(c_low) - Rl[0]
                hist.append({"stage": "D-low", "err": round(float(e0), 2)})
                log(f"D-low{it}: layer-only low band err {e0:+.2f}  layer low {P['layerBandDb'][0]:.2f}")
                P["layerBandDb"] = [float(np.clip(P["layerBandDb"][0] + e0, -40, 18)), P["layerBandDb"][1], P["layerBandDb"][2]]
            # ---------------- stage E: reverb level so the total width while singing matches in the main (mid) band ----------------
            for it in range(2):
                R = wet_measures(Measure(render(a.renderer, P, dry, tmp)), m.ons)
                if R["bwf_sing"] is None: break
                e = np.array(T["bwf_sing"]) - np.array(R["bwf_sing"])
                hist.append({"stage": "E", "band_err": [round(float(v), 2) for v in e]})
                log(f"E{it}: total singing band err " + " ".join(f"{v:+.2f}" for v in e) + f"  reverb level {P['reverbLevelDb']:.2f}")
                P["reverbLevelDb"] = float(np.clip(P["reverbLevelDb"] + 0.9 * e[1] / 0.8, -40, 8))
            # ---------------- stage F: high band ----------------
            # there the reverb's width is fixed by the pauses (stage A2), so the layer fills the rest of Suno's
            # width while singing: layer = total - reverb (power)
            for it in range(3):
                Rr = wet_measures(Measure(render(a.renderer, dict(P, widthOn=False), dry, tmp)), m.ons)["bwf_sing"]
                R = wet_measures(Measure(render(a.renderer, P, dry, tmp)), m.ons)["bwf_sing"]
                if R is None or Rr is None: break
                e = np.array(T["bwf_sing"]) - np.array(R)
                hist.append({"stage": "F", "band_err": [round(float(v), 2) for v in e]})
                log(f"F{it}: total singing band err " + " ".join(f"{v:+.2f}" for v in e) + f"  layer bands {np.round(P['layerBandDb'], 2)}")
                nb = list(P["layerBandDb"])
                for b in (2,):
                    tgt, cur, rev = 10 ** (T["bwf_sing"][b] / 10), 10 ** (R[b] / 10), 10 ** (Rr[b] / 10)
                    lay_now, lay_want = max(cur - rev, 1e-7), tgt - rev
                    step = 10 * np.log10(lay_want / lay_now) if lay_want > 1e-7 else -12.0
                    nb[b] = float(np.clip(nb[b] + 0.9 * np.clip(step, -12, 12), -40, 18))
                P["layerBandDb"] = nb
            # the louder low band of the layer also shows at section starts: re-fit the bloom depth
            for it in range(3):
                R = wet_measures(Measure(render(a.renderer, P, dry, tmp)), m.ons)
                if T["early_side"] is None or R["early_side"] is None: break
                es = T["early_side"] - R["early_side"]
                hist.append({"stage": "G", "early_side": round(float(es), 2)})
                log(f"G{it}: early_side {es:+.2f}  bloom {P['widthBloomDb']:.2f}")
                P["widthBloomDb"] = float(np.clip(P["widthBloomDb"] - 0.6 * es, 0, 40))
            for it in range(2):
                R = wet_measures(Measure(render(a.renderer, P, dry, tmp)), m.ons)
                er = (T["rise_long"] or 0) - (R["rise_long"] or 0)
                hist.append({"stage": "H", "rise_long": round(float(er), 2)})
                log(f"H{it}: rise_long {er:+.2f}  duck {P['duckDb']:.2f}")
                P["duckDb"] = float(np.clip(P["duckDb"] + 0.6 * er, 0, 24))
        if a.iters:
            y = render(a.renderer, P, dry, tmp)
            R = wet_measures(Measure(y), m.ons)
            # the reverb arrival is defined on the reverb alone (the width layer blooms in the same window)
            mm = Measure(render(a.renderer, dict(P, widthOn=False), dry, tmp)); mm.ons = list(m.ons); R["arrival"] = mm.wet_arrival_ms()
        else:
            R = None

    def r1(v): return None if v is None else round(float(v), 2)
    preset = {
        "format": "SunoChainPreset", "version": 3,
        "name": a.name or os.path.splitext(os.path.basename(a.input))[0],
        "source_file": os.path.basename(a.input),
        "eq": {"target_curve_hz": CENTERS, "target_curve_db": [r1(v) for v in curve], "target_curve_voiced_db": [r1(v) for v in curve_v], "vowel": True,
               "amount": 1.0, "max_boost_db": 12.0, "max_cut_db": 24.0},
        "dynamics": {"target_crest_db": r1(dyn["crest_db"]), "target_spread400_db": r1(dyn["spread400_db"]), "default_total_gr_db": 6.0,
                     "comp1": {"ratio": 4.0, "attack_ms": 2.0, "release_ms": 50.0, "share": 0.55},
                     "comp2": {"ratio": 4.0, "attack_ms": 30.0, "release_ms": 300.0}, "leveler_factor": 1.5},
        "deesser": {"method": "sz_match", "sib_curve_hz": CENTERS[16:], "sib_curve_db": sib_curve, "n_sib_segments": sib_curve_n,
                    "split_hz": 4000.0, "sib_median_db": r1(sib_med if np.isfinite(sib_med) else -2.5), "sib_p90_db": r1(sib_p90 if np.isfinite(sib_p90) else 4.2),
                    "n_sibilants": sib_n, "ratio": 6.0, "max_cut_db": 20.0},
        "saturation": {"drive_db": 6.0, "mix": 0.15, "measured": False},
        # delay: only applied on load when an echo was actually detected in the Suno vocal;
        # otherwise the plugin keeps your own delay settings
        "delay": {"detected": bool(P["delayOn"]), "on": bool(P["delayOn"]), "time_ms": r1(P["delayMs"]), "feedback": P["delayFeedback"],
                  "level_db": P["delayLevelDb"], "hpf_hz": P["delayHpf"], "lpf_hz": P["delayLpf"], "ping_pong": True,
                  "sync": True, "note": 2, "to_reverb": 0.5, "band_width_db": [r1(v) for v in P["delayBandDb"]]},
        "width_layer": {"on": True, "level_db": r1(P["widthLevelDb"]), "delay_ms": r1(P["widthDelayMs"]), "shift_hz": round(float(P["widthShiftHz"]), 3),
                        "tilt_db": r1(P["widthTiltDb"]), "hpf_hz": r1(P["widthHpf"]), "lpf_hz": r1(P["widthLpf"]),
                        "bloom_db": r1(P["widthBloomDb"]), "bloom_ms": r1(P["widthBloomMs"]), "side_only": True,
                        "band_db": [r1(v) for v in P["layerBandDb"]]},
        "multiband_width": {"xover_low_hz": P["xoverLowHz"], "xover_high_hz": P["xoverHighHz"],
                            "note": "side-only gains per band (low/mid/high) for reverb, width layer and delay; mid/mono untouched"},
        "reverb": {"on": True, "predelay_ms": r1(P["predelayMs"]), "rt60_s": r1(P["rt60"]),
                   "rt60_low_mul": r1(P["rt60LowMul"]), "rt60_high_mul": r1(P["rt60HighMul"]), "crossover_hz": 1500.0,
                   "size": 1.0, "mod_depth": P["modDepth"], "diffusion": P["diffusion"], "diffusion_size": P["diffusionSize"],
                   "early_ms": 22.0, "early_level_db": -100.0,
                   "hpf_hz": r1(P["wetHpf"]), "lpf_hz": r1(P["wetLpf"]), "bump_hz": r1(P["wetBumpHz"]), "bump_db": r1(P["wetBumpDb"]),
                   "width": r1(P["width"]), "level_db": r1(P["reverbLevelDb"]), "band_width_db": [r1(v) for v in P["revBandDb"]]},
        "ducking": {"depth_db": r1(P["duckDb"]), "attack_ms": 5.0, "release_ms": r1(P["duckReleaseMs"])},
        "measurements": {
            "suno": {"loudness_level_dbfs": r1(dyn["level_dbfs"]), "crest_db": r1(dyn["crest_db"]),
                     "spread400_db": r1(dyn["spread400_db"]), "wet_arrival_ms": r1(T["arrival"]),
                     "wet_rise_after_vocal_db": r1(T["rise"]), "wet_peak_re_dry_db": r1(T["wet"]),
                     "side_minus_mid_in_gaps_db": r1(T["width"]), "gap_decay": T["decay"],
                     "wet_rise_short_pauses_db": r1(T["rise_short"]), "wet_rise_long_pauses_db": r1(T["rise_long"]),
                     "wet_peak_time_long_ms": r1(T["peak_long"]), "side_minus_mid_singing_db": r1(T["sing_width"]),
                     "side_minus_mid_section_start_db": r1(T["early_side"]), "width_layer_coherence": T["coh"], "delay_detected": dly,
                     "width_bands_singing_db": [r1(v) for v in T["bw_sing"]] if T["bw_sing"] else None,
                     "width_bands_pauses_db": [r1(v) for v in T["bw_gap"]] if T["bw_gap"] else None,
                     "width_fit_bands_singing_db": [r1(v) for v in T["bwf_sing"]] if T["bwf_sing"] else None,
                     "width_fit_bands_pauses_db": [r1(v) for v in T["bwf_gap"]] if T["bwf_gap"] else None,
                     "width_meter_singing_db": width_meter["singing"] if width_meter else None,
                     "width_meter_pauses_db": width_meter["pauses"] if width_meter else None},
            "render_with_preset": None if R is None else {
                "wet_arrival_ms": r1(R["arrival"]), "wet_rise_after_vocal_db": r1(R["rise"]),
                "wet_peak_re_dry_db": r1(R["wet"]), "side_minus_mid_in_gaps_db": r1(R["width"]),
                "gap_decay": R["decay"], "wet_rise_short_pauses_db": r1(R["rise_short"]), "wet_rise_long_pauses_db": r1(R["rise_long"]),
                "wet_peak_time_long_ms": r1(R["peak_long"]), "side_minus_mid_singing_db": r1(R["sing_width"]),
                "side_minus_mid_section_start_db": r1(R["early_side"]), "width_layer_coherence": R["coh"],
                "width_bands_singing_db": [r1(v) for v in R["bw_sing"]] if R["bw_sing"] else None,
                "width_bands_pauses_db": [r1(v) for v in R["bw_gap"]] if R["bw_gap"] else None,
                "width_fit_bands_singing_db": [r1(v) for v in R["bwf_sing"]] if R["bwf_sing"] else None,
                "width_fit_bands_pauses_db": [r1(v) for v in R["bwf_gap"]] if R["bwf_gap"] else None},
            "wet_curve_suno_db": None if T["curve"] is None else [r1(v) for v in T["curve"]],
            "wet_curve_render_db": None if R is None or R["curve"] is None else [r1(v) for v in R["curve"]],
            "fit_history": hist,
            "singing_gate_db": SING_RANGE_DB, "singing_smooth_ms": SING_SMOOTH_MS,
            "excluded_ranges_s": [[round(a_, 2), round(b_, 2)] for a_, b_ in EXCLUDE],
        },
    }
    with open(a.out, "w") as fh: json.dump(preset, fh, indent=2)
    log(json.dumps(preset["reverb"])); log(json.dumps(preset["ducking"])); log(json.dumps(preset["width_layer"]))
    log("SUNO  ", json.dumps(preset["measurements"]["suno"])); log("RENDER", json.dumps(preset["measurements"]["render_with_preset"]))


if __name__ == "__main__":
    main()
