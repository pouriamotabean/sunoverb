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
    std::array<float, kNumBands> targetCurve {};   // dB, normalised (mean of 250..4k bands = 0)
    std::array<float, kNumBands> sourceCurve {};   // dB, from Learn
    bool  hasTarget = false, hasSource = false;
    float eqAmount = 1.0f, eqMaxBoostDb = 9.0f, eqMaxCutDb = 24.0f, eqLowWeight = 0.35f;

    // dynamics
    float compTotalGrDb = 6.0f;     // planned total gain reduction on typical loud syllables
    float targetCrestDb = 9.0f;     // from the Suno vocal; used when Learn measured your crest
    float sourceCrestDb = 0.0f;     // from Learn (0 = unknown)
    float targetSpreadDb = 5.5f;    // p90-p10 of 400 ms levels in the Suno vocal (phrase-level consistency)
    float sourceSpreadDb = 0.0f;    // from Learn (0 = unknown)
    float comp1Ratio = 4.0f, comp1AttackMs = 2.0f, comp1ReleaseMs = 50.0f, comp1Share = 0.55f;
    float comp2Ratio = 3.0f, comp2AttackMs = 30.0f, comp2ReleaseMs = 300.0f;   // ratio = max ratio for the leveler
    float compAmount = 1.0f;

    // de-esser (ratio based: acts when HF/full ratio exceeds the Suno target)
    float deessFreq = 5500.0f, deessTargetDb = -14.0f, deessMaxDb = 8.0f, deessAmount = 1.0f;

    // saturation (not measurable from stems: preset default)
    float satDriveDb = 6.0f, satMix = 0.15f;

    // delay
    bool  delayOn = false;
    float delayMs = 375.0f, delayFeedback = 0.25f, delayLevelDb = -18.0f, delayHpf = 300.0f, delayLpf = 5000.0f;
    bool  delayPingPong = true;

    // reverb
    bool  reverbOn = true;
    float predelayMs = 180.0f, rt60 = 3.0f, rt60LowMul = 1.0f, rt60HighMul = 0.75f, crossoverHz = 1500.0f;
    float size = 1.0f, modDepth = 0.5f;
    float earlyMs = 22.0f, earlyLevelDb = -8.0f;
    float wetHpf = 300.0f, wetLpf = 5000.0f, wetBumpHz = 2500.0f, wetBumpDb = 2.0f;
    float width = 1.0f;
    float reverbLevelDb = -10.0f;   // un-ducked wet level relative to the dry vocal (stationary signal)

    // ducking of the wet bus by the dry vocal
    float duckDb = 7.0f, duckAttackMs = 5.0f, duckReleaseMs = 120.0f;

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
    for (int i = 0; i < kNumBands; ++i) raw[i] = p.targetCurve[i] - p.sourceCurve[i];
    for (int i = 0; i < kNumBands; ++i)   // light 3-band smoothing
    {
        float s = raw[i] * 2.0f, w = 2.0f;
        if (i > 0) { s += raw[i - 1]; w += 1; }
        if (i < kNumBands - 1) { s += raw[i + 1]; w += 1; }
        // below ~300 Hz the target mostly reflects the Suno singer's pitch range, not processing:
        // match it only partially there
        const float lowW = bandCenters()[i] < 300.0f ? p.eqLowWeight : 1.0f;
        float v = s / w * p.eqAmount * p.amount * lowW;
        d[i] = std::clamp (v, -p.eqMaxCutDb, p.eqMaxBoostDb);
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
    inline float computeGain (float detector)
    {
        float lv = gainToDb (detector), over = lv - thrDb, target;
        if (2 * over < -knee) target = 0;
        else if (2 * std::abs (over) <= knee) { float t = over + knee / 2; target = (1 - 1 / ratio) * t * t / (2 * knee); }
        else target = (1 - 1 / ratio) * over;
        gr = target > gr ? atk * gr + (1 - atk) * target : rel * gr + (1 - rel) * target;
        return dbToGain (-gr) * makeup;
    }
};

