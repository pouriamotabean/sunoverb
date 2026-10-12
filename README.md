# Suno Chain v1.15

A VST3 plugin that processes a dry vocal to sound like a Suno vocal. It works without a preset, using the built-in "Suno Lead 01". Each Suno vocal you analyze becomes a new `.json` preset.

## Build
1. Replace the `Source/` folder in the repo with the one from this zip (`CMakeLists.txt` and `.github/workflows/build.yml` have not changed since the VS2026 fix; replacing everything is fine too).
2. Push → **Actions** → when it's green, download **SunoChain-VST3** → copy it to `C:\Program Files\Common Files\VST3` → rescan in Cubase.
3. If it's red, send me the log.

## New in v1.15 (FREEZE)
- **FREEZE** (glass button under SPACE, automatable): the reverb tail holds forever and nothing new enters it
  (SUNO and BLACK). Lossless while frozen (modulation parks on whole-sample delays): level constant within 0.1 dB
  over 28 s, singing during the freeze does not enter, release decays normally, no click on/off. Not saved in
  presets (it is a performance control); the host is told a 60 s tail while frozen.

## New in v1.14 (BLACK reverb + new controls)
- **BLACK reverb** (lever next to SPACE, up = BLACK): 16-line FDN (60-210 ms lines, an allpass in every line),
  a slow swell path, gentle per-line drift (< 3 cents), L/R from orthogonal line mixes. Measured against Pouria's
  Blackhole (Size 90, Gravity 12, clap + vocal solo): RT60 6.2 s (Blackhole 6.3), tail opens to L/R correlation
  0.09 (same), less low-mid ringing than SUNO, colour between SUNO and raw Blackhole. DECAY knob = x1.8, the reverb
  width-per-band knobs are SUNO-only (dimmed in BLACK). Same loudness as SUNO while singing. SUNO mode unchanged
  (bit-identical renders). Saved as reverb.black in presets.
- **Presets renamed and re-edited**: Suno Male (neutral), Black Male (BLACK, stronger ducking), Dreamy Male (BLACK,
  longer, echo into the reverb, wider), Blue Male (close, clean, short reverb, no echo), Velvet Male (warm, less air,
  more saturation), Radio Male (band-limited colour + short echo), Pouria Signature, Suno Lead Reference (default).
  Male 03 dropped (too close to the average). Old preset files are not loaded any more by the arrows if removed from
  the preset folder.
- **Factory presets built in**: the 8 presets travel inside the plugin and are written into
  Documents/SunoChain/Presets when a file of that name is missing (your own files are never overwritten), so the
  .vst3 alone is enough to give the plugin to someone.
- **Audit fixes**: the host is told the real tail (decay x1.8 in BLACK + predelay + echo, was a fixed 10 s);
  switching SUNO/BLACK fades the old tail out at its own level (was cut); presets are fully checked before anything
  is applied (EQ 26 values, S/Z 10, widths 3, numbers finite, newer versions refused with a message); FFT without
  the slow std::complex multiply (same results, less CPU in the meters / Learn); version 1.14.0 everywhere.
- **GUI**: no status screen (messages show as a quiet line for a few seconds); "by Pouria Motabean" under the logo.
- **New controls** (design/assets/v114, cut from the ChatGPT images): flat metal LOAD / SAVE / LEARN / CLEAR,
  rotary LISTEN selector (ALL / WET / WIDTH), glass MATCH LOUDNESS button (lit = gain held, blinks while
  measuring), ADVANCED slide switch.

## New in v1.13.3 (rider)
- **Rider**: 0-100 % unchanged; above 100 % it really rides (200 % = +/-6 dB, 80 % of the way): phrase-level
  spread 6.8 dB off -> 6.2 (100 %) -> 4.7 (200 %); tone unchanged. Everything else = v1.13.1.
- The "z" in «روز خوش» (d112, ~12.5 s) is parked: S/Z variants (per-event, one-fraction rule, full take-back) did
  not fix it by ear and 200 % damaged s/sh. Its voiced buzz carries a very pure 880 Hz harmonic (+30 dB over its
  surroundings, also in the dry, likely Auto-Tune), heard as ringing once the take is louder. To be re-checked on
  other vocals at the end before any fix (candidate: a resonance suppressor for sharp tonal peaks).

## New in v1.13.1 (the three open points, found and fixed)
- **Consonant detector bug**: it ran AFTER the Match EQ. The EQ's high lift made 34 % of vowels look like consonants, so the compressor, the rider and the crisp were frozen for ~40 % of the singing. Now it runs on the voice before the EQ (7.7 % frozen, i.e. real consonants only). Syllable-level jumpiness 2.13 -> 1.98 dB (Suno 1.54; the rest comes from the Match EQ's low cut, which makes the level follow the pitch a little more).
- **AIR and the S/Z**: the S/Z Match did not know about the AIR shelf, so AIR +5 made weakly detected sibilants up to 3 dB brighter. It now includes AIR in what it takes back. Overall "s" level -5.5 dB (v1.12/1.13 with AIR: -4.4, v1.9: -4.9): about 1 dB more de-essing, as you asked; never darker than you sang it. The loud "z" at 0:13 is 2.6 dB lower than in v1.9.
- **Note-onset narrowing of the highs works now**: v1.13 subtracted a high-passed copy, whose phase shift ADDED energy. Now the layer's own top band is turned down for ~20 ms at each new note. High-band width at note starts vs Suno: +3.2 -> +1.5 dB; everything else within ~2 dB.

