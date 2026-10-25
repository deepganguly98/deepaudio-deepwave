#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

// Master-bus polish (warmth, EQ, ensemble, glue compressor, M/S width) and a Dattorro-style plate reverb.
// Everything allocates in prepare(); process / processStereo are real-time safe.
namespace wgfx
{

struct MasterParams
{
    float eqLowDb = 0.0f;     // -12..12 low shelf ~120 Hz
    float eqHighDb = 0.0f;    // -12..12 high shelf ~7 kHz
    float presenceDb = 0.0f;  // -6..6 peaking ~3 kHz
    float comp = 0.0f;        // 0..1 glue amount
    float ensemble = 0.0f;    // 0..1 stereo chorus mix
    float warmth = 0.0f;      // 0..1 tape-ish saturation
    float width = 1.0f;       // 0..1.5 mid/side width, 1 = unchanged
};

namespace detail
{
// One-pole parameter smoother that snaps to the target once it is close (so neutral stays exactly neutral).
struct Smooth
{
    float cur = 0.0f, tgt = 0.0f, a = 0.01f, eps = 1.0e-5f;
    void init (double sr, float ms, float epsilon = 1.0e-5f)
    {
        a = 1.0f - std::exp (-1.0f / (0.001f * ms * (float) sr));
        eps = epsilon;
    }
    void snap (float v) { cur = tgt = v; }
    void set (float t) { tgt = std::isfinite (t) ? t : tgt; }
    bool settled() const { return cur == tgt; }
    float next()
    {
        if (cur != tgt)
        {
            cur += (tgt - cur) * a;
            if (std::abs (tgt - cur) < eps) cur = tgt;
        }
        return cur;
    }
};

inline float sane (float x) { return std::abs (x) < 1.0e6f ? x : 0.0f; }   // false for NaN/inf too

// Circular delay line, power-of-two sized. tap (d) = sample pushed d samples ago (d >= 1).
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void init (int maxLen)
    {
        int n = 4;
        while (n < maxLen + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;  w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    void push (float x) { buf[(size_t) w] = x;  w = (w + 1) & mask; }
    float tapInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
    float tap (float d) const
    {
        const int i = (int) d;
        const float f = d - (float) i;
        const float a = tapInt (i), b = tapInt (i + 1);
        return a + f * (b - a);
    }
};

// RBJ biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0.0f; }
    float tick (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void set (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        const double n = 1.0 / A0;
        b0 = (float) (B0 * n);  b1 = (float) (B1 * n);  b2 = (float) (B2 * n);
        a1 = (float) (A1 * n);  a2 = (float) (A2 * n);
    }
    void lowShelf (double sr, double f0, double dB)
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f0 / sr;
        const double c = std::cos (w), al = std::sin (w) * 0.5 * std::sqrt (2.0), s = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + s), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - s),
             (A + 1) + (A - 1) * c + s, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - s);
    }
    void highShelf (double sr, double f0, double dB)
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f0 / sr;
        const double c = std::cos (w), al = std::sin (w) * 0.5 * std::sqrt (2.0), s = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + s), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - s),
             (A + 1) - (A - 1) * c + s, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - s);
    }
    void peak (double sr, double f0, double Q, double dB)
    {
        const double A = std::pow (10.0, dB / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f0 / sr;
        const double c = std::cos (w), al = std::sin (w) / (2.0 * Q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
};
} // namespace detail

//==============================================================================
class MasterChain
{
public:
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        for (auto* s : { &sWarm, &sEns, &sComp, &sWidth })  s->init (sr, 30.0f);
        for (auto* s : { &sLow, &sHigh, &sPres })           s->init (sr, 30.0f, 1.0e-4f);

        const int maxD = (int) (0.040 * sr) + 8;
        for (auto& d : ensBuf) d.init (maxD);

        const float tc = 1.0f / (float) sr;
        peakRel = std::exp (-tc / 0.100f);
        rmsA    = 1.0f - std::exp (-tc / 0.030f);
        atkA    = 1.0f - std::exp (-tc / 0.020f);
        relFast = 1.0f - std::exp (-tc / 0.120f);
        relSlow = 1.0f - std::exp (-tc / 0.280f);
        dcR = 1.0f - 2.0f * juce::MathConstants<float>::pi * 8.0f / (float) sr;
        reset();
        sLow.snap (0);  sHigh.snap (0);  sPres.snap (0);
        sWarm.snap (0); sEns.snap (0);   sComp.snap (0);  sWidth.snap (1.0f);
        pending = MasterParams {};
    }

    void reset()
    {
        for (auto& c : eq)  for (auto& b : c) b.reset();
        for (auto& d : ensBuf) d.clear();
        for (auto& p : lfoPhase) p = 0.0f;
        lfoPhase[1] = 0.33f;  lfoPhase[2] = 0.66f;
        dcX = dcY = 0.0f;
        peakEnv = rms2 = grDb = 0.0f;
        eqCountdown = 0;  eqDirty = true;
    }

    void setParams (const MasterParams& p)
    {
        pending = p;
        sLow.set (juce::jlimit (-12.0f, 12.0f, p.eqLowDb));
        sHigh.set (juce::jlimit (-12.0f, 12.0f, p.eqHighDb));
        sPres.set (juce::jlimit (-6.0f, 6.0f, p.presenceDb));
        sComp.set (juce::jlimit (0.0f, 1.0f, p.comp));
        sEns.set (juce::jlimit (0.0f, 1.0f, p.ensemble));
        sWarm.set (juce::jlimit (0.0f, 1.0f, p.warmth));
        sWidth.set (juce::jlimit (0.0f, 1.5f, p.width));
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int nCh = juce::jmin (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();
        if (nCh < 1 || n < 1) return;
        juce::ScopedNoDenormals noDenormals;

        float* L = buffer.getWritePointer (0);
        float* R = nCh > 1 ? buffer.getWritePointer (1) : nullptr;

        for (int i = 0; i < n; ++i)
        {
            L[i] = detail::sane (L[i]);
            if (R != nullptr) R[i] = detail::sane (R[i]);
        }

        if (sWarm.cur != 0.0f || sWarm.tgt != 0.0f)                               warmth (L, R, n);
        if (! eqNeutral())                                                         eqStage (L, R, n);
        if (sEns.cur != 0.0f || sEns.tgt != 0.0f)                                 ensemble (L, R, n);
        if (sComp.cur != 0.0f || sComp.tgt != 0.0f)                               compressor (L, R, n);
        else                                                                       grDb = peakEnv = rms2 = 0.0f;
        if (R != nullptr && (sWidth.cur != 1.0f || sWidth.tgt != 1.0f))           widthStage (L, R, n);
    }

private:
    //----------------------------------------------------------------------- warmth
    // Mild asymmetric tanh, unity small-signal gain, DC removed, blended in parallel (max ~50 %).
    void warmth (float* L, float* R, int n)
    {
        constexpr float drive = 1.4f, bias = 0.15f;
        const float t0 = std::tanh (bias);
        const float norm = 1.0f / (drive * (1.0f - t0 * t0));
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * sWarm.next();
            auto shape = [&] (float x) { return (std::tanh (drive * x + bias) - t0) * norm; };
            float yl = shape (L[i]);
            float yr = R != nullptr ? shape (R[i]) : 0.0f;
            // DC blocker on the (mid) shaped signal, applied to both channels as a common correction
            const float mid = R != nullptr ? 0.5f * (yl + yr) : yl;
            const float mid0 = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
            const float dcIn = mid - mid0;      // non-linear residue of the mid signal
            dcY = dcIn - dcX + dcR * dcY;
            dcX = dcIn;
            const float corr = (dcIn - dcY);    // DC component of the residue
            yl -= corr;  yr -= corr;
            L[i] += m * (yl - L[i]);
            if (R != nullptr) R[i] += m * (yr - R[i]);
        }
    }

    //----------------------------------------------------------------------- EQ
    bool eqNeutral() const
    {
        return sLow.cur == 0.0f && sLow.tgt == 0.0f && sHigh.cur == 0.0f && sHigh.tgt == 0.0f
            && sPres.cur == 0.0f && sPres.tgt == 0.0f;
    }

    void updateEqCoefs()
    {
        for (int c = 0; c < 2; ++c)
        {
            eq[(size_t) c][0].lowShelf (sr, 120.0, sLow.cur);
            eq[(size_t) c][1].peak (sr, 3000.0, 0.9, sPres.cur);
            eq[(size_t) c][2].highShelf (sr, 7000.0, sHigh.cur);
        }
    }

    void eqStage (float* L, float* R, int n)
    {
        const int nCh = R != nullptr ? 2 : 1;
        for (int i = 0; i < n; ++i)
        {
            const bool moving = ! (sLow.settled() && sHigh.settled() && sPres.settled());
            sLow.next();  sHigh.next();  sPres.next();
            if (moving)
            {
                eqDirty = true;
                if (--eqCountdown <= 0) { updateEqCoefs();  eqCountdown = 8; }
            }
            else if (eqDirty)
            {
                updateEqCoefs();  eqDirty = false;   // final coefficients at the settled values
            }
            for (int c = 0; c < nCh; ++c)
            {
                float& s = c == 0 ? L[i] : R[i];
                float y = s;
                for (auto& b : eq[(size_t) c]) y = b.tick (y);
                s = y;
            }
        }
        if (eqNeutral())
            for (auto& c : eq) for (auto& b : c) b.reset();
    }

    //----------------------------------------------------------------------- ensemble
    void ensemble (float* L, float* R, int n)
    {
        static constexpr float baseMs[3] = { 10.0f, 14.0f, 18.0f };
        static constexpr float rate[3]   = { 0.37f, 0.71f, 1.13f };
        constexpr float k = 0.0009f;                       // depth = k / rate seconds: ~constant, tiny pitch deviation
        constexpr float wL[3] = { 1.0f / 1.257f, 0.7f / 1.257f, 0.3f / 1.257f };
        const int nCh = R != nullptr ? 2 : 1;
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float maxD = (float) ensBuf[0].mask - 4.0f;

        for (int i = 0; i < n; ++i)
        {
            const float m = sEns.next();
            ensBuf[0].push (L[i]);
            if (nCh > 1) ensBuf[1].push (R[i]);

            float wet[2] = { 0.0f, 0.0f };
            for (int v = 0; v < 3; ++v)
            {
                lfoPhase[(size_t) v] += rate[v] / (float) sr;
                if (lfoPhase[(size_t) v] >= 1.0f) lfoPhase[(size_t) v] -= 1.0f;
                const float depth = k / rate[v];
                for (int c = 0; c < nCh; ++c)
                {
                    const float ph = lfoPhase[(size_t) v] + (c == 0 ? 0.0f : 0.25f);
                    const float d = juce::jlimit (2.0f, maxD, (float) sr * (0.001f * baseMs[v] + depth * std::sin (twoPi * ph)));
                    const float g = c == 0 ? wL[v] : wL[2 - v];
                    wet[c] += g * ensBuf[(size_t) c].tap (d);
                }
            }
            const float dryG = 1.0f - 0.35f * m, wetG = 0.65f * m;
            L[i] = dryG * L[i] + wetG * wet[0];
            if (nCh > 1) R[i] = dryG * R[i] + wetG * wet[1];
        }
    }

    //----------------------------------------------------------------------- glue compressor
    static constexpr float kThresh = -20.0f, kKnee = 6.0f, kRefLevel = -14.0f;

    void compressor (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float amt = sComp.next();
            const float l = L[i], r = R != nullptr ? R[i] : l;

            const float pk = juce::jmax (std::abs (l), std::abs (r));
            peakEnv = pk > peakEnv ? pk : peakEnv * peakRel;
            const float sq = juce::jmax (l * l, r * r);
            rms2 += (sq - rms2) * rmsA;
            const float det = 0.5f * (peakEnv + std::sqrt (2.0f * rms2));
            const float lvl = 20.0f * std::log10 (juce::jmax (det, 1.0e-6f));

            const float slope = amt / (1.0f + amt);          // 1 - 1/ratio, ratio = 1 + amt (2:1 max)
            const float over = lvl - kThresh;
            float target = 0.0f;
            if (2.0f * over > kKnee)                target = slope * over;
            else if (2.0f * over > -kKnee)         { const float t = over + 0.5f * kKnee;  target = slope * t * t / (2.0f * kKnee); }

            if (target > grDb) grDb += (target - grDb) * atkA;
            else
            {
                const float depthT = juce::jlimit (0.0f, 1.0f, grDb / 8.0f);   // program dependent: deeper GR releases faster
                grDb += (target - grDb) * (relSlow + (relFast - relSlow) * depthT);
            }
            grDb = juce::jmax (0.0f, grDb);

            const float makeup = slope * (kRefLevel - kThresh);
            const float g = std::exp (0.115129255f * (makeup - grDb));
            L[i] = l * g;
            if (R != nullptr) R[i] = r * g;
        }
    }

    //----------------------------------------------------------------------- width
    void widthStage (float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float w = sWidth.next();
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * w;
            L[i] = m + s;
            R[i] = m - s;
        }
    }

    //----------------------------------------------------------------------- state
    double sr = 44100.0;
    MasterParams pending;
    detail::Smooth sLow, sHigh, sPres, sWarm, sEns, sComp, sWidth;
    std::array<std::array<detail::Biquad, 3>, 2> eq;
    int eqCountdown = 0;
    bool eqDirty = false;
    std::array<detail::DelayLine, 2> ensBuf;
    float lfoPhase[3] = { 0.0f, 0.33f, 0.66f };
    float peakEnv = 0.0f, rms2 = 0.0f, grDb = 0.0f;
    float peakRel = 0.999f, rmsA = 0.001f, atkA = 0.001f, relFast = 0.0002f, relSlow = 0.0001f;
    float dcX = 0.0f, dcY = 0.0f, dcR = 0.999f;
};

