# Suno Chain – Roadmap

Current version: **v2.0.4** (female presets, SIBILANT GUARD). v2.0.3: Windows installer + releases. Before: **v2.0.2** (Oct 22, 2026: EXACT Match EQ solver; v2.0.1: technical review P0 + P1, see README and TESTING.md). Author: Pouria Motabean.

## 1. Next steps (in order)
1. **Pouria tests v2.0** on his own and other vocals; all sound / GUI issues reported at once and fixed in one release.
2. **More factory presets**: Pouria builds presets (including female-vocal presets) and they are embedded in
   `Source/FactoryPresets.h` next to the current eight.
3. ~~Windows installer~~ done in v2.0.3 (Releases page).
4. **v2.1 – LEARN SUNO**: build a preset inside the plugin from a Suno vocal.
   - Where: a LEARN SUNO button next to LEARN, or drag a Suno stem (from Windows or Cubase) onto the plugin.
   - How: analysis on a background thread (playback never stops), progress bar, a summary of what was learned with a
     confidence value per section and a warning for dirty stems (instruments, backing vocals); the result is saved
     as a new preset next to the others and applied at once.
   - Input: a clean solo Suno vocal stem (no instruments).
   - Light version first: Match EQ, S/Z, loudness, compression (reverb and width stay from the chosen preset).
     Full version later: reverb, width, dirty-stem cleaning with a colored waveform (see section 4).

## 2. Open issues (must fix)
- **EXACT EQ by ear**: Pouria A/Bs the factory presets (LEGACY vs EXACT); if the air / 5 kHz is now too bright,
  re-tune eqAirMul / the max high boost by ear with EXACT.
