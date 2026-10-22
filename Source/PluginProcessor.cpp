#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using NRange = juce::NormalisableRange<float>;

NRange skewed (float lo, float hi, float centre) { NRange r (lo, hi); r.setSkewForCentre (centre); return r; }

// read a number at a dotted path inside the preset json
double num (const juce::var& root, const char* path, double def)
{
    juce::var v = root;
    for (auto& key : juce::StringArray::fromTokens (path, ".", ""))
    {
        if (! v.isObject()) return def;
        v = v.getProperty (juce::Identifier (key), juce::var());
    }
    if (v.isDouble() || v.isInt() || v.isInt64()) return (double) v;
    if (v.isBool()) return (bool) v ? 1.0 : 0.0;
    return def;
}

bool has (const juce::var& root, const char* path)
{
    juce::var v = root;
    for (auto& key : juce::StringArray::fromTokens (path, ".", ""))
    {
        if (! v.isObject() || ! v.getDynamicObject()->hasProperty (juce::Identifier (key))) return false;
        v = v.getProperty (juce::Identifier (key), juce::var());
    }
    return true;
}

// write a value at a dotted path, creating objects on the way
void put (juce::var& root, const char* path, const juce::var& value)
{
    auto keys = juce::StringArray::fromTokens (path, ".", "");
    if (! root.isObject()) root = juce::var (new juce::DynamicObject());
    juce::var cur = root;
    for (int i = 0; i < keys.size() - 1; ++i)
    {
        auto* obj = cur.getDynamicObject();
        juce::Identifier id (keys[i]);
        if (! obj->getProperty (id).isObject()) obj->setProperty (id, juce::var (new juce::DynamicObject()));
        cur = obj->getProperty (id);
    }
    cur.getDynamicObject()->setProperty (juce::Identifier (keys[keys.size() - 1]), value);
}

// v2.0.1: learned curves are stored with full float precision (9 significant digits round-trip exactly), so a
// reopened project sounds bit-identical to the one that was saved (v2.0 kept 3 decimals). Reading rejects
// non-finite or absurd values (a damaged session must never feed NaN into the DSP).
static juce::String floatToString (float v)
{
    char buf[32]; std::snprintf (buf, sizeof (buf), "%.9g", (double) v);
    return juce::String (buf);
}

template <size_t N>
juce::String arrToString (const std::array<float, N>& c)
{
    juce::StringArray s;
    for (auto v : c) s.add (floatToString (v));
    return s.joinIntoString (",");
}

template <size_t N>
bool arrFromString (const juce::String& str, std::array<float, N>& c, float limit = 200.0f)
{
    auto t = juce::StringArray::fromTokens (str, ",", "");
    if (t.size() != (int) N) return false;
    std::array<float, N> tmp {};
    for (size_t i = 0; i < N; ++i)
    {
        const auto tok = t[(int) i].trim();
        if (! tok.containsOnly ("0123456789+-.eE")) return false;
        const float v = tok.getFloatValue();
        if (! std::isfinite (v) || std::abs (v) > limit) return false;
        tmp[i] = v;
    }
    c = tmp;
    return true;
}

juce::String curveToString (const std::array<float, sc::kNumBands>& c) { return arrToString (c); }
bool curveFromString (const juce::String& str, std::array<float, sc::kNumBands>& c) { return arrFromString (str, c); }

// numeric array at a dotted path (exact size), false if missing
template <size_t N>
bool numArray (const juce::var& root, const char* path, std::array<float, N>& out)
{
    juce::var v = root;
    for (auto& key : juce::StringArray::fromTokens (path, ".", ""))
    {
        if (! v.isObject()) return false;
        v = v.getProperty (juce::Identifier (key), juce::var());
    }
    if (! v.isArray() || v.size() != (int) N) return false;
    for (size_t i = 0; i < N; ++i)
    {
        auto e = v[(int) i];
        if (! (e.isDouble() || e.isInt() || e.isInt64())) return false;
        out[i] = (float) (double) e;
    }
    return true;
}

template <size_t N>
juce::var toVarArray (const std::array<float, N>& a)
{
    juce::Array<juce::var> arr;
    for (auto v : a) arr.add (std::round (v * 100.0f) / 100.0f);
    return arr;
}

