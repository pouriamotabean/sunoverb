# Testing Suno Chain

Everything here runs the same sources as the plugin. CI (`.github/workflows/build.yml`) runs all of it on every push.

## Build
```
cmake -B build -A x64 -DSUNOCHAIN_TESTS=ON          # Windows (Visual Studio)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # Linux / macOS
cmake --build build --config Release
```
Targets:
- `SunoChain_VST3` – the plugin (`build/SunoChain_artefacts/Release/VST3/Suno Chain.vst3`).
- `SunoChainTests` – test app with the real processor (`PluginProcessor.cpp` + `dsp/SunoChainDSP.h`), in `build/bin`.
- `render_cli` – DSP-only offline renderer used by the Python tools, in `build/bin`.

`-DSUNOCHAIN_TESTS=OFF` builds only the plugin. Linux without the X11 extension / ALSA headers: configure with
`CXXFLAGS="-DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_ALSA=0 -DJUCE_JACK=0"`.

## SunoChainTests
```
SunoChainTests --presets-dir presets --golden tests/golden/golden.txt --tol 1.0 --state-blob tests/data/session_v2_0.bin
```
Exit code 0 = everything passed. `--only presets,state,...` runs single suites, `--verbose` prints every check.

| suite | what it proves |
|---|---|
| `solver` | the fast EXACT EQ solver gives the same gains as the reference implementation (`tests/reference`) and hits every test curve (incl. the report's "Suno Male vs a flat voice") within 0.05 dB at 44.1 / 48 / 96 kHz; LEGACY error printed for comparison |
| `guard` | SIBILANT GUARD: one sibilant made 14 dB louder than the others is pulled down (> 3 dB above 3 kHz), every other sibilant changes < 0.3 dB (dry path), a take without an outlier sounds the same with and without the guard |
| `presets` | every embedded factory preset parses, validates, learns (x2), matches loudness and renders with the WIDTH ENVELOPE (golden `wenv_*`, v2.2), with the VOWEL EQ only (golden `vowel_*`, = v2.1), with the EXACT solver on the full spectrum (golden `exact_*`, = v2.0.4) and with LEGACY (must equal v2.0: golden `preset_*` / `black_*`) a synthetic vocal (SUNO and BLACK): finite, peak <= +0.5 dBFS, not silent, WIDTH meter has data; metrics compared with the golden file; 5 broken presets are rejected and leave the session untouched |
| `sync` | the presets embedded in the plugin are identical to `presets/*.json` and every file is embedded |
| `state` | save -> new instance -> restore gives the identical state and a bit-identical render; prepareToPlay resets everything (same session renders identically twice); the stored v2.0 session (`tests/data`) restores and matches its golden render; sessions / presets from before BREATH / the EXACT solver / the SIBILANT GUARD / the VOWEL EQ / the WIDTH ENVELOPE load with it off; saved presets keep the VOWEL EQ switch and the WIDTH ENVELOPE amount; random bytes, truncated / empty data, broken JSON, NaN / Inf / 1e30 values, wrong array lengths and a foreign XML never crash and never leave NaN or out-of-range parameters |
| `realtime` | 44.1 / 48 / 88.2 / 96 kHz x blocks 32-512 (random sizes up to the maximum) x stereo, mono->stereo, mono; all effects on, automation every block, BLACK and FREEZE toggled during playback: zero allocations inside `processBlock` (counted by a replaced `operator new`), no NaN, average load reported and checked |
| `concurrent` | while an audio thread runs: presets loaded, sessions restored, Learn and Match Loudness started / stopped, knobs moved, state saved, meters read from the "message thread": no NaN, no allocation on the audio thread |
| `latency` | the reported latency equals the measured delay (cross-correlation) at 44.1 and 96 kHz |
| `switch` | BLACK and FREEZE toggled while singing: no click (largest sample step stays within the music's own) |
| `cpu` | (Linux, on request) per-callback thread CPU time, median / 99.9 % / max |

Golden file: `tests/golden/golden.txt`. The `preset_*`, `black_*` and `session_v2_0` entries were generated from
v2.0 (LEGACY EQ solver, before any v2.0.1 change); v2.0.2 reproduces them bit-exactly with GCC (`--tol 0`). The
`exact_*` entries were generated with v2.0.2 (EXACT solver); the `vowel_*` entries with v2.1.0 (VOWEL EQ), the `wenv_*` entries with v2.2.0 (WIDTH ENVELOPE). Across compilers (MSVC in CI) small float differences
are expected, hence `--tol 1.0` there. Regenerate only for an intended sound change, together with an A/B render and
a note in the README.

Sanitizers (Linux): configure a separate build with `-fsanitize=thread` (run `--only concurrent`) or
`-fsanitize=address,undefined` (run everything except `realtime`).

## Factory presets
`presets/*.json` is the only source. After editing one, run `python tools/gen_factory_presets.py`; CI runs
`--check` and fails when `Source/FactoryPresets.h` is out of date.

## Python tools
`tools/test_chain.py suno_vocal.wav preset.json` (current preset schema, older presets migrated) renders a simulated
"other singer" through `render_cli` and compares it with the Suno measurements; exit code 1 on failure
(render error, NaN, peak above `MAX_PEAK_DB`, wrong length). `RENDER_CLI=path` overrides where the renderer is.
`render_cli` rejects unknown parameter names, non-numbers, NaN / Inf, wrong array lengths, a wrong input size or
sample rate, and never leaves a half-written output file.

## pluginval
CI downloads pluginval 1.0.4 and validates the built VST3 at strictness 5 (in process, GUI tests included).
