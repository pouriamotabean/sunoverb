// Suno Chain test app: runs the REAL plugin processor (PluginProcessor.cpp + the DSP header) offline.
//
//   SunoChainTests [--presets-dir DIR] [--golden FILE] [--write-golden FILE] [--tol DB]
//                  [--state-blob FILE] [--write-state-blob FILE] [--only a,b,c] [--verbose]
//
// Suites: solver (EXACT EQ solver vs the reference implementation), presets (parse/validate/learn/render every factory preset, golden metrics), sync (embedded presets ==
// presets/*.json), state (round trip, corrupt input, legacy sessions, stored v2.0 session), realtime (allocations
// in processBlock, sample rates x block sizes x layouts, automation, freeze, mode switches, CPU per callback),
// concurrent (preset / session / learn / loudness / meters from the message thread while audio runs), latency (reported latency == measured), switch
// (no click on BLACK / FREEZE toggles); cpu (Linux only, on request: per-callback CPU time). Exit code 0 = all passed, 1 = a failure (CI reads this).
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <new>
#include <cstring>
#include <algorithm>
#include <map>
#include <vector>
#include <functional>

//==============================================================================
// allocation tracker: counts operator new calls made while the current thread is inside processBlock
static std::atomic<long> gAudioAllocs { 0 };
static thread_local bool tInAudio = false;
void* operator new (std::size_t n)
{
    if (tInAudio) gAudioAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return operator new (n); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { if (tInAudio) gAudioAllocs.fetch_add (1); return std::malloc (n == 0 ? 1 : n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { if (tInAudio) gAudioAllocs.fetch_add (1); return std::malloc (n == 0 ? 1 : n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

//==============================================================================
static int gFailures = 0, gChecks = 0;
static bool gVerbose = false;
static void check (bool ok, const juce::String& what)
{
    ++gChecks;
    if (! ok) { ++gFailures; std::printf ("  FAIL  %s\n", what.toRawUTF8()); }
    else if (gVerbose) std::printf ("  ok    %s\n", what.toRawUTF8());
}
static void info (const juce::String& s) { std::printf ("  %s\n", s.toRawUTF8()); }

//==============================================================================
// deterministic synthetic vocal: sung phrases (harmonic source, vibrato, 3 formants), "s" bursts, breaths, gaps
struct Lcg { uint32_t s = 12345; float next() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (2.0f / 16777216.0f) - 1.0f; } };

static juce::AudioBuffer<float> makeVocal (double fs, double seconds, int channels = 2)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (channels, n); b.clear();
    std::vector<float> x ((size_t) n, 0.0f);
    Lcg rng;
    juce::IIRFilter f1, f2, f3, sHp, brBp;
    f1.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, 700.0, 4.0));
    f2.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, 1200.0, 5.0));
    f3.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, 2700.0, 6.0));
    sHp.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, 4500.0));
    brBp.setCoefficients (juce::IIRCoefficients::makeBandPass (fs, 1500.0, 0.6));
    const int notes[] = { 0, 3, 5, 7, 5, 3, 0, -2, 0, 5, 7, 10 };
    double phase = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs, inPhrase = std::fmod (t, 3.0);
        float v = 0;
        if (inPhrase > 0.15 && inPhrase < 2.55)
        {
            const int syl = (int) (t / 0.3);
            const double sylT = std::fmod (t, 0.3);
            const double f0 = 147.0 * std::pow (2.0, notes[syl % 12] / 12.0) * std::pow (2.0, 0.025 * std::sin (2 * juce::MathConstants<double>::pi * 5.5 * t));
            phase += f0 / fs; phase -= std::floor (phase);
            double src = 0;
            for (int k = 1; k * f0 < std::min (fs * 0.45, 9000.0); ++k) src += std::sin (2 * juce::MathConstants<double>::pi * k * phase) / k;
            const double env = std::min (1.0, sylT / 0.02) * (0.7 + 0.3 * std::sin (2 * juce::MathConstants<double>::pi * t * 0.4));
            const float s = (float) (src * env * 0.12);
            v = 0.5f * s + f1.processSingleSampleRaw (s) + 0.8f * f2.processSingleSampleRaw (s) + 0.5f * f3.processSingleSampleRaw (s);
            if (syl % 3 == 0 && sylT < 0.06) v += 0.06f * sHp.processSingleSampleRaw (rng.next());   // "s"
        }
        else if (inPhrase >= 2.65 && inPhrase < 2.9)
            v = 0.01f * brBp.processSingleSampleRaw (rng.next());   // breath
        v += 1.0e-4f * rng.next();   // room noise floor
        x[(size_t) i] = v;
    }
    for (int c = 0; c < channels; ++c) b.copyFrom (c, 0, x.data(), n);
    return b;
}

//==============================================================================
namespace sc { struct RefExactEQ {
#include "reference/solve_exact_reference.h"
}; }