// value display
juce::AudioParameterFloatAttributes fmt (int decimals, const juce::String& unit)
{
    return juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
        [decimals, unit] (float v, int)
        {
            // note: juce::String (v, 0) would print *all* decimals, so whole numbers are rounded explicitly
            auto t = decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
            return t + (unit.isEmpty() ? "" : " " + unit);
        });
}
juce::AudioParameterFloatAttributes fmtHz()
{
    return juce::AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
        [] (float v, int) { return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String ((int) std::round (v)) + " Hz"; });
}
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SunoChainProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto pct = fmt (0, "%"), dB = fmt (1, "dB"), ms = fmt (0, "ms"), sec = fmt (2, "s");
    // v1 parameters (IDs unchanged so old sessions load); defaults = Suno Lead 01 (v1.5 fit)
    p.push_back (std::make_unique<APF> (juce::ParameterID { "amount", 1 }, "Amount", NRange (0, 100), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "output", 1 }, "Output", NRange (-24, 12), 0.0f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "eqAmount", 1 }, "Match EQ", NRange (0, 150), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "eqLow", 1 }, "EQ Low Boost", NRange (0, 100), 35.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "compAmount", 1 }, "Compression", NRange (0, 200), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "deess", 1 }, "S/Z Match", NRange (0, 200), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "satDrive", 1 }, "Sat Drive", NRange (0, 24), 6.0f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "satMix", 1 }, "Sat Mix", NRange (0, 100), 15.0f, pct));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "revOn", 1 }, "Reverb On", true));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "revBlack", 7 }, "Reverb BLACK", false));   // v1.14: SUNO / BLACK
    p.push_back (std::make_unique<APB> (juce::ParameterID { "revFreeze", 8 }, "Reverb Freeze", false));   // v1.15 (not in presets)
    p.push_back (std::make_unique<APF> (juce::ParameterID { "breath", 9 }, "Breath", NRange (0, 100), 40.0f, pct));   // v1.15.1
    p.push_back (std::make_unique<APF> (juce::ParameterID { "revLevel", 1 }, "Reverb Level", NRange (-40, 6), -5.5f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "predelay", 1 }, "Pre-delay", NRange (0, 500), 205.0f, ms));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "decay", 1 }, "Decay", skewed (0.3f, 10.0f, 2.5f), 4.64f, sec));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "width", 1 }, "Reverb Width", NRange (0, 160), 88.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "duck", 1 }, "Ducking", NRange (0, 24), 12.3f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "duckRel", 1 }, "Duck Release", skewed (10, 1500, 200), 338.0f, ms));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "wetHpf", 1 }, "Reverb HPF", skewed (20, 2000, 250), 400.0f, fmtHz()));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "wetLpf", 1 }, "Reverb LPF", skewed (1000, 20000, 5000), 14000.0f, fmtHz()));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "dlyOn", 1 }, "Delay On", false));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "dlyLevel", 1 }, "Delay Level", NRange (-40, 0), -20.0f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "dlyTime", 1 }, "Delay Time (free)", skewed (20, 2000, 300), 375.0f, ms));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "dlyFb", 1 }, "Delay Feedback", NRange (0, 95), 25.0f, pct));
    // v1.1 parameters
    p.push_back (std::make_unique<APB> (juce::ParameterID { "dlySync", 2 }, "Delay Sync", true));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "dlyNote", 2 }, "Delay Note", juce::StringArray { "1/4", "1/8 dotted", "1/8", "1/8 triplet", "1/16" }, 2));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "dlyToRev", 2 }, "Delay > Reverb", NRange (0, 100), 50.0f, pct));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "wOn", 2 }, "Vocal Width On", true));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "wLevel", 2 }, "Vocal Width", NRange (-45, -6), -28.5f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "wTone", 2 }, "Width Tone", NRange (-6, 15), 4.4f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "wMotion", 2 }, "Width on Held Notes", NRange (0, 100), 0.0f, pct));
    // v1.3: multiband width (side gain per band: low < 300 Hz, mid 300 Hz - 4 kHz, high > 4 kHz)
    auto band = [&] (const char* id, const char* name, float lo, float hi, float def)
    { p.push_back (std::make_unique<APF> (juce::ParameterID { id, 3 }, name, NRange (lo, hi), def, dB)); };
    band ("rwLow",  "Reverb Width Low",  -30, 12, -3.21f);
    band ("rwMid",  "Reverb Width Mid",  -30, 12, 3.5f);
    band ("rwHigh", "Reverb Width High", -30, 12, -13.41f);
    band ("lwLow",  "Vocal Width Low",   -40, 18, 6.81f);
    band ("lwMid",  "Vocal Width Mid",   -40, 18, 0.0f);
    band ("lwHigh", "Vocal Width High",  -40, 18, 0.63f);
    band ("dwLow",  "Echo Width Low",    -30, 12, 0.0f);
    band ("dwMid",  "Echo Width Mid",    -30, 12, 0.0f);
    band ("dwHigh", "Echo Width High",   -30, 12, 0.0f);
    // v1.5: Advanced Dynamics, relative to what Learn / the preset set (100 % = as learned)
    for (auto [id, name] : std::vector<std::pair<const char*, const char*>> { { "dynPeak", "Peak Comp" }, { "dynLeveler", "Rider" },
                                                                               { "dynSpeed", "Comp Speed" }, { "dynPunch", "Attack" } })   // v1.7: dynPunch = opto attack (ID kept for saved sessions)
        p.push_back (std::make_unique<APF> (juce::ParameterID { id, 4 }, name, NRange (0, 200), 100.0f, pct));
    // v1.8: Match Loudness gain (set by the MATCH LOUDNESS button, then held; adjustable)
    p.push_back (std::make_unique<APF> (juce::ParameterID { "loudGain", 5 }, "Loudness Match", NRange (-24, 24), 0.0f, dB));
    // v1.11: harmonic exciter (replaces the tanh saturation): amount (100 % = fitted to Suno) and air tilt
    p.push_back (std::make_unique<APF> (juce::ParameterID { "harm", 6 }, "Harmonics", NRange (0, 200), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "air", 6 }, "Air", NRange (-12, 12), 5.0f, dB));   // v1.13 default +5 by ear
    // appended last so every older parameter keeps its index
    // v2.0.2: Match EQ / S/Z solver. On = EXACT (new sessions, factory presets); off = LEGACY (sessions and presets
    // saved before v2.0.2 restore with it off, so they keep their sound). Host parameter, so it can be A/B'd.
    p.push_back (std::make_unique<APB> (juce::ParameterID { "eqExact", 10 }, "EQ Solver Exact", true));
    // v2.0.4: SIBILANT GUARD (pulls back only sibilants far louder than the singer's usual ones). On for new
    // sessions and the factory presets; off for sessions / presets saved before v2.0.4 (their sound).
    p.push_back (std::make_unique<APB> (juce::ParameterID { "sibGuard", 11 }, "Sibilant Guard", true));
    // v2.1: VOWEL EQ (the Match EQ compares your vowels with Suno's vowels; sibilants stay with the S/Z Match).
    // On for new sessions and the factory presets; off for sessions / presets saved before v2.1 (their sound).
    p.push_back (std::make_unique<APB> (juce::ParameterID { "eqVowel", 13 }, "Vowel EQ", true));
    return { p.begin(), p.end() };
}

juce::File SunoChainProcessor::presetFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("SunoChain").getChildFile ("Presets");
    d.createDirectory();
    return d;
}

SunoChainProcessor::SunoChainProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            apvts.addParameterListener (r->getParameterID(), this);
    installFactoryPresets();
    const juce::SpinLock::ScopedLockType sl (presetLock);
    restorePresetFromTree();   // fills hidden with the built-in (Suno Lead 01) defaults
}

// v1.14: the factory presets travel inside the plugin. Written into the preset folder when a file of that name is
// missing (your own presets and edited copies are never overwritten). Message thread (constructor) only.
void SunoChainProcessor::installFactoryPresets()
{
    auto dir = presetFolder();
    if (! dir.isDirectory()) return;
    for (const auto& fp : factory::presets)
    {
        auto f = dir.getChildFile (fp.file);
        const auto text = juce::String::fromUTF8 (fp.json);
        if (! f.existsAsFile()) { f.replaceWithText (text); continue; }
        // v2.0.1 - v2.0.4: the factory presets now carry BREATH, the EQ solver and the SIBILANT GUARD explicitly (a
        // preset without them loads as presets from before sounded). An installed copy that is exactly an older
        // factory text (only those entries missing, never edited) is refreshed; an edited file never.
        juce::var onDisk, current;
        if (juce::JSON::parse (f.loadFileAsString(), onDisk).failed() || juce::JSON::parse (text, current).failed()) continue;
        if (! onDisk.isObject() || ! current.isObject()) continue;
        static const char* added[][2] = { { "user", "breath" }, { "eq", "solver" }, { "deesser", "guard" }, { "eq", "vowel" }, { "eq", "target_curve_voiced_db" } };
        bool missing = false;
        for (auto& a : added)
        {
            const auto path = juce::String (a[0]) + "." + a[1];
            if (! has (onDisk, path.toRawUTF8()) && has (current, path.toRawUTF8()))
            {
                missing = true;
                if (auto* o = current[a[0]].getDynamicObject()) o->removeProperty (a[1]);   // compare without it
            }
        }
        if (! missing) continue;
        if (juce::JSON::toString (onDisk, true) == juce::JSON::toString (current, true)) f.replaceWithText (text);
    }
}