//==============================================================================
struct DeEsser
{
    Biquad hp;
    float envHf = 0, envFull = 0, envCoef = 0, red = 0, atk = 0, rel = 0;
    float targetDb = -14, maxDb = 8, amount = 1;
    void setFreq (double fs, float freq) { hp.setHPF (fs, freq); }
    void prepare (double fs, float freq)
    {
        hp.reset(); hp.setHPF (fs, freq);
        envCoef = msToCoef (5.0f, fs); atk = msToCoef (1.0f, fs); rel = msToCoef (60.0f, fs);
    }
    inline void process (float& l, float& r)
    {
        float hl = hp.process (l, 0), hr = hp.process (r, 1);
        float hf = 0.5f * (hl * hl + hr * hr), full = 0.5f * (l * l + r * r);
        envHf = envCoef * envHf + (1 - envCoef) * hf;
        envFull = envCoef * envFull + (1 - envCoef) * full;
        float target = 0;
        if (envFull > 1e-7f)
        {
            float ratioDb = 10 * std::log10 ((envHf + 1e-12f) / envFull);
            target = std::clamp ((ratioDb - targetDb) * amount, 0.0f, maxDb);
        }
        red = target > red ? atk * red + (1 - atk) * target : rel * red + (1 - rel) * target;
        float k = 1 - dbToGain (-red);
        l -= k * hl; r -= k * hr;
    }
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
    DelayLine l, r; Biquad hp, lp; float fbL = 0, fbR = 0; double fs = 48000;
    void prepare (double s) { fs = s; l.allocate ((int) (2.1 * fs)); r.allocate ((int) (2.1 * fs)); hp.reset(); lp.reset(); fbL = fbR = 0; }
    void set (float hpf, float lpf) { hp.setHPF (fs, hpf); lp.setLPF (fs, lpf); }
    inline void process (float in, float timeMs, float fb, bool pingPong, float& outL, float& outR)
    {
        int d = std::clamp ((int) (timeMs * 0.001 * fs), 1, (int) (2.0 * fs));
        float yl = l.read (d), yr = r.read (d);
        float fl = lp.process (hp.process (yl, 0), 0), fr = lp.process (hp.process (yr, 1), 1);
        if (pingPong) { l.push (in + fr * fb); r.push (fl * fb); }
        else          { l.push (in + fl * fb); r.push (in + fr * fb); }
        outL = fl; outR = pingPong ? fr : fr;
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

    void prepare (double s)
    {
        fs = s;
        for (auto& d : lines) d.allocate ((int) (0.25 * fs));
        pre.allocate ((int) (0.6 * fs));
        for (int i = 0; i < N; ++i) { lpState[i] = 0; modPhase[i] = (float) i * 0.7f; modRate[i] = 0.37f + 0.11f * (float) i; }
    }
    void clear() { for (auto& d : lines) d.clear(); pre.clear(); lpState.fill (0); }

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
    }

    inline void process (float in, float predelaySamples, float& outL, float& outR)
    {
        pre.push (in);
        float x = pre.readFrac (std::max (0.0f, predelaySamples));
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
    std::vector<float> frameLevel;              // dB
    std::vector<float> blockRms, blockPeak;     // dB
    std::vector<float> fifo; int fifoPos = 0;
    int blockLen = 2400, blockPos = 0; float bSum = 0, bPeak = 0;
    std::atomic<int> nFrames { 0 }, nBlocks { 0 };
    std::atomic<bool> active { false };
    double fs = 48000;
    std::vector<std::complex<float>> buf;
    std::vector<float> window;

    void prepare (double s)
    {
        fs = s; blockLen = (int) (0.05 * fs);
        frameBands.assign ((size_t) kMaxFrames * kNumBands, 0.0f); frameLevel.assign (kMaxFrames, -200.0f);
        blockRms.assign (kMaxBlocks, -200.0f); blockPeak.assign (kMaxBlocks, -200.0f);
        fifo.assign (kFft, 0.0f); buf.assign (kFft, {}); window.resize (kFft);
        for (int i = 0; i < kFft; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / (kFft - 1)));
    }
    void start() { nFrames = 0; nBlocks = 0; fifoPos = 0; blockPos = 0; bSum = 0; bPeak = 0; active = true; }
    void stop() { active = false; }

    static void fft (std::vector<std::complex<float>>& a)
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