static void suiteSolver()
{
    std::printf ("[solver] EXACT EQ solver: same gains as the reference, hits the curve; LEGACY kept for old sessions\n");
    using namespace sc;
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        std::array<std::array<float, kNumBands>, 6> curves {};
        const float t[26] = { -17.3f,-15.3f,-11.3f,-9.3f,-6.3f,-0.3f,2.7f,8.3f,8.0f,2.6f,4.8f,7.8f,5.3f,2.0f,0.4f,-2.4f,-5.8f,-7.0f,-9.4f,-14.0f,-15.7f,-15.8f,-17.2f,-17.6f,-22.5f,-32.8f };
        float mean = 0; for (float v : t) mean += v / 26;
        for (int k = 0; k < kNumBands; ++k)
        {
            curves[0][(size_t) k] = 2.5f * std::sin (k * 0.5f); curves[1][(size_t) k] = 6.f * std::sin (k * 0.45f);
            curves[2][(size_t) k] = 12.f * std::sin (k * 0.4f); curves[3][(size_t) k] = -10.f + 20.f * k / 25.f;
            curves[4][(size_t) k] = std::clamp (t[k] - mean, -24.f, 12.f);
            curves[5][(size_t) k] = k < 4 ? 0.0f : (k > 18 ? 15.0f : -3.0f + 0.5f * (float) k);   // like a real learned correction
        }
        ExactEqSolver fast; double worstGain = 0, worstFit = 0, worstLegacy = 0, us = 0;
        for (auto& c : curves)
        {
            const auto ref = RefExactEQ::solve (c, fs);
            auto t0 = std::chrono::steady_clock::now();
            const auto g = fast.solve (c, fs);
            us += std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
            const auto gl = MatchEQ::solve (c, fs);
            std::array<Biquad, kNumBands> bq, bl;
            for (int k = 0; k < kNumBands; ++k)
            {
                worstGain = std::max (worstGain, (double) std::abs (g[(size_t) k] - ref[(size_t) k]));
                bq[(size_t) k].setPeak (fs, bandCenters()[(size_t) k], 2.2, g[(size_t) k]);
                bl[(size_t) k].setPeak (fs, bandCenters()[(size_t) k], 2.2, gl[(size_t) k]);
            }
            for (int b = 0; b < kNumBands; ++b)
            {
                if (bandCenters()[(size_t) b] > fs * 0.45) continue;
                double r = 0, rl = 0; for (int k = 0; k < kNumBands; ++k) { r += bq[(size_t) k].magDb (fs, bandCenters()[(size_t) b]); rl += bl[(size_t) k].magDb (fs, bandCenters()[(size_t) b]); }
                worstFit = std::max (worstFit, std::abs (r - c[(size_t) b])); worstLegacy = std::max (worstLegacy, std::abs (rl - c[(size_t) b]));
            }
        }
        const auto where = juce::String (fs, 0) + " Hz";
        check (worstGain < 0.01, where + ": fast EXACT == reference (largest gain difference " + juce::String (worstGain, 5) + " dB)");
        check (worstFit < 0.05, where + ": EXACT hits every test curve (worst " + juce::String (worstFit, 3) + " dB; LEGACY worst " + juce::String (worstLegacy, 2) + " dB)");
        info (where + ": EXACT solve " + juce::String (us / (double) curves.size(), 1) + " us on average (first call includes the per-rate setup)");
    }
}

//==============================================================================
struct Rig
{
    std::unique_ptr<SunoChainProcessor> p;
    double fs = 44100; int bs = 256; bool pumpMeters = true;
    explicit Rig (double sampleRate = 44100, int block = 256, int inCh = 2, int outCh = 2)
        : p (std::make_unique<SunoChainProcessor>()), fs (sampleRate), bs (block)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (inCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        l.outputBuses.add (outCh == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        const bool ok = p->setBusesLayout (l);
        check (ok, "bus layout " + juce::String (inCh) + "->" + juce::String (outCh) + " accepted");
        p->setRateAndBufferSizeDetails (fs, bs);
        p->prepareToPlay (fs, bs);
    }
    void set (const char* id, float v) { if (auto* prm = p->apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }
    float get (const char* id) const { return p->apvts.getRawParameterValue (id)->load(); }
    // processes `in` (channels = bus input) and returns the output (channels = bus output); latency NOT removed
    juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& in, std::function<void (int)> perBlock = {}, bool randomBlocks = false)
    {
        const int outCh = p->getTotalNumOutputChannels(), inCh = p->getTotalNumInputChannels(), n = in.getNumSamples();
        juce::AudioBuffer<float> out (outCh, n); out.clear();
        juce::MidiBuffer mb; Lcg rng; rng.s = 777;
        juce::AudioBuffer<float> blk (juce::jmax (inCh, outCh), bs);
        for (int i = 0, k = 0; i < n; ++k)
        {
            int m = juce::jmin (bs, n - i);
            if (randomBlocks) m = juce::jmax (1, juce::jmin (m, (int) ((rng.next() * 0.5f + 0.5f) * bs) + 1));
            if (perBlock) perBlock (k);
            blk.setSize (juce::jmax (inCh, outCh), m, false, false, true);
            blk.clear();
            for (int c = 0; c < inCh; ++c) blk.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), i, m);
            tInAudio = true;
            p->processBlock (blk, mb);
            tInAudio = false;
            if (pumpMeters) p->pumpMeters();   // what the editor timer does
            for (int c = 0; c < outCh; ++c) out.copyFrom (c, i, blk, c, 0, m);
            i += m;
        }
        return out;
    }
    void learnAndMatch (const juce::AudioBuffer<float>& in)
    {
        juce::String msg;
        for (int k = 0; k < 2; ++k) { p->startLearn(); run (in); check (p->stopLearn (msg), "learn: " + msg); }
        p->startLoudMatch(); run (in); check (p->stopLoudMatch (msg), "loudness match: " + msg);
    }
    bool loadPresetText (const juce::String& json, const juce::String& name, juce::String& err)
    {
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sct_" + name);
        f.replaceWithText (json);
        const bool ok = p->loadPresetFile (f, err);
        f.deleteFile();
        return ok;
    }
};

//==============================================================================
struct Metrics
{
    double rms = -200, low = -200, mid = -200, high = -200, side = -200, peak = -200; uint64_t hash = 0; bool finite = true;
    juce::String toString (const juce::String& name) const
    {
        return name + " " + juce::String (rms, 3) + " " + juce::String (low, 3) + " " + juce::String (mid, 3) + " " + juce::String (high, 3)
               + " " + juce::String (side, 3) + " " + juce::String (peak, 3) + " " + juce::String::toHexString ((juce::int64) hash);
    }
};