// v1.14: a preset is checked completely before anything is applied (a broken file never half-loads)
// strict = a preset file; !strict = the copy stored inside a session (v2.0.1: only the content is checked there,
// so a session from any older version keeps its preset)
static bool validatePreset (const juce::var& json, juce::String& error, bool strict = true)
{
    if (! json.isObject()) { error = "Could not read the preset (not a JSON object)."; return false; }
    if (strict && json.getProperty ("format", "").toString() != "SunoChainPreset") { error = "Not a Suno Chain preset."; return false; }
    const int ver = (int) json.getProperty ("version", 1);
    if (strict && ver > 7) { error = "This preset is from a newer Suno Chain (version " + juce::String (ver) + "). Please update the plugin."; return false; }
    auto arr = [&] (const juce::var& v, int n, bool required, const char* what) -> bool
    {
        if (v.isVoid() || v.isUndefined()) { if (required) { error = juce::String ("Preset is missing ") + what + "."; return false; } return true; }
        if (! v.isArray() || v.size() != n) { error = juce::String ("Preset has an invalid ") + what + " (needs " + juce::String (n) + " values)."; return false; }
        for (auto& x : *v.getArray()) if (! (x.isInt() || x.isInt64() || x.isDouble()) || ! std::isfinite ((double) x)) { error = juce::String ("Preset has a non-number in ") + what + "."; return false; }
        return true;
    };
    if (! arr (json["eq"]["target_curve_db"], sc::kNumBands, strict, "EQ target curve")) return false;
    if (! arr (json["eq"]["target_curve_hz"], sc::kNumBands, false, "EQ frequency list")) return false;
    if (! arr (json["eq"]["target_curve_voiced_db"], sc::kNumBands, false, "vowel EQ target curve")) return false;
    if (! arr (json["deesser"]["sib_curve_db"], 10, false, "S/Z curve")) return false;
    for (auto* sec : { "reverb", "delay", "width_layer" })
        if (! arr (json[sec]["band_width_db"], 3, false, "width per band")) return false;
    if (! arr (json["width_layer"]["band_db"], 3, false, "width layer bands")) return false;
    // every number anywhere in the file must be finite
    std::function<bool (const juce::var&)> finite = [&] (const juce::var& v) -> bool
    {
        if (v.isDouble()) return std::isfinite ((double) v);
        if (v.isArray()) { for (auto& x : *v.getArray()) if (! finite (x)) return false; return true; }
        if (auto* o = v.getDynamicObject()) { for (auto& p : o->getProperties()) if (! finite (p.value)) return false; }
        return true;
    };
    if (! finite (json)) { error = "Preset contains an invalid number (NaN / infinity)."; return false; }
    return true;
}

SunoChainProcessor::~SunoChainProcessor()
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            apvts.removeParameterListener (r->getParameterID(), this);
}

bool SunoChainProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

//==============================================================================
sc::Params SunoChainProcessor::buildParams() const
{
    sc::Params p = hidden;
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    p.amount = v ("amount") / 100.0f;
    p.outputDb = v ("output");
    p.eqAmount = v ("eqAmount") / 100.0f;
    p.eqLowWeight = v ("eqLow") / 100.0f;
    p.compAmount = v ("compAmount") / 100.0f;
    p.deessAmount = v ("deess") / 100.0f;
    p.satDriveDb = v ("satDrive");
    p.satMix = v ("satMix") / 100.0f;
    p.reverbOn = v ("revOn") > 0.5f;
    p.reverbMode = v ("revBlack") > 0.5f ? 1 : 0;
    p.reverbFreeze = v ("revFreeze") > 0.5f;
    p.breathDb = 0.2f * v ("breath");
    p.eqExact = v ("eqExact") > 0.5f;
    p.sibGuard = v ("sibGuard") > 0.5f;
    p.eqVowel = v ("eqVowel") > 0.5f;
    p.reverbLevelDb = v ("revLevel");
    p.predelayMs = v ("predelay");
    p.rt60 = v ("decay");
    p.width = v ("width") / 100.0f;
    p.duckDb = v ("duck");
    p.duckReleaseMs = v ("duckRel");
    p.wetHpf = v ("wetHpf");
    p.wetLpf = v ("wetLpf");
    p.delayOn = v ("dlyOn") > 0.5f;
    p.delayLevelDb = v ("dlyLevel");
    p.delayMs = v ("dlyTime");
    p.delayFeedback = v ("dlyFb") / 100.0f;
    p.delaySync = v ("dlySync") > 0.5f;
    p.delayNote = (int) std::round (v ("dlyNote"));
    p.delayToReverb = v ("dlyToRev") / 100.0f;
    p.widthOn = v ("wOn") > 0.5f;
    p.widthLevelDb = v ("wLevel");
    p.widthTiltDb = v ("wTone");
    p.widthMotion = v ("wMotion") / 100.0f;
    p.revBandDb   = { v ("rwLow"), v ("rwMid"), v ("rwHigh") };
    p.layerBandDb = { v ("lwLow"), v ("lwMid"), v ("lwHigh") };
    p.delayBandDb = { v ("dwLow"), v ("dwMid"), v ("dwHigh") };
    p.peakScale = v ("dynPeak") / 100.0f; p.levelerScale = v ("dynLeveler") / 100.0f;
    // v1.13: the "dynLeveler" knob is the vocal RIDER (the old leveler is gone), 0 = off
    // v1.13.2: 0-100 % unchanged (Suno-fitted, gentle); above 100 % it really rides: 200 % = +/-6 dB, 80 % of the
    // way to your typical level (like a vocal rider plug-in)
    {
        const float rv = v ("dynLeveler") / 100.0f, x = juce::jlimit (0.0f, 1.0f, rv - 1.0f);
        p.rideRangeDb = rv <= 1.0f ? 2.0f * rv : 2.0f + 4.0f * x;
        p.rideAmount = rv <= 1.0f ? 0.25f : 0.25f + 0.55f * x;
        p.rideOn = p.rideRangeDb > 0.05f;
    }
    p.speedScale = juce::jmax (0.25f, v ("dynSpeed") / 100.0f);
    p.punch = 0.0f;                                            // v1.7: onset shaper removed
    p.attackScale = juce::jmax (0.25f, v ("dynPunch") / 100.0f);   // 100 % = 10 ms
    p.loudGainDb = v ("loudGain");
    p.excAmount = v ("harm") / 100.0f; p.airDb = v ("air");
    p.listenMode = listenMode.load();
    p.bpm = (float) hostBpm.load();
    return p;
}

