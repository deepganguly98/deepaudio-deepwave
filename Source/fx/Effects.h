#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "MasterFx.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

// Master / space effects. Everything allocates in prepare() only; process() is real-time safe.
namespace wgfx
{

//==============================================================================
// Stereo-linked peak limiter: ~1 ms attack, ~100 ms release, hard safety clamp at the ceiling.
class PeakLimiter
{
public:
    void prepare (double sr)
    {
        atk = 1.0f - std::exp (-1.0f / (0.001f * (float) sr));
        rel = 1.0f - std::exp (-1.0f / (0.100f * (float) sr));
        reset();
    }
    void reset() { gain = 1.0f; }

    // R may be nullptr (mono)
    void process (float* L, float* R, int n, float ceilingDb = -0.5f)
    {
        const float ceil = juce::Decibels::decibelsToGain (ceilingDb);
        for (int i = 0; i < n; ++i)
        {
            float l = L[i];
            float r = R != nullptr ? R[i] : 0.0f;
            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;

            const float peak = juce::jmax (std::abs (l), std::abs (r));
            const float target = peak > ceil ? ceil / peak : 1.0f;
            gain += (target - gain) * (target < gain ? atk : rel);

            l *= gain;  r *= gain;
            L[i] = juce::jlimit (-ceil, ceil, l);
            if (R != nullptr) R[i] = juce::jlimit (-ceil, ceil, r);
        }
    }

private:
    float gain = 1.0f, atk = 0.5f, rel = 0.001f;
};

//==============================================================================
// Granular-style octave-up shifter: two crossfaded delay taps whose delay shrinks 1 sample/sample.
class OctaveShifter
{
public:
    void prepare (double sr)
    {
        window = (float) juce::jmax (256.0, 0.06 * sr);
        int n = 1;
        while ((float) n < window + 8.0f) n <<= 1;
        mask = n - 1;
        for (auto& b : buf) b.assign ((size_t) n, 0.0f);
        reset();
    }
    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        w = 0;  phase = 0.0f;
    }

    // Shifts one stereo sample (the two channels share tap phase).
    void tick (float inL, float inR, float& outL, float& outR)
    {
        buf[0][(size_t) w] = inL;
        buf[1][(size_t) w] = inR;

        float p2 = phase + 0.5f;  if (p2 >= 1.0f) p2 -= 1.0f;
        const float w1 = std::sin (juce::MathConstants<float>::pi * phase);
        const float w2 = std::sin (juce::MathConstants<float>::pi * p2);
        const float g1 = w1 * w1, g2 = w2 * w2;
        const float d1 = phase * window + 1.0f, d2 = p2 * window + 1.0f;

        outL = read (0, d1) * g1 + read (0, d2) * g2;
        outR = read (1, d1) * g1 + read (1, d2) * g2;

        phase -= 1.0f / window;
        if (phase < 0.0f) phase += 1.0f;
        w = (w + 1) & mask;
    }

private:
    float read (int ch, float d) const
    {
        const float pos = (float) w - d;
        const float fl = std::floor (pos);
        const float fr = pos - fl;
        const int i0 = ((int) fl) & mask;
        const int i1 = (i0 + 1) & mask;
        return buf[(size_t) ch][(size_t) i0] * (1.0f - fr) + buf[(size_t) ch][(size_t) i1] * fr;
    }

    std::vector<float> buf[2];
    float window = 2048.0f, phase = 0.0f;
    int mask = 4095, w = 0;
};

//==============================================================================
// Stereo ping-pong delay (wet only). Time smoothed, feedback path is low-passed.
class PingPongDelay
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        int n = 1;
        while ((double) n < sr * 1.05 + 8.0) n <<= 1;
        mask = n - 1;
        bufL.assign ((size_t) n, 0.0f);
        bufR.assign ((size_t) n, 0.0f);
        dampCoef = std::exp (-juce::MathConstants<float>::twoPi * 4000.0f / (float) sr);
        time.reset (sr, 0.15);
        time.setCurrentAndTargetValue ((float) (0.33 * sr));
        reset();
    }
    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        w = 0;  lpL = lpR = 0.0f;
    }
    void setTimeMs (float ms) { time.setTargetValue (juce::jlimit (20.0f, 1000.0f, ms) * 0.001f * (float) sampleRate); }

    void tick (float in, float& wetL, float& wetR)
    {
        const float t = juce::jlimit (1.0f, (float) mask - 2.0f, time.getNextValue());
        const float dl = read (bufL, t), dr = read (bufR, t);
        lpL = dr + dampCoef * (lpL - dr);   // one-pole LP, unity DC
        lpR = dl + dampCoef * (lpR - dl);
        bufL[(size_t) w] = in + feedback * lpL;
        bufR[(size_t) w] = feedback * lpR;
        w = (w + 1) & mask;
        wetL = dl;  wetR = dr;
    }