static double db (double e) { return 10.0 * std::log10 (e + 1e-20); }

static Metrics measure (const juce::AudioBuffer<float>& b, double fs, int skip = 0)
{
    Metrics m; const int n = b.getNumSamples(), ch = b.getNumChannels();
    juce::IIRFilter lo, hiA, hiB, hi; lo.setCoefficients (juce::IIRCoefficients::makeLowPass (fs, 300.0));
    hiA.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, 300.0)); hiB.setCoefficients (juce::IIRCoefficients::makeLowPass (fs, 4000.0));
    hi.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, 4000.0));
    double e = 0, el = 0, em = 0, eh = 0, es = 0, emid = 0, pk = 0; uint64_t h = 1469598103934665603ull;
    for (int i = 0; i < n; ++i)
    {
        const float l = b.getSample (0, i), r = b.getSample (ch > 1 ? 1 : 0, i);
        if (! std::isfinite (l) || ! std::isfinite (r)) { m.finite = false; continue; }
        uint32_t bits[2]; std::memcpy (&bits[0], &l, 4); std::memcpy (&bits[1], &r, 4);
        for (auto w : bits) { h ^= w; h *= 1099511628211ull; }
        if (i < skip) continue;
        const float mo = 0.5f * (l + r), sd = 0.5f * (l - r);
        const float a = lo.processSingleSampleRaw (mo), bb = hiB.processSingleSampleRaw (hiA.processSingleSampleRaw (mo)), c = hi.processSingleSampleRaw (mo);
        e += mo * mo; el += a * a; em += bb * bb; eh += c * c; es += sd * sd; emid += mo * mo;
        pk = std::max (pk, (double) std::max (std::abs (l), std::abs (r)));
    }
    const double cnt = juce::jmax (1, n - skip);
    m.rms = db (e / cnt); m.low = db (el / cnt); m.mid = db (em / cnt); m.high = db (eh / cnt);
    m.side = db (es / cnt) - db (emid / cnt); m.peak = 20.0 * std::log10 (pk + 1e-12); m.hash = h;
    return m;
}

static std::map<juce::String, std::vector<juce::String>> readGolden (const juce::File& f)
{
    std::map<juce::String, std::vector<juce::String>> g;
    juce::StringArray lines; lines.addLines (f.loadFileAsString());
    for (auto& l : lines)
    {
        auto t = juce::StringArray::fromTokens (l.trim(), " ", "");
        if (t.size() == 8 && ! l.startsWith ("#")) g[t[0]] = std::vector<juce::String> (t.begin(), t.end());
    }
    return g;
}

static juce::StringArray gGoldenOut;
static std::map<juce::String, std::vector<juce::String>> gGolden;
static double gTol = 1.0;

static void compareGolden (const juce::String& name, const Metrics& m)
{
    gGoldenOut.add (m.toString (name));
    if (gGolden.empty()) return;
    auto it = gGolden.find (name);
    check (it != gGolden.end(), name + ": present in the golden file");
    if (it == gGolden.end()) return;
    const double got[] = { m.rms, m.low, m.mid, m.high, m.side, m.peak };
    const char* lbl[] = { "rms", "low", "mid", "high", "side", "peak" };
    double worst = 0; juce::String worstLbl;
    for (int k = 0; k < 6; ++k)
    {
        const double d = std::abs (got[k] - it->second[(size_t) k + 1].getDoubleValue());
        if (d > worst) { worst = d; worstLbl = lbl[k]; }
    }
    const bool exact = juce::String::toHexString ((juce::int64) m.hash) == it->second[7];
    check (exact || worst <= gTol + 0.0011, name + ": matches golden within " + juce::String (gTol, 2) + " dB (worst " + worstLbl + " " + juce::String (worst, 3) + " dB"
                          + (exact ? ", bit-exact)" : ", not bit-exact)"));
}

static void finiteAndSane (const juce::AudioBuffer<float>& out, double fs, const juce::String& what, double maxPeakDb = 0.5)
{
    auto m = measure (out, fs);
    check (m.finite, what + ": no NaN / Inf");
    check (m.peak <= maxPeakDb, what + ": peak " + juce::String (m.peak, 2) + " dBFS <= " + juce::String (maxPeakDb, 1));
    check (m.rms > -60.0, what + ": not silent (" + juce::String (m.rms, 1) + " dBFS)");
}

