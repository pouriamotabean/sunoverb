#pragma once
// Suno Chain - DSP core (no JUCE dependency, so it can be rendered/tested offline).
// Chain: gain staging -> Match EQ -> Comp 1 -> Comp 2 -> De-esser -> Saturation  (= dry)
//        dry -> [Delay] + [Predelay -> Early reflections + 8-line FDN reverb -> wet EQ -> width]
//        wet is ducked by the dry vocal level, then mixed under the dry.
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <complex>
#include <atomic>
#include <cstdint>

namespace sc
{
constexpr double kPi = 3.14159265358979323846;
constexpr int kNumBands = 26;
inline const std::array<float, kNumBands>& bandCenters()
{
    static const std::array<float, kNumBands> c { 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                                                  1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000,
                                                  10000, 12500, 16000 };
    return c;
}
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float gainToDb (float g)  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float msToCoef (float ms, double fs) { return ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / (0.001 * ms * fs)); }

//==============================================================================
struct Params
{
    // global
    float amount = 1.0f;            // 0..1 scales everything
    float outputDb = 0.0f;
    bool  autoGainStage = true;     // normalise singing level to -18 dBFS internally (needs Learn)

    // match EQ: target curve comes from the preset, source curve from Learn
    // dB, normalised (mean of 250..4k bands = 0). Default = built-in "Suno Lead 01" so the plugin works without a preset file.
    std::array<float, kNumBands> targetCurve { -9.5f, -12.15f, -3.46f, -5.82f, -5.68f, 1.45f, 2.05f, 10.47f, 8.17f, 2.0f, 4.1f, 6.42f, 5.04f, 1.04f, 1.17f, -1.92f, -6.03f, -7.82f, -9.97f, -12.68f, -20.1f, -16.94f, -19.72f, -16.91f, -21.3f, -32.19f };
    std::array<float, kNumBands> sourceCurve {};   // dB, from Learn
    std::array<float, kNumBands> chainEffect {};   // dB, from Learn (v1.5): what the rest of the chain does to the spectrum
    bool  hasChainEffect = false;
    bool  hasTarget = true, hasSource = false;
    float eqAmount = 1.0f, eqMaxBoostDb = 12.0f, eqMaxCutDb = 24.0f, eqLowWeight = 0.35f;
    float eqMaxBoostHighDb = 15.0f;   // v1.5: from 6 kHz up (air), where dark vocals need more than 12 dB to reach Suno

    // dynamics
    float compTotalGrDb = 6.0f;     // planned total gain reduction on typical loud syllables
    float targetCrestDb = 9.0f;     // from the Suno vocal; used when Learn measured your crest
    float sourceCrestDb = 0.0f;     // from Learn (0 = unknown)
    float targetSpreadDb = 5.5f;    // p90-p10 of 400 ms levels in the Suno vocal (phrase-level consistency)
    float sourceSpreadDb = 0.0f;    // from Learn (0 = unknown)
    float comp1Ratio = 4.0f, comp1AttackMs = 2.0f, comp1ReleaseMs = 50.0f, comp1Share = 0.55f;
    bool  comp1Punch = true;        // v1.5: comp 1 without look-ahead (onsets pass)
    float punch = 1.5f;             // v1.5: transient emphasis on syllable onsets, fitted so onsets stand out like Suno's
    // v1.5 Advanced Dynamics (1 = as learned / preset): peak compressor amount, leveler strength, leveler speed
    float peakScale = 1.0f, levelerScale = 1.0f, speedScale = 1.0f;
    float comp2Ratio = 3.0f, comp2AttackMs = 30.0f, comp2ReleaseMs = 300.0f;   // ratio = max ratio for the leveler
    float levelerFactor = 2.1f;     // calibration: nominal ratio needed per unit of spread excess (v1.5: 2.1 with Punch on)
    float compAmount = 1.0f;

    // sibilance balancer (v1.3): level of "s"/"z"/"sh" relative to the vowel around them, as in the Suno vocal.
    // deessTargetDb = detector threshold (sibilant over vowel, dB) = Suno median + deessOffsetDb (calibration),
    // deessRatio = how strongly sibilants above it are pulled down. deessFreq / deessMaxDb: unused since v1.3.
    float deessFreq = 4000.0f, deessTargetDb = -2.5f, deessRatio = 3.0f, deessMaxDb = 20.0f, deessAmount = 1.0f;
    // S/Z Match (v1.3): sibilant spectrum relative to the preceding vowel, bands 2k..16k (10 values).
    // Target from the preset (Suno), source from Learn. Default target = Suno Lead 01.
    std::array<float, 10> sibTarget { -25.18f, -26.2f, -23.47f, -16.63f, -16.35f, -15.0f, -15.38f, -11.27f, -14.66f, -23.78f };
    std::array<float, 10> sibSource {};
    bool hasSibTarget = true, hasSibSource = false;
    float deessDetLoDb = -14.0f, deessDetSpanDb = 6.0f;   // detector: raw high-band share where S/Z Match starts / is fully on

    // saturation (not measurable from stems: preset default)
    float satDriveDb = 6.0f, satMix = 0.15f;

    // delay (tempo-synced by default; feeds the reverb as well)
    bool  delayOn = false;
    float delayMs = 375.0f, delayFeedback = 0.25f, delayLevelDb = -18.0f, delayHpf = 300.0f, delayLpf = 5000.0f;
    bool  delayPingPong = true;
    bool  delaySync = true;
    int   delayNote = 2;                // 0 = 1/4, 1 = 1/8 dotted, 2 = 1/8, 3 = 1/8 triplet, 4 = 1/16
    float delayToReverb = 0.5f;         // 0..1: how much of the echo is sent into the reverb
    float bpm = 120.0f;                 // from the host

    // vocal width layer (side-only, mono-safe): two slowly modulated short delays, L/R opposite
    bool  widthOn = true;
    float widthLevelDb = -27.0f;        // layer level relative to the dry vocal
    float widthDelayMs = 1.0f, widthShiftHz = 1.0f, widthTiltDb = 0.0f;
    float widthHpf = 200.0f, widthLpf = 16000.0f;
    float widthBloomDb = 12.0f, widthBloomMs = 200.0f;   // layer starts lower at phrase starts (measured in Suno)
    float widthMotion = 0.0f;           // 0 = constant (Suno), 1 = only on held notes

    // multiband width (v1.3): gain on the Side signal of each source in three bands (Linkwitz-Riley 4th order,
    // the bands sum flat). Mid and mono stay untouched. dB, 0 = unchanged.
    float xoverLowHz = 300.0f, xoverHighHz = 4000.0f;
    std::array<float, 3> revBandDb { 0, 0, 0 }, layerBandDb { 0, 0, 0 }, delayBandDb { 0, 0, 0 };

    // reverb
    bool  reverbOn = true;
    float predelayMs = 180.0f, rt60 = 3.0f, rt60LowMul = 1.0f, rt60HighMul = 0.75f, crossoverHz = 1500.0f;
    float size = 1.0f, modDepth = 0.5f, diffusion = 0.75f, diffusionSize = 2.0f;
    float earlyMs = 22.0f, earlyLevelDb = -100.0f;   // discrete early taps off: Suno's reverb has no slap
    float wetHpf = 300.0f, wetLpf = 5000.0f, wetBumpHz = 2500.0f, wetBumpDb = 2.0f;
    float width = 1.0f;
    float reverbLevelDb = -10.0f;   // un-ducked wet level relative to the dry vocal (stationary signal)

    // ducking of the wet bus by the dry vocal
    float duckDb = 7.0f, duckAttackMs = 5.0f, duckReleaseMs = 120.0f;
    bool  duckRelative = true; float duckRelStartDb = 4.0f, duckRelRangeDb = 10.0f;   // v1.5 relative ducking

