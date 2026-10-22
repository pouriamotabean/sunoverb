// Reference EXACT solver, as sent with Pouria's EQ solver report (Oct 2026). Used by SunoChainTests only:
// the plugin's fast ExactEqSolver (dsp/SunoChainDSP.h) must give the same gains.
// Drop-in replacement for MatchEQ::solve (SunoChainDSP.h). Same signature, same clamps (-30 / +18 dB).
// No heap allocation (fixed arrays only): safe if setParams() runs on the audio thread.
static std::array<float, kNumBands> solve (const std::array<float, kNumBands>& desired, double fs)
{
    constexpr double kQ = 2.2, kLambda = 0.02;      // kLambda: small smoothing so neighbouring gains don't fight
    const auto& fc = bandCenters();
    int nb = 0; while (nb < kNumBands && fc[(size_t) nb] <= fs * 0.45) ++nb;   // bands that exist at this rate

    // M[b][k] = dB that band k (at +1 dB) adds at the centre of band b
    std::array<std::array<double, kNumBands>, kNumBands> M {};
    for (int k = 0; k < nb; ++k)
    {
        Biquad t; t.setPeak (fs, fc[(size_t) k], kQ, 1.0);
        for (int b = 0; b < nb; ++b) M[(size_t) b][(size_t) k] = t.magDb (fs, fc[(size_t) b]);
    }
    // x = argmin |M x - y|^2 + lambda |x|^2   (normal equations, Gauss-Jordan with pivoting)
    auto lsq = [&] (const std::array<double, kNumBands>& y)
    {
        std::array<std::array<double, kNumBands + 1>, kNumBands> A {};
        for (int i = 0; i < nb; ++i)
        {
            for (int j = 0; j < nb; ++j)
            {
                double v = 0;
                for (int b = 0; b < nb; ++b) v += M[(size_t) b][(size_t) i] * M[(size_t) b][(size_t) j];
                A[(size_t) i][(size_t) j] = v + (i == j ? kLambda : 0.0);
            }
            double v = 0;
            for (int b = 0; b < nb; ++b) v += M[(size_t) b][(size_t) i] * y[(size_t) b];
            A[(size_t) i][(size_t) nb] = v;
        }
        for (int c = 0; c < nb; ++c)
        {
            int piv = c;
            for (int r = c + 1; r < nb; ++r) if (std::abs (A[(size_t) r][(size_t) c]) > std::abs (A[(size_t) piv][(size_t) c])) piv = r;
            std::swap (A[(size_t) c], A[(size_t) piv]);
            const double d = A[(size_t) c][(size_t) c];
            if (std::abs (d) < 1e-12) continue;
            for (int r = 0; r < nb; ++r)
                if (r != c)
                {
                    const double f = A[(size_t) r][(size_t) c] / d;
                    for (int k = c; k <= nb; ++k) A[(size_t) r][(size_t) k] -= f * A[(size_t) c][(size_t) k];
                }
        }
        std::array<double, kNumBands> x {};
        for (int i = 0; i < nb; ++i) x[(size_t) i] = std::abs (A[(size_t) i][(size_t) i]) > 1e-12 ? A[(size_t) i][(size_t) nb] / A[(size_t) i][(size_t) i] : 0.0;
        return x;
    };
    std::array<double, kNumBands> y {};
    for (int b = 0; b < nb; ++b) y[(size_t) b] = desired[(size_t) b];
    const auto x0 = lsq (y);
    std::array<float, kNumBands> g {};
    for (int k = 0; k < nb; ++k) g[(size_t) k] = (float) std::clamp (x0[(size_t) k], -30.0, 18.0);
    // the cascade is not exactly linear in dB: correct what is left against the real response
    for (int it = 0; it < 6; ++it)
    {
        std::array<Biquad, kNumBands> tmp;
        for (int k = 0; k < nb; ++k) tmp[(size_t) k].setPeak (fs, fc[(size_t) k], kQ, g[(size_t) k]);
        std::array<double, kNumBands> err {};
        for (int b = 0; b < nb; ++b)
        {
            double r = 0;
            for (int k = 0; k < nb; ++k) r += tmp[(size_t) k].magDb (fs, fc[(size_t) b]);
            err[(size_t) b] = desired[(size_t) b] - r;
        }
        const auto dx = lsq (err);
        for (int k = 0; k < nb; ++k) g[(size_t) k] = (float) std::clamp (g[(size_t) k] + 0.8 * dx[(size_t) k], -30.0, 18.0);
    }
    return g;
}
