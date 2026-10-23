"""v2.2 WIDTH ENVELOPE measurement (same detector as sc::WidthEnvelope in Source/dsp/SunoChainDSP.h).

Level of the mid (150 Hz - 4 kHz, 30 ms one-pole power), phrase peak reference (follows peaks at once, falls
1 dB/s while within 30 dB, holds in pauses), d = reference - level, updated every 32 samples, 5 bins.
For each band (<1k measured from 150 Hz / 1-4 kHz / 4-12 kHz) and bin: the voice-coherent part of the side
(side that follows the mid within ~75 ms, unbiased estimate) re the mid. gains() = the rise of that ratio over the
bins, re the loudest bin: the layer gains the plugin applies.
"""
import numpy as np
from scipy import signal

EDGES = [0.0, 5.0, 9.5, 15.5, 24.5, 40.0]
BANDS = [(150, 1000), (1000, 4000), (4000, 12000)]
TICK = 32


def detector(mid, fs):
    sos = np.vstack([signal.butter(2, 150, 'highpass', fs=fs, output='sos'), signal.butter(2, 4000, 'lowpass', fs=fs, output='sos')])
    x = signal.sosfilt(sos, mid)
    c = np.exp(-1.0 / (0.030 * fs))
    env = signal.lfilter([1 - c], [1, -c], x * x)
    lv = 10 * np.log10(env[TICK - 1::TICK] + 1e-12)
    d = np.empty_like(lv); ref = -200.0; fall = 1.0 * TICK / fs
    for i, l in enumerate(lv):
        if l > ref: ref = l
        elif l > ref - 30.0: ref -= fall
        d[i] = min(max(ref - l, 0.0), 40.0)
    return d   # one value per tick; tick i covers samples [i*TICK, (i+1)*TICK)


def energies(x, fs, n=512, avg=7):
    """x: (samples, 2). Returns (coherent side power, mid power) per band and bin, each (3, 5), and seconds per bin."""
    mid = 0.5 * (x[:, 0] + x[:, 1]); side = 0.5 * (x[:, 0] - x[:, 1])
    d = detector(mid, fs)
    f, t, ZM = signal.stft(mid, fs, window='hann', nperseg=n, noverlap=0, boundary=None, padded=False)
    _, _, ZS = signal.stft(side, fs, window='hann', nperseg=n, noverlap=0, boundary=None, padded=False)
    X = ZS * np.conj(ZM); EM = np.abs(ZM) ** 2
    k = np.ones(avg)
    conv = lambda a: signal.convolve(a, k[None, :], mode='same')
    sX = conv(X.real) + 1j * conv(X.imag); sX2 = conv(np.abs(X) ** 2); sM = conv(EM); sM2 = conv(EM ** 2)
    with np.errstate(invalid='ignore', divide='ignore'):
        coh = (np.abs(sX) ** 2 - sX2) / (sM ** 2 - sM2) * sM   # unbiased coherent side power (per bin, per frame window)
    coh = np.nan_to_num(coh)
    centre = (np.arange(ZM.shape[1]) * n + n // 2) // TICK
    fd = d[np.clip(centre, 0, len(d) - 1)]
    C = np.zeros((3, 5)); M = np.zeros((3, 5)); secs = np.zeros(5)
    for kbin in range(5):
        sel = (fd >= EDGES[kbin]) & (fd < EDGES[kbin + 1])
        secs[kbin] = sel.sum() * n / fs
        for b, (lo, hi) in enumerate(BANDS):
            fb = (f >= lo) & (f < hi)
            C[b, kbin] = coh[fb][:, sel].sum() / avg; M[b, kbin] = sM[fb][:, sel].sum() / avg
    return C, M, secs


def side_energies(x, fs, n=512):
    """x: (samples, 2). Total side power and mid power per band and bin (3, 5), seconds per bin. Robust (no
    coherence estimate); this is what the presets' width envelope is fitted to."""
    mid = 0.5 * (x[:, 0] + x[:, 1]); side = 0.5 * (x[:, 0] - x[:, 1])
    d = detector(mid, fs)
    f, t, ZM = signal.stft(mid, fs, window='hann', nperseg=n, noverlap=0, boundary=None, padded=False)
    _, _, ZS = signal.stft(side, fs, window='hann', nperseg=n, noverlap=0, boundary=None, padded=False)
    EM = np.abs(ZM) ** 2; ES = np.abs(ZS) ** 2
    centre = (np.arange(ZM.shape[1]) * n + n // 2) // TICK
    fd = d[np.clip(centre, 0, len(d) - 1)]
    C = np.zeros((3, 5)); M = np.zeros((3, 5)); secs = np.zeros(5)
    for kbin in range(5):
        sel = (fd >= EDGES[kbin]) & (fd < EDGES[kbin + 1])
        secs[kbin] = sel.sum() * n / fs
        for b, (lo, hi) in enumerate(BANDS):
            fb = (f >= lo) & (f < hi)
            C[b, kbin] = ES[fb][:, sel].sum(); M[b, kbin] = EM[fb][:, sel].sum()
    return C, M, secs


def gains(stems, min_secs=6.0):
    """stems: list of energies() results (Suno stems), each stem normalised to its own loudest-bin mid power and
    pooled. Per band: coherent side re mid in each bin minus the loudest bin (dB). Bins with less than min_secs of
    material in total (or a non-positive estimate) take the previous bin's value. Clamped to -6..+12 dB."""
    C = np.zeros((3, 5)); M = np.zeros((3, 5)); secs = np.zeros(5)
    for c, m, s in stems:
        w = 1.0 / max(m[:, 0].sum(), 1e-30)
        C += c * w; M += m * w; secs += s
    g = np.zeros((3, 5))
    for b in range(3):
        r0 = C[b, 0] / M[b, 0] if C[b, 0] > 0 else None
        last = 0.0
        for k in range(1, 5):
            if r0 and secs[k] >= min_secs and C[b, k] > 0 and M[b, k] > 0:
                last = float(np.clip(10 * np.log10(C[b, k] / M[b, k] / r0), -6.0, 12.0))
            g[b, k] = last
    return g, secs
