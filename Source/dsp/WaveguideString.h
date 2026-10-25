#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

/*
    WaveguideString — single-delay-loop digital waveguide (Jaffe & Smith 1983,
    Smith "Physical Audio Signal Processing" ch. 9).

        x[n] ──►(+)──► z^-N (fractional, Lagrange-3) ──► H_loop(z) ──► H_disp(z) ──┬──► y[n]
                 ▲                                                               │
                 └──────────────────────── g ◄───────────────────────────────────┘

    * N = fs/f0 − (group delays of H_loop, H_disp, interpolator) so the loop
      resonates at f0.  N is smoothed per-sample so continuous pitch glides
      (slides, bends, vibrato) are click-free.
    * H_loop: one-pole low-pass  (1−b)/(1−b z^-1)  — frequency-dependent loss,
      i.e. "brightness" / how fast upper partials decay.
    * H_disp: first-order all-pass — stiffness-induced inharmonicity (slight
      sharpening of upper partials, the "piano/steel-string" character).
    * g: round-trip loop gain (sustain). g→1 gives electric-guitar sustain.
    * Excitation: filtered noise burst shaped by a pick-position comb
      (1 − z^-(βN)), β∈(0,1) = fractional distance from bridge.
    * Optional soft-clip "pickup → amp" stage after the loop for drive.

    Everything here is allocation-free after prepare(); safe on the audio thread.
*/
class WaveguideString
{
public:
    void prepare (double sampleRate, double minFreqHz = 20.0)
    {
        fs = sampleRate;
        const int maxLen = (int) std::ceil (fs / minFreqHz) + 8;
        buf.assign ((size_t) maxLen, 0.0f);
        mask = maxLen;                   // not pow2; we use modulo via branch
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        writeIdx = 0; lpState = 0.0f; apState = 0.0f; apIn = 0.0f;
        hpState = 0.0f; hpPrev = 0.0f; bowDcX = 0.0f; bowDcY = 0.0f;
        std::fill (std::begin (excHist), std::end (excHist), 0.0f);
        excRemaining = 0; excState = 0.0f;
        smoothedDelay = targetDelay;
    }

    // ---- control (any thread, but values are read per-block/per-sample)
    void setFrequency (float hz)          { lastHz = hz; targetDelay = computeDelay (hz); updateLoopGain(); }
    void setFrequencyImmediate (float hz) { lastHz = hz; targetDelay = computeDelay (hz); smoothedDelay = targetDelay; updateLoopGain(); }
    void setBrightness (float b01)        // 0 = dark/muted, 1 = bright/metallic
    {
        // map to one-pole coefficient; b≈0.0 (bright) … 0.7 (dull)
        lpCoeff = juce::jmap (juce::jlimit (0.0f, 1.0f, b01), 0.0f, 1.0f, 0.75f, 0.02f);
        targetDelay = computeDelay (lastHz);   // keep pitch true as tone moves
    }
    void setSustain (float s01)           // 0 = short pluck, 1 = ~infinite
    {
        sustain01 = juce::jlimit (0.0f, 1.0f, s01);
        updateLoopGain();
    }
    void setInharmonicity (float i01)     { apCoeff = juce::jmap (juce::jlimit (0.0f, 1.0f, i01), 0.0f, 1.0f, 0.0f, -0.35f); targetDelay = computeDelay (lastHz); }
    void setPickPosition (float beta)     { pickPos = juce::jlimit (0.02f, 0.5f, beta); }
    void setGlideSeconds (float s)        { glideCoeff = s <= 0.0f ? 0.0f : std::exp (-1.0f / (s * (float) fs)); }

    // deterministic noise for tests / offline audits (does not change the sound's character)
    void setNoiseSeed (juce::int64 seed) { rng.setSeed (seed); }

    // ---- excitation
    void pluck (float velocity01)
    {
        // noise burst length ≈ one period, amplitude ∝ velocity, pre-filtered
        // by a one-pole whose cutoff tracks velocity (hard pick = brighter).
        excRemaining = (int) smoothedDelay;
        excGain = juce::jlimit (0.0f, 1.0f, velocity01);
        // a soft finger is a smooth bipolar pulse (full harmonic spectrum, no DC) plus a touch of noise:
        // far more energy at the fundamental than filtered noise, so the tail stays full, not thin.
        excPulseLen = juce::jmax (4, (int) ((0.55f - 0.35f * excGain) * smoothedDelay));
        excPulsePos = 0;
        excLp = juce::jmap (excGain, 0.0f, 1.0f, 0.9f, 0.4f);   // soft finger, not a hard pick
        excState = 0.0f;
        excCombDelay = juce::jmax (1, (int) (pickPos * smoothedDelay));
    }