void SunoChainProcessor::prepareToPlay (double sampleRate, int)
{
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        chain.p = buildParams();
    }
    {
        const juce::SpinLock::ScopedLockType ml (meterLock);   // the editor timer may be reading the meter
        chain.prepare (sampleRate);
    }
    chain.reset();
    setLatencySamples (chain.getLatency());
    dirty = false;
}

void SunoChainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                if (std::abs (*bpm - hostBpm.load()) > 0.01 && *bpm > 10.0) { hostBpm = *bpm; dirty = true; }
    if (dirty.load() && paramBatch.load() == 0)   // v2.0.1: never apply a half-loaded preset / session
    {
        const juce::SpinLock::ScopedTryLockType sl (presetLock);
        if (sl.isLocked())
        {
            dirty = false;
            chain.setParams (buildParams());
        }
    }
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels(), n = buffer.getNumSamples();
    if (inCh == 1 && outCh == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0);
    float* R = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    chain.process (L, R, n);
}

//==============================================================================
bool SunoChainProcessor::loadPresetFile (const juce::File& f, juce::String& error)
{
    juce::var json;
    auto res = juce::JSON::parse (f.loadFileAsString(), json);
    if (res.failed()) { error = "Could not read JSON: " + res.getErrorMessage(); return false; }
    if (! validatePreset (json, error)) return false;

    const BatchScope batch (*this);   // the audio thread keeps the old settings until everything below is set
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        presetJson = json;
        presetLoaded = true;
        presetName = json.getProperty ("name", f.getFileNameWithoutExtension()).toString();
        restorePresetFromTree();
    }
    currentPresetFile = f;

    auto set = [this] (const char* id, double value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) value));
    };
    set ("eqAmount", num (json, "eq.amount", 1.0) * 100.0);
    set ("satDrive", num (json, "saturation.drive_db", 6.0));
    set ("satMix", num (json, "saturation.mix", 0.15) * 100.0);
    set ("revOn", num (json, "reverb.on", 1.0));
    set ("revBlack", num (json, "reverb.black", 0.0));
    set ("revLevel", num (json, "reverb.level_db", -5.5));
    set ("predelay", num (json, "reverb.predelay_ms", 205.0));
    set ("decay", num (json, "reverb.rt60_s", 4.64));
    set ("width", num (json, "reverb.width", 0.88) * 100.0);
    set ("wetHpf", num (json, "reverb.hpf_hz", 400.0));
    set ("wetLpf", num (json, "reverb.lpf_hz", 14000.0));
    set ("duck", num (json, "ducking.depth_db", 12.3));
    set ("duckRel", num (json, "ducking.release_ms", 338.0));
    if (has (json, "width_layer"))
    {
        set ("wOn", num (json, "width_layer.on", 1.0));
        set ("wLevel", num (json, "width_layer.level_db", -28.5));
        set ("wTone", num (json, "width_layer.tilt_db", 4.4));
        if (has (json, "width_layer.motion")) set ("wMotion", num (json, "width_layer.motion", 0.0) * 100.0);
    }
    {   // multiband width (v1.3 presets); older presets: flat (0 dB) = the v1.2 sound
        std::array<float, 3> rb { 0, 0, 0 }, lb { 0, 0, 0 };
        numArray (json, "reverb.band_width_db", rb); numArray (json, "width_layer.band_db", lb);
        set ("rwLow", rb[0]); set ("rwMid", rb[1]); set ("rwHigh", rb[2]);
        set ("lwLow", lb[0]); set ("lwMid", lb[1]); set ("lwHigh", lb[2]);
    }
    // Delay: only applied when the Suno vocal actually had an echo, or when it is a preset you saved
    // yourself. Otherwise your own delay settings stay as they are.
    if (num (json, "delay.detected", 0.0) > 0.5 || num (json, "delay.user_set", 0.0) > 0.5)
    {
        set ("dlyOn", num (json, "delay.on", 0.0));
        set ("dlyLevel", num (json, "delay.level_db", -20.0));
        set ("dlyTime", num (json, "delay.time_ms", 375.0));
        set ("dlyFb", num (json, "delay.feedback", 0.25) * 100.0);
        set ("dlySync", num (json, "delay.sync", 1.0));
        set ("dlyNote", num (json, "delay.note", 2.0));
        set ("dlyToRev", num (json, "delay.to_reverb", 0.5) * 100.0);
        std::array<float, 3> db { 0, 0, 0 }; numArray (json, "delay.band_width_db", db);
        set ("dwLow", db[0]); set ("dwMid", db[1]); set ("dwHigh", db[2]);
    }
    if (has (json, "user.amount"))
    {
        set ("amount", num (json, "user.amount", 100.0)); set ("output", num (json, "user.output_db", 0.0));
        set ("eqLow", num (json, "user.eq_low", 35.0)); set ("compAmount", num (json, "user.compression", 100.0));
        set ("deess", num (json, "user.deess", 100.0));
    }
    set ("dynPeak", num (json, "user.dyn_peak", 100.0)); set ("dynLeveler", num (json, "user.dyn_leveler", 100.0));
    set ("dynSpeed", num (json, "user.dyn_speed", 100.0)); set ("dynPunch", num (json, "user.dyn_punch", 100.0));
        set ("harm", num (json, "user.harmonics", 100.0)); set ("air", num (json, "user.air_db", 5.0));
        // v2.0.1: a preset saved before BREATH existed (<= v1.15.0) loads with BREATH off, so it sounds as it did;
        // the factory presets carry user.breath = 40 explicitly
        set ("breath", num (json, "user.breath", 0.0));
        // v2.0.2: presets saved before the EXACT solver keep the LEGACY one (their sound)
        set ("eqExact", json["eq"]["solver"].toString() == "exact" ? 1.0 : 0.0);
        set ("sibGuard", num (json, "deesser.guard", 0.0));
        set ("eqVowel", num (json, "eq.vowel", 0.0));   // v2.1: older presets without it keep their sound
    dirty = true;
    return true;
}

