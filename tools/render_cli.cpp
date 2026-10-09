// Offline renderer for testing the DSP core.
// usage: render_cli params.txt in.f32 out.f32 samplerate [learn]
// in/out: interleaved stereo float32. params: key=value per line (arrays comma separated)
#include "../Source/dsp/SunoChainDSP.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

static std::vector<float> parseList (const std::string& s)
{ std::vector<float> v; std::stringstream ss (s); std::string t; while (std::getline (ss, t, ',')) v.push_back (std::stof (t)); return v; }

int main (int argc, char** argv)
{
    if (argc < 5) { std::fprintf (stderr, "usage\n"); return 1; }
    std::map<std::string, std::string> kv;
    std::ifstream pf (argv[1]); std::string line;
    while (std::getline (pf, line)) { auto e = line.find ('='); if (e != std::string::npos) kv[line.substr (0, e)] = line.substr (e + 1); }
    sc::Params p;
    auto F = [&] (const char* k, float& dst) { if (kv.count (k)) dst = std::stof (kv[k]); };
    auto B = [&] (const char* k, bool& dst) { if (kv.count (k)) dst = std::stof (kv[k]) != 0; };
    F ("amount", p.amount); F ("outputDb", p.outputDb); F ("compAmount", p.compAmount); F ("deessAmount", p.deessAmount); F ("eqLowWeight", p.eqLowWeight);
    F ("eqAmount", p.eqAmount); F ("eqMaxBoostDb", p.eqMaxBoostDb); F ("eqMaxCutDb", p.eqMaxCutDb);
    F ("compTotalGrDb", p.compTotalGrDb); F ("targetCrestDb", p.targetCrestDb); F ("targetSpreadDb", p.targetSpreadDb);
    F ("comp1Ratio", p.comp1Ratio); F ("comp1AttackMs", p.comp1AttackMs); F ("comp1ReleaseMs", p.comp1ReleaseMs);
    F ("comp2Ratio", p.comp2Ratio); F ("comp2AttackMs", p.comp2AttackMs); F ("comp2ReleaseMs", p.comp2ReleaseMs);
    F ("deessFreq", p.deessFreq); F ("deessTargetDb", p.deessTargetDb); F ("deessMaxDb", p.deessMaxDb);
    F ("satDriveDb", p.satDriveDb); F ("satMix", p.satMix);
    B ("delayOn", p.delayOn); F ("delayMs", p.delayMs); F ("delayFeedback", p.delayFeedback); F ("delayLevelDb", p.delayLevelDb);
    F ("delayHpf", p.delayHpf); F ("delayLpf", p.delayLpf); B ("delayPingPong", p.delayPingPong);
    B ("reverbOn", p.reverbOn); F ("predelayMs", p.predelayMs); F ("rt60", p.rt60); F ("rt60LowMul", p.rt60LowMul);
    F ("rt60HighMul", p.rt60HighMul); F ("crossoverHz", p.crossoverHz); F ("size", p.size); F ("modDepth", p.modDepth); F ("diffusion", p.diffusion); F ("diffusionSize", p.diffusionSize);
    F ("earlyMs", p.earlyMs); F ("earlyLevelDb", p.earlyLevelDb); F ("wetHpf", p.wetHpf); F ("wetLpf", p.wetLpf);
    F ("wetBumpHz", p.wetBumpHz); F ("wetBumpDb", p.wetBumpDb); F ("width", p.width); F ("reverbLevelDb", p.reverbLevelDb);
    F ("levelerFactor", p.levelerFactor); F ("bpm", p.bpm); F ("delayToReverb", p.delayToReverb); B ("delaySync", p.delaySync);
    if (kv.count ("delayNote")) p.delayNote = (int) std::stof (kv["delayNote"]);
    B ("widthOn", p.widthOn); F ("widthLevelDb", p.widthLevelDb); F ("widthDelayMs", p.widthDelayMs); F ("widthShiftHz", p.widthShiftHz);
    F ("widthTiltDb", p.widthTiltDb); F ("widthHpf", p.widthHpf); F ("widthLpf", p.widthLpf); F ("widthBloomDb", p.widthBloomDb);
    F ("widthBloomMs", p.widthBloomMs); F ("widthMotion", p.widthMotion); B ("autoGainStage", p.autoGainStage);
    F ("duckDb", p.duckDb); F ("duckAttackMs", p.duckAttackMs); F ("duckReleaseMs", p.duckReleaseMs);
    if (kv.count ("targetCurve")) { auto v = parseList (kv["targetCurve"]); for (int i = 0; i < sc::kNumBands && i < (int) v.size(); ++i) p.targetCurve[i] = v[i]; p.hasTarget = true; }

    std::ifstream in (argv[2], std::ios::binary | std::ios::ate);
    size_t bytes = (size_t) in.tellg(); in.seekg (0);
    std::vector<float> x (bytes / 4); in.read ((char*) x.data(), (std::streamsize) bytes);
    size_t n = x.size() / 2; double fs = std::stod (argv[4]);
    std::vector<float> L (n), R (n);
    for (size_t i = 0; i < n; ++i) { L[i] = x[2 * i]; R[i] = x[2 * i + 1]; }

    sc::Chain chain;
    chain.p = p; chain.prepare (fs);
    if (argc > 5 && std::string (argv[5]) == "learn")
    {
        chain.learner.start();
        for (size_t i = 0; i < n; ++i) chain.learner.push (0.5f * (L[i] + R[i]));
        chain.learner.stop();
        float lv, cr, sp;
        if (chain.learner.summarise (p.sourceCurve, lv, cr, sp))
        {
            p.hasSource = true; p.sourceLevelDb = lv; p.hasSourceLevel = true; p.sourceCrestDb = cr; p.sourceSpreadDb = sp;
            std::fprintf (stderr, "learned: level %.1f dBFS crest %.1f dB spread %.1f dB\n", lv, cr, sp);
            auto corr = sc::computeCorrection (p);
            std::fprintf (stderr, "correction:");
            for (float c : corr) std::fprintf (stderr, " %.1f", c);
            std::fprintf (stderr, "\n");
        }
        chain.setParams (p);
    }
    const int block = 512;
    for (size_t i = 0; i < n; i += block)
    {
        int m = (int) std::min ((size_t) block, n - i);
        chain.process (L.data() + i, R.data() + i, m);
    }
    for (size_t i = 0; i < n; ++i) { x[2 * i] = L[i]; x[2 * i + 1] = R[i]; }
    // compensate the plugin latency so output aligns with input (like a DAW would)
    {
        int lat = chain.getLatency();
        std::vector<float> L2 (n, 0.0f), R2 (n, 0.0f);
        for (size_t i = 0; i + (size_t) lat < n; ++i) { L2[i] = L[i + (size_t) lat]; R2[i] = R[i + (size_t) lat]; }
        for (size_t i = 0; i < n; ++i) { x[2 * i] = L2[i]; x[2 * i + 1] = R2[i]; }
    }
    std::ofstream out (argv[3], std::ios::binary); out.write ((const char*) x.data(), (std::streamsize) (x.size() * 4));
    return 0;
}
