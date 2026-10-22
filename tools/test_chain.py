"""Functional test: simulated 'user' vocal -> Learn -> process with preset -> compare with Suno target."""
import sys, json, os, subprocess, tempfile, importlib.util
import numpy as np
from scipy import signal
from scipy.ndimage import uniform_filter1d
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sa", os.path.join(here, "suno_analyze.py")); sa = importlib.util.module_from_spec(spec); spec.loader.exec_module(sa)
SR = sa.SR
if len(sys.argv) < 3:
    sys.exit("usage: test_chain.py suno_vocal.wav preset.json   (needs build/bin/render_cli or RENDER_CLI=path)")
suno_path, preset_path = sys.argv[1], sys.argv[2]
pr = json.load(open(preset_path))


def pget(path, default):
    """value at a dotted path of the preset, or the same fallback the plugin uses"""
    v = pr
    for k in path.split("."):
        if not isinstance(v, dict) or k not in v:
            return default
        v = v[k]
    return v


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


# v2.0.1: mapped from the CURRENT preset schema (version 7, see PluginProcessor.cpp loadPresetFile /
# restorePresetFromTree), with the plugin's own fallbacks. Old presets (version <= 2, de-esser freq/ratio) are
# migrated: their de-esser fields are passed on, newer ones use the S/Z Match curve.
if pget("format", "") != "SunoChainPreset":
    fail("not a Suno Chain preset")
if not isinstance(pget("eq.target_curve_db", None), list) or len(pr["eq"]["target_curve_db"]) != 26:
    fail("eq.target_curve_db must have 26 values")
flat = dict(
    eqAmount=pget("eq.amount", 1.0), targetCurve=pr["eq"]["target_curve_db"],
    eqMaxBoostDb=pget("eq.max_boost_db", 12.0), eqMaxCutDb=pget("eq.max_cut_db", 24.0), eqMaxBoostHighDb=pget("eq.max_boost_high_db", 15.0),
    targetCrestDb=pget("dynamics.target_crest_db", 9.0), targetSpreadDb=pget("dynamics.target_spread400_db", 5.5),
    levelerFactor=pget("dynamics.leveler_factor", 2.1), compTotalGrDb=pget("dynamics.default_total_gr_db", 6.0),
    comp2Ratio=pget("dynamics.comp2.ratio", 4.0), comp2AttackMs=pget("dynamics.comp2.attack_ms", 30.0), comp2ReleaseMs=pget("dynamics.comp2.release_ms", 300.0),
    satDriveDb=pget("saturation.drive_db", 6.0), satMix=pget("saturation.mix", 0.15),
    reverbOn=pget("reverb.on", True), reverbMode=1 if pget("reverb.black", False) else 0,
    predelayMs=pget("reverb.predelay_ms", 205.0), rt60=pget("reverb.rt60_s", 4.64), rt60LowMul=pget("reverb.rt60_low_mul", 1.0),
    rt60HighMul=pget("reverb.rt60_high_mul", 0.7), crossoverHz=pget("reverb.crossover_hz", 1500.0), size=pget("reverb.size", 1.0),
    modDepth=min(0.5, pget("reverb.mod_depth", 0.5)), diffusion=pget("reverb.diffusion", 0.75), diffusionSize=pget("reverb.diffusion_size", 2.0),
    wetHpf=pget("reverb.hpf_hz", 400.0), wetLpf=pget("reverb.lpf_hz", 14000.0), wetBumpHz=pget("reverb.bump_hz", 3150.0), wetBumpDb=pget("reverb.bump_db", 4.5),
    width=pget("reverb.width", 0.88), reverbLevelDb=pget("reverb.level_db", -5.5), revBandDb=pget("reverb.band_width_db", [0, 0, 0]),
    duckDb=pget("ducking.depth_db", 12.3), duckReleaseMs=pget("ducking.release_ms", 338.0), duckAttackMs=pget("ducking.attack_ms", 5.0),
    delayOn=bool(pget("delay.on", False)) and (pget("delay.detected", False) or pget("delay.user_set", False)),
    widthOn=pget("width_layer.on", True), widthLevelDb=pget("width_layer.level_db", -28.5), widthTiltDb=pget("width_layer.tilt_db", 4.4),
    widthMotion=pget("width_layer.motion", 0.0), layerBandDb=pget("width_layer.band_db", [0, 0, 0]),
    xoverLowHz=pget("multiband_width.xover_low_hz", 300.0), xoverHighHz=pget("multiband_width.xover_high_hz", 4000.0),
    amount=pget("user.amount", 100.0) / 100.0, compAmount=pget("user.compression", 100.0) / 100.0, deessAmount=pget("user.deess", 100.0) / 100.0,
    eqLowWeight=pget("user.eq_low", 35.0) / 100.0, excAmount=pget("user.harmonics", 100.0) / 100.0, airDb=pget("user.air_db", 5.0))
