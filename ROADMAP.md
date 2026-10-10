# Suno Chain – Roadmap

**Done in v1.2:** S/Z balance (EQ no longer makes "s"/"z" ~10 dB louder), de-ess meter, compression calibrated on an uncompressed vocal.
**Done in v1.9:** sibilant ceiling off (it ate s/sh/ch), S/Z never darker than the raw voice. Open: the voiced "z" (+3 dB) needs a fix that is verified in the real plugin, not only offline.
**Done in v1.8:** per-sibilant ceiling (single loud s/z), voiced z detection, **Match Loudness (stage 3)**.
**Done in v1.7:** natural vocal compressor (always on, no look-ahead, holds on consonants/breaths, Punch removed, Attack knob), per-band S/Z Match (keeps the "s" air).
**Done in v1.5:** reverb detune fix, level-based S/Z Match, closed-loop EQ in Learn, +15 dB air limit, Punch + leveler recalibration, relative ducking, Advanced Dynamics (Peak/Leveler/Speed/Punch), branding "BY POURIA MOTABEAN".
**Done in v1.4:** premium GUI, first version (faceplate, knobs, pills from ChatGPT; lamps, curves, meters, glass in code; Advanced drawer as a second module; resizable; preset stepping).
**Done in v1.3:** S/Z Match (sibilant spectrum matched to Suno, detection before the EQ, no effect on vowels; replaces the v1.2 balancer that dulled the vowel after each "s"), multiband width for reverb / width layer / echo (LR4, side only) fitted per band from the Suno vocal, WIDTH graph (output vs Suno per band, singing and pauses), Advanced drawer (first version).


## Agreed order of stages
0. **Build and test v1.3**: build, Load the new preset, Learn again (20+ s, with s/z), listen; check the WIDTH tab.
1. **Test on 2–3 more Suno vocals** (WAV, different genres/singers): a preset per vocal + confidence report, fix the analyzer, averaged "Suno sound" preset, more accurate decay and 2–4 kHz width.
   **+ Cleaning dirty Suno stems** (built and tested in the Python analyzer now, moved into the plugin in stage 5):
   - Auto-detect and exclude: **instrument bleed** (energy without human voice structure; reverb in pauses only measured where the level drops continuously), **backing vocals** (several simultaneous pitches → out of EQ/width/dynamics), **vocal FX** like distortion/telephone (sections whose spectrum/distortion differs a lot from the rest → excluded, or their own "FX" preset).
   - Report: seconds used / excluded (bleed, backing, FX) + confidence per parameter.
   - In the plugin (stage 5): waveform with colored sections (green clean, yellow backing, red bleed, violet FX) that the user can change by dragging; analysis only on the green sections.
   - Optional for very dirty stems: separation with UVR before analysis (simple in the Python analyzer; in the plugin, cleaning with UVR beforehand is recommended).
   - For now: when sending a vocal, the user says which time ranges have backing/FX.
2. **Harmonic saturation**: pitch-tracked exciter, in-key harmonics, key selection (12 roots × major/minor/harmonic minor + Auto), Suno harmonic profile.
3. ~~**Learned makeup gain**: LUFS in the preset + Match Loudness (learn, then hold).~~ (done in v1.8)
4. **Sound polish**: EQ error under 1.5 dB, syllable-level compression, per-section width, reverb decay.
5. **In-plugin preset capture**: C++ analyzer, drag a file onto the plugin, progress bar, confidence indicator, module locks, A/B.
6. **Premium GUI**: style **chosen** → `design/reference_mockup.png` (minimal analog-luxury: matte graphite, thin walnut sides, black machined knobs with a thin amber line, violet only for Width, a single large AMOUNT knob, EQ display with amber/white curves + green fill, 3 slim meters IN/GR/OUT, labels in thin small caps, lots of empty space, ADVANCED button). Main layer: Match EQ, Compression, S/Z, Saturation | Level, Decay, Pre-delay, Ducking | Width, Tone, Held Notes | Level, Feedback, Note, Sync. Everything else goes in the **Advanced drawer**: the ADVANCED button extends the window downward (main view unchanged), with smaller, dimmer knobs under each column: Tone (Low Boost, Attack, Release, Ratio, Threshold, Makeup, Hold), Space (reverb Width, HPF, LPF, Duck Release), Width (Low/Mid/High, Bloom), Echo (free Time, > Reverb, Low/Mid/High width); Output also here; drawer open/closed is remembered. **Required realism details**: real walnut grain on the side cheeks with slight lacquer sheen; matte graphite with fine texture and soft light from above; analog screws in all four corners (slightly different slot angles); a little subtle imperfection (fine scratches, a hint of dust, edge wear) so it feels real but not dirty; static shadows baked into the background; knobs in 3 layers (static drop shadow, static metal body with its lighting, only the indicator line + glow rotates); **knob glow like a real lamp** drawn in code (bright core + soft halo spreading onto the metal); analyzer: curves and the green fill with soft gradient, halo and fade, + a glass reflection layer on top. Pixel dimensions are set by Claude from the reference image (the layout map comes before the ChatGPT prompts). Separate assets made in ChatGPT "matching the reference image": empty faceplate, knob (base + cap) in two sizes, empty EQ glass, meter housing, preset pill, sync button (on/off), logo; text, curves, arcs and meters drawn in code. Resizable, HiDPI.
7. **v2.0**: final tests on real projects, user guide.