private:
    float read (const std::vector<float>& b, float d) const
    {
        const float pos = (float) w - d;
        const float fl = std::floor (pos);
        const float fr = pos - fl;
        const int i0 = ((int) fl) & mask;
        const int i1 = (i0 + 1) & mask;
        return b[(size_t) i0] * (1.0f - fr) + b[(size_t) i1] * fr;
    }

    std::vector<float> bufL, bufR;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> time;
    double sampleRate = 44100.0;
    float feedback = 0.45f, dampCoef = 0.5f, lpL = 0.0f, lpR = 0.0f;
    int mask = 65535, w = 0;
};

//==============================================================================
// delay -> reverb, with an octave-up shimmer feedback around the reverb (fed back one chunk late).
class SpaceFx
{
public:
    void prepare (double sr)
    {
        reverb.prepare (sr);
        wetS.reset (sr, 0.05);        wetS.setCurrentAndTargetValue (0.0f);
        delay.prepare (sr);
        shifter.prepare (sr);
        delayMixS.reset (sr, 0.05);   delayMixS.setCurrentAndTargetValue (0.0f);
        shimS.reset (sr, 0.05);       shimS.setCurrentAndTargetValue (0.0f);
        dryS.reset (sr, 0.05);        dryS.setCurrentAndTargetValue (1.0f);
        reset();
    }
    void reset()
    {
        reverb.reset();  delay.reset();  shifter.reset();
        for (auto& a : shimFb) a.fill (0.0f);
    }

    // R may be nullptr (mono). Processes in place.
    void process (float* L, float* R, int n, float space, float shimmer, float delayMix, float delayMs)
    {
        delay.setTimeMs (delayMs);
        delayMixS.setTargetValue (0.7f * delayMix);
        shimS.setTargetValue (0.5f * shimmer);
        dryS.setTargetValue (2.0f * (1.0f - 0.25f * space));

        // wet-only reverb; shimmer implies some wash so it is audible with Space at 0
        const float eff = juce::jmax (space, 0.5f * shimmer);
        reverb.setParams (0.55f + 0.35f * eff, 0.40f + 0.50f * eff, 0.45f, 8.0f + 22.0f * eff);
        wetS.setTargetValue (kWetGain * eff);

        int pos = 0;
        while (pos < n)
        {
            const int m = juce::jmin (kChunk, n - pos);
            float xL[kChunk], xR[kChunk], rL[kChunk], rR[kChunk];

            for (int i = 0; i < m; ++i)
            {
                const float inL = L[pos + i];
                const float inR = R != nullptr ? R[pos + i] : inL;
                float wl, wr;
                delay.tick (0.5f * (inL + inR), wl, wr);
                const float dm = delayMixS.getNextValue();
                xL[i] = inL + dm * wl;
                xR[i] = inR + dm * wr;
                rL[i] = xL[i] + shimFb[0][(size_t) i];
                rR[i] = xR[i] + shimFb[1][(size_t) i];
            }

            reverb.processStereo (rL, rR, m);
            for (int i = 0; i < m; ++i)
            {
                const float wg = wetS.getNextValue();
                rL[i] *= wg;  rR[i] *= wg;
            }

            for (int i = 0; i < m; ++i)
            {
                const float dg = dryS.getNextValue();
                float oL = xL[i] * dg + rL[i];
                float oR = xR[i] * dg + rR[i];

                float sl, sr;
                shifter.tick (rL[i], rR[i], sl, sr);
                const float sg = shimS.getNextValue();
                shimFb[0][(size_t) i] = sg * std::tanh (sl);
                shimFb[1][(size_t) i] = sg * std::tanh (sr);

                if (R != nullptr) { L[pos + i] = oL; R[pos + i] = oR; }
                else              L[pos + i] = 0.5f * (oL + oR);
            }
            pos += m;
        }
    }

private:
    static constexpr int kChunk = 32;
    static constexpr float kWetGain = 1.4f;   // wet-only plate level at eff = 1
    PlateReverb reverb;
    PingPongDelay delay;
    OctaveShifter shifter;
    std::array<std::array<float, kChunk>, 2> shimFb {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> delayMixS, shimS, dryS, wetS;
};

} // namespace wgfx