bool SunoChainProcessor::savePresetFile (const juce::File& f, juce::String& error)
{
    juce::var j;
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        // start from the loaded preset (keeps its EQ curve and measurements), or from the built-in one
        j = juce::JSON::parse (juce::JSON::toString (presetJson.isObject() ? presetJson : juce::var (new juce::DynamicObject())));
        if (! j.isObject()) j = juce::var (new juce::DynamicObject());
        put (j, "format", "SunoChainPreset");
        put (j, "version", 5);
        put (j, "dynamics.punch", hidden.punch);
        put (j, "name", f.getFileNameWithoutExtension());
        juce::Array<juce::var> curve;
        for (auto v : hidden.targetCurve) curve.add (v);
        put (j, "eq.target_curve_db", curve);
        if (hidden.hasTargetV) put (j, "eq.target_curve_voiced_db", toVarArray (hidden.targetCurveV));
        put (j, "dynamics.target_crest_db", hidden.targetCrestDb);
        put (j, "dynamics.target_spread400_db", hidden.targetSpreadDb);
        put (j, "dynamics.leveler_factor", hidden.levelerFactor);
        put (j, "loudness.integrated_lufs", targetLufs);
        put (j, "deesser.method", "sz_match");
        put (j, "deesser.sib_curve_db", toVarArray (hidden.sibTarget));
        put (j, "multiband_width.xover_low_hz", hidden.xoverLowHz);
        put (j, "multiband_width.xover_high_hz", hidden.xoverHighHz);
        put (j, "reverb.rt60_high_mul", hidden.rt60HighMul);
        put (j, "reverb.bump_hz", hidden.wetBumpHz);
        put (j, "reverb.bump_db", hidden.wetBumpDb);
        put (j, "reverb.mod_depth", hidden.modDepth);
        put (j, "reverb.diffusion", hidden.diffusion);
        put (j, "reverb.diffusion_size", hidden.diffusionSize);
        put (j, "width_layer.shift_hz", hidden.widthShiftHz);
        put (j, "width_layer.delay_ms", hidden.widthDelayMs);
        put (j, "width_layer.hpf_hz", hidden.widthHpf);
        put (j, "width_layer.lpf_hz", hidden.widthLpf);
        put (j, "width_layer.bloom_db", hidden.widthBloomDb);
        put (j, "width_layer.bloom_ms", hidden.widthBloomMs);
    }
    auto v = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };
    put (j, "eq.amount", v ("eqAmount") / 100.0);
    put (j, "eq.solver", v ("eqExact") > 0.5 ? "exact" : "legacy");
    put (j, "deesser.guard", v ("sibGuard") > 0.5);
    put (j, "eq.vowel", v ("eqVowel") > 0.5);
    put (j, "saturation.drive_db", v ("satDrive"));
    put (j, "saturation.mix", v ("satMix") / 100.0);
    put (j, "reverb.on", v ("revOn") > 0.5);
    put (j, "reverb.black", v ("revBlack") > 0.5);
    put (j, "reverb.level_db", v ("revLevel"));
    put (j, "reverb.predelay_ms", v ("predelay"));
    put (j, "reverb.rt60_s", v ("decay"));
    put (j, "reverb.width", v ("width") / 100.0);
    put (j, "reverb.hpf_hz", v ("wetHpf"));
    put (j, "reverb.lpf_hz", v ("wetLpf"));
    put (j, "ducking.depth_db", v ("duck"));
    put (j, "ducking.release_ms", v ("duckRel"));
    put (j, "width_layer.on", v ("wOn") > 0.5);
    put (j, "width_layer.level_db", v ("wLevel"));
    put (j, "width_layer.tilt_db", v ("wTone"));
    put (j, "width_layer.motion", v ("wMotion") / 100.0);
    put (j, "reverb.band_width_db", toVarArray (std::array<float, 3> { (float) v ("rwLow"), (float) v ("rwMid"), (float) v ("rwHigh") }));
    put (j, "width_layer.band_db", toVarArray (std::array<float, 3> { (float) v ("lwLow"), (float) v ("lwMid"), (float) v ("lwHigh") }));
    put (j, "delay.band_width_db", toVarArray (std::array<float, 3> { (float) v ("dwLow"), (float) v ("dwMid"), (float) v ("dwHigh") }));
    put (j, "delay.user_set", true);
    put (j, "delay.on", v ("dlyOn") > 0.5);
    put (j, "delay.level_db", v ("dlyLevel"));
    put (j, "delay.time_ms", v ("dlyTime"));
    put (j, "delay.feedback", v ("dlyFb") / 100.0);
    put (j, "delay.sync", v ("dlySync") > 0.5);
    put (j, "delay.note", (int) std::round (v ("dlyNote")));
    put (j, "delay.to_reverb", v ("dlyToRev") / 100.0);
    put (j, "user.amount", v ("amount"));
    put (j, "user.output_db", v ("output"));
    put (j, "user.eq_low", v ("eqLow"));
    put (j, "user.compression", v ("compAmount"));
    put (j, "user.deess", v ("deess"));
    put (j, "user.dyn_peak", v ("dynPeak")); put (j, "user.dyn_leveler", v ("dynLeveler"));
    put (j, "user.dyn_speed", v ("dynSpeed")); put (j, "user.dyn_punch", v ("dynPunch"));
    put (j, "user.harmonics", v ("harm")); put (j, "user.air_db", v ("air")); put (j, "user.breath", v ("breath"));
    if (! f.replaceWithText (juce::JSON::toString (j, false))) { error = "Could not write " + f.getFullPathName(); return false; }
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        presetJson = j; presetLoaded = true; presetName = f.getFileNameWithoutExtension();
    }
    currentPresetFile = f;
    return true;
}

