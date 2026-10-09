#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using Range = juce::NormalisableRange<float>;

Range skewed (float lo, float hi, float centre) { Range r (lo, hi); r.setSkewForCentre (centre); return r; }

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

juce::String curveToString (const std::array<float, sc::kNumBands>& c)
{
    juce::StringArray s;
    for (auto v : c) s.add (juce::String (v, 3));
    return s.joinIntoString (",");
}

bool curveFromString (const juce::String& str, std::array<float, sc::kNumBands>& c)
{
    auto t = juce::StringArray::fromTokens (str, ",", "");
    if (t.size() != sc::kNumBands) return false;
    for (int i = 0; i < sc::kNumBands; ++i) c[(size_t) i] = t[i].getFloatValue();
    return true;
}
} // namespace

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SunoChainProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto pct = juce::AudioParameterFloatAttributes().withLabel ("%");
    auto dB  = juce::AudioParameterFloatAttributes().withLabel ("dB");
    auto ms  = juce::AudioParameterFloatAttributes().withLabel ("ms");
    auto hz  = juce::AudioParameterFloatAttributes().withLabel ("Hz");
    auto sec = juce::AudioParameterFloatAttributes().withLabel ("s");

    p.push_back (std::make_unique<APF> (juce::ParameterID { "amount", 1 }, "Amount", Range (0, 100), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "output", 1 }, "Output", Range (-24, 12), 0.0f, dB));

    p.push_back (std::make_unique<APF> (juce::ParameterID { "eqAmount", 1 }, "Match EQ", Range (0, 150), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "eqLow", 1 }, "EQ Low Match", Range (0, 100), 35.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "compAmount", 1 }, "Compression", Range (0, 200), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "deess", 1 }, "De-ess", Range (0, 200), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "satDrive", 1 }, "Sat Drive", Range (0, 24), 6.0f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "satMix", 1 }, "Sat Mix", Range (0, 100), 15.0f, pct));

    p.push_back (std::make_unique<APB> (juce::ParameterID { "revOn", 1 }, "Reverb On", true));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "revLevel", 1 }, "Reverb Level", Range (-40, 6), -10.0f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "predelay", 1 }, "Pre-delay", Range (0, 500), 180.0f, ms));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "decay", 1 }, "Decay", skewed (0.3f, 10.0f, 2.5f), 3.0f, sec));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "width", 1 }, "Width", Range (0, 160), 100.0f, pct));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "duck", 1 }, "Ducking", Range (0, 24), 7.0f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "duckRel", 1 }, "Duck Release", skewed (10, 1000, 150), 120.0f, ms));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "wetHpf", 1 }, "Reverb HPF", skewed (20, 2000, 250), 300.0f, hz));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "wetLpf", 1 }, "Reverb LPF", skewed (1000, 20000, 5000), 5000.0f, hz));

    p.push_back (std::make_unique<APB> (juce::ParameterID { "dlyOn", 1 }, "Delay On", false));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "dlyLevel", 1 }, "Delay Level", Range (-40, 0), -18.0f, dB));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "dlyTime", 1 }, "Delay Time", skewed (20, 2000, 300), 375.0f, ms));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "dlyFb", 1 }, "Delay Feedback", Range (0, 95), 25.0f, pct));
    return { p.begin(), p.end() };
}

SunoChainProcessor::SunoChainProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            apvts.addParameterListener (r->getParameterID(), this);
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
    return p;
}

void SunoChainProcessor::prepareToPlay (double sampleRate, int)
{
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        chain.p = buildParams();
    }
    chain.prepare (sampleRate);
    chain.reset();
    dirty = false;
}

void SunoChainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (dirty.load())
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
    if (res.failed() || ! json.isObject()) { error = "Could not read JSON: " + res.getErrorMessage(); return false; }
    if (json.getProperty ("format", "").toString() != "SunoChainPreset") { error = "Not a Suno Chain preset."; return false; }

    auto curveVar = json["eq"]["target_curve_db"];
    if (! curveVar.isArray() || curveVar.size() != sc::kNumBands) { error = "Preset has no valid EQ target curve."; return false; }

    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        presetJson = json;
        presetLoaded = true;
        presetName = json.getProperty ("name", f.getFileNameWithoutExtension()).toString();
        restorePresetFromTree();   // hidden values from presetJson
    }

    // host-visible parameters
    auto set = [this] (const char* id, double value)
    {
        if (auto* prm = apvts.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) value));
    };
    set ("eqAmount", num (json, "eq.amount", 1.0) * 100.0);
    set ("satDrive", num (json, "saturation.drive_db", 6.0));
    set ("satMix", num (json, "saturation.mix", 0.15) * 100.0);
    set ("revOn", num (json, "reverb.on", 1.0));
    set ("revLevel", num (json, "reverb.level_db", -10.0));
    set ("predelay", num (json, "reverb.predelay_ms", 180.0));
    set ("decay", num (json, "reverb.rt60_s", 3.0));
    set ("width", num (json, "reverb.width", 1.0) * 100.0);
    set ("wetHpf", num (json, "reverb.hpf_hz", 300.0));
    set ("wetLpf", num (json, "reverb.lpf_hz", 5000.0));
    set ("duck", num (json, "ducking.depth_db", 7.0));
    set ("duckRel", num (json, "ducking.release_ms", 120.0));
    set ("dlyOn", num (json, "delay.on", 0.0));
    set ("dlyLevel", num (json, "delay.level_db", -18.0));
    set ("dlyTime", num (json, "delay.time_ms", 375.0));
    set ("dlyFb", num (json, "delay.feedback", 0.25) * 100.0);
    dirty = true;
    return true;
}

// presetJson -> hidden (lock held by caller)
void SunoChainProcessor::restorePresetFromTree()
{
    const auto& j = presetJson;
    if (! j.isObject()) return;
    auto c = j["eq"]["target_curve_db"];
    if (c.isArray() && c.size() == sc::kNumBands)
    {
        for (int i = 0; i < sc::kNumBands; ++i) hidden.targetCurve[(size_t) i] = (float) (double) c[i];
        hidden.hasTarget = true;
    }
    hidden.eqMaxBoostDb   = (float) num (j, "eq.max_boost_db", 9.0);
    hidden.eqMaxCutDb     = (float) num (j, "eq.max_cut_db", 24.0);
    hidden.targetCrestDb  = (float) num (j, "dynamics.target_crest_db", 9.0);
    hidden.targetSpreadDb = (float) num (j, "dynamics.target_spread400_db", 5.5);
    hidden.compTotalGrDb  = (float) num (j, "dynamics.default_total_gr_db", 6.0);
    hidden.comp1Ratio     = (float) num (j, "dynamics.comp1.ratio", 4.0);
    hidden.comp1AttackMs  = (float) num (j, "dynamics.comp1.attack_ms", 2.0);
    hidden.comp1ReleaseMs = (float) num (j, "dynamics.comp1.release_ms", 50.0);
    hidden.comp1Share     = (float) num (j, "dynamics.comp1.share", 0.55);
    hidden.comp2Ratio     = (float) num (j, "dynamics.comp2.ratio", 4.0);
    hidden.comp2AttackMs  = (float) num (j, "dynamics.comp2.attack_ms", 30.0);
    hidden.comp2ReleaseMs = (float) num (j, "dynamics.comp2.release_ms", 300.0);
    hidden.deessFreq      = (float) num (j, "deesser.freq_hz", 5500.0);
    hidden.deessTargetDb  = (float) num (j, "deesser.target_ratio_db", -14.0);
    hidden.deessMaxDb     = (float) num (j, "deesser.max_db", 8.0);
    hidden.delayHpf       = (float) num (j, "delay.hpf_hz", 300.0);
    hidden.delayLpf       = (float) num (j, "delay.lpf_hz", 5000.0);
    hidden.delayPingPong  = num (j, "delay.ping_pong", 1.0) > 0.5;
    hidden.rt60LowMul     = (float) num (j, "reverb.rt60_low_mul", 1.0);
    hidden.rt60HighMul    = (float) num (j, "reverb.rt60_high_mul", 0.7);
    hidden.crossoverHz    = (float) num (j, "reverb.crossover_hz", 1500.0);
    hidden.size           = (float) num (j, "reverb.size", 1.0);
    hidden.modDepth       = (float) num (j, "reverb.mod_depth", 0.5);
    hidden.earlyMs        = (float) num (j, "reverb.early_ms", 22.0);
    hidden.earlyLevelDb   = (float) num (j, "reverb.early_level_db", -8.0);
    hidden.wetBumpHz      = (float) num (j, "reverb.bump_hz", 2500.0);
    hidden.wetBumpDb      = (float) num (j, "reverb.bump_db", 2.0);
    hidden.duckAttackMs   = (float) num (j, "ducking.attack_ms", 5.0);
}

