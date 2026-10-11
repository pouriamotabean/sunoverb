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
    F ("eqAmount", p.eqAmount); F ("eqMaxBoostDb", p.eqMaxBoostDb); F ("eqMaxBoostHighDb", p.eqMaxBoostHighDb); F ("eqMaxCutDb", p.eqMaxCutDb);
    F ("compTotalGrDb", p.compTotalGrDb); F ("targetCrestDb", p.targetCrestDb); F ("targetSpreadDb", p.targetSpreadDb);
    F ("comp1Ratio", p.comp1Ratio); F ("comp1AttackMs", p.comp1AttackMs); F ("comp1ReleaseMs", p.comp1ReleaseMs);
    F ("comp2Ratio", p.comp2Ratio); F ("comp2AttackMs", p.comp2AttackMs); F ("comp2ReleaseMs", p.comp2ReleaseMs);
    F ("deessFreq", p.deessFreq); F ("deessRatio", p.deessRatio); F ("deessTargetDb", p.deessTargetDb); F ("deessMaxDb", p.deessMaxDb);
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
    if (kv.count ("sibTarget")) { auto v = parseList (kv["sibTarget"]); for (int i = 0; i < 10 && i < (int) v.size(); ++i) p.sibTarget[(size_t) i] = v[(size_t) i]; p.hasSibTarget = true; }
    if (kv.count ("sibSource")) { auto v = parseList (kv["sibSource"]); for (int i = 0; i < 10 && i < (int) v.size(); ++i) p.sibSource[(size_t) i] = v[(size_t) i]; p.hasSibSource = true; }
    B ("hasSibTarget", p.hasSibTarget); B ("comp1Punch", p.comp1Punch); F ("punch", p.punch); F ("attackScale", p.attackScale); B ("sibCeiling", p.sibCeiling); F ("excAmount", p.excAmount); F ("excAdb", p.excAdb); F ("excBdb", p.excBdb); F ("airDb", p.airDb); F ("crispDepth", p.crispDepth); F ("eqAirMul", p.eqAirMul); B ("rideOn", p.rideOn); F ("rideRangeDb", p.rideRangeDb); F ("rideAmount", p.rideAmount); F ("rideSpeedMs", p.rideSpeedMs); F ("lookaheadMs", p.lookaheadMs); F ("wetMudDb", p.wetMudDb); F ("wetAirDb", p.wetAirDb); F ("onsetNarrowDb", p.onsetNarrowDb); F ("onsetNarrowMs", p.onsetNarrowMs); F ("rt60HighMul", p.rt60HighMul); F ("optoRatioPer", p.optoRatioPer); B ("useExciter", p.useExciter); if (kv.count ("listenMode")) p.listenMode = std::stoi (kv["listenMode"]); B ("deessRelDetect", p.deessRelDetect); F ("freezeHfDb", p.freezeHfDb); F ("voicedShare", p.voicedShare); F ("optoFastRelMs", p.optoFastRelMs); F ("optoSlowRelMs", p.optoSlowRelMs); F ("optoThrDb", p.optoThrDb); F ("optoRatioPer", p.optoRatioPer); F ("peakScale", p.peakScale); F ("levelerScale", p.levelerScale); F ("speedScale", p.speedScale); B ("duckRelative", p.duckRelative); F ("duckRelStartDb", p.duckRelStartDb); F ("duckRelRangeDb", p.duckRelRangeDb); F ("xoverLowHz", p.xoverLowHz); F ("xoverHighHz", p.xoverHighHz);
    auto A3 = [&] (const char* k, std::array<float, 3>& dst) { if (kv.count (k)) { auto v = parseList (kv[k]); for (int i = 0; i < 3 && i < (int) v.size(); ++i) dst[(size_t) i] = v[(size_t) i]; } };
    A3 ("revBandDb", p.revBandDb); A3 ("layerBandDb", p.layerBandDb); A3 ("delayBandDb", p.delayBandDb); F ("deessDetLoDb", p.deessDetLoDb); F ("deessDetSpanDb", p.deessDetSpanDb);
    if (kv.count ("targetCurve")) { auto v = parseList (kv["targetCurve"]); for (int i = 0; i < sc::kNumBands && i < (int) v.size(); ++i) p.targetCurve[i] = v[i]; p.hasTarget = true; }

    std::ifstream in (argv[2], std::ios::binary | std::ios::ate);
    size_t bytes = (size_t) in.tellg(); in.seekg (0);
    std::vector<float> x (bytes / 4); in.read ((char*) x.data(), (std::streamsize) bytes);
    size_t n = x.size() / 2; double fs = std::stod (argv[4]);
    std::vector<float> L (n), R (n);
    for (size_t i = 0; i < n; ++i) { L[i] = x[2 * i]; R[i] = x[2 * i + 1]; }

    if (argc > 5 && std::string (argv[5]) == "meter")   // analysis only: the plugin's width meter over the whole file
    {
        sc::WidthMeter wm; wm.prepare (fs, 0.0);
        for (size_t i = 0; i < n; ++i) wm.push (L[i], R[i]);
        std::array<float, 6> v {}; wm.values (v);
        std::fprintf (stderr, "meter:"); for (float a : v) std::fprintf (stderr, " %.2f", a); std::fprintf (stderr, "\n");
        return 0;
    }
    if (argc > 5 && std::string (argv[5]) == "sib")   // analysis only: sibilant curve of the whole file
    {
        sc::SibilantAnalyzer a; a.prepare (fs, (double) n / fs + 1.0);
        for (size_t i = 0; i < n; ++i) a.push (0.5f * (L[i] + R[i]));
        std::array<float, 10> c {}; int ns = 0;
        if (argc > 6)   // segments from a reference file (raw vocal, same length, interleaved stereo f32)
        {
            std::ifstream rf (argv[6], std::ios::binary | std::ios::ate); size_t rb = (size_t) rf.tellg(); rf.seekg (0);
            std::vector<float> rx (rb / 4); rf.read ((char*) rx.data(), (std::streamsize) rb);
            sc::SibilantAnalyzer ra; ra.prepare (fs, (double) n / fs + 1.0);
            for (size_t i = 0; i + 1 < rx.size() && i / 2 < n; i += 2) ra.push (0.5f * (rx[i] + rx[i + 1]));
            ns = a.summarise (c, &ra);
        }
        else ns = a.summarise (c);
        std::fprintf (stderr, "sibcurve: %d", ns); for (float v : c) std::fprintf (stderr, " %.2f", v); std::fprintf (stderr, "\n");
        return 0;
    }
    sc::Chain chain;
    chain.p = p; chain.prepare (fs);
    // learn = like the plugin: the vocal plays THROUGH the chain while Learn listens (input + output);
    // learn2 = learn twice (the second pass refines the closed-loop EQ, like pressing RE-LEARN)
    const std::string mode = argc > 5 ? std::string (argv[5]) : std::string();
    const int passes = mode == "learn" ? 1 : mode == "learn2" ? 2 : mode == "learn3" ? 3 : mode == "learn4" ? 4 : 0;
    const bool closedLoop = ! kv.count ("noClosedLoop");
    for (int pass = 0; pass < passes; ++pass)
    {
        const auto applied = sc::computeCorrection (p);
        std::vector<float> l2 (L), r2 (R);
        chain.reset(); chain.learner.start();
        for (size_t i = 0; i < n; i += 512) chain.process (l2.data() + i, r2.data() + i, (int) std::min ((size_t) 512, n - i));
        chain.learner.stop();
        float lv, cr, sp; std::array<float, sc::kNumBands> outC {};
        if (chain.learner.summarise (p.sourceCurve, lv, cr, sp, &outC))
        {
            p.hasSource = true; p.sourceLevelDb = lv; p.hasSourceLevel = true; p.sourceCrestDb = cr; p.sourceSpreadDb = sp;
            if (closedLoop) { p.chainEffect = sc::Learner::chainEffectFrom (p.sourceCurve, outC, applied); p.hasChainEffect = true; }
            std::fprintf (stderr, "learned: level %.1f dBFS crest %.1f dB spread %.1f dB\n", lv, cr, sp);
            std::fprintf (stderr, "chain effect:"); for (float c : p.chainEffect) std::fprintf (stderr, " %.1f", c); std::fprintf (stderr, "\n");
            auto corr = sc::computeCorrection (p);
            std::fprintf (stderr, "correction:"); for (float c : corr) std::fprintf (stderr, " %.1f", c); std::fprintf (stderr, "\n");
        }
        std::array<float, 10> sc10 {}; int ns = chain.learner.sib.summarise (sc10);
        if (ns >= 3 && ! kv.count ("sibSource")) { p.sibSource = sc10; p.hasSibSource = true; }
        std::fprintf (stderr, "sibcurve: %d", ns); for (float v : sc10) std::fprintf (stderr, " %.2f", v); std::fprintf (stderr, "\n");
        chain.setParams (p);
        std::fprintf (stderr, "szmatch: taken back %.1f dB  s-level before %.1f  suno %.1f\n", chain.sibAlpha, chain.sibLevelInfo[0], chain.sibLevelInfo[1]);
    }
    if (passes) chain.prepare (fs);   // fresh state for the render
    std::vector<float> sibDbg; if (std::getenv ("SIBDBG")) chain.sibCeil.dbg = &sibDbg;
    const int block = 512;
    for (size_t i = 0; i < n; i += block)
    {
        int m = (int) std::min ((size_t) block, n - i);
        chain.process (L.data() + i, R.data() + i, m);
    }
    if (! sibDbg.empty()) { FILE* f = std::fopen (std::getenv ("SIBDBG"), "wb"); std::fwrite (sibDbg.data(), 4, sibDbg.size(), f); std::fclose (f); }
    for (size_t i = 0; i < n; ++i) { x[2 * i] = L[i]; x[2 * i + 1] = R[i]; }
    // compensate the plugin latency so output aligns with input (like a DAW would)
    {
        int lat = chain.getLatency();
        std::vector<float> L2 (n, 0.0f), R2 (n, 0.0f);
        for (size_t i = 0; i + (size_t) lat < n; ++i) { L2[i] = L[i + (size_t) lat]; R2[i] = R[i + (size_t) lat]; }
        for (size_t i = 0; i < n; ++i) { x[2 * i] = L2[i]; x[2 * i + 1] = R2[i]; }
    }
    std::fprintf (stderr, "widthmeter:"); for (auto& m : chain.mWidthBands) std::fprintf (stderr, " %.2f", m.load()); std::fprintf (stderr, "\n");
    std::ofstream out (argv[3], std::ios::binary); out.write ((const char*) x.data(), (std::streamsize) (x.size() * 4));
    return 0;
}