## New in v1.13 (sound round: rider, reverb, width, delay)
- **Vocal RIDER** (Advanced, the old "Leveler" knob): rides words/phrases toward your typical level, +/-2 dB at 100 % (+/-4 at 200 %, **0 = off**), slow (~300 ms), measured **20 ms ahead** so it is never late, held in pauses and on consonants. It replaces the leveler, which made phrases too flat: phrase-level movement is now 3.8 dB (v1.12: 2.8, Suno 4.8, your dry 6.3). The GR meter also shows the rider's cuts. Plugin latency is now 20 ms (Cubase compensates).
- **Reverb tone**: 250 Hz-1 kHz -5 dB (it was muddy) and +10 dB air shelf from 5 kHz (it was 5-7 dB darker than Suno). Tail tone now within ~2 dB of Suno above 500 Hz.
- **Reverb + width** (presets): more reverb under the voice and less bloom after phrases (ducking 6.4 -> 2 dB, level -3 dB), vocal layer +5 dB with the mid band opened, reverb side narrower in mid/high. Width over time vs Suno (singing / phrase tail, low/mid/high): within ~+/-2 dB (v1.12 was 5-7 dB off). Your Blackhole reference measures about the same.
- **Delay** (presets): ping-pong ~70 % L/R, 1/8 dotted, 25 % feedback, 300 Hz-5 kHz, -20 dB, ducked with the reverb, 50 % into the reverb.
- **SATURATION** 100 % = the old 130 %. **AIR** default +5 dB. **S/Z** +12 %.
- Real-plugin check on your takes: consonants -0.8 to +1.8 dB vs v1.9 (the two quiet ones are brighter because of AIR +5, which you asked for).

## New in v1.12 (OTT-style crisp, more air, typed values)
- **SATURATION is now an OTT-style "crisp"** (your v1.11 note: the exciter distorted at 100 %, sweet spot ~25 %, and you wanted OTT-like). The voice is split at 2.5 kHz; each band is pulled toward its own typical level: quiet top detail up (up to +7 dB), peaks slightly down, the body only gently. Nothing new is generated, so no distortion. Level-normalised, untouched in pauses and on every consonant (s/z/sh/ch/j: the gain is back to 0 within ~3 ms). 100 % = soft (quiet top detail +1.5 dB); turn it up for more.
- **Match EQ air**: boosts from 8 kHz up are x1.5 (ramping from 4 kHz), so 100 % now gives what you heard at 150 % on top, without raising the mids.
- **AIR** (Advanced) is now a plain high shelf at 10 kHz (+/-12 dB).
- **Type a value**: click the text under any knob, type ("300", "-6", "12k"), Enter. Esc cancels; double-click the knob = default.
- Real-plugin check on your takes: consonants -1.3 to +0.3 dB vs v1.9 (the voiced "z" +0.1).

## New in v1.11 (harmonic saturation, stage 2)
- **SATURATION is now a harmonic exciter** (the old tanh saturation is gone). Measured on the 4 Suno vocals vs your voice after the chain: Suno's vowels have ~2 dB more *harmonic* energy at 4-8 kHz and ~4 dB more air at 10-16 kHz. Two bands of your voice (2-4.5 kHz and 5-8 kHz) go through Chebyshev shapers that add the octave and octave+fifth of what goes in, level-normalised (same amount loud or soft), gated off on s/z/sh/ch and breaths. At 100 % it lands on Suno's average: air -19.2 dB (Suno -19.1), harmonic ratio 4-8 kHz 6.9 dB (Suno 7.0), 8-12 kHz 4.7 (4.8).
- **Advanced > AIR** (+/-12 dB): more or less of the top band only. (Replaces Sat Drive.)
- **Key selection**: not needed for this design. A sung note is periodic, so shaping it can only create harmonics of that same note (the natural harmonic series of the voice), never notes outside the key. A key selector only matters if notes are added that are not harmonics (harmonizer / pitch-shift), which we do not do.
- Checked with the real plugin on your takes: consonants +0.2 to +0.7 dB vs v1.10 (never darker).

## New in v1.10 (LISTEN + ear-calibrated presets)
- **LISTEN** (bottom bar, click to step): ALL = normal; **WET** = only reverb + echo, *after* the ducking (the main line removed, so you hear exactly what the space adds); **WIDTH** = only the width layer. Blinks while soloing; never saved with the session or preset.
- **New presets**, calibrated by ear from your edit (POURIA SUNO VERB): reverb -6.4 dB, ducking +3.9 dB with a slower release, decay x0.8, reverb low cut at least 300 Hz, built-in saturation off, and a low-end floor so a Suno stem with a steep high-pass is not copied (that made Male 02 sound like a telephone). Includes **POURIA SUNO VERB 2** = your preset with the reverb HPF at 300 Hz. Tool: `tools/calibrate_preset.py`.
- **Match Loudness** never pushes peaks over -1 dBFS (the new Suno stems are mastered ~6 dB louder).
- Every release is now checked with the real plugin code on your takes (44.1 kHz, 2x Learn) before delivery; consonant levels may not drop more than 1.5 dB against the previous version.

## New in v1.9 (S/Z fix)
- **The v1.8 "sibilant ceiling" is switched off.** In the plugin it pulled s/sh/ch 3-10 dB down (the 2-5 kHz body of "sh/ch" up to 9 dB), so words lost their consonants. The voiced-z detector from v1.8 is off too. S/Z is back to the v1.7 behaviour you found fine.
- **New safety rule for S/Z Match**: it may only take back what the Match EQ added. A sibilant can end up as bright as Suno's, but **never darker than you sang it** (the old allowance of 3 dB below your raw "s" is gone).
- Match Loudness (v1.8) stays.

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