//==============================================================================
static void suitePresets()
{
    std::printf ("[presets] every factory preset: parse, validate, learn x2, match loudness, render (LEGACY and EXACT EQ)\n");
    const double fs = 44100; auto vox = makeVocal (fs, 12.0);
    for (const auto& fp : factory::presets)
    {
        // EXACT solver (what the factory preset asks for since v2.0.2): its own golden entries
        {
            Rig e (fs, 256); juce::String err;
            e.loadPresetText (juce::String::fromUTF8 (fp.json), fp.file, err);
            check (e.get ("eqExact") == 1.0f, juce::String (fp.file) + ": factory preset uses the EXACT EQ solver");
            e.learnAndMatch (vox);
            auto outE = e.run (vox);
            finiteAndSane (outE, fs, juce::String (fp.file) + " EXACT");
            compareGolden (juce::String ("exact_") + fp.file, measure (outE, fs, e.p->getLatencySamples()));
        }
        // LEGACY solver: must reproduce v2.0 exactly (golden entries preset_ / black_ were made with v2.0)
        Rig r (fs, 256);
        juce::String err;
        const bool ok = r.loadPresetText (juce::String::fromUTF8 (fp.json), fp.file, err);
        check (ok, juce::String (fp.file) + ": loads (" + err + ")");
        if (! ok) continue;
        r.set ("eqExact", 0.0f);
        r.learnAndMatch (vox);
        auto out = r.run (vox);
        finiteAndSane (out, fs, fp.file);
        {
            const auto mtr = r.p->getMeters(); bool ok = true;
            for (auto v : mtr.widthBands) ok = ok && std::isfinite (v);
            check (ok && mtr.widthBands[1] > -100.0f, juce::String (fp.file) + ": WIDTH meter has data (" + juce::String (mtr.widthBands[1], 1) + " dB mid band)");
        }
        compareGolden (juce::String ("preset_") + fp.file, measure (out, fs, r.p->getLatencySamples()));
        // the same preset in BLACK
        r.set ("revBlack", 1.0f);
        auto outB = r.run (vox);
        finiteAndSane (outB, fs, juce::String (fp.file) + " BLACK");
        compareGolden (juce::String ("black_") + fp.file, measure (outB, fs, r.p->getLatencySamples()));
    }
    // broken presets must be rejected and must not change anything
    Rig r (fs, 256);
    juce::MemoryBlock before; r.p->getStateInformation (before);
    const char* bad[] = { "not json", "{\"format\":\"Other\"}", "{\"format\":\"SunoChainPreset\",\"version\":99}",
                          "{\"format\":\"SunoChainPreset\",\"version\":7,\"eq\":{\"target_curve_db\":[1,2,3]}}",
                          "{\"format\":\"SunoChainPreset\",\"version\":7,\"eq\":{\"target_curve_db\":[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,\"x\"]}}" };
    int k = 0;
    for (auto* b : bad)
    {
        juce::String err;
        check (! r.loadPresetText (b, "bad" + juce::String (k++) + ".json", err) && err.isNotEmpty(), "broken preset rejected with a message: " + err);
    }
    juce::MemoryBlock after; r.p->getStateInformation (after);
    check (before == after, "rejected presets left the session untouched");
}

static void suiteSync (const juce::File& dir)
{
    std::printf ("[sync] embedded factory presets == %s\n", dir.getFullPathName().toRawUTF8());
    int files = 0;
    for (const auto& fp : factory::presets)
    {
        auto f = dir.getChildFile (fp.file);
        check (f.existsAsFile(), juce::String (fp.file) + ": exists in presets/");
        if (! f.existsAsFile()) continue;
        ++files;
        juce::var a, b;
        juce::JSON::parse (juce::String::fromUTF8 (fp.json), a); juce::JSON::parse (f.loadFileAsString(), b);
        check (juce::JSON::toString (a, true) == juce::JSON::toString (b, true), juce::String (fp.file) + ": embedded copy == file");
    }
    check (dir.findChildFiles (juce::File::findFiles, false, "*.json").size() == files, "every presets/*.json is embedded");
}

//==============================================================================
static juce::MemoryBlock stateOf (SunoChainProcessor& p) { juce::MemoryBlock m; p.getStateInformation (m); return m; }

static std::unique_ptr<juce::XmlElement> xmlOf (const juce::MemoryBlock& m) { return juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize()); }

static juce::MemoryBlock blobOf (const juce::XmlElement& x) { juce::MemoryBlock m; juce::AudioProcessor::copyXmlToBinary (x, m); return m; }

static void allParamsInRange (SunoChainProcessor& p, const juce::String& what)
{
    bool ok = true;
    for (auto* prm : p.getParameters())
    {
        const float v = prm->getValue();
        if (! std::isfinite (v) || v < 0.0f || v > 1.0f) ok = false;
    }
    check (ok, what + ": all parameters finite and in range");
}