**Branding:** "BY POURIA MOTABEAN" under the logo (done in v1.5).

## GUI pass 2 (agreed Oct 10) - done in v1.6
- **Bigger text everywhere**: LOAD/SAVE/LEARN/CLEAR ~15 px (all the same size), Learn timer amber + blinking dot, knob labels and section titles +20-25 %, drawer/meter/legend labels 10-11 px, status line 13 px (at 100 % size).
- **Advanced as a side panel on the RIGHT** instead of below (window ~1620 x 800 instead of 1200 x 1166, fits a 1080p screen): sections stacked vertically (Tone/Dynamics, Reverb, Width, Echo), each with a title and two rows of knobs. New ChatGPT asset: empty vertical panel (walnut on the right, screws, horizontal dividers).
- **Value arc around the small knobs**: thin and dim normally, glowing like a lamp while hovered/turned; bipolar knobs (width bands, Output) start from 12 o'clock. Slightly more spacing between knobs.

## Sound (more accurate)
1. **Harmonic saturation (top priority for the user)**: a pitch-tracked Harmonic Exciter that generates each harmonic separately and only adds the ones that land in key (2/4/8 octaves, 3/6 fifths; the 5th only if the major third fits the scale; the 7th off by default). Key: **Root (12 notes) + Major / Minor / Harmonic minor** (e.g. A minor with G#, the "Spanish" feel; E Phrygian dominant has the same notes) or **Auto** (estimated from the note histogram during Learn). Analysis: the "harmonic profile" of held notes in the Suno vocal vs your voice (from Learn), applying the difference, like Match EQ but per harmonic.
2. **Learned makeup gain**: the preset stores the Suno vocal's loudness (LUFS, e.g. −19.2 for Suno Lead 01); a **Match Loudness** button measures the output, sets the makeup to reach that number and then **holds** it (no pumping). Can be turned on or off.
3. **Width**: closer at 2–4 kHz; per-section changes (Suno varies about ±3 dB between sections).
4. **EQ**: residual error from 2.7 dB toward under 1.5 dB (finer bands at the top, a bit of dynamic EQ on consonants). Measured on NEW_WET (Oct 10): 150 Hz-8 kHz error 1.8 dB, but above 8 kHz 4.5-6.5 dB darker than Suno because the boost is capped at +12 dB; check with a WAV (not MP3) whether a higher cap or an air shelf is needed.
5. **Compression**: closer emulation of attack/release behavior and syllable-level transients, not just phrase-level spread.
6. **Reverb decay**: a more accurate estimate (only 4 usable long pauses in this song).
7. ~~**Multiband width for every source (vocal layer, reverb, delay) + width graph**~~ (done in v1.3; still open: delay band defaults from a Suno vocal that has an echo): each source gets a main Width + Low (<~300 Hz) / Mid (300–4k) / High (>4k); only the Side is split (Linkwitz-Riley, sums flat), the Mid and mono stay untouched; default values from the Suno analysis (reverb: side/mid per band in pauses and while singing; layer: per-band coherence; delay: if there's an echo, otherwise a sensible default), auto + manual model; a graph of Suno's per-band width vs the output, next to the EQ graph. The delay currently has no width control (fixed ping-pong). Previous note, multiband M/S reverb: per-band width of the reverb (Suno while singing: 100–300 −19.2, 300–1k −17.9, 1–3k −14.3, 3–6k −14.1, 6–12k −25.5 dB side/mid), measured in the analyzer and fitted in closed loop; the Mid of the main vocal stays untouched.

## Accepted suggestions (Oct 9)
- **A/B with the Suno vocal itself**: load the Suno stem inside the plugin, one button to switch between your processed vocal and Suno, loudness-matched.
- **Suno match indicator**: live numbers for EQ, width, dynamics, s/z and reverb showing how close the output is to Suno.
- **Breath control**: detect breaths and bring them to the level measured in Suno (not removed).
- **Verse / Chorus mode**: two full snapshots + an automatable Morph knob.
- **Delay throw**: delay only on the end of phrases.
- **Smooth**: gentle dynamic EQ for momentary resonances (separate from the mic EQ).
- **Phone/mono listen**: monitoring only, does not affect the output.
- **Low-latency monitoring mode for recording**: lookahead and heavy parts off temporarily.
- (Deferred to later versions: track role Lead/Double/Backing/Adlib. The user always sings and shapes the adlibs to his own taste.)
- (Rejected by the user: loudness relative to the beat. Goal = reach Suno first, then apply his own taste.)

## Controls
8. **"Auto + manual adjustment" model for every section**: the value comes from Learn/preset (shown as a marker on the knob), the user can change it; Threshold/Makeup are relative to the auto value; double-click / Reset to Learned; **re-Learn does not erase manual tweaks** (only the auto value underneath is updated); Save stores both. Starting with an **Advanced Dynamics** panel: Attack, Release, Ratio, relative Threshold, Makeup, Hold (+ pause threshold), Lookahead on/off (for monitoring without latency).

## Workflow
7. **In-plugin preset capture**: drop a Suno vocal onto the plugin → C++ analyzer in the background → preset.
8. **Test on several Suno vocals** (different genres and singers) + an averaged "Suno sound" preset.
9. **Confidence indicator** per parameter, **lock** per module, **A/B**.

## Look
10. **Premium GUI**: image-based (backgrounds, knobs, panels from ChatGPT), text and meters drawn in code, resizable, HiDPI (2x).