    // Continuous bow/breath-like energy injection (for "sustain by pressure")
    void setContinuousDrive (float d01) { contDrive = juce::jlimit (0.0f, 1.0f, d01); }

    // E-bow style sustainer: saturating positive feedback of the loop signal, so
    // a moving finger keeps the string singing (and a still one lets it decay).
    void setBow (float level) { bowLevel = juce::jlimit (0.0f, 1.0f, level); }
    // 0 = pure tanh sustainer, 1 = pure friction (stick-slip) bow. Default 0.7.
    void setBowFriction (float mix01) { bowStick = juce::jlimit (0.0f, 1.0f, mix01); }

    // ---- render
    inline float processSample()
    {
        // 1. pitch glide: one-pole toward target delay length
        smoothedDelay = glideCoeff * smoothedDelay + (1.0f - glideCoeff) * targetDelay;

        // 2. read fractional delay with 3rd-order Lagrange interpolation
        const float d   = juce::jlimit (3.0f, (float) (mask - 4), smoothedDelay);
        const int   di  = (int) d;
        const float fr  = d - (float) di;
        const float y0 = tap (di - 1), y1 = tap (di), y2 = tap (di + 1), y3 = tap (di + 2);
        // Lagrange-3 coefficients around y1..y2 with fraction fr
        const float c0 = -fr * (fr - 1.0f) * (fr - 2.0f) / 6.0f;
        const float c1 =  (fr + 1.0f) * (fr - 1.0f) * (fr - 2.0f) / 2.0f;
        const float c2 = -(fr + 1.0f) * fr * (fr - 2.0f) / 2.0f;
        const float c3 =  (fr + 1.0f) * fr * (fr - 1.0f) / 6.0f;
        float v = c0 * y0 + c1 * y1 + c2 * y2 + c3 * y3;

        // 3. loop low-pass (frequency-dependent loss)
        lpState = (1.0f - lpCoeff) * v + lpCoeff * lpState;
        v = lpState;

        // 4. dispersion all-pass  H(z) = (a + z^-1) / (1 + a z^-1)
        const float apOut = apCoeff * v + apIn - apCoeff * apState;
        apIn = v; apState = apOut;
        v = apOut;

        // 5. excitation
        float exc = 0.0f;
        if (excRemaining > 0)
        {
            const float white = rng.nextFloat() * 2.0f - 1.0f;
            excState = (1.0f - excLp) * white + excLp * excState;
            float pulse = 0.0f;
            if (excPulsePos < excPulseLen)
            {
                const float x = 6.2831853f * (float) excPulsePos / (float) excPulseLen;
                pulse = std::sin (x) * (1.0f - std::cos (x));      // zero-mean, band-limited
                ++excPulsePos;
            }
            exc = (0.7f * pulse + 0.35f * excState) * excGain;
            --excRemaining;
        }
        if (contDrive > 0.0f)
            exc += (rng.nextFloat() * 2.0f - 1.0f) * contDrive * 0.12f;

        // pick-position comb: subtract the same excitation delayed by βN
        // (implemented by writing exc into the loop and −exc βN samples "later"
        //  via a second small circular read on the excitation history)
        excHist[excHistIdx] = exc;
        const int rd = (excHistIdx - excCombDelay + kExcHist) % kExcHist;
        const float combed = exc - excHist[rd];
        excHistIdx = (excHistIdx + 1) % kExcHist;

        // 6. feedback & write
        float fb = loopGain * v;
        if (bowLevel > 1.0e-5f)
        {
            // friction (stick-slip) term on relative velocity, blended with the
            // old saturating e-bow term. Constant bow velocity => the reference
            // friction is subtracted and the injection is DC-blocked, so no
            // offset can build up in the loop.
            const float fric = 20.0f * (frictionTable (kBowVel - v) - fricRef);
            const float inj  = bowLevel * (bowStick * fric + (1.0f - bowStick) * std::tanh (3.0f * v));
            const float dcOut = inj - bowDcX + 0.999f * bowDcY;
            bowDcX = inj; bowDcY = dcOut;
            fb += dcOut + (rng.nextFloat() * 2.0f - 1.0f) * bowLevel * 0.05f;
        }
        float in = combed + fb;
        if (bowLevel > 1.0e-5f)
            in /= 1.0f + 0.35f * std::abs (in);   // bounds the sustained amplitude
        // safety: NaN/inf/runaway guard (never write garbage into the delay line),
        // hard clamp, and denormal flush
        if (! (std::abs (in) < 8.0f)) { clearLoop(); in = 0.0f; }
        in = juce::jlimit (-4.0f, 4.0f, in);
        if (std::abs (in) < 1.0e-20f) in = 0.0f;
        if (std::abs (lpState) < 1.0e-20f) lpState = 0.0f;
        if (std::abs (apState) < 1.0e-20f) apState = 0.0f;
        buf[(size_t) writeIdx] = in;
        writeIdx = (writeIdx + 1) % mask;

        // 7. output: loop signal through a gentle pickup high-pass + soft drive
        hpState = 0.995f * (hpState + v - hpPrev); hpPrev = v;
        if (std::abs (hpState) < 1.0e-20f) hpState = 0.0f;
        if (std::abs (hpPrev) < 1.0e-20f) hpPrev = 0.0f;
        float out = hpState;
        if (drive > 0.0f)
            out = std::tanh (out * (1.0f + drive * 12.0f)) / (1.0f + drive * 1.5f);
        return out;
    }