static void suiteState (const juce::File& blobIn, const juce::File& blobOut)
{
    std::printf ("[state] round trip, corrupt input, legacy sessions\n");
    const double fs = 44100; auto vox = makeVocal (fs, 12.0);
    // 1) round trip: a learned session with tweaked knobs restores exactly (parameters, hidden state, sound)
    Rig a (fs, 256);
    juce::String err;
    a.loadPresetText (juce::String::fromUTF8 (factory::presets[6].json), factory::presets[6].file, err);
    a.learnAndMatch (vox);
    a.set ("revBlack", 1.0f); a.set ("deess", 135.0f); a.set ("breath", 70.0f); a.set ("dlyOn", 1.0f); a.set ("wMotion", 30.0f);
    auto s1 = stateOf (*a.p);
    Rig b (fs, 256);
    b.p->setStateInformation (s1.getData(), (int) s1.getSize());
    auto s2 = stateOf (*b.p);
    check (s1 == s2, "round trip: restored session saves the identical state");
    bool same = true;
    for (int i = 0; i < a.p->getParameters().size(); ++i) same = same && a.p->getParameters()[i]->getValue() == b.p->getParameters()[i]->getValue();
    check (same, "round trip: every parameter identical");
    a.p->prepareToPlay (fs, 256); b.p->prepareToPlay (fs, 256);
    auto oa = a.run (vox), ob = b.run (vox);
    check (measure (oa, fs).hash == measure (ob, fs).hash, "round trip: restored session renders bit-identical");
    a.p->prepareToPlay (fs, 256); b.p->prepareToPlay (fs, 256);
    auto oa2 = a.run (vox), ob2 = b.run (vox);
    check (measure (oa, fs).hash == measure (oa2, fs).hash, "prepareToPlay resets everything: a session renders bit-identical twice");
    check (measure (ob, fs).hash == measure (ob2, fs).hash, "prepareToPlay resets everything: a restored session renders bit-identical twice");
    check (measure (oa2, fs).hash == measure (ob2, fs).hash, "round trip: second render identical too");
    {   // sample-rate change (44.1 -> 96 kHz): the used instance renders exactly like a fresh one at 96 kHz
        const double fs2 = 96000; auto vox2 = makeVocal (fs2, 6.0);
        a.p->setRateAndBufferSizeDetails (fs2, 256); a.p->prepareToPlay (fs2, 256); a.fs = fs2;
        Rig c (fs2, 256); c.p->setStateInformation (s1.getData(), (int) s1.getSize()); c.p->prepareToPlay (fs2, 256);
        auto x1 = a.run (vox2), x2 = c.run (vox2);
        check (measure (x1, fs2).hash == measure (x2, fs2).hash, "sample-rate change: renders like a fresh instance at the new rate");
        check (a.p->getLatencySamples() == c.p->getLatencySamples(), "sample-rate change: latency updated (" + juce::String (a.p->getLatencySamples()) + " samples)");
    }
    if (blobOut != juce::File()) { blobOut.replaceWithData (s1.getData(), s1.getSize()); info ("wrote " + blobOut.getFullPathName()); }

    // 2) a stored session from v2.0 (tests/data) loads and sounds as it did
    if (blobIn.existsAsFile())
    {
        juce::MemoryBlock old; blobIn.loadFileAsData (old);
        Rig c (fs, 256);
        c.p->setStateInformation (old.getData(), (int) old.getSize());
        allParamsInRange (*c.p, "v2.0 session");
        c.p->prepareToPlay (fs, 256);
        auto oc = c.run (vox);
        finiteAndSane (oc, fs, "v2.0 session");
        compareGolden ("session_v2_0", measure (oc, fs, c.p->getLatencySamples()));
    }

    // 3) sessions from before a parameter existed keep their old sound
    {
        auto x = xmlOf (s1);
        for (auto* id : { "breath", "revBlack", "revFreeze" })
            for (auto* e = x->getFirstChildElement(); e != nullptr; e = e->getNextElement())
                if (e->getStringAttribute ("id") == id) { x->removeChildElement (e, true); break; }
        auto legacy = blobOf (*x);
        Rig d (fs, 256);
        d.p->setStateInformation (legacy.getData(), (int) legacy.getSize());
        check (d.get ("breath") == 0.0f, "session without BREATH (<= v1.15.0) restores with BREATH off (was 40 %: changed the old mix)");
        check (d.get ("revBlack") == 0.0f, "session without BLACK (<= v1.13) restores as SUNO");
        check (d.get ("revFreeze") == 0.0f, "session without FREEZE restores unfrozen");
        Rig d2 (fs, 256);
        {
            auto x2 = xmlOf (s1);
            for (auto* e = x2->getFirstChildElement(); e != nullptr; e = e->getNextElement())
                if (e->getStringAttribute ("id") == "eqExact") { x2->removeChildElement (e, true); break; }
            auto legacy2 = blobOf (*x2);
            d2.p->setStateInformation (legacy2.getData(), (int) legacy2.getSize());
        }
        check (d2.get ("eqExact") == 0.0f, "session from before v2.0.2 restores with the LEGACY EQ solver");
        Rig fresh (fs, 256);
        check (fresh.get ("eqExact") == 1.0f, "a new instance uses the EXACT EQ solver");
    }
    {   // installed factory presets from v2.0 (no breath entry, never edited) are refreshed; edited copies are not
        auto dir = SunoChainProcessor::presetFolder();
        auto f1 = dir.getChildFile (factory::presets[0].file), f2 = dir.getChildFile (factory::presets[1].file);
        const auto keep1 = f1.loadFileAsString(), keep2 = f2.loadFileAsString();
        auto old = juce::JSON::parse (juce::String::fromUTF8 (factory::presets[0].json));
        if (auto* u = old["user"].getDynamicObject()) u->removeProperty ("breath");
        f1.replaceWithText (juce::JSON::toString (old, true));
        auto edited = juce::JSON::parse (juce::String::fromUTF8 (factory::presets[1].json));
        if (auto* u = edited["user"].getDynamicObject()) { u->removeProperty ("breath"); u->setProperty ("amount", 77.0); }
        f2.replaceWithText (juce::JSON::toString (edited, true));
        { SunoChainProcessor fresh; }
        check (f1.loadFileAsString() == juce::String::fromUTF8 (factory::presets[0].json), "unedited v2.0 factory preset on disk refreshed (BREATH 40 % explicit)");
        check (f2.loadFileAsString() == juce::JSON::toString (edited, true), "edited preset on disk left untouched");
        f1.replaceWithText (keep1); f2.replaceWithText (keep2);
    }
    {   // a preset saved before BREATH existed loads with BREATH off; factory presets keep their 40 %
        auto x = juce::JSON::parse (juce::String::fromUTF8 (factory::presets[6].json));
        if (auto* u = x["user"].getDynamicObject()) u->removeProperty ("breath");
        Rig e (fs, 256);
        e.loadPresetText (juce::JSON::toString (x), "old_user.json", err);
        check (e.get ("breath") == 0.0f, "preset without user.breath loads with BREATH off");
        Rig e2 (fs, 256);
        {
            auto x3 = juce::JSON::parse (juce::String::fromUTF8 (factory::presets[6].json));
            if (auto* q = x3["eq"].getDynamicObject()) q->removeProperty ("solver");
            e2.loadPresetText (juce::JSON::toString (x3), "old_eq.json", err);
        }
        check (e2.get ("eqExact") == 0.0f, "preset without eq.solver loads with the LEGACY EQ solver");
        {   // save -> load keeps the solver choice
            auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sct_saved.json");
            for (float v : { 0.0f, 1.0f })
            {
                e2.set ("eqExact", v); juce::String er2; e2.p->savePresetFile (tmp, er2);
                Rig e3 (fs, 256); e3.p->loadPresetFile (tmp, er2);
                check (e3.get ("eqExact") == v, "saved preset keeps the EQ solver (" + juce::String (v, 0) + ")");
            }
            tmp.deleteFile();
        }
        Rig f2 (fs, 256);
        f2.loadPresetText (juce::String::fromUTF8 (factory::presets[6].json), "fac.json", err);
        check (f2.get ("breath") == 40.0f, "factory preset loads with BREATH 40 %");
    }

    // 4) corrupt / hostile input: never crashes, never leaves NaN or out-of-range values
    juce::Array<juce::MemoryBlock> blobs;
    { Lcg rng; juce::MemoryBlock m (4096); for (size_t i = 0; i < m.getSize(); ++i) m[i] = (char) (rng.next() * 127); blobs.add (m); }
    blobs.add (juce::MemoryBlock (s1.getData(), s1.getSize() / 2));                 // truncated
    blobs.add (juce::MemoryBlock());                                                // empty
    {
        auto x = xmlOf (s1);
        auto* ex = x->getChildByName ("EXTRA");
        ex->setAttribute ("presetJson", "{ this is not json");
        ex->setAttribute ("sourceCurve", "nan,1,2");
        ex->setAttribute ("sourceLevel", "inf");
        ex->setAttribute ("sourceCrest", "nan");
        ex->setAttribute ("chainEffect", "1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39,1e39");
        ex->setAttribute ("uiScale", "nan");
        blobs.add (blobOf (*x));
    }
    {
        auto x = xmlOf (s1);
        auto* ex = x->getChildByName ("EXTRA");
        juce::String curve; for (int i = 0; i < sc::kNumBands; ++i) curve << (i ? "," : "") << "nan";
        ex->setAttribute ("sourceCurve", curve); ex->setAttribute ("hasSource", 1);
        ex->setAttribute ("presetJson", "{\"format\":\"SunoChainPreset\",\"version\":7,\"eq\":{\"target_curve_db\":[1e30,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]},\"reverb\":{\"rt60_s\":1e30}}");
        for (auto* e = x->getFirstChildElement(); e != nullptr; e = e->getNextElement())
            if (e->hasAttribute ("value")) e->setAttribute ("value", e->getStringAttribute ("id") == "decay" ? "nan" : "1e30");
        blobs.add (blobOf (*x));
    }
    {
        juce::XmlElement wrong ("SOMETHING_ELSE"); wrong.setAttribute ("x", 1); blobs.add (blobOf (wrong));
    }
    int k = 0;
    for (auto& bl : blobs)
    {
        Rig g (fs, 256);
        g.p->setStateInformation (bl.getData(), (int) bl.getSize());
        allParamsInRange (*g.p, "corrupt session " + juce::String (k));
        g.p->prepareToPlay (fs, 256);
        auto og = g.run (vox);
        auto m = measure (og, fs);
        check (m.finite && m.peak < 100.0, "corrupt session " + juce::String (k) + ": output finite and bounded (peak " + juce::String (m.peak, 1) + " dBFS)");
        ++k;
    }
}