sib = pget("deesser.sib_curve_db", None)
if isinstance(sib, list):
    if len(sib) != 10:
        fail("deesser.sib_curve_db must have 10 values")
    flat["sibTarget"] = sib
elif "freq_hz" in pr.get("deesser", {}):                       # migration: version 1-2 presets
    flat["deessFreq"] = pr["deesser"]["freq_hz"]
    flat["deessTargetDb"] = pr["deesser"].get("target_ratio_db", 0.0)
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
    cli = os.environ.get("RENDER_CLI") or next((c for c in (os.path.join(here, "..", "build", "bin", "render_cli"), os.path.join(here, "..", "build", "bin", "Release", "render_cli.exe"),
                                                              os.path.join(here, "..", "build", "bin", "render_cli.exe"), os.path.join(here, "..", "build_cli", "render_cli")) if os.path.exists(c)), None)
    if cli is None:
        fail("render_cli not found: build it (cmake --build build --target render_cli) or set RENDER_CLI")
    r = subprocess.run([cli, pf, os.path.join(t, "in.f32"), os.path.join(t, "out.f32"), str(SR), "learn"], capture_output=True, text=True)
    print(r.stderr.strip())
    if r.returncode != 0 or not os.path.exists(os.path.join(t, "out.f32")):
        fail("render_cli failed (exit %d)" % r.returncode)
    y = np.fromfile(os.path.join(t, "out.f32"), dtype=np.float32).reshape(-1, 2).astype(np.float64)

tgt = np.array(pr["eq"]["target_curve_db"])
def report(name, sig):
    mm = sa.Measure(sig); c = mm.target_curve(); d = mm.dynamics()
    err = c - tgt; sel = np.array(sa.CENTERS) >= 300
    print(f"{name:8s} curve err vs Suno (>=300Hz) rms {np.sqrt(np.mean(err[sel]**2)):.1f} dB | crest {d['crest_db']:.1f} (Suno {flat['targetCrestDb']}) | spread400 {d['spread400_db']:.1f} (Suno {flat['targetSpreadDb']}) | level {d['level_dbfs']:.1f}")
    return mm
report("user in", U.astype(np.float64)); mo = report("output", y)
o = mo.offset_stats() or {}
print("output wet: arrival", mo.wet_arrival_ms(), "| rise", round(o.get('rise_db', 0), 1), "| wet re dry", round(o.get('wet_re_dry_db', 0), 1),
      "| width", round(mo.width_db() or 0, 1), "| decay", (mo.gap_decay() or {}).get('slope_db_s'))
print("Suno      : arrival", pget("measurements.suno.wet_arrival_ms", "?"), "| rise", pget("measurements.suno.wet_rise_after_vocal_db", "?"),
      "| wet re dry", pget("measurements.suno.wet_peak_re_dry_db", "?"), "| width", pget("measurements.suno.side_minus_mid_in_gaps_db", "?"),
      "| decay", pget("measurements.suno.gap_decay.slope_db_s", "?"))
peak = 20 * np.log10(np.abs(y).max() + 1e-12)
print("peak out dBFS", round(peak, 1), "nan?", bool(np.isnan(y).any()))
sa_out = os.environ.get("SAVE")
if sa_out:
    import wave
    for nm, sig in (("user_in", U), ("processed", y)):
        z = (np.clip(sig, -1, 1) * 32767).astype("<i2")
        with wave.open(os.path.join(sa_out, nm + ".wav"), "wb") as w: w.setnchannels(2); w.setsampwidth(2); w.setframerate(SR); w.writeframes(z.tobytes())

if not np.isfinite(y).all():
    fail("output contains NaN / Inf")
if peak > float(os.environ.get("MAX_PEAK_DB", "0.5")):
    fail("output peak %.1f dBFS is above the limit" % peak)
if len(y) != len(U):
    fail("output length differs from the input")
print("PASS")