    void setDrive (float d01) { drive = juce::jlimit (0.0f, 1.0f, d01); }

    bool isSilent (float thresholdDb = -80.0f) const
    {
        return std::abs (lpState) < juce::Decibels::decibelsToGain (thresholdDb) && excRemaining == 0;
    }

private:
    inline float tap (int delaySamples) const
    {
        int idx = writeIdx - delaySamples;
        while (idx < 0) idx += mask;
        return buf[(size_t) idx];
    }

    // Sustain = how long the string rings, in SECONDS, independent of pitch (loop gain is per round trip,
    // so it must scale with f0). T60 = 0.35 s / (1 - sustain): this matches the old mapping at middle C,
    // reaching ~70 s (a drone) at sustain = 1.
    void updateLoopGain()
    {
        const float t60 = 0.35f / juce::jmax (1.0f - sustain01, 0.005f);
        const float g = std::exp (-6.9078f / (t60 * juce::jmax (lastHz, 20.0f)));
        loopGain = juce::jlimit (0.5f, 0.99995f, g);
    }

    float computeDelay (float hz) const
    {
        hz = juce::jlimit (20.0f, juce::jmin (8000.0f, (float) (fs * 0.45)), hz);
        // Exact PHASE delay (samples) of the in-loop filters at the fundamental.
        //  one-pole (1-b)/(1-b z^-1):  lag = atan2 (b sin w, 1 - b cos w)
        //  all-pass (a+z^-1)/(1+a z^-1): lag = atan2 (sin w, a + cos w) - atan2 (a sin w, 1 + a cos w)
        // The Lagrange-3 read is centred on the requested fractional delay d
        // (taps di-1..di+2 around di+fr; tap(1) is the newest sample), so the
        // interpolator adds no extra offset: loop period = d + lag_lp + lag_ap.
        const double w  = 6.283185307179586 * (double) hz / fs;
        const double sw = std::sin (w), cw = std::cos (w);
        const double b  = (double) lpCoeff, a = (double) apCoeff;
        const double lpLag = std::atan2 (b * sw, 1.0 - b * cw);
        const double apLag = std::atan2 (sw, a + cw) - std::atan2 (a * sw, 1.0 + a * cw);
        const double comp  = (lpLag + apLag) / w;
        return juce::jmax (3.5f, (float) (fs / (double) hz - comp));
    }

    static inline float frictionTable (float x)
    {
        // STK Bowed-style bow table: x * (|slope*x| + 0.75)^-4 (stick-slip curve,
        // bounded, negative differential slope beyond the stick region).
        const float s = std::abs (x * 5.0f) + 0.75f;
        const float s2 = s * s;
        return x / (s2 * s2);
    }

    inline void clearLoop()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        lpState = apState = apIn = 0.0f;
        bowDcX = bowDcY = 0.0f;
    }

    double fs = 48000.0;
    std::vector<float> buf;
    int mask = 1, writeIdx = 0;

    float targetDelay = 100.0f, smoothedDelay = 100.0f, glideCoeff = 0.0f;
    float lpCoeff = 0.3f, lpState = 0.0f;
    float apCoeff = 0.0f, apState = 0.0f, apIn = 0.0f;
    float loopGain = 0.995f, sustain01 = 0.9f;
    float pickPos = 0.13f;
    float drive = 0.0f, hpState = 0.0f, hpPrev = 0.0f;
    float contDrive = 0.0f, bowLevel = 0.0f, lastHz = 440.0f;
    static constexpr float kBowVel = 0.2f;      // constant bow velocity (slip region of the table)
    float bowStick = 0.7f;
    float fricRef = frictionTable (kBowVel);
    float bowDcX = 0.0f, bowDcY = 0.0f;

    // excitation
    static constexpr int kExcHist = 2048;
    float excHist[kExcHist] {};
    int excHistIdx = 0, excCombDelay = 10, excRemaining = 0, excPulseLen = 8, excPulsePos = 0;
    float excGain = 0.0f, excLp = 0.3f, excState = 0.0f;
    juce::Random rng;
};
