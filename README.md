# Suno Chain v1.8

A VST3 plugin that processes a dry vocal to sound like a Suno vocal. It works without a preset, using the built-in "Suno Lead 01". Each Suno vocal you analyze becomes a new `.json` preset.

## Build
1. Replace the `Source/` folder in the repo with the one from this zip (`CMakeLists.txt` and `.github/workflows/build.yml` have not changed since the VS2026 fix; replacing everything is fine too).
2. Push → **Actions** → when it's green, download **SunoChain-VST3** → copy it to `C:\Program Files\Common Files\VST3` → rescan in Cubase.
3. If it's red, send me the log.

## New in v1.8 (sibilant ceiling + Match Loudness)
- **Sibilant ceiling**: S/Z Match treats all your "s/z" with one learned curve; now each single sibilant is also checked: if the chain raised its hiss (relative to the vowel before it) more than it raised your other sibilants (+1 dB), just that one is pulled back. Your "z" at 0:13 went from +3.1 dB to +0.5 dB; the overall "s" level stays at Suno's (-6.4). Shown on the S/Z meter.
- **MATCH LOUDNESS** (bottom bar, roadmap stage 3): press it, play the vocal (10 s or more, including a loud part), press STOP. It measures your output (ITU-R BS.1770) and sets the gain that brings it to the Suno vocal's loudness from the preset (Suno Lead 01: -19.4 LUFS), then **holds** it (no pumping). The button then shows the gain; it is also the **Loudness** knob in Advanced (double-click = 0 dB = off). Press again any time to re-measure.
- Detector for voiced "z / j": also looks at the hiss level against the vowel, not only the high-band share.

## New in v1.7 (natural compression + S/Z air)
- **New vocal compressor**, always on (Learn only sets the level it works at; the old design could switch itself off, which is why GR stayed at 0):
  - opto stage (~3:1 soft knee, 10 ms attack, two-stage release 150 ms / 1.2 s) + gentle peak catcher + a slow, light leveler;
  - **no look-ahead**: the gain never drops before a syllable starts, so ch/j heads are no longer eaten;
  - **holds its gain on every consonant and breath** (s, z, sh, ch, j, t, k, f ...): consonants pass at the gain of the vowel around them, so they neither jump up nor get squashed (measured on 4 of your takes: median +0.2 dB, 90 % within 0.8 dB; v1.6 was +1.2 / 2.7 dB);
  - the Punch onset shaper is removed (it made z/ch/j jump).
  - Typical gain reduction on your takes: ~1-2 dB on average, 4-5 dB on the loud notes, peaks about 7-8 dB.
- **Advanced > Attack** (was Punch): opto attack, 100 % = 10 ms. **Speed** now sets the release of both stages.
- **S/Z Match per band**: each band of your "s" is brought to Suno's own level there, and only where yours is louder. The 10 kHz+ air of the "s" now matches Suno within ~1 dB instead of 3-7 dB darker.

## New in v1.6 (GUI pass 2)
- **ADVANCED** opens a side module on the RIGHT (window 1680 x 800 instead of 1200 x 1166): Tone / Dynamics, Reverb, Vocal width, Echo stacked.
- Bigger text everywhere; LEARN shows a blinking red lamp and the seconds while recording.
- Every knob has a thin value arc that glows while you touch it; band widths, Output and Tone start from 0 dB.

## New in v1.5
- Reverb modulation 2.5 -> 0.5: the old value smeared the reverb pitch by about +/-20 cents (audible detune on tuned vocals); now about +/-2.
- S/Z Match matches the overall level of your "s/z/sh" (relative to the vowel) to Suno's; stable with few sibilants, never boosts, at most 3 dB darker than your raw "s".
- Closed-loop EQ: during Learn the plugin also listens to its own output and corrects what compression, saturation and reverb do to the tone. **Learn, then RE-LEARN once** (second pass refines).
- Air: the EQ may lift up to +15 dB from 6 kHz up (was 12).
- Punch: syllable onsets stand out like Suno's; leveler recalibrated.
- Relative ducking: the reverb also opens when a held note fades below its phrase level, not only after silence.
- Advanced > Dynamics: PEAK, LEVELER, SPEED, PUNCH (100 % = learned / preset).
- Width layer low band set from Suno's coherence (no audible beating in the lows).

