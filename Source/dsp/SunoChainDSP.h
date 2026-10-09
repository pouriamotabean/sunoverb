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
    bool  hasTarget = true, hasSource = false;
    float eqAmount = 1.0f, eqMaxBoostDb = 12.0f, eqMaxCutDb = 24.0f, eqLowWeight = 0.35f;

    // dynamics
    float compTotalGrDb = 6.0f;     // planned total gain reduction on typical loud syllables
    float targetCrestDb = 9.0f;     // from the Suno vocal; used when Learn measured your crest
    float sourceCrestDb = 0.0f;     // from Learn (0 = unknown)
    float targetSpreadDb = 5.5f;    // p90-p10 of 400 ms levels in the Suno vocal (phrase-level consistency)
    float sourceSpreadDb = 0.0f;    // from Learn (0 = unknown)
    float comp1Ratio = 4.0f, comp1AttackMs = 2.0f, comp1ReleaseMs = 50.0f, comp1Share = 0.55f;
    float comp2Ratio = 3.0f, comp2AttackMs = 30.0f, comp2ReleaseMs = 300.0f;   // ratio = max ratio for the leveler
    float levelerFactor = 1.5f;     // calibration: nominal ratio needed per unit of spread excess (tested on real vocals)
    float compAmount = 1.0f;

    // de-esser (ratio based: acts when HF/full ratio exceeds the Suno target)
    float deessFreq = 5500.0f, deessTargetDb = -14.0f, deessMaxDb = 8.0f, deessAmount = 1.0f;

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

    // reverb
    bool  reverbOn = true;
    float predelayMs = 180.0f, rt60 = 3.0f, rt60LowMul = 1.0f, rt60HighMul = 0.75f, crossoverHz = 1500.0f;
    float size = 1.0f, modDepth = 2.5f, diffusion = 0.75f, diffusionSize = 2.0f;
    float earlyMs = 22.0f, earlyLevelDb = -100.0f;   // discrete early taps off: Suno's reverb has no slap
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
        // Low end: the Suno target there mostly reflects the Suno singer's pitch range and stem bleed.
        //  < 120 Hz : cuts only, never boost (boosting would only add rumble / mud)
        //  120-300  : cuts in full, boosts scaled by "Low Match"
        float v = s / w * p.eqAmount * p.amount;
        const float fc = bandCenters()[i];
        if (fc < 120.0f)      v = std::min (v, 0.0f);
        else if (fc < 300.0f) v = v > 0.0f ? v * p.eqLowWeight : v;
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
        setHighShelf (tilt, fs, 2500.0, p.widthTiltDb);
        incA = p.widthShiftHz / fs; incB = p.widthShiftHz * 1.37 / fs;
        dA = (float) (p.widthDelayMs * 0.001 * fs); dB = (float) (p.widthDelayMs * 1.6 * 0.001 * fs);
        gain = (p.widthOn && amt > 0) ? dbToGain (p.widthLevelDb) * amt * 1.41421356f : 0.0f;   // two voices -> side
        bloomFloor = dbToGain (-std::max (0.0f, p.widthBloomDb)); bloomSamples = (float) (std::max (1.0f, p.widthBloomMs) * 0.001 * fs);
        motion = std::clamp (p.widthMotion, 0.0f, 1.0f);
    }
    static void setHighShelf (Biquad& b, double fs, double f, double db)
    {
        double A = std::pow (10.0, db / 40.0), w = 2 * kPi * f / fs, c = std::cos (w), sn = std::sin (w), al = sn / 2 * std::sqrt (2.0), sq = 2 * std::sqrt (A) * al;
        b.setNorm (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                   (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
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
    MicroWidth widthLayer;
    Learner learner;
    Biquad wetHp, wetLp, wetBump;
    DelayLine laL, laR;                       // lookahead for the compressors
    int lookahead = 0;
    float duckEnv = 0, duckGr = 0, duckEnvCoef = 0, duckAtk = 0, duckRel = 0;
    float rmsEnv = 0, rmsCoef = 0, holdEnv = 0, holdCoef = 0, holdDb = -60;
    float inGain = 1, outGain = 1, wetGain = 0, delayGain = 0, delaySend = 0, satDrive = 1, satMix = 0;
    float predelaySamp = 0;
    double fs = 48000;
    std::array<float, kNumBands> lastCorrection {};
    float eqMakeup = 1.0f;
    bool eqValid = false;
    // meters (written by the audio thread, read by the editor)
    std::atomic<float> mGr1 { 0 }, mGr2 { 0 }, mDuck { 0 }, mIn { -100 }, mOut { -100 }, mWidth { -100 }, mDelayMs { 0 };

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        lookahead = (int) std::round (0.005 * fs);
        laL.allocate (lookahead + 4); laR.allocate (lookahead + 4);
        eq.prepare (fs); eqValid = false; deess.prepare (fs, p.deessFreq); c1.reset(); c2.reset(); delay.prepare (fs); rev.prepare (fs);
        widthLayer.prepare (fs); learner.prepare (fs);
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
        g1 *= cAmt;
        const float typicalPeak = -18.0f + crestIn;
        const float thr1 = g1 > 0.05f ? typicalPeak - g1 / (1.0f - 1.0f / p.comp1Ratio) : 50.0f;
        c1.set (fs, thr1, p.comp1Ratio, p.comp1AttackMs, p.comp1ReleaseMs, g1 * 0.7f);

        // Comp 2 (leveler, 20 ms RMS detector, lookahead, holds during pauses).
        // The Suno target includes its reverb (+~0.8 dB of spread), so the dry aim is slightly lower.
        const float spreadTarget = std::max (2.0f, p.targetSpreadDb - 0.8f);
        float r2 = learned ? std::clamp (1.0f + p.levelerFactor * (spreadIn / spreadTarget - 1.0f), 1.0f, std::max (1.0f, p.comp2Ratio))
                           : std::min (1.6f, std::max (1.0f, p.comp2Ratio));   // gentle default until Learn
        r2 = 1.0f + (r2 - 1.0f) * cAmt;
        const float thr2 = -18.0f - spreadIn * 0.5f - 2.0f;     // RMS domain, just under the quiet phrases
        const float makeup2 = (-18.0f - thr2) * (1.0f - 1.0f / r2);
        c2.set (fs, r2 > 1.01f ? thr2 : 50.0f, r2, p.comp2AttackMs, p.comp2ReleaseMs, r2 > 1.01f ? makeup2 : 0.0f);
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
        deess.setFreq (fs, p.deessFreq);
        deess.targetDb = p.deessTargetDb; deess.maxDb = p.deessMaxDb; deess.amount = p.deessAmount * amt;
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

        duckEnvCoef = msToCoef (10.0f, fs);
        duckAtk = msToCoef (p.duckAttackMs, fs); duckRel = msToCoef (p.duckReleaseMs, fs);
    }

    float currentDelayMs() const { return p.delaySync ? noteMs (p.delayNote, p.bpm) : p.delayMs; }

    void process (float* L, float* R, int n)
    {
        const float duckDepth = p.duckDb * std::clamp (p.amount, 0.0f, 1.0f);
        const float presenceFloor = -18.0f - 15.0f;
        const float dlyMs = currentDelayMs();
        float maxGr1 = 0, maxGr2 = 0, maxDuck = 0, pkIn = 0, pkOut = 0, wE = 0, dE = 0;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R ? R[i] : L[i];
            pkIn = std::max (pkIn, std::max (std::abs (l), std::abs (r)));
            learner.push (0.5f * (l + r));
            l *= inGain; r *= inGain;
            l = eq.process (l, 0) * eqMakeup; r = eq.process (r, 1) * eqMakeup;
            // detectors run on the un-delayed signal, gains are applied to the 5 ms delayed signal
            float pk = std::max (std::abs (l), std::abs (r)), e = 0.5f * (l * l + r * r);
            rmsEnv = rmsCoef * rmsEnv + (1 - rmsCoef) * e;
            holdEnv = holdCoef * holdEnv + (1 - holdCoef) * e;
            const bool pause = 10 * std::log10 (holdEnv + 1e-12f) < holdDb;
            float g1 = c1.computeGain (pk, pause);
            float g2 = c2.computeGain (std::sqrt (rmsEnv) * g1, pause);   // RMS domain
            maxGr1 = std::max (maxGr1, c1.gr); maxGr2 = std::max (maxGr2, c2.gr);
            laL.push (l); laR.push (r);
            l = laL.read (lookahead - 1) * g1 * g2; r = laR.read (lookahead - 1) * g1 * g2;
            deess.process (l, r);
            if (satMix > 0)
            {
                float sl = std::tanh (l * satDrive) / satDrive, sr = std::tanh (r * satDrive) / satDrive;
                l += satMix * (sl - l); r += satMix * (sr - r);
            }
            float mono = 0.5f * (l + r);
            // width layer: side only, follows the dry vocal (not ducked)
            float ws = widthLayer.process (mono);
            wE += ws * ws; dE += mono * mono;
            // wet bus: delay (also feeding the reverb) + reverb
            float wl = 0, wr = 0, dl = 0, dr = 0;
            if (delayGain > 0)
            {
                delay.process (mono, dlyMs, p.delayFeedback, p.delayPingPong, dl, dr);
                wl += dl * delayGain; wr += dr * delayGain;
            }
            if (wetGain > 0)
            {
                float rl, rr; rev.process (mono + 0.5f * (dl + dr) * delaySend, predelaySamp, rl, rr);
                rl = wetBump.process (wetLp.process (wetHp.process (rl, 0), 0), 0);
                rr = wetBump.process (wetLp.process (wetHp.process (rr, 1), 1), 1);
                float m = 0.5f * (rl + rr), s = 0.5f * (rl - rr) * p.width;
                wl += (m + s) * wetGain; wr += (m - s) * wetGain;
            }
            // ducking by dry level
            duckEnv = duckEnvCoef * duckEnv + (1 - duckEnvCoef) * mono * mono;
            float lvl = 10 * std::log10 (duckEnv + 1e-12f);
            float target = duckDepth * std::clamp ((lvl - presenceFloor) / 10.0f, 0.0f, 1.0f);
            duckGr = target > duckGr ? duckAtk * duckGr + (1 - duckAtk) * target : duckRel * duckGr + (1 - duckRel) * target;
            maxDuck = std::max (maxDuck, duckGr);
            float dg = dbToGain (-duckGr);
            l += wl * dg + ws; r += wr * dg - ws;
            L[i] = l * outGain;
            if (R) R[i] = r * outGain;
            pkOut = std::max (pkOut, std::max (std::abs (L[i]), R ? std::abs (R[i]) : 0.0f));
        }
        mDelayMs = dlyMs;
        mGr1 = maxGr1; mGr2 = maxGr2; mDuck = maxDuck; mIn = gainToDb (pkIn); mOut = gainToDb (pkOut);
        mWidth = dE > 1e-9f ? 10 * std::log10 (wE / dE + 1e-12f) : -100.0f;
    }
};
} // namespace sc