//==============================================================================
// Dattorro/Griesinger plate: mono-summed input -> predelay -> 4 input allpasses -> cross-coupled
// figure-eight tank (modulated allpass, delay, damping LP, allpass, delay) -> multi-tap stereo output.
class PlateReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        k = (float) (sr / 29761.0);
        predelay.init ((int) (0.101 * sr) + 8);
        for (int i = 0; i < 4; ++i)
        {
            inLen[i] = juce::jmax (2, (int) std::lround (kInLen[i] * k));
            inAp[i].init (inLen[i]);
        }
        for (int s = 0; s < 2; ++s)
        {
            modLen[s] = kModLen[s] * k;
            modExc = 10.0f * k;
            mod[s].init ((int) (modLen[s] + modExc) + 4);
            d1[s].init ((int) (kD1[s] * k * kMaxScale) + 4);
            d2[s].init ((int) (kD2[s] * k * kMaxScale) + 4);
            ap2Len[s] = juce::jmax (2, (int) std::lround (kAp2[s] * k));
            ap2[s].init (ap2Len[s]);
        }
        sScale.init (sr, 300.0f, 1.0e-5f);
        sGain.init (sr, 100.0f, 1.0e-6f);
        sDamp.init (sr, 100.0f, 1.0e-6f);
        sPre.init (sr, 100.0f, 1.0e-3f);
        setParams (0.5f, 0.5f, 0.5f, 10.0f);
        sScale.snap (sScale.tgt);  sGain.snap (sGain.tgt);  sDamp.snap (sDamp.tgt);  sPre.snap (sPre.tgt);
        reset();
    }

    void reset()
    {
        predelay.clear();
        for (auto& a : inAp) a.clear();
        for (int s = 0; s < 2; ++s) { mod[s].clear(); d1[s].clear(); d2[s].clear(); ap2[s].clear(); lp[s] = 0.0f; }
        bwState = 0.0f;  lfo[0] = 0.0f;  lfo[1] = 0.37f;
    }

    // size 0..1 (tank length), decay 0..1 (RT60 0.2..8 s), damping 0..1, predelayMs 0..100. 100 % wet out.
    void setParams (float size, float decay, float damping, float predelayMs)
    {
        size = juce::jlimit (0.0f, 1.0f, size);
        decay = juce::jlimit (0.0f, 1.0f, decay);
        damping = juce::jlimit (0.0f, 1.0f, damping);
        predelayMs = juce::jlimit (0.0f, 100.0f, predelayMs);
        const float scale = 0.5f + size;                       // 0.5 .. 1.5
        const float rt60 = 0.2f * std::pow (40.0f, decay);     // 0.2 .. 8 s
        const float loopSec = ((kModLen[0] + kModLen[1] + kAp2[0] + kAp2[1])
                               + (kD1[0] + kD1[1] + kD2[0] + kD2[1]) * scale) / 29761.0f;
        const float g = std::pow (10.0f, -3.0f * 0.5f * loopSec / rt60);   // g is applied twice per round trip
        sScale.set (scale);
        sGain.set (juce::jlimit (0.0f, 0.985f, g));
        sDamp.set (0.75f * damping);
        sPre.set (predelayMs * 0.001f * (float) sr);
    }

    void processStereo (float* left, float* right, int numSamples)
    {
        juce::ScopedNoDenormals noDenormals;
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float lfoInc0 = 0.83f / (float) sr, lfoInc1 = 0.67f / (float) sr;
        constexpr float outGain = 0.55f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float scale = sScale.next(), g = sGain.next(), damp = sDamp.next();
            const float pre = juce::jlimit (1.0f, (float) predelay.mask - 3.0f, sPre.next());

            const float in = 0.5f * (detail::sane (left[i]) + detail::sane (right[i]));
            predelay.push (in);
            float x = predelay.tap (pre);
            bwState += 0.85f * (x - bwState);                  // mild input bandwidth
            x = bwState;
            x = allpass (inAp[0], inLen[0], 0.75f, x);
            x = allpass (inAp[1], inLen[1], 0.75f, x);
            x = allpass (inAp[2], inLen[2], 0.625f, x);
            x = allpass (inAp[3], inLen[3], 0.625f, x);

            const float endL = d2[0].tap (len2 (0, scale));
            const float endR = d2[1].tap (len2 (1, scale));
            const float xin[2] = { x + g * endR, x + g * endL };

            lfo[0] += lfoInc0;  if (lfo[0] >= 1.0f) lfo[0] -= 1.0f;
            lfo[1] += lfoInc1;  if (lfo[1] >= 1.0f) lfo[1] -= 1.0f;

            for (int s = 0; s < 2; ++s)
            {
                // modulated allpass (g = 0.7): v = x - g*v_D ; y = g*v + v_D
                const float md = modLen[s] + modExc * std::sin (twoPi * lfo[s]);
                const float vd = mod[s].tap (md);
                const float v = detail::sane (xin[s] - 0.7f * vd);
                mod[s].push (v);
                float y = 0.7f * v + vd;

                d1[s].push (y);
                y = d1[s].tap (len1 (s, scale));
                lp[s] = (1.0f - damp) * y + damp * lp[s];
                y = allpass (ap2[s], ap2Len[s], 0.5f, lp[s]);
                d2[s].push (y);
            }

            auto t1 = [&] (int s, float pos) { return d1[s].tap (juce::jmax (1.0f, pos * k * scale)); };
            auto t2 = [&] (int s, float pos) { return d2[s].tap (juce::jmax (1.0f, pos * k * scale)); };
            auto ta = [&] (int s, float pos) { return ap2[s].tapInt (juce::jlimit (1, ap2Len[s], (int) std::lround (pos * k))); };

            const float oL = t1 (1, 266) + t1 (1, 2974) - ta (1, 1913) + t2 (1, 1996) - t1 (0, 1990) - ta (0, 187) - t2 (0, 1066);
            const float oR = t1 (0, 353) + t1 (0, 3627) - ta (0, 1228) + t2 (0, 2673) - t1 (1, 2111) - ta (1, 335) - t2 (1, 121);
            left[i]  = detail::sane (outGain * oL);
            right[i] = detail::sane (outGain * oR);
        }
    }

