# Suno Chain v1.1

A VST3 plugin that processes a dry vocal to sound like a Suno vocal. It works without a preset, using the built-in "Suno Lead 01". Each Suno vocal you analyze becomes a new `.json` preset.

## Build
1. Replace all the files in the repo with the files from this zip: `Source/`, `CMakeLists.txt`, `.github/workflows/build.yml`.
2. Push → **Actions** → when it's green, download **SunoChain-VST3** → copy it to `C:\Program Files\Common Files\VST3` → rescan in Cubase.
3. If it's red, send me the log.

## Usage
1. Put the plugin **after Auto-Tune** on the vocal track.
2. **Learn My Voice** → play 10–60 s of your vocal → **Stop**.
3. **Load** → pick a preset; presets live in `Documents\SunoChain\Presets`. Put `presets/Suno_Lead_01.json` there.
4. **Save** saves everything (knobs plus the preset) as a new preset. Your learned voice is stored with the Cubase project.

## Sections
| Section | Knobs |
|---|---|
| Tone & Dynamics | Match EQ, Low Boost (low-end boost only, 120–300 Hz; below 120 Hz the EQ never boosts), Compression (leveler and compressor), De-ess, Saturation |
| Space (Reverb) | Level, Pre-delay, Decay, Width, Ducking, Duck Release, HPF/LPF |
| Vocal Width | Width (level of the width layer), Tone (high-frequency tilt), Held Notes (0 = constant like Suno; 100% = width only on held notes) |
| Echo (Delay) | Sync (follows the project tempo) with Note (default 1/8), or Time when Sync is off; Level, Feedback, > Reverb (how much of the echo goes into the reverb) |
| Meters | IN/OUT, COMP and LEVEL (gain reduction of each compressor), DUCK (reverb ducking), WIDTH (width-layer level relative to the vocal) |

**Vocal Width** only lives in the Side channel. On mono systems (phones, weak speakers) it disappears completely and the vocal is untouched, so no phasing.
After real silence (the start of a section) the vocal stays mono for about 200 ms and then the width opens; after a short breath it barely changes. That's the behavior measured in Suno.
To make some sections (e.g. the chorus) wider than others, automate **Vocal Width** in Cubase.

**Delay** is only changed by a preset if the Suno vocal actually had an echo, or if it's a preset you saved yourself. Otherwise your own delay settings stay as they are.

## New preset from each Suno vocal
Upload the Suno vocal; I run `tools/suno_analyze.py` and you get a `.json` back. The analyzer:
- Measures EQ, dynamics, de-ess, reverb, ducking (short and long pauses separately), width while singing and in pauses, and the width layer (level, band, phase rotation, bloom).
- Renders a dry version through this exact DSP (`tools/render_cli.cpp`) and corrects the parameters until the numbers match the Suno vocal, in two stages: reverb first, then width.
- Use `tools/eval_vocal.py` to check the result on your own dry vocal.
