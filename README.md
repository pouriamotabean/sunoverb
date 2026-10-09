# Suno Chain (v1.0)

A VST3 plugin that processes a dry vocal to sound like a Suno vocal (EQ, compression, de-ess, saturation, ducked reverb, delay), driven by presets measured from Suno vocal stems.

## Build (same workflow as our previous plugins)
1. Make a new GitHub repo and upload everything in this folder to the repo root (`CMakeLists.txt`, `.github/workflows/build.yml`, `Source/...`).
2. Push to `main`. Open **Actions** and wait for **Build VST3** to finish (the first run is slower because it downloads JUCE).
3. If it's green: **Artifacts → SunoChain-VST3** → unzip → copy `Suno Chain.vst3` to `C:\Program Files\Common Files\VST3` → rescan plugins in Cubase.
4. If it's red: download the log and send it to me; I'll send back a fix.

## Usage
1. Put **Suno Chain** on your vocal track as an insert.
2. **Load Preset** → pick a `.json` file (start with `presets/Suno_Lead_01.json`).
3. **Learn My Voice** → play 10–60 s of your dry vocal → **Stop**.
   The plugin measures your voice's spectrum, level and dynamics, then:
   - sets the **EQ** to the difference between your voice and the Suno target (the green line on the graph),
   - sets the **compressors** from the difference in transients (crest) and in phrase-to-phrase level spread.
4. Adjust to taste with **Amount** and the other knobs.

> Learn is stored with the project. Re-learn whenever you change mic, singer or recording space.

## Knobs
| Section | Knob | What it does |
|---|---|---|
| Master | Amount | Overall strength of the whole effect |
| | Output | Output level (input level is restored automatically) |
| Tone | Match EQ | How much of the Suno EQ to apply |
| | Low Match | Low-end matching below 300 Hz (low by default, because that region of the Suno target reflects the singer's pitch, not processing) |
| | Compression | Overall amount of both compressors |
| | De-ess | Only acts when the "s" sounds are brighter than in the Suno vocal |
| | Sat Drive / Mix | Saturation (couldn't be measured from the stem; preset default) |
| Space | Level | Reverb level relative to the dry vocal (before ducking) |
| | Pre-delay / Decay / Width | Pre-delay, tail length, stereo width |
| | Ducking / Duck Release | How much the reverb drops while you sing, and how fast it comes back |
| | HPF / LPF | Filters on the reverb |
| Echo | Level / Time / Feedback | Delay (off when no echo was found in the Suno vocal) |

## New preset from each Suno vocal
Upload the Suno vocal stem (WAV preferred) and I'll analyze it with `tools/suno_analyze.py`, which:
1. measures EQ, crest, phrase spread, de-ess, wet arrival, ducking, wet level, width, decay, reverb EQ and echo,
2. renders a dry version through this exact DSP (`tools/render_cli.cpp`), measures it the same way, and corrects the parameters until the render matches the Suno vocal (closed loop),
3. outputs a `.json` file to load with **Load Preset**.

## Limits
- **Decay** is estimated from the few long pauses in the vocal; accuracy is roughly ±20%.
- **Saturation** isn't measurable from a stem; its value is a default.
- The EQ target includes the Suno singer's voice; **Low Match** and **Match EQ** let you control how much of it to copy.
- Suno stems are probably made by source separation, and MP3 removes some highs. Prefer WAV.