    inline void push (float x)   // audio thread
    {
        if (! active) return;
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
        fifo[(size_t) fifoPos++] = x;
        if (fifoPos >= kFft)
        {
            fifoPos = 0;
            int f = nFrames.load();
            if (f >= kMaxFrames) { active = false; return; }
            double e = 0;
            for (int i = 0; i < kFft; ++i) { buf[(size_t) i] = { fifo[(size_t) i] * window[(size_t) i], 0.0f }; e += fifo[(size_t) i] * fifo[(size_t) i]; }
            fft (buf);
            frameLevel[(size_t) f] = (float) (10 * std::log10 (e / kFft + 1e-20));
            const auto& fc = bandCenters();
            for (int b = 0; b < kNumBands; ++b)
            {
                double lo = fc[b] / std::pow (2.0, 1.0 / 6), hi = fc[b] * std::pow (2.0, 1.0 / 6);
                int k0 = std::max (1, (int) std::ceil (lo * kFft / fs)), k1 = std::min (kFft / 2, (int) std::floor (hi * kFft / fs));
                double s = 0; int cnt = 0;
                for (int k = k0; k <= k1; ++k) { s += std::norm (buf[(size_t) k]); ++cnt; }
                frameBands[(size_t) f * kNumBands + b] = (float) (10 * std::log10 (cnt > 0 ? s / cnt : 1e-20) + 1e-9);
            }
            nFrames = f + 1;
        }
    }

    // message thread: summarise. Returns false if not enough material.
    bool summarise (std::array<float, kNumBands>& curve, float& levelDb, float& crestDb, float& spreadDb) const
    {
        int nf = nFrames.load(), nb = nBlocks.load();
        if (nf < 20 || nb < 40) return false;
        std::vector<float> lv (frameLevel.begin(), frameLevel.begin() + nf);
        auto sorted = lv; std::sort (sorted.begin(), sorted.end());
        float p95 = sorted[(size_t) (0.95 * (nf - 1))];
        std::array<double, kNumBands> acc {}; int used = 0;
        for (int f = 0; f < nf; ++f)
        {
            if (lv[(size_t) f] < p95 - 25) continue;
            for (int b = 0; b < kNumBands; ++b) acc[b] += std::pow (10.0, frameBands[(size_t) f * kNumBands + b] / 10.0);
            ++used;
        }
        if (used < 10) return false;
        for (int b = 0; b < kNumBands; ++b) curve[b] = (float) (10 * std::log10 (acc[b] / used + 1e-30));
        normaliseCurve (curve);
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
class Chain
{
public:
    Params p;
    MatchEQ eq;
    Compressor c1, c2;
    DeEsser deess;
    StereoDelay delay;
    FDNReverb rev;
    Learner learner;
    Biquad wetHp, wetLp, wetBump;
    float duckEnv = 0, duckGr = 0, duckEnvCoef = 0, duckAtk = 0, duckRel = 0;
    float inGain = 1, outGain = 1, wetGain = 0, delayGain = 0, satDrive = 1, satMix = 0;
    float predelaySamp = 0;
    double fs = 48000;
    std::array<float, kNumBands> lastCorrection {};
    float eqMakeup = 1.0f;
    bool eqValid = false;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        eq.prepare (fs); eqValid = false; deess.prepare (fs, p.deessFreq); c1.reset(); c2.reset(); delay.prepare (fs); rev.prepare (fs); learner.prepare (fs);
        wetHp.reset(); wetLp.reset(); wetBump.reset();
        setParams (p);
    }
    void reset() { rev.clear(); delay.l.clear(); delay.r.clear(); c1.reset(); c2.reset(); duckGr = duckEnv = 0; }

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

        // Comp 1 (fast, peaks): gain reduction from the crest-factor difference
        // Comp 2 (slow leveler): ratio from the phrase-level spread difference, threshold under the quiet phrases
        const bool learned = p.sourceCrestDb > 0.0f;
        const float crestIn = learned ? p.sourceCrestDb : 12.0f;
        const float spreadIn = (learned && p.sourceSpreadDb > 0.0f) ? p.sourceSpreadDb : 8.0f;
        const float cAmt = p.compAmount * amt;
        float g1 = learned ? std::clamp ((crestIn - p.targetCrestDb) * 1.3f, 0.0f, 10.0f) : p.compTotalGrDb * p.comp1Share;
        g1 *= cAmt;
        const float typicalPeak = -18.0f + crestIn;
        const float thr1 = g1 > 0.05f ? typicalPeak - g1 / (1.0f - 1.0f / p.comp1Ratio) : 50.0f;
        c1.set (fs, thr1, p.comp1Ratio, p.comp1AttackMs, p.comp1ReleaseMs, g1 * 0.7f);

        // The Suno target includes its reverb, which adds ~0.8 dB of spread on its own, so aim the dry
        // signal slightly lower. Measured in tests: the leveler only removes ~30 % of the nominal
        // ratio's effect on 400 ms levels (attack/release, syllable gaps), hence the x3.5 factor.
        const float spreadTarget = std::max (2.0f, p.targetSpreadDb - 0.8f);
        float r2 = learned ? std::clamp (1.0f + 3.5f * (spreadIn / spreadTarget - 1.0f), 1.0f, std::max (1.0f, p.comp2Ratio))
                           : std::min (2.0f, std::max (1.0f, p.comp2Ratio));   // gentle default until Learn
        r2 = 1.0f + (r2 - 1.0f) * cAmt;
        const float thr2 = typicalPeak - g1 * 0.5f - spreadIn * 0.5f - 3.0f;   // below the quiet (p10) phrases
        const float med2 = (typicalPeak - g1 * 0.5f) - thr2;                    // median phrase sits this far above
        const float makeup2 = med2 * (1.0f - 1.0f / r2);
        c2.set (fs, r2 > 1.01f ? thr2 : 50.0f, r2, p.comp2AttackMs, p.comp2ReleaseMs, r2 > 1.01f ? makeup2 : 0.0f);

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
        deess.setFreq (fs, p.deessFreq);
        deess.targetDb = p.deessTargetDb; deess.maxDb = p.deessMaxDb; deess.amount = p.deessAmount * amt;
        satDrive = dbToGain (p.satDriveDb); satMix = p.satMix * amt;

        delay.set (p.delayHpf, p.delayLpf);
        delayGain = (p.delayOn && amt > 0) ? dbToGain (p.delayLevelDb) * amt : 0.0f;

        rev.set (p);
        predelaySamp = (float) (p.predelayMs * 0.001 * fs);
        wetHp.setHPF (fs, std::max (20.0f, p.wetHpf), 0.9);
        wetLp.setLPF (fs, std::min ((float) (fs * 0.45), p.wetLpf), 0.8);
        wetBump.setPeak (fs, p.wetBumpHz, 1.0, p.wetBumpDb);
        wetGain = (p.reverbOn && amt > 0) ? dbToGain (p.reverbLevelDb) * amt : 0.0f;

        duckEnvCoef = msToCoef (10.0f, fs);
        duckAtk = msToCoef (p.duckAttackMs, fs); duckRel = msToCoef (p.duckReleaseMs, fs);
    }

