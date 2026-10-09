"""Evaluate the chain on a real dry vocal: phrase-level spread, onset overshoot, width, EQ error.
usage: eval_vocal.py dry.(wav|mp3) preset.json [key=value ...overrides]"""
import sys, os, json, subprocess, tempfile, importlib.util
import numpy as np
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sa", os.path.join(here, "suno_analyze.py")); sa = importlib.util.module_from_spec(spec); spec.loader.exec_module(sa)
SR = sa.SR

def preset_to_flat(pr):
    r, dk, w, dl = pr["reverb"], pr["ducking"], pr.get("width_layer", {}), pr["delay"]
    extra = dict(modDepth=r.get("mod_depth", 0.5), diffusion=r.get("diffusion", 0.0), diffusionSize=r.get("diffusion_size", 1.0), earlyLevelDb=r.get("early_level_db", -8.0))
    f = dict(targetCurve=pr["eq"]["target_curve_db"], eqAmount=pr["eq"]["amount"], targetCrestDb=pr["dynamics"]["target_crest_db"],
             targetSpreadDb=pr["dynamics"]["target_spread400_db"], comp2Ratio=pr["dynamics"]["comp2"]["ratio"],
             comp2AttackMs=pr["dynamics"]["comp2"]["attack_ms"], comp2ReleaseMs=pr["dynamics"]["comp2"]["release_ms"],
             deessFreq=pr["deesser"]["freq_hz"], deessTargetDb=pr["deesser"]["target_ratio_db"],
             satDriveDb=pr["saturation"]["drive_db"], satMix=pr["saturation"]["mix"],
             predelayMs=r["predelay_ms"], rt60=r["rt60_s"], rt60LowMul=r["rt60_low_mul"], rt60HighMul=r["rt60_high_mul"],
             wetHpf=r["hpf_hz"], wetLpf=r["lpf_hz"], wetBumpHz=r["bump_hz"], wetBumpDb=r["bump_db"], width=r["width"],
             reverbLevelDb=r["level_db"], duckDb=dk["depth_db"], duckReleaseMs=dk["release_ms"],
             delayOn=dl["on"], delayLevelDb=dl["level_db"], delayFeedback=dl["feedback"])
    f.update(extra)
    if "leveler_factor" in pr["dynamics"]: f["levelerFactor"] = pr["dynamics"]["leveler_factor"]
    if "sync" in dl: f.update(delaySync=dl["sync"], delayNote=dl["note"], delayToReverb=dl["to_reverb"])
    if w: f.update(widthOn=w["on"], widthLevelDb=w["level_db"], widthDelayMs=w["delay_ms"], widthShiftHz=w["shift_hz"], widthTiltDb=w["tilt_db"],
                   widthHpf=w["hpf_hz"], widthLpf=w["lpf_hz"], widthBloomDb=w["bloom_db"], widthBloomMs=w["bloom_ms"])
    return f

def render(flat, x, learn=True):
    t = tempfile.mkdtemp()
    with open(t + "/p.txt", "w") as fh:
        for k, v in flat.items():
            if isinstance(v, list): v = ",".join(map(str, v))
            elif isinstance(v, bool): v = int(v)
            fh.write(f"{k}={v}\n")
    x.astype(np.float32).tofile(t + "/i.f32")
    r = subprocess.run([os.path.join(here, "..", "build_cli", "render_cli"), t + "/p.txt", t + "/i.f32", t + "/o.f32", str(SR)] + (["learn"] if learn else []),
                       capture_output=True, text=True, check=True)
    return np.fromfile(t + "/o.f32", dtype=np.float32).reshape(-1, 2).astype(np.float64), r.stderr.strip()

def overshoot(dry, out):
    ed = sa.env5((dry[:, 0] + dry[:, 1]) / 2); eo = sa.env5((out[:, 0] + out[:, 1]) / 2)
    n = min(len(ed), len(eo)); ed, eo = ed[:n], eo[:n]
    top = np.percentile(ed, 95); v = ed > top - 20
    starts = [i for i in range(20, n - 60) if v[i] and not v[i - 8:i].any()]
    ov = [(eo[i + 3] - ed[i + 3]) - np.median(eo[i + 40:i + 60] - ed[i + 40:i + 60]) for i in starts if v[i + 40:i + 60].all()]
    return (np.median(ov), np.percentile(ov, 75), len(ov)) if ov else (np.nan, np.nan, 0)

def evaluate(dry, pr, overrides=None, verbose=True):
    flat = preset_to_flat(pr); flat.update(overrides or {})
    full, log = render(flat, dry)
    clean, _ = render(dict(flat, reverbOn=0, delayOn=0, widthOn=0), dry)
    m = sa.Measure(full); mc = sa.Measure(clean)
    tgt = np.array(pr["eq"]["target_curve_db"]); c = mc.target_curve(); sel = np.array(sa.CENTERS) >= 150
    res = dict(spread_full=m.dynamics()["spread400_db"], spread_dry=mc.dynamics()["spread400_db"],
               overshoot=overshoot(dry, clean), side_mid_singing=float(np.median((m.es - m.em)[m.sing])),
               eq_err=float(np.sqrt(np.mean((c - tgt)[sel] ** 2))), mono_diff_db=None, log=log)
    # mono compatibility: mono sum of full output vs mono sum with width layer off
    nowidth, _ = render(dict(flat, widthOn=0), dry)
    mf, mn = full.mean(1), nowidth.mean(1); k = min(len(mf), len(mn))
    res["mono_diff_db"] = float(20 * np.log10(np.std(mf[:k] - mn[:k]) / np.std(mn[:k]) + 1e-12))
    return res, full

if __name__ == "__main__":
    dry = sa.load(sys.argv[1]); pr = json.load(open(sys.argv[2]))
    ov = {}
    for a in sys.argv[3:]:
        k, v = a.split("="); ov[k] = float(v)
    res, _ = evaluate(dry, pr, ov)
    print(res["log"].splitlines()[0] if res["log"] else "")
    print(f"spread full {res['spread_full']:.2f} (Suno {pr['dynamics']['target_spread400_db']}) | spread dry-path {res['spread_dry']:.2f} | "
          f"onset overshoot median {res['overshoot'][0]:+.1f} dB p75 {res['overshoot'][1]:+.1f} (n={res['overshoot'][2]}) | "
          f"side-mid singing {res['side_mid_singing']:.1f} | EQ err {res['eq_err']:.1f} dB | width layer in mono {res['mono_diff_db']:.0f} dB")