## GUI (v1.4)
Image faceplate and knobs (made in ChatGPT, see `design/`), everything else drawn in code: glowing indicator lamps, curves, meters.
- Header: preset pill (click = load, < > = previous / next preset in the folder), LOAD, SAVE, LEARN (shows STOP while learning, RE-LEARN once learned), CLEAR.
- Glass display: **EQ** / **WIDTH** tabs. Meters: IN, GR (both compressors), S/Z, DUCK, OUT.
- Lamps next to SPACE / WIDTH / ECHO switch those sections on and off (knobs of a section that is off go dark).
- Hover or drag a knob to see its value; double-click resets it. Drag the bottom-right corner to resize (60–160 %).
- **ADVANCED +** opens a side module on the right with the rest of the controls.

## Usage
1. Insert chain: **mic corrective EQ → Auto-Tune → Suno Chain** (no compressor needed; Suno Chain does the compression).
   **Whenever the chain before the plugin changes, Learn again.**
2. **Learn My Voice** → play 20–60 s of your vocal (with some "s", "z", "sh" in it) → **Stop**. The status line says how many s/z sounds were learned; S/Z Match needs at least 3.
3. **Load** → pick a preset; presets live in `Documents\SunoChain\Presets`. Put `presets/Suno_Lead_01.json` there.
4. **Save** saves everything (knobs plus the preset) as a new preset. Your learned voice is stored with the Cubase project.

## Sections
| Section | Knobs |
|---|---|
| Tone & Dynamics | Match EQ, Low Boost (low-end boost only, 120–300 Hz; below 120 Hz the EQ never boosts), Compression (leveler and compressor), **S/Z Match** (gives your "s", "z", "sh" the color and level of Suno's; 100% = exactly Suno, 0% = only the vowel EQ), Saturation |
| Space (Reverb) | Level, Pre-delay, Decay, Width, Ducking, Duck Release, HPF/LPF |
| Vocal Width | Width (level of the width layer), Tone (high-frequency tilt), Held Notes (0 = constant like Suno; 100% = width only on held notes) |
| Echo (Delay) | Sync (follows the project tempo) with Note (default 1/8), or Time when Sync is off; Level, Feedback, > Reverb (how much of the echo goes into the reverb) |
| Advanced (button top right) | Width per band (Low < 300 Hz, Mid 300 Hz–4 kHz, High > 4 kHz) for the reverb, the vocal width layer and the echo. Side only: mono and the centre stay untouched. Values come from the preset. |
| Graph | **EQ** tab: Suno target, your voice, applied EQ. **WIDTH** tab: your output's width per band while singing and in the pauses (violet) next to Suno (orange line). Needs a few seconds of playback. |
| Meters | IN/OUT, COMP and LEVEL (gain reduction of each compressor), S / Z (how much S/Z Match cuts at its deepest band), DUCK (reverb ducking), WIDTH (width-layer level relative to the vocal) |

**S/Z Match** (v1.3) works like a second Match EQ that switches on only during "s", "z" and "sh": it compares the spectrum of your sibilants (from Learn) with Suno's (from the preset) and corrects the difference, on top of the vowel EQ. It detects the sibilants on your raw signal, before the EQ, so vowels are never touched, and it lets go within ~30 ms.

**Vocal Width** only lives in the Side channel. On mono systems (phones, weak speakers) it disappears completely and the vocal is untouched, so no phasing.
After real silence (the start of a section) the vocal stays mono for about 200 ms and then the width opens; after a short breath it barely changes. That's the behavior measured in Suno.
To make some sections (e.g. the chorus) wider than others, automate **Vocal Width** in Cubase.

**Delay** is only changed by a preset if the Suno vocal actually had an echo, or if it's a preset you saved yourself. Otherwise your own delay settings stay as they are.

## New preset from each Suno vocal
Upload the Suno vocal; I run `tools/suno_analyze.py` and you get a `.json` back. The analyzer:
- Measures EQ, dynamics, the sibilant spectrum (S/Z Match), reverb, ducking (short and long pauses separately), width while singing and in pauses **per band**, and the width layer (level, band, phase rotation, bloom).
- Renders a dry version through this exact DSP (`tools/render_cli.cpp`) and corrects the parameters until the numbers match the Suno vocal: reverb, reverb width per band (from the pauses), width layer (coherence), then width per band while singing, bloom and ducking.
- `--keep-character previous.json` keeps an approved reverb character (decay, tone, predelay) and fits only levels, ducking and width.
- Use `tools/eval_vocal.py` to check the result on your own dry vocal.