// presetJson -> hidden (lock held by caller). Fallbacks = Suno Lead 01 (v1.5 fit), so older presets get the
// measured width layer and reverb character too.
void SunoChainProcessor::restorePresetFromTree()
{
    const auto& j = presetJson;
    auto c = j.isObject() ? j["eq"]["target_curve_db"] : juce::var();
    if (c.isArray() && c.size() == sc::kNumBands)
    {
        for (int i = 0; i < sc::kNumBands; ++i) hidden.targetCurve[(size_t) i] = (float) (double) c[i];
        hidden.hasTarget = true;
    }
    {   // v2.1: the Suno vowel curve (presets made before v2.1 have none: the VOWEL EQ then uses the full curve)
        auto cv = j.isObject() ? j["eq"]["target_curve_voiced_db"] : juce::var();
        hidden.hasTargetV = cv.isArray() && cv.size() == sc::kNumBands;
        if (hidden.hasTargetV) for (int i = 0; i < sc::kNumBands; ++i) hidden.targetCurveV[(size_t) i] = juce::jlimit (-100.0f, 100.0f, (float) (double) cv[i]);
    }
    hidden.eqMaxBoostDb   = (float) num (j, "eq.max_boost_db", 12.0);
    hidden.eqMaxCutDb     = (float) num (j, "eq.max_cut_db", 24.0);
    hidden.targetCrestDb  = (float) num (j, "dynamics.target_crest_db", 9.0);
    hidden.targetSpreadDb = (float) num (j, "dynamics.target_spread400_db", 5.5);
    // v1.5: Punch (onset emphasis) came with a stronger leveler; presets older than v4 get the v1.5 calibration
    const bool v4 = num (j, "version", 1.0) >= 4.0;
    hidden.levelerFactor  = (float) (v4 ? num (j, "dynamics.leveler_factor", 2.1) : 2.1);
    hidden.punch          = (float) (v4 ? num (j, "dynamics.punch", 1.5) : 1.5);
    hidden.eqMaxBoostHighDb = (float) num (j, "eq.max_boost_high_db", 15.0);
    targetLufs = (float) num (j, "loudness.integrated_lufs", -19.41);
    hidden.compTotalGrDb  = (float) num (j, "dynamics.default_total_gr_db", 6.0);
    hidden.comp1Ratio     = (float) num (j, "dynamics.comp1.ratio", 4.0);
    hidden.comp1AttackMs  = (float) num (j, "dynamics.comp1.attack_ms", 2.0);
    hidden.comp1ReleaseMs = (float) num (j, "dynamics.comp1.release_ms", 50.0);
    hidden.comp1Share     = (float) num (j, "dynamics.comp1.share", 0.55);
    hidden.comp2Ratio     = (float) num (j, "dynamics.comp2.ratio", 4.0);
    hidden.comp2AttackMs  = (float) num (j, "dynamics.comp2.attack_ms", 30.0);
    hidden.comp2ReleaseMs = (float) num (j, "dynamics.comp2.release_ms", 300.0);
    // v1.3 S/Z Match: Suno's sibilant spectrum (older presets: the built-in Suno Lead 01 measurement)
    {
        std::array<float, 10> sc10 {};
        hidden.sibTarget = numArray (j, "deesser.sib_curve_db", sc10) ? sc10 : sc::Params().sibTarget;
        hidden.hasSibTarget = true;
    }
    hidden.xoverLowHz     = (float) num (j, "multiband_width.xover_low_hz", 300.0);
    hidden.xoverHighHz    = (float) num (j, "multiband_width.xover_high_hz", 4000.0);
    // width targets for the WIDTH graph (Suno, same bands as the meter); built-in = Suno Lead 01
    widthTargetSing = { -18.95f, -15.75f, -24.27f }; widthTargetGap = { -8.91f, -0.16f, -16.58f };
    hasWidthTarget = true;
    if (j.isObject())
    {
        std::array<float, 3> a {}, b {};
        const bool ok = numArray (j, "measurements.suno.width_meter_singing_db", a) && numArray (j, "measurements.suno.width_meter_pauses_db", b);
        if (ok) { widthTargetSing = a; widthTargetGap = b; }
        else hasWidthTarget = false;   // a preset without width data (older version): no target shown
    }
    hidden.delayHpf       = (float) num (j, "delay.hpf_hz", 300.0);
    hidden.delayLpf       = (float) num (j, "delay.lpf_hz", 5000.0);
    hidden.delayPingPong  = num (j, "delay.ping_pong", 1.0) > 0.5;
    hidden.rt60LowMul     = (float) num (j, "reverb.rt60_low_mul", 1.0);
    hidden.rt60HighMul    = (float) num (j, "reverb.rt60_high_mul", 0.7);
    hidden.crossoverHz    = (float) num (j, "reverb.crossover_hz", 1500.0);
    hidden.size           = (float) num (j, "reverb.size", 1.0);
    // v1 presets had discrete early taps and little modulation; v1.1 always uses the diffuse Suno-like onset
    const bool v2 = num (j, "version", 1.0) >= 2.0;
    // v1.5: depth capped at 0.5 (2.5 smeared the reverb pitch by about +/-20 cents: audible detune on tuned vocals)
    hidden.modDepth       = (float) std::min (0.5, v2 ? num (j, "reverb.mod_depth", 0.5) : 0.5);
    hidden.diffusion      = (float) num (j, "reverb.diffusion", 0.75);
    hidden.diffusionSize  = (float) num (j, "reverb.diffusion_size", 2.0);
    hidden.earlyMs        = (float) num (j, "reverb.early_ms", 22.0);
    hidden.earlyLevelDb   = (float) (v2 ? num (j, "reverb.early_level_db", -100.0) : -100.0);
    hidden.wetBumpHz      = (float) num (j, "reverb.bump_hz", 3150.0);
    hidden.wetBumpDb      = (float) num (j, "reverb.bump_db", 4.5);
    hidden.duckAttackMs   = (float) num (j, "ducking.attack_ms", 5.0);
    hidden.widthDelayMs   = (float) num (j, "width_layer.delay_ms", 1.0);
    hidden.widthShiftHz   = (float) num (j, "width_layer.shift_hz", 1.45);
    hidden.widthHpf       = (float) num (j, "width_layer.hpf_hz", 151.0);
    hidden.widthLpf       = (float) num (j, "width_layer.lpf_hz", 16000.0);
    hidden.widthBloomDb   = (float) num (j, "width_layer.bloom_db", 26.6);
    hidden.widthBloomMs   = (float) num (j, "width_layer.bloom_ms", 187.0);

    // v2.0.1: a preset (or the copy inside a session) with absurd numbers must not reach the DSP. The ranges are
    // far outside anything the analyzer writes (all factory and older presets are inside them, checked by
    // tests), so real presets are untouched.
    auto lim = [] (float& v, float lo, float hi) { v = std::isfinite (v) ? juce::jlimit (lo, hi, v) : juce::jlimit (lo, hi, 0.0f); };
    for (auto& v : hidden.targetCurve) lim (v, -100.0f, 100.0f);
    for (auto& v : hidden.sibTarget) lim (v, -100.0f, 100.0f);
    for (auto& v : widthTargetSing) lim (v, -100.0f, 100.0f);
    for (auto& v : widthTargetGap) lim (v, -100.0f, 100.0f);
    lim (hidden.eqMaxBoostDb, 0, 40); lim (hidden.eqMaxCutDb, 0, 60); lim (hidden.eqMaxBoostHighDb, 0, 40);
    lim (hidden.targetCrestDb, 0, 60); lim (hidden.targetSpreadDb, 0, 60); lim (hidden.levelerFactor, 0, 20); lim (hidden.punch, 0, 10);
    lim (targetLufs, -70, 0); lim (hidden.compTotalGrDb, 0, 40);
    lim (hidden.comp1Ratio, 1, 50); lim (hidden.comp1AttackMs, 0.01f, 1000); lim (hidden.comp1ReleaseMs, 1, 10000); lim (hidden.comp1Share, 0, 1);
    lim (hidden.comp2Ratio, 1, 50); lim (hidden.comp2AttackMs, 0.01f, 1000); lim (hidden.comp2ReleaseMs, 1, 10000);
    lim (hidden.xoverLowHz, 20, 5000); lim (hidden.xoverHighHz, 200, 20000);
    lim (hidden.delayHpf, 10, 5000); lim (hidden.delayLpf, 200, 22000);
    lim (hidden.rt60LowMul, 0.05f, 5); lim (hidden.rt60HighMul, 0.05f, 5); lim (hidden.crossoverHz, 50, 15000); lim (hidden.size, 0.1f, 4);
    lim (hidden.modDepth, 0, 0.5f); lim (hidden.diffusion, 0, 0.99f); lim (hidden.diffusionSize, 0.1f, 8);
    lim (hidden.earlyMs, 0, 500); lim (hidden.earlyLevelDb, -100, 12);
    lim (hidden.wetBumpHz, 20, 20000); lim (hidden.wetBumpDb, -30, 30); lim (hidden.duckAttackMs, 0.01f, 1000);
    lim (hidden.widthDelayMs, 0, 100); lim (hidden.widthShiftHz, 0, 50); lim (hidden.widthHpf, 10, 5000); lim (hidden.widthLpf, 500, 22000);
    lim (hidden.widthBloomDb, 0, 80); lim (hidden.widthBloomMs, 1, 5000);
}

