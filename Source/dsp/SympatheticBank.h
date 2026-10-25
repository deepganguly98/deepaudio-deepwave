#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

/*
    SympatheticBank — sympathetic string resonance.

    Design choice (real-time safety): voices render independently into the
    output buffer, so a true shared bridge summing every string and feeding it
    back into every loop would need cross-voice state and a feedback path that
    could go unstable.  Instead each voice owns a small bank of narrow
    two-pole resonators tuned to (a) six open-string pitches (E A D G B E, guitar
    tuning) and (b) harmonics 2 and 3 of the voice's own note.  The voice's own
    output drives the bank (feed-forward, unit-peak resonators, small fraction),
    and the result is added to the output only.  It is open-loop, so it is
    unconditionally stable and cannot run away.  Strings that match the played
    note's partials ring audibly; others stay quiet — the sympathetic effect.

    Fixed-size, allocation-free after construction.
*/
class SympatheticBank
{
public:
    static constexpr int kOpen = 6, kHarm = 2, kN = kOpen + kHarm;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        static constexpr double open[kOpen] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63 };
        for (int k = 0; k < kOpen; ++k) setResonator (k, open[k]);
        setNote (440.0f);
        reset();
    }

    void reset()
    {
        for (int k = 0; k < kN; ++k) y1[k] = y2[k] = 0.0f;
    }

    // Retune the harmonic resonators (call per block, not per sample).
    void setNote (float hz)
    {
        if (std::abs (hz - lastHz) < lastHz * 0.002f) return;
        lastHz = hz;
        for (int h = 0; h < kHarm; ++h) setResonator (kOpen + h, (double) hz * (double) (h + 2));
    }

    // Returns the wet sympathetic signal for input x, scaled by amount.
    inline float process (float x, float amount)
    {
        float sum = 0.0f;
        for (int k = 0; k < kN; ++k)
        {
            float y = b0[k] * x + c1[k] * y1[k] - c2[k] * y2[k];
            if (std::abs (y) < 1.0e-20f) y = 0.0f;
            y2[k] = y1[k]; y1[k] = y;
            sum += y;
        }
        return sum * amount * 0.12f;
    }

private:
    void setResonator (int k, double f)
    {
        f = juce::jlimit (20.0, fs * 0.45, f);
        const double w = 6.283185307179586 * f / fs;
        const double t60 = 2.0;                                  // seconds
        const double R = std::exp (-6.907755 / (t60 * fs));
        c1[k] = (float) (2.0 * R * std::cos (w));
        c2[k] = (float) (R * R);
        b0[k] = (float) ((1.0 - R) * 2.0 * std::sin (w));       // ~unit peak gain
    }

    double fs = 48000.0;
    float lastHz = -1000.0f;
    float b0[kN] {}, c1[kN] {}, c2[kN] {};
    float y1[kN] {}, y2[kN] {};
};