//==============================================================================
static void suiteRealtime()
{
    std::printf ("[realtime] allocations / NaN / CPU across sample rates, block sizes, layouts, automation, toggles\n");
    const double rates[] = { 44100, 48000, 88200, 96000 };
    const int blocks[] = { 32, 64, 128, 256, 512 };
    const int layouts[][2] = { { 2, 2 }, { 1, 2 }, { 1, 1 } };
    double worstLoad = 0, sumLoad = 0; int nLoad = 0; juce::String worstWhere;
    for (double fs : rates)
    {
        auto vox = makeVocal (fs, 6.0, 2);
        for (int bs : blocks)
            for (auto& lay : layouts)
            {
                Rig r (fs, bs, lay[0], lay[1]);
                r.pumpMeters = false;   // timing: audio work only
                juce::String err;
                r.loadPresetText (juce::String::fromUTF8 (factory::presets[0].json), "rt.json", err);
                r.set ("dlyOn", 1.0f); r.set ("wOn", 1.0f); r.set ("revOn", 1.0f); r.set ("breath", 100.0f); r.set ("dlySync", 0.0f);
                juce::AudioBuffer<float> in (lay[0], vox.getNumSamples());
                for (int c = 0; c < lay[0]; ++c) in.copyFrom (c, 0, vox, c, 0, vox.getNumSamples());
                r.run (in);   // warm up (first callbacks may size meters etc.)
                const long before = gAudioAllocs.load();
                const int nBlocks = vox.getNumSamples() / bs;
                double maxBlockMs = 0; int maxAt = -1;
                auto t0 = std::chrono::steady_clock::now(); const auto tStart = t0;
                auto out = r.run (in, [&] (int blk)
                {
                    // host automation from the audio thread, every block
                    r.set ("revLevel", -20.0f + 10.0f * std::sin (blk * 0.05f));
                    r.set ("deess", 100.0f + 50.0f * std::sin (blk * 0.03f));
                    r.set ("dlyTime", 300.0f + 100.0f * std::sin (blk * 0.02f));
                    if (blk == nBlocks / 4) r.set ("revBlack", 1.0f);
                    if (blk == nBlocks / 2) r.set ("revFreeze", 1.0f);
                    if (blk == 3 * nBlocks / 4) { r.set ("revFreeze", 0.0f); r.set ("revBlack", 0.0f); }
                    auto now = std::chrono::steady_clock::now();
                    const double ms = std::chrono::duration<double, std::milli> (now - t0).count();
                    if (blk > 0 && ms > maxBlockMs) { maxBlockMs = ms; maxAt = blk - 1; }
                    t0 = now;
                }, true);
                const long allocs = gAudioAllocs.load() - before;
                const auto where = juce::String (fs, 0) + " Hz / " + juce::String (bs) + " / " + juce::String (lay[0]) + "->" + juce::String (lay[1]);
                check (allocs == 0, where + ": no allocation in processBlock (" + juce::String (allocs) + ")");
                auto m = measure (out, fs);
                check (m.finite, where + ": no NaN / Inf");
                const double budget = 1000.0 * bs / fs, load = maxBlockMs / budget;
                sumLoad += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - tStart).count() / (1000.0 * in.getNumSamples() / fs); ++nLoad;
                if (load > worstLoad) { worstLoad = load; worstWhere = where + " (callback " + juce::String (maxAt) + ")"; }
                if (gVerbose) info (where + ": worst callback " + juce::String (maxBlockMs, 3) + " ms at block " + juce::String (maxAt) + ", budget " + juce::String (budget, 3) + " ms");
            }
    }
    // wall-clock times on a shared CI machine include scheduler noise, so the single worst callback is reported,
    // and the check is on the average load (a real overload shows there too)
    info ("worst single callback (wall clock, includes scheduler noise): " + juce::String (100.0 * worstLoad, 1) + " % of its budget (" + worstWhere + ")");
    info ("average load: " + juce::String (100.0 * sumLoad / juce::jmax (1, nLoad), 1) + " % of real time (all configurations)");
    check (sumLoad / juce::jmax (1, nLoad) < 0.5, "average callback uses < 50 % of real time");

}