    void process (float* L, float* R, int n)
    {
        const float duckDepth = p.duckDb * std::clamp (p.amount, 0.0f, 1.0f);
        const float presenceFloor = -18.0f - 15.0f;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R ? R[i] : L[i];
            learner.push (0.5f * (l + r));
            l *= inGain; r *= inGain;
            // match EQ
            l = eq.process (l, 0) * eqMakeup; r = eq.process (r, 1) * eqMakeup;
            // compressors (stereo linked)
            float g = c1.computeGain (std::max (std::abs (l), std::abs (r))); l *= g; r *= g;
            g = c2.computeGain (std::max (std::abs (l), std::abs (r)));      l *= g; r *= g;
            deess.process (l, r);
            if (satMix > 0)
            {
                float sl = std::tanh (l * satDrive) / satDrive, sr = std::tanh (r * satDrive) / satDrive;
                l += satMix * (sl - l); r += satMix * (sr - r);
            }
            // wet bus
            float mono = 0.5f * (l + r);
            float wl = 0, wr = 0;
            if (wetGain > 0)
            {
                float rl, rr; rev.process (mono, predelaySamp, rl, rr);
                rl = wetBump.process (wetLp.process (wetHp.process (rl, 0), 0), 0);
                rr = wetBump.process (wetLp.process (wetHp.process (rr, 1), 1), 1);
                float m = 0.5f * (rl + rr), s = 0.5f * (rl - rr) * p.width;
                wl += (m + s) * wetGain; wr += (m - s) * wetGain;
            }
            if (delayGain > 0)
            {
                float dl, dr; delay.process (mono, p.delayMs, p.delayFeedback, p.delayPingPong, dl, dr);
                wl += dl * delayGain; wr += dr * delayGain;
            }
            // ducking by dry level
            duckEnv = duckEnvCoef * duckEnv + (1 - duckEnvCoef) * mono * mono;
            float lvl = 10 * std::log10 (duckEnv + 1e-12f);
            float target = duckDepth * std::clamp ((lvl - presenceFloor) / 10.0f, 0.0f, 1.0f);
            duckGr = target > duckGr ? duckAtk * duckGr + (1 - duckAtk) * target : duckRel * duckGr + (1 - duckRel) * target;
            float dg = dbToGain (-duckGr);
            l += wl * dg; r += wr * dg;
            L[i] = l * outGain;
            if (R) R[i] = r * outGain;
        }
    }
};
} // namespace sc