juce::String SunoChainProcessor::getPresetName() const { const juce::SpinLock::ScopedLockType sl (presetLock); return presetName; }
bool SunoChainProcessor::hasPreset() const { const juce::SpinLock::ScopedLockType sl (presetLock); return presetLoaded; }

//==============================================================================
void SunoChainProcessor::startLearn()
{
    {   // the correction that is active while Learn listens (needed for the closed-loop EQ)
        const juce::SpinLock::ScopedLockType sl (presetLock);
        learnAppliedCorr = sc::computeCorrection (buildParams());
    }
    chain.startLearn();
}

double SunoChainProcessor::learnSeconds() const { return chain.learner.nBlocks.load() * 0.05; }

bool SunoChainProcessor::stopLearn (juce::String& message)
{
    chain.stopLearn();
    std::array<float, sc::kNumBands> curve {}, outCurve {};
    float level = 0, crest = 0, spread = 0;
    if (! chain.learner.summarise (curve, level, crest, spread, &outCurve))
    {
        message = "Not enough singing captured - play at least ~10 s of your vocal, then stop.";
        return false;
    }
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        hidden.sourceCurve = curve; hidden.hasSource = true;
        hidden.sourceLevelDb = level; hidden.hasSourceLevel = true;
        // closed-loop EQ: what the rest of the chain did to the spectrum while you played (v1.5)
        hidden.chainEffect = sc::Learner::chainEffectFrom (curve, outCurve, learnAppliedCorr);
        hidden.hasChainEffect = true;
        hidden.sourceCrestDb = crest;
        hidden.sourceSpreadDb = spread;
    }
    {   // v2.1 VOWEL EQ: the same over the vowel frames only
        std::array<float, sc::kNumBands> cv {}, ov {};
        const bool okV = chain.learner.summariseVoiced (cv, ov);
        const juce::SpinLock::ScopedLockType sl (presetLock);
        hidden.hasSourceV = okV; hidden.hasChainEffectV = okV;
        if (okV) { hidden.sourceCurveV = cv; hidden.chainEffectV = sc::Learner::chainEffectFrom (cv, ov, learnAppliedCorr); }
    }
    std::array<float, 10> sib {};
    const int nSib = chain.learner.sib.summarise (sib);
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        hidden.hasSibSource = nSib >= 3;
        if (hidden.hasSibSource) hidden.sibSource = sib;
    }
    dirty = true;
    message = "Learned: level " + juce::String (level, 1) + " dBFS, crest " + juce::String (crest, 1)
              + " dB, spread " + juce::String (spread, 1) + " dB, "
              + (nSib >= 3 ? juce::String (nSib) + " s/z sounds" : juce::String ("too few s/z sounds (learn a longer part)"))
              + ". RE-LEARN once more to refine the EQ.";
    return true;
}

void SunoChainProcessor::startLoudMatch() { chain.startLoud(); }

bool SunoChainProcessor::stopLoudMatch (juce::String& message)
{
    chain.stopLoud();
    const float measured = chain.loud.integrated();
    if (chain.loud.seconds() < 5.0 || measured < -69.0f)
    {
        message = "Not enough audio measured - play at least ~10 s of the vocal, then stop.";
        return false;
    }
    const float target = getTargetLufs();
    float gain = juce::jlimit (-24.0f, 24.0f, target - measured);   // measured before this gain: absolute, not added
    // v1.10: never push the loudest peak over -1 dBFS (the Suno stems are mastered loud)
    const float peakDb = juce::Decibels::gainToDecibels (chain.loud.peak.load(), -100.0f);
    const bool capped = peakDb > -100.0f && peakDb + gain > -1.0f;
    if (capped) gain = -1.0f - peakDb;
    if (auto* prm = apvts.getParameter ("loudGain")) prm->setValueNotifyingHost (prm->convertTo0to1 (gain));
    message = "Loudness: measured " + juce::String (measured, 1) + " LUFS, Suno " + juce::String (target, 1)
              + " LUFS -> " + (gain >= 0 ? "+" : "") + juce::String (gain, 1) + " dB, held"
              + (capped ? " (limited so peaks stay under -1 dBFS)." : ".");
    return true;
}

bool SunoChainProcessor::hasLearned() const { const juce::SpinLock::ScopedLockType sl (presetLock); return hidden.hasSource; }

void SunoChainProcessor::clearLearn()
{
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        hidden.hasSource = false; hidden.hasSourceLevel = false; hidden.sourceCrestDb = 0; hidden.sourceSpreadDb = 0;
        hidden.hasSibSource = false; hidden.hasChainEffect = false; hidden.hasSourceV = false; hidden.hasChainEffectV = false;
    }
    dirty = true;
}

SunoChainProcessor::CurveSnapshot SunoChainProcessor::getCurves() const
{
    CurveSnapshot s;
    const juce::SpinLock::ScopedLockType sl (presetLock);
    auto p = buildParams();
    s.target = p.targetCurve; s.source = p.sourceCurve;
    s.hasTarget = p.hasTarget; s.hasSource = p.hasSource;
    s.correction = sc::computeCorrection (p);
    s.sourceLevel = p.sourceLevelDb; s.sourceCrest = p.sourceCrestDb; s.targetCrest = p.targetCrestDb;
    s.widthTargetSing = widthTargetSing; s.widthTargetGap = widthTargetGap; s.hasWidthTarget = hasWidthTarget;
    return s;
}

void SunoChainProcessor::pumpMeters()
{
    const juce::SpinLock::ScopedTryLockType ml (meterLock);
    if (ml.isLocked()) chain.pumpMeters();
}

SunoChainProcessor::Meters SunoChainProcessor::getMeters() const
{
    Meters m { chain.mGr1.load(), chain.mGr2.load(), chain.mDuck.load(), chain.mIn.load(), chain.mOut.load(), chain.mWidth.load(),
               hostBpm.load(), chain.mDelayMs.load(), chain.mBlocks.load(), chain.mDeess.load(), {} };
    for (size_t i = 0; i < 6; ++i) m.widthBands[i] = chain.mWidthBands[i].load();
    return m;
}

