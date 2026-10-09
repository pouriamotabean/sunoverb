"""Functional test: simulated 'user' vocal -> Learn -> process with preset -> compare with Suno target."""
import sys, json, os, subprocess, tempfile, importlib.util
import numpy as np
from scipy import signal
from scipy.ndimage import uniform_filter1d
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sa", os.path.join(here, "suno_analyze.py")); sa = importlib.util.module_from_spec(spec); spec.loader.exec_module(sa)
SR = sa.SR
suno_path, preset_path = sys.argv[1], sys.argv[2]
pr = json.load(open(preset_path))
flat = dict(
    eqAmount=pr["eq"]["amount"], targetCurve=pr["eq"]["target_curve_db"], targetCrestDb=pr["dynamics"]["target_crest_db"], targetSpreadDb=pr["dynamics"]["target_spread400_db"],
    comp2Ratio=pr["dynamics"]["comp2"]["ratio"], comp2AttackMs=pr["dynamics"]["comp2"]["attack_ms"], comp2ReleaseMs=pr["dynamics"]["comp2"]["release_ms"],
    deessFreq=pr["deesser"]["freq_hz"], deessTargetDb=pr["deesser"]["target_ratio_db"],
    satDriveDb=pr["saturation"]["drive_db"], satMix=pr["saturation"]["mix"],
    predelayMs=pr["reverb"]["predelay_ms"], rt60=pr["reverb"]["rt60_s"], rt60LowMul=pr["reverb"]["rt60_low_mul"],
    rt60HighMul=pr["reverb"]["rt60_high_mul"], wetHpf=pr["reverb"]["hpf_hz"], wetLpf=pr["reverb"]["lpf_hz"],
    wetBumpHz=pr["reverb"]["bump_hz"], wetBumpDb=pr["reverb"]["bump_db"], width=pr["reverb"]["width"],
    reverbLevelDb=pr["reverb"]["level_db"], duckDb=pr["ducking"]["depth_db"], duckReleaseMs=pr["ducking"]["release_ms"],
    delayOn=pr["delay"]["on"])
for kv in os.environ.get('OVR','').split(';'):
    if kv: k,v=kv.split('='); flat[k]=float(v)

x = sa.load(suno_path); m = sa.Measure(x)
g = np.repeat(m.sing.astype(float), sa.H5)[:len(x)]; g = np.pad(g, (0, len(x) - len(g))); g = uniform_filter1d(g, 480)
dry = m.M * g
# make it a "different singer/recording": darker + boomier, more dynamic, quieter
b, a = signal.butter(1, 2500, 'low', fs=SR); dark = 0.6 * dry + 0.6 * signal.lfilter(b, a, dry)
sos = signal.butter(2, [120, 300], 'bandpass', fs=SR, output='sos'); dark += 0.8 * signal.sosfilt(sos, dry)
rng = np.random.default_rng(3)
slow = uniform_filter1d(rng.standard_normal(len(dry)), int(0.7 * SR)); slow /= np.abs(slow).max()
DEPTH = float(os.environ.get('DEPTH', '10'))
user = dark * 10 ** (DEPTH * slow / 20) * 0.6
U = np.stack([user, user], 1).astype(np.float32)

with tempfile.TemporaryDirectory() as t:
    pf = os.path.join(t, "p.txt")
    with open(pf, "w") as fh:
        for k, v in flat.items():
            if isinstance(v, list): v = ",".join(str(z) for z in v)
            elif isinstance(v, bool): v = int(v)
            fh.write(f"{k}={v}\n")
    U.tofile(os.path.join(t, "in.f32"))
    r = subprocess.run([os.path.join(here, "..", "build_cli", "render_cli"), pf, os.path.join(t, "in.f32"), os.path.join(t, "out.f32"), str(SR), "learn"],
                       capture_output=True, text=True); print(r.stderr.strip())
    y = np.fromfile(os.path.join(t, "out.f32"), dtype=np.float32).reshape(-1, 2).astype(np.float64)

tgt = np.array(pr["eq"]["target_curve_db"])
def report(name, sig):
    mm = sa.Measure(sig); c = mm.target_curve(); d = mm.dynamics()
    err = c - tgt; sel = np.array(sa.CENTERS) >= 300
    print(f"{name:8s} curve err vs Suno (>=300Hz) rms {np.sqrt(np.mean(err[sel]**2)):.1f} dB | crest {d['crest_db']:.1f} (Suno {pr['dynamics']['target_crest_db']}) | spread400 {d['spread400_db']:.1f} (Suno {pr['dynamics']['target_spread400_db']}) | level {d['level_dbfs']:.1f}")
    return mm
report("user in", U.astype(np.float64)); mo = report("output", y)
o = mo.offset_stats() or {}
print("output wet: arrival", mo.wet_arrival_ms(), "| rise", round(o.get('rise_db', 0), 1), "| wet re dry", round(o.get('wet_re_dry_db', 0), 1),
      "| width", round(mo.width_db() or 0, 1), "| decay", (mo.gap_decay() or {}).get('slope_db_s'))
print("Suno      : arrival", pr["measurements"]["suno"]["wet_arrival_ms"], "| rise", pr["measurements"]["suno"]["wet_rise_after_vocal_db"],
      "| wet re dry", pr["measurements"]["suno"]["wet_peak_re_dry_db"], "| width", pr["measurements"]["suno"]["side_minus_mid_in_gaps_db"],
      "| decay", round(pr["measurements"]["suno"]["gap_decay"]["slope_db_s"], 1))
print("peak out dBFS", round(20*np.log10(np.abs(y).max()), 1), "nan?", np.isnan(y).any())
sa_out = os.environ.get("SAVE")
if sa_out:
    import wave
    for nm, sig in (("user_in", U), ("processed", y)):
        z = (np.clip(sig, -1, 1) * 32767).astype("<i2")
        with wave.open(os.path.join(sa_out, nm + ".wav"), "wb") as w: w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(z.tobytes())