- ~~Female vocal: one harsh "sh"~~ addressed in v2.0.4 (female presets + SIBILANT GUARD; waiting for Pouria's ear).
- (was) **Female vocal: one harsh "sh"** (Pouria's first test on a female voice, v1.15.2; everything else was fine).
  Waiting for the dry + wet clip, the preset name and the timestamp. Suspects: the S/Z detector and de-esser bands
  fitted on male voices (a female "sh" sits higher) and the male preset's Match EQ lifting that region.
  The fix must be general (any voice), not a patch for this one "sh".
- **Parked: the "z" of d112 at ~12.5 s** (a pure 880 Hz harmonic in the voiced buzz, present in the dry too; possibly
  the singer's pronunciation). Re-test on several other vocals. Already tried and rejected: per-event S/Z,
  one-fraction S/Z, full take-back (200 % damages s/sh).

## 3. Rules for every change
- Validate with the real plugin code (harness) on real takes before delivery; regression against the previous
  version (SUNO mode bit-identical unless the change is meant to alter it).
- No consonant may get more than 1.5 dB darker without a reason.
- General solutions only, no per-sound patches. Build and deliver only when Pouria approves.

## 4. Ideas backlog (accepted, not scheduled)
- **Rhythmic gate on the reverb**: tempo-synced from the host; 8 step patterns (up to 16 steps), rates 1/4-1/64
  incl. triplet and dotted, attack/decay/sustain/release, mix, randomize; off by default (Advanced). Low priority.
- **A/B with the Suno vocal itself**: load the Suno stem in the plugin, one button to switch, loudness-matched.
- **Suno match indicator**: live numbers for EQ, width, dynamics, S/Z and reverb (how close the output is to Suno).
- **Verse / Chorus mode**: two full snapshots and an automatable Morph knob.
- **Delay throw**: delay only on phrase ends.
- **Smooth**: gentle dynamic EQ for momentary resonances.
- **Phone / mono listen**: monitoring only.
- **Low-latency monitoring mode** for recording (lookahead and heavy parts off temporarily).
- **Auto + manual model for every control**: learned value shown as a marker on the knob, manual tweaks survive a
  re-Learn, double-click resets to the learned value.
- **Width envelope** closer to Suno: highs ~4.5 dB narrower in the first 50 ms of each note; low band 6-7 dB wider
  while singing; phrase tails open +7 dB low/mid at once, highs after ~150 ms.
- **Sound polish**: EQ error under 1.5 dB, syllable-level compression behaviour, per-section width, better reverb
  decay estimate.
- **Dirty-stem cleaning** (for the full LEARN SUNO): auto-detect and exclude instrument bleed, backing vocals and
  vocal FX; report seconds used / excluded and confidence per parameter; colored waveform (green clean, yellow
  backing, red bleed, violet FX) editable by dragging.
- Deferred: track roles (Lead / Double / Backing / Adlib).

## 4b. Technical review – remaining (P2)
- Split `SunoChainDSP.h` into one file per module (move only, regression before / after, no algorithm change).
- CHANGELOG.md / PRESET_SCHEMA.md as separate files; README: tested hosts, latency, known limits.
- Learn / Match Loudness still run their analysis FFTs on the audio thread while they are active (measurement
  mode only); could move to a worker like the WIDTH meter.

## 5. Dropped / rejected
- BLUE reverb (two algorithms are enough: SUNO + BLACK; plates and shimmer = Valhalla).
- Copper name plate on the GUI (replaced by the "by Pouria Motabean" byline).
- Status screen (LCD) on the GUI (too much; a small temporary status line instead).
- Key-aware harmonic exciter (shaping a sung note only yields its own harmonics, so key selection is pointless).
- Loudness relative to the beat (goal = reach Suno first, then personal taste).
- Saturation oversampling: not needed. Checked in v2.0: no waveshaper in the chain since v1.11 (tanh and exciter
  off, SATURATION = Crisp dynamics); a test tone shows nothing generated above the input (-80 dB).

## 6. Release history
- **v2.0.2**: EXACT Match EQ / S/Z solver (Pouria's report), LEGACY kept for older sessions and presets.
- **v2.0.1**: technical review P0 + P1: tests in CMake and CI (+ pluginval), legacy sessions keep their sound, exact
  project reopen, damaged-session safety, no half-loaded presets, Learn race fix, CPU peaks down; sound unchanged.
- **v2.0**: final release of the v1 line. Version 2.0.0, Persian user guide (GUIDE_FA.md), full test of all presets
  in SUNO and BLACK (no NaN, no clicks, peaks below 0 dBFS), aliasing check.
- **v1.15.2**: BREATH really works (refitted on Pouria's real breath; releases in 5 ms when the voice returns).
- **v1.15.1**: crash on plugin removal fixed (GUI images owned by the editor); BREATH control added.
- **v1.15**: FREEZE (reverb tail held forever, nothing new enters, no clicks).
- **v1.14**: BLACK reverb (16-line FDN fitted to Pouria's Blackhole), new controls and graphics, presets renamed and
  embedded in the plugin, preset validation, dynamic tail length, mode-switch fade, faster FFT.
- **v1.13.3**: the rider really rides above 100 % (S/Z = v1.13.1).
- **v1.13**: vocal rider (20 ms lookahead) replaces the leveler; reverb tone fixed; reverb / width / ducking fitted to
  Suno's width over time; safe delay defaults; saturation / air / S/Z defaults by ear.
- **v1.12**: OTT-style Crisp replaces the exciter as SATURATION; Match EQ air x1.5 above 8 kHz; AIR = shelf; typed
  values under the knobs.
- **v1.11**: harmonic exciter (SATURATION + AIR) fitted to the Suno average.
- **v1.10**: LISTEN (wet / width solo); ear-calibrated presets; Match Loudness peak cap; real-plugin regression
  before every delivery.
- **v1.9**: sibilant ceiling off (it ate s/sh/ch); S/Z never darker than the raw voice.
- **v1.8**: per-sibilant ceiling, voiced "z" detection, Match Loudness.
- **v1.7**: natural vocal compressor (holds on consonants and breaths, Attack knob); per-band S/Z Match.
- **v1.6**: GUI pass 2 (bigger text, Advanced as a side panel, value arcs on the small knobs).
- **v1.5**: reverb detune fix, level-based S/Z Match, closed-loop EQ in Learn, relative ducking, Advanced Dynamics,
  "by Pouria Motabean" branding.
- **v1.4**: premium GUI, first version.
- **v1.3**: S/Z Match, multiband width for reverb / width layer / echo, WIDTH graph, Advanced drawer.
- **v1.2**: S/Z balance, de-ess meter, compression calibrated on an uncompressed vocal.

## 7. After Suno Chain
- ScreenRec (one-button screen + master-audio recorder plugin) – its own project.