private:
    static constexpr float kMaxScale = 1.5f;
    static constexpr int kInLen[4] = { 142, 107, 379, 277 };
    static constexpr float kModLen[2] = { 672.0f, 908.0f };
    static constexpr float kD1[2]  = { 4453.0f, 4217.0f };
    static constexpr float kAp2[2] = { 1800.0f, 2656.0f };
    static constexpr float kD2[2]  = { 3720.0f, 3163.0f };

    float len1 (int s, float scale) const { return juce::jmax (1.0f, kD1[s] * k * scale); }
    float len2 (int s, float scale) const { return juce::jmax (1.0f, kD2[s] * k * scale); }

    static float allpass (detail::DelayLine& d, int len, float g, float x)
    {
        const float vd = d.tapInt (len);
        const float v = x - g * vd;
        d.push (v);
        return g * v + vd;
    }

    double sr = 44100.0;
    float k = 1.0f, modExc = 10.0f, bwState = 0.0f;
    detail::DelayLine predelay;
    detail::DelayLine inAp[4];
    int inLen[4] = { 142, 107, 379, 277 };
    detail::DelayLine mod[2], d1[2], d2[2], ap2[2];
    float modLen[2] = { 672.0f, 908.0f };
    int ap2Len[2] = { 1800, 2656 };
    float lp[2] = { 0.0f, 0.0f };
    float lfo[2] = { 0.0f, 0.37f };
    detail::Smooth sScale, sGain, sDamp, sPre;
};

} // namespace wgfx