static void suiteConcurrent()
{
    // message-thread activity while the audio thread runs: presets, sessions, learn, loudness, knobs, meters
    std::printf ("[concurrent] message-thread work while audio runs\n");
    const double fs = 48000; auto vox = makeVocal (fs, 4.0);
    Rig r (fs, 128);
    juce::String err;
    r.loadPresetText (juce::String::fromUTF8 (factory::presets[1].json), "c.json", err);
    auto session = stateOf (*r.p);
    std::atomic<bool> stop { false }; std::atomic<bool> bad { false }; std::atomic<long> audioAllocs { 0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> blk (2, 128); juce::MidiBuffer mb; int pos = 0;
        while (! stop.load())
        {
            for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, vox, c, pos, 128);
            pos = (pos + 128) % (vox.getNumSamples() - 128);
            const long a0 = gAudioAllocs.load();
            tInAudio = true; r.p->processBlock (blk, mb); tInAudio = false;
            audioAllocs += gAudioAllocs.load() - a0;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 128; ++i) if (! std::isfinite (blk.getSample (c, i))) bad = true;
        }
    });
    for (int it = 0; it < 120; ++it)
    {
        r.loadPresetText (juce::String::fromUTF8 (factory::presets[(size_t) it % std::size (factory::presets)].json), "c" + juce::String (it) + ".json", err);
        r.set ("amount", (float) (it % 100)); r.set ("revBlack", (float) (it % 2)); r.set ("revFreeze", (float) ((it / 3) % 2));
        if (it % 5 == 0) r.p->setStateInformation (session.getData(), (int) session.getSize());
        if (it % 7 == 0) { r.p->startLearn(); std::this_thread::sleep_for (std::chrono::milliseconds (60)); juce::String m; r.p->stopLearn (m); }
        if (it % 9 == 0) { r.p->startLoudMatch(); std::this_thread::sleep_for (std::chrono::milliseconds (60)); juce::String m; r.p->stopLoudMatch (m); }
        juce::MemoryBlock s; r.p->getStateInformation (s);
        r.p->pumpMeters(); (void) r.p->getMeters(); (void) r.p->getCurves();   // the editor timer
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    stop = true; audio.join();
    check (! bad.load(), "concurrent: audio stayed finite");
    check (audioAllocs.load() == 0, "concurrent: no allocation on the audio thread (" + juce::String (audioAllocs.load()) + ")");
}

//==============================================================================
static void suiteLatency()
{
    std::printf ("[latency] reported latency == measured delay (dry path)\n");
    for (double fs : { 44100.0, 96000.0 })
    {
        Rig r (fs, 256);
        r.set ("revOn", 0.0f); r.set ("dlyOn", 0.0f); r.set ("wOn", 0.0f);
        const int n = (int) (fs * 2.0);
        juce::AudioBuffer<float> in (2, n); in.clear();
        Lcg rng; for (int i = (int) (0.5 * fs); i < (int) (1.5 * fs); ++i) { const float v = 0.1f * rng.next(); in.setSample (0, i, v); in.setSample (1, i, v); }
        auto out = r.run (in);
        const int lat = r.p->getLatencySamples(); int best = -1; double bestC = -1;
        for (int lag = 0; lag < (int) (0.1 * fs); ++lag)
        {
            double c = 0; for (int i = (int) (0.6 * fs); i < (int) (1.4 * fs); i += 3) c += (double) in.getSample (0, i) * out.getSample (0, i + lag);
            if (c > bestC) { bestC = c; best = lag; }
        }
        check (std::abs (best - lat) <= 2, juce::String (fs, 0) + " Hz: reported " + juce::String (lat) + " samples, measured " + juce::String (best));
    }
}