juce::String SunoChainProcessor::getPresetName() const { const juce::SpinLock::ScopedLockType sl (presetLock); return presetName; }
bool SunoChainProcessor::hasPreset() const { const juce::SpinLock::ScopedLockType sl (presetLock); return presetLoaded; }

//==============================================================================
void SunoChainProcessor::startLearn()
{
    chain.learner.start();
}

double SunoChainProcessor::learnSeconds() const { return chain.learner.nBlocks.load() * 0.05; }

bool SunoChainProcessor::stopLearn (juce::String& message)
{
    chain.learner.stop();
    std::array<float, sc::kNumBands> curve {};
    float level = 0, crest = 0, spread = 0;
    if (! chain.learner.summarise (curve, level, crest, spread))
    {
        message = "Not enough singing captured - play at least ~10 s of your vocal, then stop.";
        return false;
    }
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        hidden.sourceCurve = curve; hidden.hasSource = true;
        hidden.sourceLevelDb = level; hidden.hasSourceLevel = true;
        hidden.sourceCrestDb = crest;
        hidden.sourceSpreadDb = spread;
    }
    dirty = true;
    message = "Learned: level " + juce::String (level, 1) + " dBFS, crest " + juce::String (crest, 1) + " dB, phrase spread " + juce::String (spread, 1) + " dB";
    return true;
}

bool SunoChainProcessor::hasLearned() const { const juce::SpinLock::ScopedLockType sl (presetLock); return hidden.hasSource; }

void SunoChainProcessor::clearLearn()
{
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        hidden.hasSource = false; hidden.hasSourceLevel = false; hidden.sourceCrestDb = 0; hidden.sourceSpreadDb = 0;
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
    return s;
}

//==============================================================================
void SunoChainProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::ValueTree extra ("EXTRA");
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
    if (! state.isValid()) return;
    auto extra = state.getChildWithName ("EXTRA");
    state.removeChild (extra, nullptr);
    apvts.replaceState (state);
    if (extra.isValid())
    {
        const juce::SpinLock::ScopedLockType sl (presetLock);
        presetLoaded = (bool) extra.getProperty ("presetLoaded", false);
        presetName = extra.getProperty ("presetName", presetName).toString();
        juce::var j;
        if (juce::JSON::parse (extra.getProperty ("presetJson", "").toString(), j).wasOk() && j.isObject())
        { presetJson = j; restorePresetFromTree(); }
        hidden.hasSource = (bool) extra.getProperty ("hasSource", false)
                           && curveFromString (extra.getProperty ("sourceCurve", "").toString(), hidden.sourceCurve);
        hidden.hasSourceLevel = hidden.hasSource;
        hidden.sourceLevelDb = (float) (double) extra.getProperty ("sourceLevel", -18.0);
        hidden.sourceCrestDb = hidden.hasSource ? (float) (double) extra.getProperty ("sourceCrest", 0.0) : 0.0f;
        hidden.sourceSpreadDb = hidden.hasSource ? (float) (double) extra.getProperty ("sourceSpread", 0.0) : 0.0f;
    }
    dirty = true;
}

juce::AudioProcessorEditor* SunoChainProcessor::createEditor() { return new SunoChainEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SunoChainProcessor(); }
