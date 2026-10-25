#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

/*
    BodyResonator — "commuted body" approximated as a parallel bank of fixed
    modal band-pass resonators (RBJ biquad, 0 dB peak), tuned like an acoustic
    guitar top/back/air modes.  Output = dry + amount * sum(mode gains * BPF_k).

    One instance per voice (8 biquads/sample, trivially cheap).  All storage is
    fixed-size; nothing allocates, locks or logs.  Real-time safe.
*/
class BodyResonator
{
public:
    static constexpr int kModes = 8;

    void prepare (double sampleRate)
    {
        // freq (Hz), Q, gain  — air (Helmholtz) ~100, top plate ~200, back ~280,
        // then higher plate modes and a few bright "top" modes.
        static constexpr double modes[kModes][3] = {
            {  100.0, 14.0, 1.00 }, {  205.0, 18.0, 0.90 }, {  285.0, 20.0, 0.60 },
            {  410.0, 24.0, 0.45 }, {  560.0, 26.0, 0.35 }, {  780.0, 30.0, 0.30 },
            { 1180.0, 34.0, 0.22 }, { 2350.0, 40.0, 0.14 } };
        for (int k = 0; k < kModes; ++k)
        {
            const double f = juce::jmin (modes[k][0], sampleRate * 0.45);
            const double w = 6.283185307179586 * f / sampleRate;
            const double alpha = std::sin (w) / (2.0 * modes[k][1]);
            const double a0 = 1.0 + alpha;
            b0[k]  = (float) (alpha / a0);       // b1 = 0, b2 = -b0
            a1[k]  = (float) (-2.0 * std::cos (w) / a0);
            a2[k]  = (float) ((1.0 - alpha) / a0);
            gain[k] = (float) modes[k][2];
        }
        reset();
    }

    void reset()
    {
        for (int k = 0; k < kModes; ++k) z1[k] = z2[k] = 0.0f;
    }

    // Returns the WET part only (already scaled by amount); caller adds it to the dry signal.
    inline float process (float x, float amount)
    {
        float sum = 0.0f;
        for (int k = 0; k < kModes; ++k)
        {
            // transposed direct form II, b1 = 0, b2 = -b0
            const float y = b0[k] * x + z1[k];
            z1[k] = z2[k] - a1[k] * y;
            z2[k] = -b0[k] * x - a2[k] * y;
            if (std::abs (z1[k]) < 1.0e-20f) z1[k] = 0.0f;
            if (std::abs (z2[k]) < 1.0e-20f) z2[k] = 0.0f;
            sum += gain[k] * y;
        }
        return sum * amount * 1.2f;
    }

private:
    float b0[kModes] {}, a1[kModes] {}, a2[kModes] {}, gain[kModes] {};
    float z1[kModes] {}, z2[kModes] {};
};
