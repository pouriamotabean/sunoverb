"""Ear calibration for analyzer presets (v1.10).

The analyzer matches measurable targets of the Suno stem. Pouria's own edit of the averaged preset
(POURIA_SUNO_VERB, Oct 2026) showed where those measurements land wrong by ear, consistently across
all stems: reverb ~6 dB too loud, too little ducking with too fast a release, decay too long, reverb
low end too muddy, built-in saturation unwanted. This applies those corrections, plus a low-end floor
so a stem that had a steep high-pass (thin, "telephone") is not copied as a target.

usage: python3 calibrate_preset.py in.json out.json
"""
import json, sys

CAL = dict(reverb_level_db=-6.4, duck_add_db=3.9, duck_release_mul=4.7, duck_release_max_ms=600.0,
           rt60_mul=0.8, reverb_width_mul=1.23, reverb_hpf_min_hz=300.0, saturation_mix=0.0)
# target curve below 200 Hz may not fall further below the 200 Hz band than a full male voice does
# (Suno Lead 01 / Male 05 shape): offsets in dB re 200 Hz
LOW_FLOOR = {160: -3.0, 125: -9.0, 100: -12.0, 80: -14.0, 63: -18.0, 50: -20.0}


# v1.13 (measured on Pouria's takes against the 4 Suno stems' width-over-time and tail-tone tables, and his
# Blackhole/sidechain reference): more reverb under the voice and less bloom after phrases, vocal layer wider in
# the low/mid bands, reverb side narrower in the mid/high bands. Relative to the v1.10 calibrated values.
CAL2 = dict(reverb_level_db=-3.1, duck_db=-4.4, layer_level_db=+5.1, layer_band_mid_db=7.0, layer_band_high_db=1.5,
            rev_band_mid_add_db=-3.0, rev_band_high_add_db=-9.3)


def calibrate_v113(p):
    r, d, w = p["reverb"], p["ducking"], p["width_layer"]
    r["level_db"] = round(r["level_db"] + CAL2["reverb_level_db"], 2)
    d["depth_db"] = round(max(0.0, d["depth_db"] + CAL2["duck_db"]), 2)
    w["level_db"] = round(w["level_db"] + CAL2["layer_level_db"], 2)
    w["band_db"] = [w["band_db"][0], CAL2["layer_band_mid_db"], CAL2["layer_band_high_db"]]
    b = r["band_width_db"]; r["band_width_db"] = [b[0], round(b[1] + CAL2["rev_band_mid_add_db"], 2), round(max(-40.0, b[2] + CAL2["rev_band_high_add_db"]), 2)]
    p.setdefault("calibration", {})["v1_13"] = CAL2
    return p


def calibrate(p):
    r, d = p["reverb"], p["ducking"]
    before = dict(level=r["level_db"], rt60=r["rt60_s"], hpf=r["hpf_hz"], width=r["width"], duck=d["depth_db"], rel=d["release_ms"])
    r["level_db"] = round(r["level_db"] + CAL["reverb_level_db"], 2)
    r["rt60_s"] = round(r["rt60_s"] * CAL["rt60_mul"], 2)
    r["hpf_hz"] = max(r["hpf_hz"], CAL["reverb_hpf_min_hz"])
    r["width"] = round(min(1.6, r["width"] * CAL["reverb_width_mul"]), 3)
    d["depth_db"] = round(min(24.0, d["depth_db"] + CAL["duck_add_db"]), 2)
    d["release_ms"] = round(min(CAL["duck_release_max_ms"], max(d["release_ms"], d["release_ms"] * CAL["duck_release_mul"])), 1)
    p["saturation"]["mix"] = CAL["saturation_mix"]
    hz, c = p["eq"]["target_curve_hz"], p["eq"]["target_curve_db"]
    ref = c[hz.index(200)]
    raised = {}
    for f, off in LOW_FLOOR.items():
        i = hz.index(f)
        if c[i] < ref + off: raised[f] = round(ref + off - c[i], 1); c[i] = round(ref + off, 2)
    p["calibration"] = {"method": "ear calibration from POURIA_SUNO_VERB", "values": CAL, "low_floor_raised_db": raised,
                        "before": before}
    return p


if __name__ == "__main__":
    p = json.load(open(sys.argv[1]))
    if len(sys.argv) > 3 and sys.argv[3] == "v113": p = calibrate_v113(p)
    else: p = calibrate_v113(calibrate(p))
    json.dump(p, open(sys.argv[2], "w"), indent=1)
    print(sys.argv[2], "ok")