//==============================================================================
void SunoChainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree extra ("EXTRA");
    extra.setProperty ("stateVersion", kStateVersion, nullptr);   // v2.0.1: session schema (absent = v2.0 or older)
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        extra.setProperty ("presetLoaded", presetLoaded, nullptr);
        extra.setProperty ("presetName", presetName, nullptr);
        extra.setProperty ("presetJson", juce::JSON::toString (presetJson, true), nullptr);
        extra.setProperty ("hasSource", hidden.hasSource, nullptr);
        extra.setProperty ("sourceCurve", curveToString (hidden.sourceCurve), nullptr);
        extra.setProperty ("sourceLevel", hidden.sourceLevelDb, nullptr);
        extra.setProperty ("sourceCrest", hidden.sourceCrestDb, nullptr);
        extra.setProperty ("sourceSpread", hidden.sourceSpreadDb, nullptr);
        extra.setProperty ("hasSibSource", hidden.hasSibSource, nullptr);
        extra.setProperty ("advancedOpen", advancedOpen, nullptr);
        extra.setProperty ("uiScale", uiScale, nullptr);
        extra.setProperty ("presetFile", currentPresetFile.getFullPathName(), nullptr);
        extra.setProperty ("sibSource", arrToString (hidden.sibSource), nullptr);
        extra.setProperty ("hasChainEffect", hidden.hasChainEffect, nullptr);
        extra.setProperty ("chainEffect", arrToString (hidden.chainEffect), nullptr);
        extra.setProperty ("hasSourceV", hidden.hasSourceV, nullptr);   // v2.1 VOWEL EQ
        extra.setProperty ("sourceCurveV", arrToString (hidden.sourceCurveV), nullptr);
        extra.setProperty ("chainEffectV", arrToString (hidden.chainEffectV), nullptr);
    }
    state.removeChild (state.getChildWithName ("EXTRA"), nullptr);
    state.appendChild (extra, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void SunoChainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid() || ! state.hasType (apvts.state.getType())) return;   // not our session: ignore
    auto extra = state.getChildWithName ("EXTRA");
    state.removeChild (extra, nullptr);
    const BatchScope batch (*this);
    // v2.0.1: sanitise every stored parameter (NaN / out of range from a damaged session -> default / clamped) and
    // give sessions from before a parameter existed the value that reproduces their old sound
    for (auto* prm : getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        if (r == nullptr) continue;
        auto child = state.getChildWithProperty ("id", r->getParameterID());
        if (! child.isValid())
        {
            // BREATH arrived in v1.15.1 with a 40 % default: an older session had no breath control at all
            // the EXACT EQ solver arrived in v2.0.2: an older session keeps the LEGACY one (its sound)
            const auto pid = r->getParameterID();
            const float legacy = (pid == "breath" || pid == "eqExact" || pid == "sibGuard" || pid == "eqVowel") ? 0.0f : r->convertFrom0to1 (r->getDefaultValue());
            juce::ValueTree c ("PARAM"); c.setProperty ("id", r->getParameterID(), nullptr); c.setProperty ("value", legacy, nullptr);
            state.appendChild (c, nullptr);
            continue;
        }
        const auto txt = child.getProperty ("value").toString().trim();
        const double v = txt.getDoubleValue();
        const bool numeric = txt.isNotEmpty() && txt.containsOnly ("0123456789+-.eE") && std::isfinite (v);
        const auto range = r->getNormalisableRange();
        child.setProperty ("value", numeric ? juce::jlimit (range.start, range.end, (float) v) : r->convertFrom0to1 (r->getDefaultValue()), nullptr);
    }
    apvts.replaceState (state);
    if (extra.isValid())
    {
        auto finiteOr = [] (const juce::var& v, double lo, double hi, double fallback)
        {
            const double d = (double) v; return std::isfinite (d) ? juce::jlimit (lo, hi, d) : fallback;
        };
        const juce::SpinLock::ScopedLockType sl (presetLock);
        presetLoaded = (bool) extra.getProperty ("presetLoaded", false);
        presetName = extra.getProperty ("presetName", presetName).toString();
        juce::var j; juce::String err;
        // the embedded preset goes through the same validation as a preset file; a damaged one keeps the current
        if (juce::JSON::parse (extra.getProperty ("presetJson", "").toString(), j).wasOk() && j.isObject() && validatePreset (j, err, false))
            presetJson = j;
        restorePresetFromTree();
        hidden.hasSource = (bool) extra.getProperty ("hasSource", false)
                           && curveFromString (extra.getProperty ("sourceCurve", "").toString(), hidden.sourceCurve);
        hidden.hasSourceLevel = hidden.hasSource;
        hidden.sourceLevelDb = (float) finiteOr (extra.getProperty ("sourceLevel", -18.0), -120.0, 24.0, -18.0);
        hidden.sourceCrestDb = hidden.hasSource ? (float) finiteOr (extra.getProperty ("sourceCrest", 0.0), 0.0, 60.0, 0.0) : 0.0f;
        hidden.sourceSpreadDb = hidden.hasSource ? (float) finiteOr (extra.getProperty ("sourceSpread", 0.0), 0.0, 60.0, 0.0) : 0.0f;
        advancedOpen = (bool) extra.getProperty ("advancedOpen", false);
        uiScale = (float) finiteOr (extra.getProperty ("uiScale", 1.0), 0.6, 1.6, 1.0);
        {
            const auto pf = extra.getProperty ("presetFile", "").toString();
            currentPresetFile = juce::File::isAbsolutePath (pf) ? juce::File (pf) : juce::File();
        }
        hidden.hasChainEffect = hidden.hasSource && (bool) extra.getProperty ("hasChainEffect", false)
                                && arrFromString (extra.getProperty ("chainEffect", "").toString(), hidden.chainEffect);
        hidden.hasSibSource = hidden.hasSource && (bool) extra.getProperty ("hasSibSource", false)
                              && arrFromString (extra.getProperty ("sibSource", "").toString(), hidden.sibSource);
        hidden.hasSourceV = hidden.hasSource && (bool) extra.getProperty ("hasSourceV", false)
                            && arrFromString (extra.getProperty ("sourceCurveV", "").toString(), hidden.sourceCurveV)
                            && arrFromString (extra.getProperty ("chainEffectV", "").toString(), hidden.chainEffectV);
        hidden.hasChainEffectV = hidden.hasSourceV;
    }
    dirty = true;
}

juce::AudioProcessorEditor* SunoChainProcessor::createEditor() { return new SunoChainEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SunoChainProcessor(); }