static void suiteSwitch()
{
    std::printf ("[switch] BLACK and FREEZE toggles while singing: no click\n");
    const double fs = 44100; auto vox = makeVocal (fs, 10.0);
    Rig r (fs, 256);
    r.learnAndMatch (vox);
    const int nb = vox.getNumSamples() / 256;
    struct Ev { int block; const char* id; float v; };
    const Ev evs[] = { { nb / 5, "revBlack", 1 }, { 2 * nb / 5, "revFreeze", 1 }, { 3 * nb / 5, "revFreeze", 0 }, { 4 * nb / 5, "revBlack", 0 } };
    auto out = r.run (vox, [&] (int b) { for (auto& e : evs) if (b == e.block) r.set (e.id, e.v); });
    // compare the sharpest step around each toggle with the sharpest steps of the music elsewhere
    auto d2 = [&] (int i) { return std::abs (out.getSample (0, i) - 2 * out.getSample (0, i - 1) + out.getSample (0, i - 2)); };
    std::vector<float> all; for (int i = 2; i < out.getNumSamples(); ++i) all.push_back (d2 (i));
    std::vector<float> sorted (all); std::sort (sorted.begin(), sorted.end());
    const float ref = sorted[(size_t) (sorted.size() * 0.9999)];
    for (auto& e : evs)
    {
        const int at = e.block * 256; float mx = 0;
        for (int i = juce::jmax (2, at - 64); i < juce::jmin (out.getNumSamples(), at + (int) (0.05 * fs)); ++i) mx = std::max (mx, d2 (i));
        check (mx <= 1.5f * ref, juce::String (e.id) + " -> " + juce::String (e.v, 0) + ": no click (step " + juce::String (mx / ref, 2) + " x the music's 99.99th percentile)");
    }
}


static void suiteCpu()   // Linux: per-callback thread CPU time (scheduler noise excluded)
{
   #if JUCE_LINUX
    for (double fs : { 44100.0, 96000.0 })
    {
        auto vox = makeVocal (fs, 6.0);
        Rig r (fs, 64); juce::String err;
        r.loadPresetText (juce::String::fromUTF8 (factory::presets[0].json), "rt.json", err);
        r.set ("dlyOn", 1.0f); r.set ("wOn", 1.0f);
        r.learnAndMatch (vox);
        std::vector<double> cpu, wall;
        juce::AudioBuffer<float> blk (2, 64); juce::MidiBuffer mb;
        for (int i = 0; i + 64 <= vox.getNumSamples(); i += 64)
        {
            for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, vox, c, i, 64);
            timespec a, b; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &a); auto w0 = std::chrono::steady_clock::now();
            r.p->processBlock (blk, mb);
            clock_gettime (CLOCK_THREAD_CPUTIME_ID, &b); auto w1 = std::chrono::steady_clock::now();
            cpu.push_back ((b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) * 1e-6); wall.push_back (std::chrono::duration<double, std::milli> (w1 - w0).count());
        }
        auto pct = [] (std::vector<double> v, double q) { std::sort (v.begin(), v.end()); return v[(size_t) (q * (v.size() - 1))]; };
        info (juce::String (fs, 0) + " Hz / 64: budget " + juce::String (64000.0 / fs, 3) + " ms | cpu median " + juce::String (pct (cpu, 0.5), 3) + " p99.9 " + juce::String (pct (cpu, 0.999), 3) + " max " + juce::String (pct (cpu, 1.0), 3)
              + " | wall max " + juce::String (pct (wall, 1.0), 3));
        const double thr = pct (cpu, 0.995); juce::String idx; int cnt = 0;
        for (size_t k = 0; k < cpu.size() && cnt < 40; ++k) if (cpu[k] >= thr) { idx << (int) k << " "; ++cnt; }
        info ("  heavy blocks: " + idx);
    }
   #endif
}

//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::File presetsDir, golden, writeGolden, blobIn, blobOut;
    juce::StringArray only;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]); auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        auto file = [] (const juce::String& s) { return juce::File::getCurrentWorkingDirectory().getChildFile (s); };
        if (a == "--presets-dir") presetsDir = file (next());
        else if (a == "--golden") golden = file (next());
        else if (a == "--write-golden") writeGolden = file (next());
        else if (a == "--state-blob") blobIn = file (next());
        else if (a == "--write-state-blob") blobOut = file (next());
        else if (a == "--tol") gTol = next().getDoubleValue();
        else if (a == "--only") only = juce::StringArray::fromTokens (next(), ",", "");
        else if (a == "--verbose") gVerbose = true;
    }
    if (golden.existsAsFile()) gGolden = readGolden (golden);
    else if (golden != juce::File()) { std::printf ("golden file not found: %s\n", golden.getFullPathName().toRawUTF8()); return 1; }
    auto want = [&] (const char* s) { return only.isEmpty() || only.contains (s); };
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (want ("solver")) suiteSolver();
    if (want ("presets")) suitePresets();
    if (want ("sync") && presetsDir != juce::File()) suiteSync (presetsDir);
    if (want ("state")) suiteState (blobIn, blobOut);
    if (want ("realtime")) suiteRealtime();
    if (want ("concurrent")) suiteConcurrent();
    if (want ("latency")) suiteLatency();
    if (want ("switch")) suiteSwitch();
    if (only.contains ("cpu")) suiteCpu();
    if (writeGolden != juce::File())
    {
        writeGolden.replaceWithText ("# name rms low mid high side-minus-mid peak hash (dB; generated by SunoChainTests --write-golden)\n" + gGoldenOut.joinIntoString ("\n") + "\n");
        std::printf ("wrote %s (%d entries)\n", writeGolden.getFullPathName().toRawUTF8(), gGoldenOut.size());
    }
    std::printf ("\n%d checks, %d failed (%.1f s)\n", gChecks, gFailures, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    return gFailures == 0 ? 0 : 1;
}