    // learned
    float sourceLevelDb = -18.0f;   // median singing RMS of your input (dBFS), from Learn
    bool  hasSourceLevel = false;
};

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    inline float process (float x, int ch)
    {
        double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }
    void setNorm (double B0, double B1, double B2, double A0, double A1, double A2)
    { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }
    void setPeak (double fs, double f, double q, double db)
    {
        f = std::min (f, fs * 0.45);
        double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        setNorm (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setHPF (double fs, double f, double q = 0.7071)
    {
        f = std::min (f, fs * 0.45);
        double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        setNorm ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setLPF (double fs, double f, double q = 0.7071)
    {
        f = std::min (f, fs * 0.45);
        double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        setNorm ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighShelf (double fs, double f, double db, double q = 0.7071)
    {
        f = std::min (f, fs * 0.45);
        double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), sn = std::sin (w), al = sn / (2 * q), sq = 2 * std::sqrt (A) * al;
        setNorm (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                 (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void setAllpass (double fs, double f, double q = 0.7071)
    {
        f = std::min (f, fs * 0.45);
        double w = 2 * kPi * f / fs, al = std::sin (w) / (2 * q), c = std::cos (w);
        setNorm (1 - al, -2 * c, 1 + al, 1 + al, -2 * c, 1 - al);
    }
    double magDb (double fs, double f) const
    {
        std::complex<double> z = std::polar (1.0, -2 * kPi * f / fs);
        auto num = b0 + b1 * z + b2 * z * z;
        auto den = 1.0 + a1 * z + a2 * z * z;
        return 20 * std::log10 (std::max (1e-12, std::abs (num / den)));
    }
};

//==============================================================================
// 26-band graphic EQ with iterative gain solving, so the cascade really hits the requested curve.
struct MatchEQ
{
    static constexpr double kQ = 2.2;
    std::array<Biquad, kNumBands> bands;
    std::array<float, kNumBands> gainsDb {};
    double fs = 48000;

    void prepare (double sampleRate) { fs = sampleRate; for (auto& b : bands) { b.reset(); b.setIdentity(); } }

    // desired: correction in dB at each band centre
    static std::array<float, kNumBands> solve (const std::array<float, kNumBands>& desired, double fs)
    {
        std::array<float, kNumBands> g = desired;
        std::array<Biquad, kNumBands> tmp;
        const auto& fc = bandCenters();
        for (int it = 0; it < 12; ++it)
        {
            for (int k = 0; k < kNumBands; ++k) tmp[k].setPeak (fs, fc[k], kQ, g[k]);
            for (int b = 0; b < kNumBands; ++b)
            {
                if (fc[b] > fs * 0.45) continue;
                double r = 0;
                for (int k = 0; k < kNumBands; ++k) r += tmp[k].magDb (fs, fc[b]);
                g[b] = (float) std::clamp (g[b] + 0.75 * (desired[b] - r), -30.0, 18.0);
            }
        }
        return g;
    }
    void setGains (const std::array<float, kNumBands>& g)
    {
        gainsDb = g;
        const auto& fc = bandCenters();
        for (int k = 0; k < kNumBands; ++k)
        {
            if (fc[k] > fs * 0.45 || std::abs (g[k]) < 0.01f) bands[k].setIdentity();
            else bands[k].setPeak (fs, fc[k], kQ, g[k]);
        }
    }
    inline float process (float x, int ch) { for (auto& b : bands) x = b.process (x, ch); return x; }
    double responseDb (double f) const { double r = 0; for (auto& b : bands) r += b.magDb (fs, f); return r; }
};

// correction = target - source, smoothed and limited
inline std::array<float, kNumBands> computeCorrection (const Params& p)
{
    std::array<float, kNumBands> d {};
    if (! (p.hasTarget && p.hasSource)) return d;
    std::array<float, kNumBands> raw {};
    for (int i = 0; i < kNumBands; ++i) raw[i] = p.targetCurve[i] - p.sourceCurve[i] - (p.hasChainEffect ? p.chainEffect[i] : 0.0f);
    for (int i = 0; i < kNumBands; ++i)   // light 3-band smoothing
    {
        float s = raw[i] * 2.0f, w = 2.0f;
        if (i > 0) { s += raw[i - 1]; w += 1; }
        if (i < kNumBands - 1) { s += raw[i + 1]; w += 1; }
        // Low end: the Suno target there mostly reflects the Suno singer's pitch range and stem bleed.
        //  < 120 Hz : cuts only, never boost (boosting would only add rumble / mud)
        //  120-300  : cuts in full, boosts scaled by "Low Match"
        float v = s / w * p.eqAmount * p.amount;
        const float fc = bandCenters()[i];
        if (fc < 120.0f)      v = std::min (v, 0.0f);
        else if (fc < 300.0f) v = v > 0.0f ? v * p.eqLowWeight : v;
        d[i] = std::clamp (v, -p.eqMaxCutDb, fc >= 6000.0f ? std::max (p.eqMaxBoostDb, p.eqMaxBoostHighDb) : p.eqMaxBoostDb);
    }
    return d;
}

//==============================================================================
struct Compressor
{
    float thrDb = 0, ratio = 1, knee = 6, atk = 0, rel = 0, makeup = 1, gr = 0;
    void set (double fs, float thresholdDb, float r, float attackMs, float releaseMs, float makeupDb)
    { thrDb = thresholdDb; ratio = std::max (1.0f, r); atk = msToCoef (attackMs, fs); rel = msToCoef (releaseMs, fs); makeup = dbToGain (makeupDb); }
    void reset() { gr = 0; }
    // hold = true freezes the gain reduction (used during pauses so the next phrase starts at the same level)
    inline float computeGain (float detector, bool hold = false)
    {
        if (hold) return dbToGain (-gr) * makeup;
        float lv = gainToDb (detector), over = lv - thrDb, target;
        if (2 * over < -knee) target = 0;
        else if (2 * std::abs (over) <= knee) { float t = over + knee / 2; target = (1 - 1 / ratio) * t * t / (2 * knee); }
        else target = (1 - 1 / ratio) * over;
        gr = target > gr ? atk * gr + (1 - atk) * target : rel * gr + (1 - rel) * target;
        return dbToGain (-gr) * makeup;
    }
};

//==============================================================================
// radix-2 FFT, in place
inline void fftInPlace (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        double ang = -2 * kPi / (double) len;
        std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1, 0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
}

// Sibilant analysis shared by Learn (your voice) and the preset analyzer (Suno vocal): spectrum of "s"/"z"/"sh"
// relative to the vowel level just before them, in the 26-band grid from 2 kHz up.
// 10 ms frames, band powers from a ~21 ms Hann FFT (sharp bands: no leakage from the boosted top end); sibilant frame = high band (>4 kHz, LR4) share above -6 dB and level within 30 dB of the loud
// singing; segments of at least 60 ms; vowel reference = median level of the non-sibilant frames 30..250 ms before.
struct SibilantAnalyzer
{
    static constexpr int kFirstBand = 16;                     // 2000 Hz
    static constexpr int kSibBands = kNumBands - kFirstBand;  // 10 bands: 2k .. 16k
    int maxFrames = 12000;                                    // 120 s (Learn); the analyzer uses more
    Biquad hf1, hf2; double fs = 48000;
    int nfft = 1024; std::vector<float> ring, win; int ringPos = 0; std::vector<std::complex<float>> fbuf;
    std::array<int, kSibBands> k0 {}, k1 {}; double winNorm = 1;
    std::vector<float> full, hf, bands;   // per frame: power
    int frameLen = 480, pos = 0, n = 0; double aFull = 0, aHf = 0; std::array<double, kSibBands> aB {};   // aB unused since FFT bands
    void prepare (double s, double seconds = 120.0)
    {
        fs = s; frameLen = std::max (1, (int) std::round (0.01 * fs)); maxFrames = (int) (seconds * 100.0);
        hf1.setHPF (fs, 4000.0); hf2.setHPF (fs, 4000.0); hf1.reset(); hf2.reset();
        nfft = 512; while (nfft < 0.021 * fs) nfft <<= 1;          // ~21 ms window (1024 at 48 kHz)
        ring.assign ((size_t) nfft, 0.0f); win.resize ((size_t) nfft); fbuf.assign ((size_t) nfft, {}); ringPos = 0; winNorm = 0;
        for (int i = 0; i < nfft; ++i) { win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / nfft)); winNorm += win[(size_t) i] * win[(size_t) i]; }
        for (int b = 0; b < kSibBands; ++b)   // third-octave band edges in FFT bins
        {
            const double fc = bandCenters()[(size_t) (kFirstBand + b)];
            k0[(size_t) b] = std::max (1, (int) std::ceil (fc / std::pow (2.0, 1.0 / 6) * nfft / fs));
            k1[(size_t) b] = std::min (nfft / 2 - 1, (int) std::floor (fc * std::pow (2.0, 1.0 / 6) * nfft / fs));
        }
        full.assign ((size_t) maxFrames, 0.0f); hf.assign ((size_t) maxFrames, 0.0f); bands.assign ((size_t) maxFrames * kSibBands, 0.0f);
        reset();
    }
    void reset() { pos = 0; n = 0; aFull = aHf = 0; aB.fill (0); std::fill (ring.begin(), ring.end(), 0.0f); ringPos = 0; }
    inline void push (float x)
    {
        if (n >= maxFrames) return;
        float h = hf2.process (hf1.process (x, 0), 0);
        aFull += x * x; aHf += h * h;
        ring[(size_t) ringPos] = x; ringPos = (ringPos + 1) % nfft;
        if (++pos >= frameLen)
        {
            full[(size_t) n] = (float) (aFull / frameLen); hf[(size_t) n] = (float) (aHf / frameLen);
            for (int i = 0; i < nfft; ++i) fbuf[(size_t) i] = { ring[(size_t) ((ringPos + i) % nfft)] * win[(size_t) i], 0.0f };
            fftInPlace (fbuf);
            for (int b = 0; b < kSibBands; ++b)   // band power (mean-square units, same scale as "full")
            {
                double e = 0; for (int k = k0[(size_t) b]; k <= k1[(size_t) b]; ++k) e += std::norm (fbuf[(size_t) k]);
                bands[(size_t) n * kSibBands + b] = (float) (2.0 * e / (winNorm * nfft));
            }
            ++n; pos = 0; aFull = aHf = 0; aB.fill (0);
        }
    }
    // curve: dB re vowel per band (2k..16k). Returns the number of sibilant segments used (needs >= 3).
    // segRef (analysis tools only): take the sibilant segments from another, time-aligned analysis (e.g. the
    // raw vocal) and measure this one on exactly those segments.
    int summarise (std::array<float, kSibBands>& curve, const SibilantAnalyzer* segRef = nullptr) const
    {
        const int N = segRef ? std::min (n, segRef->n) : n;
        if (N < 100) return 0;
        auto levels = [N] (const SibilantAnalyzer& a, std::vector<float>& fdb, std::vector<float>& sh)
        {
            fdb.resize ((size_t) N); sh.resize ((size_t) N);
            for (int i = 0; i < N; ++i) { fdb[(size_t) i] = 10 * std::log10 (a.full[(size_t) i] + 1e-20f); sh[(size_t) i] = 10 * std::log10 ((a.hf[(size_t) i] + 1e-20f) / (a.full[(size_t) i] + 1e-20f)); }
        };
        std::vector<float> fdb, sh, rdb, rsh;
        levels (*this, fdb, sh);
        if (segRef) levels (*segRef, rdb, rsh); else { rdb = fdb; rsh = sh; }
        auto srt = rdb; std::sort (srt.begin(), srt.end());
        const float rtop = srt[(size_t) (0.95 * (N - 1))];
        auto srt2 = fdb; std::sort (srt2.begin(), srt2.end());
        const float top = srt2[(size_t) (0.95 * (N - 1))];
        std::vector<char> sib ((size_t) N);
        for (int i = 0; i < N; ++i) sib[(size_t) i] = rsh[(size_t) i] > -6.0f && rdb[(size_t) i] > rtop - 30.0f;
        std::vector<std::array<float, kSibBands>> rows;
        for (int i = 0; i < N;)
        {
            if (! sib[(size_t) i]) { ++i; continue; }
            int j = i; while (j < N && sib[(size_t) j]) ++j;
            if (j - i >= 6 && i > 28)
            {
                std::vector<float> v;
                for (int k = i - 25; k < i - 3; ++k) if (! sib[(size_t) k] && fdb[(size_t) k] > top - 30.0f) v.push_back (fdb[(size_t) k]);
                if (v.size() >= 5)
                {
                    std::sort (v.begin(), v.end()); const float vref = v[v.size() / 2];
                    std::array<float, kSibBands> row {};
                    for (int b = 0; b < kSibBands; ++b)
                    {
                        double e = 0; for (int k = i; k < j; ++k) e += bands[(size_t) k * kSibBands + b];
                        row[(size_t) b] = (float) (10 * std::log10 (e / (j - i) + 1e-20)) - vref;
                    }
                    rows.push_back (row);
                }
            }
            i = j;
        }
        if (rows.size() < 3) return (int) rows.size();
        for (int b = 0; b < kSibBands; ++b)
        {
            std::vector<float> c; for (auto& r : rows) c.push_back (r[(size_t) b]);
            std::sort (c.begin(), c.end()); curve[(size_t) b] = c[c.size() / 2];
        }
        return (int) rows.size();
    }
};

struct DeEsser
{
    // "S/Z Match" (v1.3). The Match EQ makes your vowels sound like Suno's; this makes your "s"/"z"/"sh" sound
    // like Suno's: a second set of EQ gains (from 1.25 kHz up) that is blended in only while the sound is
    // sibilant. delta = (Suno sibilant spectrum - your sibilant spectrum) - vowel EQ correction, so on an "s"
    // the total correction is exactly the sibilant match. Both spectra are measured relative to the vowel
    // before each sibilant, so the "s" level relative to the vowel follows Suno too.
    //  - detection on the signal BEFORE the Match EQ (raw high-band share: vowels below about -16 dB,
    //    sibilants above about -10 dB), so bright vowels are never touched and nothing can freeze;
    //  - released within ~30 ms after the sibilant; the detector runs 5 ms ahead (lookahead).
    // Without a learned sibilant curve it simply takes the vowel EQ's high boost back off the sibilants.
    static constexpr int kFirst = 14;              // 1250 Hz
    static constexpr int kN = kNumBands - kFirst;  // 12 filters
    std::array<Biquad, kN> f; std::array<float, kN> gainsDb {};
    Biquad hp1, hp2; double sfs = 48000;
    float ePreHf = 0, ePreFull = 0, envCoef = 0, atk = 0, rel = 0, amount = 1.0f, maxCut = 0, detLo = -14.0f, detSpan = 6.0f;
    float k = 0, kTarget = 0, kApplied = -1; int counter = 0;
    // the compressors pull vowels down more than the quieter sibilants; on a sibilant the S/Z Match also
    // applies the gain reduction the preceding vowel had, so the "s" keeps its Suno level over the vowel
    float vowelGainDb = 0, compLift = 0, liftTarget = 0, liftG = 1;

    void prepare (double fs)
    {
        sfs = fs; vowelGainDb = 0; compLift = liftTarget = 0; liftG = 1; hp1.reset(); hp2.reset(); hp1.setHPF (fs, 4000.0); hp2.setHPF (fs, 4000.0);
        for (auto& u : f) { u.reset(); u.setIdentity(); }
        envCoef = msToCoef (5.0f, fs); atk = msToCoef (1.5f, fs); rel = msToCoef (30.0f, fs);
        k = kTarget = 0; kApplied = -1;
    }
    // delta (dB per band, all 26; only >= 2 kHz used) at amount 1; solved for the overlapping cascade
    void setDelta (std::array<float, kNumBands> delta, double fs)
    {
        for (int i = 0; i < kNumBands; ++i) if (bandCenters()[(size_t) i] < 2000.0f) delta[(size_t) i] = 0.0f;
        auto g = MatchEQ::solve (delta, fs);
        maxCut = 0;
        for (int i = 0; i < kN; ++i)
        {
            float v = g[(size_t) (kFirst + i)];
            if (bandCenters()[(size_t) (kFirst + i)] > fs * 0.45) v = 0;
            gainsDb[(size_t) i] = v; maxCut = std::max (maxCut, -v);
        }
        kApplied = -1;
    }
    // compGainDb: current gain of the compressors (dB, <= 0 when reducing), same time base as pre
    inline void detect (float preL, float preR, float compGainDb)   // un-delayed, before the Match EQ
    {
        float a = hp2.process (hp1.process (preL, 0), 0), b = hp2.process (hp1.process (preR, 1), 1);
        ePreHf   = envCoef * ePreHf   + (1 - envCoef) * 0.5f * (a * a + b * b);
        ePreFull = envCoef * ePreFull + (1 - envCoef) * 0.5f * (preL * preL + preR * preR);
        if ((++counter & 15) != 0) return;
        const float shareDb = 10 * std::log10 ((ePreHf + 1e-14f) / (ePreFull + 1e-12f));
        const bool voiced = 10 * std::log10 (ePreFull + 1e-12f) > -60.0f;
        const float w = voiced ? std::clamp ((shareDb - detLo) / detSpan, 0.0f, 1.0f) : 0.0f;
        kTarget = w * amount;
        if (voiced && w <= 0.0f) { const float c = 0.97f; vowelGainDb = c * vowelGainDb + (1 - c) * compGainDb; }   // ~25 ms
        liftTarget = std::clamp (compGainDb - vowelGainDb, 0.0f, 12.0f) * kTarget;
    }
    inline void apply (float& l, float& r)
    {
        k = kTarget > k ? atk * k + (1 - atk) * kTarget : rel * k + (1 - rel) * kTarget;
        compLift = liftTarget > compLift ? atk * compLift + (1 - atk) * liftTarget : rel * compLift + (1 - rel) * liftTarget;
        if (compLift > 0.01f) { const float g = dbToGain (-compLift); l *= g; r *= g; }
        const float kq = k < 0.003f ? 0.0f : k;
        if (std::abs (kq - kApplied) > 0.01f || (kq == 0.0f && kApplied != 0.0f))
        {
            for (int i = 0; i < kN; ++i)
            {
                const float g = kq * gainsDb[(size_t) i];
                if (std::abs (g) < 0.01f) f[(size_t) i].setIdentity();
                else f[(size_t) i].setPeak (sfs, bandCenters()[(size_t) (kFirst + i)], MatchEQ::kQ, g);
            }
            kApplied = kq;
        }
        for (auto& u : f) { l = u.process (l, 0); r = u.process (r, 1); }   // always run (identity when idle)
    }
    float currentCut() const { return k * maxCut; }   // dB at the most-cut band (meter)
};

//==============================================================================
struct DelayLine
{
    std::vector<float> buf; int w = 0, mask = 0;
    void allocate (int maxSamples)
    { int n = 1; while (n < maxSamples + 4) n <<= 1; buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    inline float read (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
    inline float readFrac (float d) const
    {
        int i = (int) d; float f = d - (float) i;
        return read (i) * (1 - f) + read (i + 1) * f;
    }
};

//==============================================================================
struct StereoDelay
{
    DelayLine l, r; Biquad hp, lp; double fs = 48000; float dSmooth = -1.0f, dCoef = 0.0f;
    void prepare (double s) { fs = s; l.allocate ((int) (2.1 * fs)); r.allocate ((int) (2.1 * fs)); hp.reset(); lp.reset(); dSmooth = -1.0f; dCoef = msToCoef (50.0f, fs); }
    void set (float hpf, float lpf) { hp.setHPF (fs, hpf); lp.setLPF (fs, lpf); }
    inline void process (float in, float timeMs, float fb, bool pingPong, float& outL, float& outR)
    {
        float target = std::clamp ((float) (timeMs * 0.001 * fs), 1.0f, (float) (2.0 * fs));
        dSmooth = dSmooth < 0 ? target : dCoef * dSmooth + (1 - dCoef) * target;   // glide on tempo changes
        float yl = l.readFrac (dSmooth), yr = r.readFrac (dSmooth);
        float fl = lp.process (hp.process (yl, 0), 0), fr = lp.process (hp.process (yr, 1), 1);
        if (pingPong) { l.push (in + fr * fb); r.push (fl * fb); }
        else          { l.push (in + fl * fb); r.push (in + fr * fb); }
        outL = fl; outR = fr;
    }
};

inline float noteMs (int note, float bpm)
{
    static const float beats[5] = { 1.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f };   // 1/4, 1/8., 1/8, 1/8T, 1/16
    return 60000.0f / std::max (20.0f, bpm) * beats[std::clamp (note, 0, 4)];
}

//==============================================================================
// 90-degree phase splitter (two allpass chains, Olli Niemitalo's coefficients): gives an analytic signal
// I + jQ that is accurate from ~0.002*fs to ~0.498*fs.
struct Hilbert
{
    static constexpr int kN = 4;
    // coefficients are squared in the filter: H(z) = (a^2 - z^-2) / (1 - a^2 z^-2); path 1 delayed by one sample.
    // Verified: 90 +/- 1 degrees from 30 Hz to 23 kHz at 48 kHz.
    double a1[kN] { 0.6923878, 0.9360654322959, 0.9882295226860, 0.9987488452737 };
    double a2[kN] { 0.4021921162426, 0.8561710882420, 0.9722909545651, 0.9952884791278 };
    double x1[kN][2] {}, y1[kN][2] {}, x2[kN][2] {}, y2[kN][2] {}, iDelay = 0;
    Hilbert() { for (int i = 0; i < kN; ++i) { a1[i] *= a1[i]; a2[i] *= a2[i]; } }
    void reset() { for (int i = 0; i < kN; ++i) for (int k = 0; k < 2; ++k) x1[i][k] = y1[i][k] = x2[i][k] = y2[i][k] = 0; iDelay = 0; }
    static inline double chain (double x, const double* a, double (*xs)[2], double (*ys)[2])
    {
        for (int i = 0; i < kN; ++i)
        {
            double y = a[i] * (x + ys[i][1]) - xs[i][1];
            xs[i][1] = xs[i][0]; xs[i][0] = x; ys[i][1] = ys[i][0]; ys[i][0] = y;
            x = y;
        }
        return x;
    }
    // I and Q are 90 degrees apart (Q leads I); analytic signal = I - jQ
    inline void process (float in, float& I, float& Q)
    {
        double i = chain (in, a1, x1, y1);
        I = (float) iDelay; iDelay = i;
        Q = (float) chain (in, a2, x2, y2);
    }
};

// Vocal width layer, modelled on the Suno measurement: a dry-correlated component that lives only in
// the Side channel (so mono playback is untouched), band-limited with a gentle high tilt, near-zero
// delay. Two single-sideband frequency shifts (+f1 / -f2 Hz): the same tiny phase rotation at every
// frequency, which is what the Suno layer showed (high frequencies stay coherent as long as lows).
struct MicroWidth
{
    DelayLine bufI, bufQ; Biquad hp, lp, tilt; Hilbert hil; double fs = 48000;
    double phA = 0, phB = 0.31, incA = 0, incB = 0; float dA = 0, dB = 0, gain = 0;
    // phrase / syllable tracking for bloom and motion
    float envF = 0, envS = 0, cF = 0, cS = 0, silentFor = 1e9f, sinceOnset = 1e9f, gEnv = 1, gCoefUp = 0, gCoefDn = 0;
    float bloomFloor = 1, bloomSamples = 1, motion = 0, phraseAge = 1e9f, pauseLen = 0;
    void prepare (double s)
    {
        fs = s; bufI.allocate ((int) (0.03 * fs)); bufQ.allocate ((int) (0.03 * fs)); hp.reset(); lp.reset(); tilt.reset(); hil.reset();
        cF = msToCoef (5, fs); cS = msToCoef (80, fs); gCoefUp = msToCoef (60, fs); gCoefDn = msToCoef (25, fs);
    }
    void set (const Params& p, float amt)
    {
        hp.setHPF (fs, p.widthHpf, 0.7071); lp.setLPF (fs, p.widthLpf, 0.7071);
        tilt.setHighShelf (fs, 2500.0, p.widthTiltDb);
        incA = p.widthShiftHz / fs; incB = p.widthShiftHz * 1.37 / fs;
        dA = (float) (p.widthDelayMs * 0.001 * fs); dB = (float) (p.widthDelayMs * 1.6 * 0.001 * fs);
        gain = (p.widthOn && amt > 0) ? dbToGain (p.widthLevelDb) * amt * 1.41421356f : 0.0f;   // two voices -> side
        bloomFloor = dbToGain (-std::max (0.0f, p.widthBloomDb)); bloomSamples = (float) (std::max (1.0f, p.widthBloomMs) * 0.001 * fs);
        motion = std::clamp (p.widthMotion, 0.0f, 1.0f);
    }
    // returns the side signal to add (L += s, R -= s)
    inline float process (float mono)
    {
        if (gain <= 0) return 0.0f;
        float x = tilt.process (lp.process (hp.process (mono, 0), 0), 0);
        float I, Q; hil.process (x, I, Q);
        bufI.push (I); bufQ.push (Q);
        phA += incA; if (phA >= 1) phA -= 1; phB += incB; if (phB >= 1) phB -= 1;
        const double tw = 2 * kPi;
        float ia = bufI.readFrac (dA), qa = bufQ.readFrac (dA), ib = bufI.readFrac (dB), qb = bufQ.readFrac (dB);
        float a = ia * (float) std::cos (tw * phA) + qa * (float) std::sin (tw * phA);   // shifted up   (Re{(I - jQ) e^{+j phi}})
        float b = ib * (float) std::cos (tw * phB) - qb * (float) std::sin (tw * phB);   // shifted down (Re{(I - jQ) e^{-j phi}})
        // envelopes for bloom (phrase starts) and motion (held notes vs syllable onsets)
        float e = mono * mono;
        envF = cF * envF + (1 - cF) * e; envS = cS * envS + (1 - cS) * e;
        float lf = 10 * std::log10 (envF + 1e-12f), ls = 10 * std::log10 (envS + 1e-12f);
        bool silent = lf < -18.0f - 30.0f;
        silentFor = silent ? silentFor + 1 : 0;
        if (! silent && lf > ls + 6.0f) sinceOnset = 0; else sinceOnset += 1;
        // bloom: after a pause the layer starts lower and opens over bloomMs. Depth and time scale with
        // the length of the pause (Suno: mono for ~200 ms after real silence, only slightly lower after a breath).
        if (silent) { if (silentFor > 0.08f * (float) fs) { phraseAge = 0; pauseLen = silentFor; } }
        else phraseAge += 1;
        float depth = std::min (1.0f, pauseLen / (0.4f * (float) fs));
        float floorG = std::pow (bloomFloor, depth);
        float bloom = floorG + (1 - floorG) * std::min (1.0f, phraseAge / (bloomSamples * (1.0f + depth)));
        // motion: on held notes only (opens ~150 ms after the last syllable onset)
        float held = std::clamp ((sinceOnset - 0.05f * (float) fs) / (0.15f * (float) fs), 0.0f, 1.0f);
        float target = bloom * (1 - motion + motion * held);
        gEnv = target > gEnv ? gCoefUp * gEnv + (1 - gCoefUp) * target : gCoefDn * gEnv + (1 - gCoefDn) * target;
        return 0.5f * (a - b) * gain * gEnv;
    }
};

//==============================================================================
// Three-band gain on a side signal: LR4 crossovers (low band all-pass compensated), so with all gains at
// 0 dB the result is only an all-pass of the side channel. Gains glide to avoid zipper noise.
struct SideBands
{
    Biquad lpA, lpB, hpA, hpB, lp2A, lp2B, hp2A, hp2B, ap; double fs = 48000;
    std::array<float, 3> g { 1, 1, 1 }, gT { 1, 1, 1 }; float gc = 0;
    void prepare (double s) { fs = s; for (auto* b : { &lpA, &lpB, &hpA, &hpB, &lp2A, &lp2B, &hp2A, &hp2B, &ap }) b->reset(); gc = msToCoef (20.0f, fs); }
    void set (float fLow, float fHigh, const std::array<float, 3>& db, float scale = 1.0f)
    {
        fLow = std::clamp (fLow, 40.0f, 2000.0f); fHigh = std::clamp (fHigh, fLow * 2.0f, (float) (fs * 0.4));
        lpA.setLPF (fs, fLow); lpB.setLPF (fs, fLow); hpA.setHPF (fs, fLow); hpB.setHPF (fs, fLow);
        lp2A.setLPF (fs, fHigh); lp2B.setLPF (fs, fHigh); hp2A.setHPF (fs, fHigh); hp2B.setHPF (fs, fHigh);
        ap.setAllpass (fs, fHigh);
        for (int i = 0; i < 3; ++i) gT[(size_t) i] = dbToGain (std::clamp (db[(size_t) i], -40.0f, 18.0f) * scale);
    }
    inline float process (float s, int ch = 0)
    {
        for (int i = 0; i < 3; ++i) g[(size_t) i] = gc * g[(size_t) i] + (1 - gc) * gT[(size_t) i];
        float lo = ap.process (lpB.process (lpA.process (s, ch), ch), ch);
        float hi = hpB.process (hpA.process (s, ch), ch);
        float mid = lp2B.process (lp2A.process (hi, ch), ch);
        float top = hp2B.process (hp2A.process (hi, ch), ch);
        return g[0] * lo + g[1] * mid + g[2] * top;
    }
};

//==============================================================================
// 8-line FDN, Hadamard feedback, two-band per-line decay, light delay modulation.
struct FDNReverb
{
    static constexpr int N = 8;
    std::array<DelayLine, N> lines;
    std::array<float, N> len {}, gLow {}, gHigh {}, lpState {}, modPhase {}, modRate {};
    float lpA = 0, modDepthSamp = 0, outScale = 1; double fs = 48000;
    DelayLine pre;
    std::array<float, 8> erTime {}, erGain {};
    float erLevel = 0.4f;
    // input diffusion: series allpasses smear the first echoes (Suno's reverb arrives diffuse, no slap)
    static constexpr int kAp = 6;
    std::array<DelayLine, kAp> ap; std::array<int, kAp> apLen {}; float apG = 0.7f; int nAp = kAp;

    void prepare (double s)
    {
        fs = s;
        for (auto& d : lines) d.allocate ((int) (0.25 * fs));
        pre.allocate ((int) (0.6 * fs));
        for (auto& a : ap) a.allocate ((int) (0.06 * fs));
        for (int i = 0; i < N; ++i) { lpState[i] = 0; modPhase[i] = (float) i * 0.7f; modRate[i] = 0.37f + 0.11f * (float) i; }
    }
    void clear() { for (auto& d : lines) d.clear(); pre.clear(); for (auto& a : ap) a.clear(); lpState.fill (0); }

    void set (const Params& p)
    {
        static const float base[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.7f, 73.1f };
        float rtL = std::max (0.1f, p.rt60 * p.rt60LowMul), rtH = std::max (0.1f, p.rt60 * p.rt60HighMul);
        double gMidSum = 0;
        for (int i = 0; i < N; ++i)
        {
            len[i] = (float) (base[i] * p.size * 0.001 * fs);
            gLow[i]  = (float) std::pow (10.0, -3.0 * len[i] / (rtL * fs));
            gHigh[i] = (float) std::pow (10.0, -3.0 * len[i] / (rtH * fs));
            double gm = std::pow (10.0, -3.0 * len[i] / (p.rt60 * fs));
            gMidSum += gm * gm;
        }
        lpA = (float) (1.0 - std::exp (-2 * kPi * p.crossoverHz / fs));
        modDepthSamp = (float) (p.modDepth * 0.0006 * fs);   // modDepth 1 = 0.6 ms
        // steady-state energy normalisation (calibrated in tests): output ~ input power * level
        double g2 = gMidSum / N;
        // x2.72 (= +8.7 dB, measured with white noise) so that level 0 dB gives a stationary wet
        // signal with the same power per channel as the dry input, roughly independent of RT60.
        outScale = (float) (std::sqrt (1.0 - g2) * 2.72);
        static const float erT[8] = { 0.35f, 0.5f, 0.62f, 0.75f, 0.88f, 1.0f, 1.15f, 1.3f };
        for (int i = 0; i < 8; ++i) { erTime[i] = (float) (erT[i] * p.earlyMs * 0.001 * fs); erGain[i] = std::pow (0.85f, (float) i) * 0.5f; }
        erLevel = dbToGain (p.earlyLevelDb);
        static const float apMs[kAp] = { 4.7f, 7.3f, 11.1f, 16.9f, 23.3f, 31.7f };
        for (int i = 0; i < kAp; ++i) apLen[(size_t) i] = std::max (1, (int) (apMs[i] * p.size * p.diffusionSize * 0.001 * fs));
        apG = std::clamp (p.diffusion, 0.0f, 0.85f);
        nAp = apG > 0.01f ? kAp : 0;
    }

    inline void process (float in, float predelaySamples, float& outL, float& outR)
    {
        pre.push (in);
        float x = pre.readFrac (std::max (0.0f, predelaySamples));
        for (int i = 0; i < nAp; ++i)
        {
            float d = ap[(size_t) i].read (apLen[(size_t) i] - 1);
            float v = x + apG * d;
            ap[(size_t) i].push (v);
            x = d - apG * v;
        }
        // early reflections, alternating sides
        float eL = 0, eR = 0;
        for (int i = 0; i < 8; ++i)
        {
            float v = pre.readFrac (std::max (0.0f, predelaySamples + erTime[i])) * erGain[i];
            if (i & 1) eR += v; else eL += v;
        }
        // read lines
        float y[N];
        for (int i = 0; i < N; ++i)
        {
            float d = len[i];
            if (i < 4)
            {
                modPhase[i] += (float) (2 * kPi * modRate[i] / fs);
                if (modPhase[i] > 2 * kPi) modPhase[i] -= (float) (2 * kPi);
                d += modDepthSamp * (1.0f + std::sin (modPhase[i]));
            }
            float v = lines[i].readFrac (d);
            lpState[i] += lpA * (v - lpState[i]);
            y[i] = gLow[i] * lpState[i] + gHigh[i] * (v - lpState[i]);
        }
        // outputs before mixing (decorrelated taps)
        outL = (y[0] - y[2] + y[4] - y[6] + y[1] * 0.5f - y[5] * 0.5f) * outScale * 0.9f + eL * erLevel;
        outR = (y[1] - y[3] + y[5] - y[7] + y[2] * 0.5f - y[6] * 0.5f) * outScale * 0.9f + eR * erLevel;
        // Hadamard 8 (fast WHT), normalised
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j) { float a = y[j], b = y[j + h]; y[j] = a + b; y[j + h] = a - b; }
        const float norm = 0.35355339f;
        static const float inSign[N] = { 1, -1, 1, 1, -1, 1, -1, -1 };
        for (int i = 0; i < N; ++i) lines[i].push (y[i] * norm + x * inSign[i] * 0.35355339f);
    }
};

//==============================================================================
struct Learner
{
    // collects spectrum (4096 FFT frames) + 50 ms crest blocks of the raw input
    static constexpr int kFft = 4096;
    static constexpr int kMaxFrames = 1200;     // ~100 s at 48k
    static constexpr int kMaxBlocks = 4000;
    std::vector<float> frameBands;              // kMaxFrames * kNumBands (dB)
    std::vector<float> frameBandsOut;           // same frames, the plugin's own output (mid): closed-loop EQ (v1.5)
    std::vector<float> frameLevel;              // dB
    std::vector<float> blockRms, blockPeak;     // dB
    std::vector<float> fifo, fifoOut; int fifoPos = 0;
    int blockLen = 2400, blockPos = 0; float bSum = 0, bPeak = 0;
    std::atomic<int> nFrames { 0 }, nBlocks { 0 };
    std::atomic<bool> active { false };
    double fs = 48000;
    std::vector<std::complex<float>> buf, bufOut;
    std::vector<float> window;
    SibilantAnalyzer sib;

    void prepare (double s)
    {
        fs = s; blockLen = (int) (0.05 * fs); sib.prepare (s);
        frameBands.assign ((size_t) kMaxFrames * kNumBands, 0.0f); frameBandsOut.assign ((size_t) kMaxFrames * kNumBands, 0.0f);
        frameLevel.assign (kMaxFrames, -200.0f); fifoOut.assign (kFft, 0.0f); bufOut.assign (kFft, {});
        blockRms.assign (kMaxBlocks, -200.0f); blockPeak.assign (kMaxBlocks, -200.0f);
        fifo.assign (kFft, 0.0f); buf.assign (kFft, {}); window.resize (kFft);
        for (int i = 0; i < kFft; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / (kFft - 1)));
    }
    void start() { nFrames = 0; nBlocks = 0; fifoPos = 0; blockPos = 0; bSum = 0; bPeak = 0; sib.reset(); active = true; }
    void stop() { active = false; }

    static void fft (std::vector<std::complex<float>>& a) { fftInPlace (a); }

    // x = raw input (mono), out = the plugin's output mid for the same sample
    inline void push (float x, float out = 0.0f)   // audio thread
    {
        if (! active) return;
        sib.push (x);
        // crest blocks
        bSum += x * x; bPeak = std::max (bPeak, std::abs (x));
        if (++blockPos >= blockLen)
        {
            int b = nBlocks.load();
            if (b < kMaxBlocks)
            {
                blockRms[(size_t) b] = 10 * std::log10 (bSum / (float) blockLen + 1e-20f);
                blockPeak[(size_t) b] = gainToDb (bPeak);
                nBlocks = b + 1;
            }
            blockPos = 0; bSum = 0; bPeak = 0;
        }
        fifoOut[(size_t) fifoPos] = out;
        fifo[(size_t) fifoPos++] = x;
        if (fifoPos >= kFft)
        {
            fifoPos = 0;
            int f = nFrames.load();
            if (f >= kMaxFrames) { active = false; return; }
            double e = 0;
            for (int i = 0; i < kFft; ++i) { buf[(size_t) i] = { fifo[(size_t) i] * window[(size_t) i], 0.0f }; e += fifo[(size_t) i] * fifo[(size_t) i]; }
            fft (buf);
            for (int i = 0; i < kFft; ++i) bufOut[(size_t) i] = { fifoOut[(size_t) i] * window[(size_t) i], 0.0f };
            fft (bufOut);
            frameLevel[(size_t) f] = (float) (10 * std::log10 (e / kFft + 1e-20));
            const auto& fc = bandCenters();
            for (int b = 0; b < kNumBands; ++b)
            {
                double lo = fc[b] / std::pow (2.0, 1.0 / 6), hi = fc[b] * std::pow (2.0, 1.0 / 6);
                int k0 = std::max (1, (int) std::ceil (lo * kFft / fs)), k1 = std::min (kFft / 2, (int) std::floor (hi * kFft / fs));
                double s = 0, so = 0; int cnt = 0;
                for (int k = k0; k <= k1; ++k) { s += std::norm (buf[(size_t) k]); so += std::norm (bufOut[(size_t) k]); ++cnt; }
                frameBands[(size_t) f * kNumBands + b] = (float) (10 * std::log10 (cnt > 0 ? s / cnt : 1e-20) + 1e-9);
                frameBandsOut[(size_t) f * kNumBands + b] = (float) (10 * std::log10 (cnt > 0 ? so / cnt + 1e-20 : 1e-20));
            }
            nFrames = f + 1;
        }
    }

    // closed-loop EQ: effect of the rest of the chain = output - (input + correction that was active), smoothed
    static std::array<float, kNumBands> chainEffectFrom (const std::array<float, kNumBands>& source, const std::array<float, kNumBands>& out,
                                                         const std::array<float, kNumBands>& appliedCorr)
    {
        std::array<float, kNumBands> expect {}, e {}, r {};
        for (int b = 0; b < kNumBands; ++b) expect[(size_t) b] = source[(size_t) b] + appliedCorr[(size_t) b];
        normaliseCurve (expect);
        for (int b = 0; b < kNumBands; ++b) r[(size_t) b] = out[(size_t) b] - expect[(size_t) b];
        for (int b = 0; b < kNumBands; ++b)
        {
            float s = r[(size_t) b] * 2, w = 2;
            if (b > 0) { s += r[(size_t) b - 1]; w += 1; }
            if (b < kNumBands - 1) { s += r[(size_t) b + 1]; w += 1; }
            const float fc = bandCenters()[(size_t) b];
            e[(size_t) b] = fc < 120.0f ? 0.0f : std::clamp (s / w, -6.0f, 6.0f);   // lows: Learn material too thin to trust
        }
        return e;
    }

    // message thread: summarise. Returns false if not enough material.
    // outCurve (optional): normalised spectrum of the plugin's output over the same frames
    bool summarise (std::array<float, kNumBands>& curve, float& levelDb, float& crestDb, float& spreadDb,
                    std::array<float, kNumBands>* outCurve = nullptr) const
    {
        int nf = nFrames.load(), nb = nBlocks.load();
        if (nf < 20 || nb < 40) return false;
        std::vector<float> lv (frameLevel.begin(), frameLevel.begin() + nf);
        auto sorted = lv; std::sort (sorted.begin(), sorted.end());
        float p95 = sorted[(size_t) (0.95 * (nf - 1))];
        std::array<double, kNumBands> acc {}, accOut {}; int used = 0;
        for (int f = 0; f < nf; ++f)
        {
            if (lv[(size_t) f] < p95 - 25) continue;
            for (int b = 0; b < kNumBands; ++b)
            {
                acc[b] += std::pow (10.0, frameBands[(size_t) f * kNumBands + b] / 10.0);
                accOut[b] += std::pow (10.0, frameBandsOut[(size_t) f * kNumBands + b] / 10.0);
            }
            ++used;
        }
        if (used < 10) return false;
        for (int b = 0; b < kNumBands; ++b) curve[b] = (float) (10 * std::log10 (acc[b] / used + 1e-30));
        normaliseCurve (curve);
        if (outCurve != nullptr)
        {
            for (int b = 0; b < kNumBands; ++b) (*outCurve)[(size_t) b] = (float) (10 * std::log10 (accOut[b] / used + 1e-30));
            normaliseCurve (*outCurve);
        }
        // blocks
        std::vector<float> br (blockRms.begin(), blockRms.begin() + nb), bp (blockPeak.begin(), blockPeak.begin() + nb);
        auto s2 = br; std::sort (s2.begin(), s2.end());
        float top = s2[(size_t) (0.95 * (nb - 1))];
        std::vector<float> act, crest;
        for (int i = 0; i < nb; ++i) if (br[(size_t) i] > top - 20) { act.push_back (br[(size_t) i]); crest.push_back (bp[(size_t) i] - br[(size_t) i]); }
        if (act.size() < 20) return false;
        std::sort (act.begin(), act.end()); std::sort (crest.begin(), crest.end());
        levelDb = act[act.size() / 2];
        crestDb = crest[crest.size() / 2];
        // phrase-level consistency: 400 ms windows whose 8 x 50 ms blocks are all voiced (> top - 15 dB)
        std::vector<float> a4;
        for (int i = 0; i + 8 <= nb; i += 8)
        {
            bool voiced = true; double e = 0;
            for (int k = 0; k < 8; ++k) { float v = br[(size_t) (i + k)]; voiced = voiced && v > top - 15; e += std::pow (10.0, v / 10.0); }
            if (voiced) a4.push_back ((float) (10 * std::log10 (e / 8 + 1e-20)));
        }
        if (a4.size() >= 5) { std::sort (a4.begin(), a4.end()); spreadDb = a4[(size_t) (0.9 * (a4.size() - 1))] - a4[(size_t) (0.1 * (a4.size() - 1))]; }
        else spreadDb = 0;
        return true;
    }
    static void normaliseCurve (std::array<float, kNumBands>& c)
    {
        double m = 0; int n = 0; const auto& fc = bandCenters();
        for (int b = 0; b < kNumBands; ++b) if (fc[b] >= 250 && fc[b] <= 4000) { m += c[b]; ++n; }
        m /= n;
        for (auto& v : c) v = (float) (v - m);
    }
};

//==============================================================================
// Width meter: side-minus-mid per band (low 100-300 Hz, mid 400-3000 Hz, high 5-12 kHz), separately while
// singing and in the pauses. It classifies time exactly like the preset analyzer (singing = mid 12 dB above
// side and within 30 dB of the loud parts; pause = 60-400 ms after a phrase of >= 400 ms, if the pause lasts
// >= 150 ms), from the signal it measures, so the plugin's WIDTH graph and the Suno target in the preset are
// the same measurement (render_cli 'meter' mode runs this on the Suno vocal).
struct WidthMeter
{
    double fs = 48000; int hop5 = 240;
    // 5 ms envelopes (10 ms window, 20 ms smoothing in dB), as the analyzer's env5
    std::vector<float> rM, rS; int rPos = 0, rLen = 480, c5 = 0; double sM = 0, sS = 0;
    std::array<float, 4> hM {}, hS {}; int hI = 0;
    float top = -200, topUp = 0, topDn = 0;
    // labels per 5 ms frame: 0 none, 1 singing, 2 pause, -1 pending
    static constexpr int kLab = 512; std::array<signed char, kLab> lab {}; long frame = 0;
    int singRun = 0, nonRun = 0; bool phraseOk = false; long pendStart = -1;
    // FFT
    int nfft = 2048, fftHop = 512, fPos = 0, fCount = 0; std::vector<float> ringM, ringS, win;
    std::vector<std::complex<float>> bM, bS; std::array<int, 3> k0 {}, k1 {};
    struct Pending { long centre; std::array<double, 6> e; };
    std::vector<Pending> queue;
    std::array<std::array<double, 6>, 2> acc {}; double coef = 1.0; bool sumAll = false;

    void prepare (double s, double memorySeconds)
    {
        fs = s; hop5 = std::max (1, (int) std::round (0.005 * fs)); rLen = 2 * hop5;
        rM.assign ((size_t) rLen, 0.0f); rS.assign ((size_t) rLen, 0.0f); rPos = 0; c5 = 0; sM = sS = 0;
        hM.fill (-200.0f); hS.fill (-200.0f); hI = 0; top = -200; frame = 0; lab.fill (0);
        topUp = (float) (1.0 - std::exp (-0.005 / 2.0)); topDn = 0.3f * 0.005f;   // rises over ~2 s, falls 0.3 dB/s
        singRun = nonRun = 0; phraseOk = false; pendStart = -1;
        nfft = 1024; while (nfft < 0.0427 * fs) nfft <<= 1; fftHop = nfft / 4; fPos = 0; fCount = 0;
        ringM.assign ((size_t) nfft, 0.0f); ringS.assign ((size_t) nfft, 0.0f); win.resize ((size_t) nfft);
        bM.assign ((size_t) nfft, {}); bS.assign ((size_t) nfft, {});
        for (int i = 0; i < nfft; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / nfft));
        const double lo[3] { 100, 400, 5000 }, hi[3] { 300, 3000, 12000 };
        for (int b = 0; b < 3; ++b) { k0[(size_t) b] = std::max (1, (int) std::ceil (lo[b] * nfft / fs)); k1[(size_t) b] = std::min (nfft / 2 - 1, (int) std::floor (hi[b] * nfft / fs)); }
        queue.clear(); queue.reserve (64);
        for (auto& a : acc) a.fill (0.0);
        sumAll = memorySeconds <= 0; coef = sumAll ? 1.0 : std::exp (-(double) fftHop / (memorySeconds * fs));
    }
    inline void push (float l, float r)
    {
        const float m = 0.5f * (l + r), sd = 0.5f * (l - r);
        // 10 ms running mean square
        sM += (double) m * m - (double) rM[(size_t) rPos] * rM[(size_t) rPos];
        sS += (double) sd * sd - (double) rS[(size_t) rPos] * rS[(size_t) rPos];
        rM[(size_t) rPos] = m; rS[(size_t) rPos] = sd; rPos = (rPos + 1) % rLen;
        ringM[(size_t) fPos] = m; ringS[(size_t) fPos] = sd; fPos = (fPos + 1) % nfft;
        if (++c5 >= hop5) { c5 = 0; frame5(); }
        if (++fCount >= fftHop) { fCount = 0; analyse(); }
    }
    void frame5()
    {
        hM[(size_t) hI] = 10 * std::log10 ((float) std::max (sM, 0.0) / rLen + 1e-20f);
        hS[(size_t) hI] = 10 * std::log10 ((float) std::max (sS, 0.0) / rLen + 1e-20f); hI = (hI + 1) & 3;
        const float em = 0.25f * (hM[0] + hM[1] + hM[2] + hM[3]), es = 0.25f * (hS[0] + hS[1] + hS[2] + hS[3]);
        if (em > top) top += (em - top) * topUp; else top -= topDn;
        if (em > top + 6.0f) top = em - 6.0f;                     // never far below the loud parts
        const bool sing = (em - es) > 12.0f && em > top - 30.0f;
        const long f = frame++;
        auto& L = lab[(size_t) (f % kLab)];
        if (sing)
        {
            if (pendStart >= 0) { for (long k = pendStart; k < f; ++k) if (lab[(size_t) (k % kLab)] == -1) lab[(size_t) (k % kLab)] = 0; pendStart = -1; }
            ++singRun; nonRun = 0; L = 1;
            return;
        }
        if (singRun > 0) { phraseOk = singRun >= 80; singRun = 0; nonRun = 0; }
        ++nonRun;
        // pause window 60..400 ms after a long enough phrase; confirmed once the pause has lasted 150 ms
        if (phraseOk && nonRun > 12 && nonRun <= 80 && em > top - 60.0f)
        {
            if (nonRun < 30) { L = -1; if (pendStart < 0) pendStart = f; }
            else
            {
                if (pendStart >= 0) { for (long k = pendStart; k < f; ++k) if (lab[(size_t) (k % kLab)] == -1) lab[(size_t) (k % kLab)] = 2; pendStart = -1; }
                L = 2;
            }
        }
        else L = 0;
    }
    void analyse()
    {
        for (int i = 0; i < nfft; ++i)
        {
            const int k = (fPos + i) % nfft;
            bM[(size_t) i] = { ringM[(size_t) k] * win[(size_t) i], 0.0f };
            bS[(size_t) i] = { ringS[(size_t) k] * win[(size_t) i], 0.0f };
        }
        fftInPlace (bM); fftInPlace (bS);
        Pending pd; pd.centre = frame - (long) std::round ((nfft / 2.0) / hop5);
        for (int b = 0; b < 3; ++b)
        {
            double em = 0, es = 0;
            for (int k = k0[(size_t) b]; k <= k1[(size_t) b]; ++k) { em += std::norm (bM[(size_t) k]); es += std::norm (bS[(size_t) k]); }
            pd.e[(size_t) (2 * b)] = em; pd.e[(size_t) (2 * b + 1)] = es;
        }
        if (queue.size() < 60) queue.push_back (pd);
        // resolve frames whose label is final
        size_t keep = 0;
        for (size_t q = 0; q < queue.size(); ++q)
        {
            const auto& it = queue[q];
            if (it.centre < 0 || frame - it.centre >= kLab - 8) continue;           // too old / before start
            if (it.centre >= frame) { queue[keep++] = it; continue; }
            const int L = lab[(size_t) (it.centre % kLab)];
            if (L == -1) { queue[keep++] = it; continue; }
            if (L == 1 || L == 2)
            {
                auto& a = acc[L == 1 ? 0 : 1];
                for (int i = 0; i < 6; ++i) a[(size_t) i] = (sumAll ? a[(size_t) i] : coef * a[(size_t) i]) + (sumAll ? 1.0 : (1 - coef)) * it.e[(size_t) i];
            }
        }
        queue.resize (keep);
    }
    // side - mid in dB: [0..2] singing low/mid/high, [3..5] pauses; -100 = no data yet
    void values (std::array<float, 6>& out) const
    {
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < 3; ++b)
            {
                const double em = acc[(size_t) c][(size_t) (2 * b)], es = acc[(size_t) c][(size_t) (2 * b + 1)];
                out[(size_t) (c * 3 + b)] = em > 1e-12 ? (float) (10 * std::log10 ((es + 1e-30) / em)) : -100.0f;
            }
    }
};

//==============================================================================
class Chain
{
public:
    Params p;
    MatchEQ eq;
    Compressor c1, c2;
    DeEsser deess;
    StereoDelay delay;
    FDNReverb rev;
    MicroWidth widthLayer;
    SideBands revSB, layerSB, delaySB;
    Learner learner;
    WidthMeter widthMeter;   // output width per band (WIDTH graph)
    Biquad wetHp, wetLp, wetBump;
    DelayLine laL, laR;                       // lookahead for the compressors
    int lookahead = 0;
    float duckEnv = 0, duckGr = 0, duckEnvCoef = 0, duckAtk = 0, duckRel = 0, phraseRef = -100, phraseDecay = 0;
    float rmsEnv = 0, rmsCoef = 0, holdEnv = 0, holdCoef = 0, holdDb = -60; bool punchOn = true;
    float trFast = 0, trSlow = 0, trFa = 0, trFr = 0, trSa = 0, trSr = 0, trGainDb = 0, trSm = 0, punchAmt = 0;
    float inGain = 1, outGain = 1, wetGain = 0, delayGain = 0, delaySend = 0, satDrive = 1, satMix = 0;
    float predelaySamp = 0;
    double fs = 48000;
    std::array<float, kNumBands> lastCorrection {}, lastSibDelta {};
    bool sibValid = false;
    float sibAlpha = 0; std::array<float, 2> sibLevelInfo { 0, 0 };   // analysis: share of the lift taken back; s level before / Suno
    float eqMakeup = 1.0f;
    bool eqValid = false;
    // meters (written by the audio thread, read by the editor)
    std::atomic<float> mGr1 { 0 }, mGr2 { 0 }, mDuck { 0 }, mIn { -100 }, mOut { -100 }, mWidth { -100 }, mDelayMs { 0 }, mDeess { 0 };
    std::atomic<uint32_t> mBlocks { 0 };   // lets the editor see when the host stopped calling us
    std::array<std::atomic<float>, 6> mWidthBands {};    // side-mid dB: [0..2] singing low/mid/high, [3..5] pauses (-100 = no data); bands 100-300 / 400-3k / 5-12k

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lookahead = (int) std::round (0.005 * fs);
        laL.allocate (lookahead + 4); laR.allocate (lookahead + 4);
        eq.prepare (fs); eqValid = false; sibValid = false; deess.prepare (fs); c1.reset(); c2.reset(); delay.prepare (fs); rev.prepare (fs);
        widthLayer.prepare (fs); learner.prepare (fs);
        revSB.prepare (fs); layerSB.prepare (fs); delaySB.prepare (fs);
        widthMeter.prepare (fs, 8.0);
        for (auto& m : mWidthBands) m = -100.0f;
        wetHp.reset(); wetLp.reset(); wetBump.reset();
        setParams (p);
    }
    int getLatency() const { return lookahead; }
    void reset() { rev.clear(); delay.l.clear(); delay.r.clear(); laL.clear(); laR.clear(); c1.reset(); c2.reset(); duckGr = duckEnv = 0; }

    void setParams (const Params& np)
    {
        p = np;
        const float amt = std::clamp (p.amount, 0.0f, 1.0f);
        // gain staging: bring singing level to -18 dBFS internally, restore afterwards
        float stage = (p.autoGainStage && p.hasSourceLevel) ? std::clamp (-18.0f - p.sourceLevelDb, -24.0f, 30.0f) : 0.0f;
        inGain = dbToGain (stage);
        outGain = dbToGain (-stage + p.outputDb);
        auto corr = computeCorrection (p);
        if (corr != lastCorrection || ! eqValid) { eq.setGains (MatchEQ::solve (corr, fs)); lastCorrection = corr; eqValid = true; }

        // Comp 1 (fast, peak detector): gain reduction from the crest-factor difference
        const bool learned = p.sourceCrestDb > 0.0f;
        const float crestIn = learned ? p.sourceCrestDb : 12.0f;
        const float spreadIn = (learned && p.sourceSpreadDb > 0.0f) ? p.sourceSpreadDb : 8.0f;
        const float cAmt = p.compAmount * amt;
        float g1 = learned ? std::clamp ((crestIn - p.targetCrestDb) * 1.3f, 0.0f, 10.0f) : p.compTotalGrDb * p.comp1Share;
        g1 *= cAmt * std::max (0.0f, p.peakScale);
        const float typicalPeak = -18.0f + crestIn;
        const float thr1 = g1 > 0.05f ? typicalPeak - g1 / (1.0f - 1.0f / p.comp1Ratio) : 50.0f;
        c1.set (fs, thr1, p.comp1Ratio, p.comp1AttackMs, p.comp1ReleaseMs, g1 * 0.7f);
        punchOn = p.comp1Punch;
        punchAmt = std::clamp (p.punch, 0.0f, 2.0f) * cAmt;
        trFa = msToCoef (0.5f, fs); trFr = msToCoef (40.0f, fs); trSa = msToCoef (30.0f, fs); trSr = msToCoef (40.0f, fs); trSm = msToCoef (2.0f, fs);

        // Comp 2 (leveler, 20 ms RMS detector, lookahead, holds during pauses).
        // The Suno target includes its reverb (+~0.8 dB of spread), so the dry aim is slightly lower.
        const float spreadTarget = std::max (2.0f, p.targetSpreadDb - 0.8f);
        float r2 = learned ? std::clamp (1.0f + p.levelerFactor * std::max (0.0f, p.levelerScale) * (spreadIn / spreadTarget - 1.0f), 1.0f, std::max (1.0f, p.comp2Ratio))
                           : std::min (1.6f, std::max (1.0f, p.comp2Ratio));   // gentle default until Learn
        r2 = 1.0f + (r2 - 1.0f) * cAmt;
        const float thr2 = -18.0f - spreadIn * 0.5f - 2.0f;     // RMS domain, just under the quiet phrases
        const float makeup2 = (-18.0f - thr2) * (1.0f - 1.0f / r2);
        const float spd = std::clamp (p.speedScale, 0.25f, 4.0f);   // >1 = faster
        c2.set (fs, r2 > 1.01f ? thr2 : 50.0f, r2, p.comp2AttackMs / spd, p.comp2ReleaseMs / spd, r2 > 1.01f ? makeup2 : 0.0f);
        rmsCoef = msToCoef (20.0f, fs);
        holdCoef = msToCoef (10.0f, fs);
        holdDb = -18.0f - 20.0f;                                  // below this the singer is pausing: freeze gains

        // keep loudness after the match EQ: energy change of the correction, weighted by your spectrum
        eqMakeup = 1.0f;
        if (p.hasSource && p.hasTarget)
        {
            double a = 0, b = 0;
            for (int i = 0; i < kNumBands; ++i)
            {
                double e = std::pow (10.0, p.sourceCurve[(size_t) i] / 10.0);
                a += e; b += e * std::pow (10.0, corr[(size_t) i] / 10.0);
            }
            eqMakeup = (float) std::sqrt (a / std::max (b, 1e-12));
        }
        {   // S/Z Match (v1.5): bring the overall level of your sibilants (relative to the vowel before them) to
            // Suno's, by taking back part of the high lift the vowel EQ gave them. The level is the sum over the
            // bands from 2 kHz up, which stays stable even with only a few "s" in the Learn take (the single
            // bands do not). Never boosts sibilants; at most 3 dB darker than your raw "s".
            std::array<float, kNumBands> delta {};
            const int f0 = SibilantAnalyzer::kFirstBand;
            const float makeupDb = gainToDb (eqMakeup);   // the EQ make-up lifts sibilants as well
            std::array<float, kNumBands> u {};
            for (int b = f0; b < kNumBands; ++b) u[(size_t) b] = std::max (0.0f, corr[(size_t) b] + makeupDb);
            for (int b = f0; b < kNumBands; ++b)   // smooth shape, + up to 3 dB below raw
            {
                float sm = u[(size_t) b] * 2, w = 2;
                if (b > f0) { sm += u[(size_t) b - 1]; w += 1; }
                if (b < kNumBands - 1) { sm += u[(size_t) b + 1]; w += 1; }
                delta[(size_t) b] = sm / w + (bandCenters()[(size_t) b] > fs * 0.45 ? 0.0f : 3.0f);
            }
            float alpha = 0;
            if (p.hasSource && p.hasSibSource && p.hasSibTarget)
            {
                auto sumDb = [&] (auto fn) { double e = 0; for (int b = f0; b < kNumBands; ++b) e += std::pow (10.0, fn (b) / 10.0); return 10 * std::log10 (e + 1e-30); };
                const double lTarget = sumDb ([&] (int b) { return (double) p.sibTarget[(size_t) (b - f0)]; });
                auto post = [&] (int b, float a) { return (double) (p.sibSource[(size_t) (b - f0)] + corr[(size_t) b] + makeupDb - a * delta[(size_t) b]); };
                const double lPost = sumDb ([&] (int b) { return post (b, 0.0f); });
                const double want = std::max (0.0, lPost - lTarget) * std::clamp (p.deessAmount, 0.0f, 2.0f);
                if (want > 0.05)
                {
                    float lo = 0, hi = 1;
                    for (int it = 0; it < 30; ++it)
                    {
                        const float mid = 0.5f * (lo + hi);
                        if (lPost - sumDb ([&] (int b) { return post (b, mid); }) < want) lo = mid; else hi = mid;
                    }
                    alpha = 0.5f * (lo + hi);
                }
                sibLevelInfo = { (float) lPost, (float) lTarget };
            }
            else alpha = std::clamp (p.deessAmount, 0.0f, 1.0f) * 0.75f;   // no learned s/z: take most of the lift back
            for (int b = 0; b < kNumBands; ++b) delta[(size_t) b] = b >= f0 ? -alpha * delta[(size_t) b] : 0.0f;
            sibAlpha = alpha;
            if (delta != lastSibDelta || ! sibValid) { deess.setDelta (delta, fs); lastSibDelta = delta; sibValid = true; }
        }
        deess.amount = 1.0f; deess.detLo = p.deessDetLoDb; deess.detSpan = std::max (1.0f, p.deessDetSpanDb);
        satDrive = dbToGain (p.satDriveDb); satMix = p.satMix * amt;

        delay.set (p.delayHpf, p.delayLpf);
        delayGain = (p.delayOn && amt > 0) ? dbToGain (p.delayLevelDb) * amt : 0.0f;
        delaySend = (p.delayOn && amt > 0) ? std::clamp (p.delayToReverb, 0.0f, 1.0f) * dbToGain (p.delayLevelDb + 6.0f) : 0.0f;

        rev.set (p);
        predelaySamp = (float) (p.predelayMs * 0.001 * fs);
        wetHp.setHPF (fs, std::max (20.0f, p.wetHpf), 0.9);
        wetLp.setLPF (fs, std::min ((float) (fs * 0.45), p.wetLpf), 0.8);
        wetBump.setPeak (fs, p.wetBumpHz, 1.0, p.wetBumpDb);
        wetGain = (p.reverbOn && amt > 0) ? dbToGain (p.reverbLevelDb) * amt : 0.0f;
        widthLayer.set (p, amt);
        revSB.set (p.xoverLowHz, p.xoverHighHz, p.revBandDb);
        layerSB.set (p.xoverLowHz, p.xoverHighHz, p.layerBandDb);
        delaySB.set (p.xoverLowHz, p.xoverHighHz, p.delayBandDb);

        duckEnvCoef = msToCoef (10.0f, fs);
        duckAtk = msToCoef (p.duckAttackMs, fs); duckRel = msToCoef (p.duckReleaseMs, fs);
        phraseDecay = (float) (6.0 / fs);   // phrase reference falls 6 dB/s
    }

    float currentDelayMs() const { return p.delaySync ? noteMs (p.delayNote, p.bpm) : p.delayMs; }

    void process (float* L, float* R, int n)
    {
        const float duckDepth = p.duckDb * std::clamp (p.amount, 0.0f, 1.0f);
        const float presenceFloor = -18.0f - 15.0f;
        const float dlyMs = currentDelayMs();
        float maxGr1 = 0, maxGr2 = 0, maxDuck = 0, pkIn = 0, pkOut = 0, wE = 0, dE = 0, maxDs = 0;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R ? R[i] : L[i];
            pkIn = std::max (pkIn, std::max (std::abs (l), std::abs (r)));
            const float rawMono = 0.5f * (l + r);
            l *= inGain; r *= inGain;
            const float preL = l, preR = r;
            l = eq.process (l, 0) * eqMakeup; r = eq.process (r, 1) * eqMakeup;
            // detectors run on the un-delayed signal, gains are applied to the 5 ms delayed signal
            float pk = std::max (std::abs (l), std::abs (r)), e = 0.5f * (l * l + r * r);
            rmsEnv = rmsCoef * rmsEnv + (1 - rmsCoef) * e;
            holdEnv = holdCoef * holdEnv + (1 - holdCoef) * e;
            const bool pause = 10 * std::log10 (holdEnv + 1e-12f) < holdDb;
            laL.push (l); laR.push (r);
            const float laOutL = laL.read (lookahead - 1), laOutR = laR.read (lookahead - 1);
            // Comp 1 (peak): with Punch it listens to the delayed signal, i.e. it reacts AFTER the start of a
            // syllable instead of anticipating it, so consonant/onset transients pass (Suno's onsets stand
            // out more than a look-ahead compressor allows). The leveler keeps its look-ahead.
            const float pkNow = punchOn ? std::max (std::abs (laOutL), std::abs (laOutR)) : pk;
            float g1 = c1.computeGain (pkNow, pause);
            float g2 = c2.computeGain (std::sqrt (rmsEnv) * g1, pause);   // RMS domain
            maxGr1 = std::max (maxGr1, c1.gr); maxGr2 = std::max (maxGr2, c2.gr);
            deess.detect (preL, preR, gainToDb (g1 * g2));     // 5 ms ahead of the audio it acts on
            float gT = 1.0f;
            if (punchAmt > 0)
            {   // transient emphasis: fast envelope above slow envelope = a syllable is starting (detected 5 ms ahead)
                trFast = e > trFast ? trFa * trFast + (1 - trFa) * e : trFr * trFast + (1 - trFr) * e;
                trSlow = e > trSlow ? trSa * trSlow + (1 - trSa) * e : trSr * trSlow + (1 - trSr) * e;
                const float tr = 10 * std::log10 ((trFast + 1e-12f) / (trSlow + 1e-12f));
                const float want = pause ? 0.0f : std::clamp (tr, 0.0f, 12.0f) * 0.5f * punchAmt;
                trGainDb = trSm * trGainDb + (1 - trSm) * want;
                gT = dbToGain (trGainDb);
            }
            l = laOutL * g1 * g2 * gT; r = laOutR * g1 * g2 * gT;
            deess.apply (l, r);
            maxDs = std::max (maxDs, deess.currentCut());
            if (satMix > 0)
            {
                float sl = std::tanh (l * satDrive) / satDrive, sr = std::tanh (r * satDrive) / satDrive;
                l += satMix * (sl - l); r += satMix * (sr - r);
            }
            float mono = 0.5f * (l + r);
            // width layer: side only, follows the dry vocal (not ducked)
            float ws = layerSB.process (widthLayer.process (mono));
            wE += ws * ws; dE += mono * mono;
            // wet bus: delay (also feeding the reverb) + reverb
            float wl = 0, wr = 0, dl = 0, dr = 0;
            if (delayGain > 0)
            {
                delay.process (mono, dlyMs, p.delayFeedback, p.delayPingPong, dl, dr);
                const float dm = 0.5f * (dl + dr), ds = delaySB.process (0.5f * (dl - dr));
                wl += (dm + ds) * delayGain; wr += (dm - ds) * delayGain;
            }
            if (wetGain > 0)
            {
                float rl, rr; rev.process (mono + 0.5f * (dl + dr) * delaySend, predelaySamp, rl, rr);
                rl = wetBump.process (wetLp.process (wetHp.process (rl, 0), 0), 0);
                rr = wetBump.process (wetLp.process (wetHp.process (rr, 1), 1), 1);
                float m = 0.5f * (rl + rr), s = revSB.process (0.5f * (rl - rr) * p.width);
                wl += (m + s) * wetGain; wr += (m - s) * wetGain;
            }
            // ducking by dry level
            duckEnv = duckEnvCoef * duckEnv + (1 - duckEnvCoef) * mono * mono;
            float lvl = 10 * std::log10 (duckEnv + 1e-12f);
            float target = duckDepth * std::clamp ((lvl - presenceFloor) / 10.0f, 0.0f, 1.0f);
            if (p.duckRelative)
            {   // v1.5: also let go when the voice fades below its own phrase level (held endings, tuned tails),
                // so the reverb blooms at the end of a phrase as in Suno, not only after real silence
                phraseRef = lvl > phraseRef ? lvl : phraseRef - phraseDecay;
                const float below = phraseRef - lvl;
                target *= std::clamp (1.0f - (below - p.duckRelStartDb) / std::max (1.0f, p.duckRelRangeDb), 0.0f, 1.0f);
            }
            duckGr = target > duckGr ? duckAtk * duckGr + (1 - duckAtk) * target : duckRel * duckGr + (1 - duckRel) * target;
            maxDuck = std::max (maxDuck, duckGr);
            float dg = dbToGain (-duckGr);
            l += wl * dg + ws; r += wr * dg - ws;
            widthMeter.push (l, r);   // before the output gain: ratios only
            L[i] = l * outGain;
            if (R) R[i] = r * outGain;
            learner.push (rawMono, 0.5f * (l + r));
            pkOut = std::max (pkOut, std::max (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f));
        }
        mDelayMs = dlyMs; mDeess = maxDs;
        { std::array<float, 6> wv {}; widthMeter.values (wv); for (size_t k = 0; k < 6; ++k) mWidthBands[k] = wv[k]; }
        mBlocks = mBlocks.load() + 1;
        mGr1 = maxGr1; mGr2 = maxGr2; mDuck = maxDuck; mIn = gainToDb (pkIn); mOut = gainToDb (pkOut);
        mWidth = dE > 1e-9f ? 10 * std::log10 (wE / dE + 1e-12f) : -100.0f;
    }
};
} // namespace sc
