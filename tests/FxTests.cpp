#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "fx/Effects.h"
#include "fx/MasterFx.h"
#include <chrono>
#include <cmath>
#include <vector>

namespace
{
constexpr double kFs = 48000.0;
constexpr int kBlock = 512;

using Clock = std::chrono::steady_clock;

double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

// Runs `in` (nCh x N) through the chain in blocks of kBlock.
std::vector<std::vector<float>> runChain (wgfx::MasterChain& chain, const std::vector<std::vector<float>>& in)
{
    const int nCh = (int) in.size(), N = (int) in[0].size();
    auto out = in;
    juce::AudioBuffer<float> buf (nCh, kBlock);
    for (int pos = 0; pos < N; pos += kBlock)
    {
        const int m = std::min (kBlock, N - pos);
        for (int c = 0; c < nCh; ++c)
            std::copy_n (in[(size_t) c].data() + pos, m, buf.getWritePointer (c));
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), nCh, m);
        chain.process (view);
        for (int c = 0; c < nCh; ++c)
            std::copy_n (buf.getReadPointer (c), m, out[(size_t) c].data() + pos);
    }
    return out;
}

std::vector<float> sine (double f, double amp, int N)
{
    std::vector<float> x ((size_t) N);
    for (int i = 0; i < N; ++i) x[(size_t) i] = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / kFs));
    return x;
}

std::vector<float> noise (int N, juce::Random& r, float amp)
{
    std::vector<float> x ((size_t) N);
    for (auto& v : x) v = amp * (r.nextFloat() * 2.0f - 1.0f);
    return x;
}

// RMS over [from, N)
double rmsOf (const std::vector<float>& x, int from)
{
    double s = 0.0;
    for (size_t i = (size_t) from; i < x.size(); ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / (double) (x.size() - (size_t) from));
}

bool allFinite (const std::vector<float>& x, float limit)
{
    for (float v : x) if (! std::isfinite (v) || std::abs (v) > limit) return false;
    return true;
}
} // namespace

struct FxTests : public juce::UnitTest
{
    FxTests() : juce::UnitTest ("Fx", "WaveGrid") {}

    // Gain in dB of a sine through the chain (steady state, second half).
    double toneGainDb (const wgfx::MasterParams& p, double f, double amp, double seconds = 1.5)
    {
        wgfx::MasterChain c;
        c.prepare (kFs, kBlock);
        c.setParams (p);
        const int N = (int) (seconds * kFs);
        auto x = sine (f, amp, N);
        auto y = runChain (c, { x, x });
        return toDb (rmsOf (y[0], N / 2)) - toDb (rmsOf (x, N / 2));
    }

    void runTest() override
    {
        juce::Random rnd (42);

        beginTest ("MasterChain neutral passthrough");
        {
            const int N = (int) (2 * kFs);
            auto nl = noise (N, rnd, 0.5f), nr = noise (N, rnd, 0.5f);
            wgfx::MasterChain c;
            c.prepare (kFs, kBlock);
            c.setParams (wgfx::MasterParams {});
            auto y = runChain (c, { nl, nr });
            double err = 0.0, ref = 0.0;
            for (int i = 0; i < N; ++i)
            {
                err += std::pow ((double) y[0][(size_t) i] - nl[(size_t) i], 2) + std::pow ((double) y[1][(size_t) i] - nr[(size_t) i], 2);
                ref += (double) nl[(size_t) i] * nl[(size_t) i] + (double) nr[(size_t) i] * nr[(size_t) i];
            }
            const double errDb = 10.0 * std::log10 (std::max (err, 1.0e-30) / ref);
            logMessage ("neutral error: " + juce::String (errDb, 1) + " dB re input");
            expect (errDb < -90.0, "neutral chain must be transparent, got " + juce::String (errDb));

            // mono buffer and return-to-neutral after excursion
            wgfx::MasterParams p;  p.eqLowDb = 6;  p.comp = 1;  p.ensemble = 1;  p.warmth = 1;  p.width = 1.4f;
            c.setParams (p);
            runChain (c, { nl, nr });
            c.setParams (wgfx::MasterParams {});
            runChain (c, { nl, nr });   // let smoothing settle, then re-measure from a settled state
            c.reset();
            auto m = runChain (c, { nl });
            double e2 = 0.0;
            for (int i = 0; i < N; ++i) e2 += std::pow ((double) m[0][(size_t) i] - nl[(size_t) i], 2);
            expect (10.0 * std::log10 (std::max (e2, 1.0e-30) / ref) < -90.0, "mono neutral passthrough after returning to default");
        }

        beginTest ("EQ shelf and presence gains");
        {
            struct Case { const char* name; double f; wgfx::MasterParams p; double want; };
            auto mk = [] (float lo, float hi, float pr) { wgfx::MasterParams p; p.eqLowDb = lo; p.eqHighDb = hi; p.presenceDb = pr; return p; };
            const std::vector<Case> cases {
                { "low +9 @30Hz",   30.0,    mk (9, 0, 0),  9.0 },
                { "low -9 @30Hz",   30.0,    mk (-9, 0, 0), -9.0 },
                { "high +9 @18kHz", 18000.0, mk (0, 9, 0),  9.0 },
                { "high -9 @18kHz", 18000.0, mk (0, -9, 0), -9.0 },
                { "pres +5 @3kHz",  3000.0,  mk (0, 0, 5),  5.0 },
                { "pres -5 @3kHz",  3000.0,  mk (0, 0, -5), -5.0 },
                { "low +9 @2kHz leaves mids", 2000.0, mk (9, 0, 0), 0.0 },
                { "high +9 @500Hz leaves mids", 500.0, mk (0, 9, 0), 0.0 },
            };
            for (auto& c : cases)
            {
                const double g = toneGainDb (c.p, c.f, 0.25);
                logMessage (juce::String (c.name) + ": " + juce::String (g, 2) + " dB (want " + juce::String (c.want, 1) + ")");
                expect (std::abs (g - c.want) < 1.0, juce::String (c.name) + " measured " + juce::String (g));
            }
        }

        beginTest ("Compressor");
        {
            wgfx::MasterParams p;  p.comp = 0.0f;
            expect (std::abs (toneGainDb (p, 440.0, 0.8)) < 0.01, "comp=0 is level neutral");

            p.comp = 1.0f;
            // detector level of a sine == its amplitude; threshold -20 dB, ratio 2:1, makeup +3 dB
            const double amp = 0.794;                       // -2 dBFS
            const double lvl = toDb (amp);
            const double expected = -(lvl + 20.0) * 0.5 + 3.0;
            const double loud = toneGainDb (p, 440.0, amp, 3.0);
            logMessage ("comp=1 loud tone (-2 dBFS): " + juce::String (loud, 2) + " dB (expected " + juce::String (expected, 2) + ")");
            expect (std::abs (loud - expected) < 1.5, "loud tone reduction");
            expect (loud < -3.0, "loud tone is actually reduced");

            const double quiet = toneGainDb (p, 440.0, 0.2, 3.0);   // -14 dBFS: the auto-makeup reference
            logMessage ("comp=1 reference tone (-14 dBFS): " + juce::String (quiet, 2) + " dB");
            expect (std::abs (quiet) < 1.5, "reference-level program is loudness neutral");
        }

        beginTest ("Ensemble mono compatibility");
        {
            const int N = (int) (4 * kFs);
            auto n = noise (N, rnd, 0.3f);
            wgfx::MasterChain c;
            c.prepare (kFs, kBlock);
            wgfx::MasterParams p;  p.ensemble = 1.0f;
            c.setParams (p);
            auto y = runChain (c, { n, n });
            std::vector<float> sum ((size_t) N), dry ((size_t) N);
            double diff = 0.0;
            for (int i = 0; i < N; ++i)
            {
                sum[(size_t) i] = y[0][(size_t) i] + y[1][(size_t) i];
                dry[(size_t) i] = 2.0f * n[(size_t) i];
                diff += std::abs ((double) y[0][(size_t) i] - y[1][(size_t) i]);
            }
            const double rel = toDb (rmsOf (sum, N / 4)) - toDb (rmsOf (dry, N / 4));
            logMessage ("ensemble L+R energy vs dry: " + juce::String (rel, 2) + " dB; mean |L-R| " + juce::String (diff / N, 4));
            expect (std::abs (rel) < 3.0, "mono-sum level within 3 dB");
            expect (diff / N > 0.005, "ensemble actually widens the image");

            // band-limited check: 250 Hz bands (notch hunt) -- sum of sines with random phases averaged
            double worst = 0.0;
            for (double f : { 100.0, 220.0, 440.0, 900.0, 1800.0, 3500.0 })
            {
                auto s = sine (f, 0.3, N);
                c.reset();
                auto o = runChain (c, { s, s });
                std::vector<float> m ((size_t) N);
                for (int i = 0; i < N; ++i) m[(size_t) i] = 0.5f * (o[0][(size_t) i] + o[1][(size_t) i]);
                worst = std::max (worst, std::abs (toDb (rmsOf (m, N / 2)) - toDb (rmsOf (s, N / 2))));
            }
            logMessage ("ensemble worst sine mono-sum deviation: " + juce::String (worst, 2) + " dB");
            expect (worst < 6.0, "no deep notches for sines at full ensemble");
        }

        beginTest ("Width");
        {
            const int N = (int) kFs;
            auto a = noise (N, rnd, 0.4f), b = noise (N, rnd, 0.4f);
            wgfx::MasterChain c;
            c.prepare (kFs, kBlock);
            wgfx::MasterParams p;  p.width = 0.0f;
            c.setParams (p);
            auto y = runChain (c, { a, b });
            double d = 0.0;
            for (int i = N / 2; i < N; ++i) d = std::max (d, (double) std::abs (y[0][(size_t) i] - y[1][(size_t) i]));
            expect (d < 1.0e-5, "width=0 collapses to mono, max |L-R| = " + juce::String (d));
            // mid preserved
            double mid = 0.0;
            for (int i = N / 2; i < N; ++i) mid = std::max (mid, (double) std::abs (y[0][(size_t) i] - 0.5f * (a[(size_t) i] + b[(size_t) i])));
            expect (mid < 1.0e-5, "mid preserved");

            p.width = 1.5f;
            c.setParams (p);
            y = runChain (c, { a, b });
            double sIn = 0, sOut = 0;
            for (int i = N / 2; i < N; ++i)
            {
                sIn  += std::pow (0.5 * (a[(size_t) i] - b[(size_t) i]), 2);
                sOut += std::pow (0.5 * (y[0][(size_t) i] - y[1][(size_t) i]), 2);
            }
            expect (std::abs (10.0 * std::log10 (sOut / sIn) - 3.52) < 0.1, "width=1.5 boosts side by 3.5 dB");
        }

        beginTest ("Bounded and NaN safe");
        {
            const int N = (int) (2 * kFs);
            auto a = noise (N, rnd, 4.0f);
            a[1000] = std::numeric_limits<float>::quiet_NaN();
            a[2000] = std::numeric_limits<float>::infinity();
            wgfx::MasterChain c;
            c.prepare (kFs, kBlock);
            wgfx::MasterParams p;  p.eqLowDb = 12;  p.eqHighDb = 12;  p.presenceDb = 6;  p.comp = 1;  p.ensemble = 1;  p.warmth = 1;  p.width = 1.5f;
            c.setParams (p);
            auto y = runChain (c, { a, a });
            expect (allFinite (y[0], 1000.0f) && allFinite (y[1], 1000.0f), "output finite with NaN/inf/hot input and all params maxed");
        }

        beginTest ("PlateReverb decay");
        {
            for (float decay : { 0.4f, 0.7f, 1.0f })
            {
                wgfx::PlateReverb r;
                r.prepare (kFs);
                r.setParams (0.5f, decay, 0.3f, 0.0f);
                const int N = (int) (14 * kFs);
                std::vector<float> L ((size_t) N, 0.0f), R ((size_t) N, 0.0f);
                L[0] = R[0] = 1.0f;
                r.processStereo (L.data(), R.data(), N);
                // Schroeder backward integration of the summed channels
                std::vector<double> edc ((size_t) N);
                double acc = 0.0;
                for (int i = N - 1; i >= 0; --i) { acc += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]; edc[(size_t) i] = acc; }
                auto tAt = [&] (double db) { for (int i = 0; i < N; ++i) if (10.0 * std::log10 (edc[(size_t) i] / edc[0]) < db) return i / kFs; return 1.0e9; };
                const double t5 = tAt (-5.0), t25 = tAt (-25.0);
                const double rt = (t25 - t5) * 3.0;     // T20 extrapolated to 60 dB
                const double nominal = 0.2 * std::pow (40.0, decay);
                logMessage ("plate decay=" + juce::String (decay, 1) + ": RT60(T20) " + juce::String (rt, 2) + " s, nominal " + juce::String (nominal, 2)
                            + " s (damping 0.3 shortens HF)");
                expect (rt > 0.5 * nominal && rt < 1.3 * nominal, "RT within expectation");
                const double tail = tAt (-60.0);
                expect (tail < 1.2 * nominal * 1.0 + 0.5, "EDC reaches -60 dB in time");
                // smoothness: EDC curvature -- compare slope in first/second half of the 5..35 dB range
                const double t15 = tAt (-15.0), t35 = tAt (-35.0);
                const double s1 = 10.0 / (t15 - t5), s2 = 20.0 / (t35 - t15 + 1.0e-9);   // dB/s, early vs late
                logMessage ("   slope early " + juce::String (s1, 1) + " dB/s, late " + juce::String (s2, 1) + " dB/s");
                expect (std::abs (s1 - s2) / s2 < 0.5, "decay slope roughly constant (smooth, no knee)");
                expect (allFinite (L, 10.0f) && allFinite (R, 10.0f), "IR finite");
                // energy at end of the 14 s run must be tiny
                double endE = 0.0;
                for (int i = N - 4800; i < N; ++i) endE += (double) L[(size_t) i] * L[(size_t) i];
                expect (decay < 0.99f ? endE < 1.0e-12 : true, "tail is gone");
            }
        }

        beginTest ("PlateReverb damping");
        {
            auto tailDb = [&] (float damping)
            {
                wgfx::PlateReverb r;  r.prepare (kFs);  r.setParams (0.5f, 0.8f, damping, 0.0f);
                const int N = (int) (4 * kFs);
                std::vector<float> L ((size_t) N, 0.0f), R ((size_t) N, 0.0f);
                L[0] = R[0] = 1.0f;
                r.processStereo (L.data(), R.data(), N);
                // high-passed (first difference) energy in 1.5..3 s
                double e = 0.0;
                for (int i = (int) (1.5 * kFs); i < (int) (3 * kFs); ++i) e += std::pow ((double) L[(size_t) i] - L[(size_t) i - 1], 2);
                return 10.0 * std::log10 (e + 1.0e-30);
            };
            const double d0 = tailDb (0.0f), d1 = tailDb (1.0f);
            logMessage ("HF tail energy 1.5-3 s: damping 0 " + juce::String (d0, 1) + " dB, damping 1 " + juce::String (d1, 1) + " dB");
            expect (d1 < d0 - 6.0, "damping darkens the tail");
        }

        beginTest ("PlateReverb stability");
        {
            wgfx::PlateReverb r;
            r.prepare (kFs);
            r.setParams (1.0f, 1.0f, 0.0f, 100.0f);
            const int blocks = (int) (30 * kFs / kBlock);
            std::vector<float> L (kBlock), R (kBlock);
            bool ok = true;
            double peak = 0.0;
            for (int b = 0; b < blocks && ok; ++b)
            {
                for (int i = 0; i < kBlock; ++i) { L[(size_t) i] = rnd.nextFloat() * 2.0f - 1.0f;  R[(size_t) i] = rnd.nextFloat() * 2.0f - 1.0f; }
                if (b % 400 == 399) r.setParams (rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat(), rnd.nextFloat() * 100.0f);
                r.processStereo (L.data(), R.data(), kBlock);
                for (int i = 0; i < kBlock; ++i)
                {
                    peak = std::max ({ peak, (double) std::abs (L[(size_t) i]), (double) std::abs (R[(size_t) i]) });
                    if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) ok = false;
                }
            }
            logMessage ("plate 30 s full-scale noise, decay=1 damping=0 (+ random param jumps): peak " + juce::String (peak, 2));
            expect (ok && peak < 20.0, "finite and bounded");
        }

        beginTest ("SpaceFx stable");
        {
            wgfx::SpaceFx s;
            s.prepare (kFs);
            const int blocks = (int) (30 * kFs / kBlock);
            std::vector<float> L (kBlock), R (kBlock);
            bool ok = true;
            double peak = 0.0;
            for (int b = 0; b < blocks && ok; ++b)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = 0.5f * (rnd.nextFloat() * 2.0f - 1.0f);
                    L[(size_t) i] = v;  R[(size_t) i] = 0.5f * v;
                }
                s.process (L.data(), R.data(), kBlock, 1.0f, 1.0f, 1.0f, 330.0f);
                for (int i = 0; i < kBlock; ++i)
                {
                    peak = std::max ({ peak, (double) std::abs (L[(size_t) i]), (double) std::abs (R[(size_t) i]) });
                    if (! std::isfinite (L[(size_t) i]) || ! std::isfinite (R[(size_t) i])) ok = false;
                }
            }
            logMessage ("SpaceFx max settings, 30 s noise: peak " + juce::String (peak, 2));
            expect (ok && peak < 30.0, "SpaceFx finite and bounded at max settings");

            // loudness balance: wet/dry on a noise burst, space 0 vs 1
            auto level = [&] (float space)
            {
                wgfx::SpaceFx f;  f.prepare (kFs);
                double e = 0.0;  int cnt = 0;
                for (int b = 0; b < 200; ++b)
                {
                    for (int i = 0; i < kBlock; ++i) { L[(size_t) i] = 0.2f * (rnd.nextFloat() * 2.0f - 1.0f);  R[(size_t) i] = L[(size_t) i]; }
                    f.process (L.data(), R.data(), kBlock, space, 0.0f, 0.0f, 330.0f);
                    if (b >= 100) for (int i = 0; i < kBlock; ++i) { e += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i];  cnt += 2; }
                }
                return 10.0 * std::log10 (e / cnt);
            };
            const double l0 = level (0.0f), l1 = level (1.0f), lh = level (0.5f);
            logMessage ("SpaceFx noise level: space0 " + juce::String (l0, 1) + " dB, space0.5 " + juce::String (lh, 1) + " dB, space1 " + juce::String (l1, 1) + " dB");
            expect (std::abs (l1 - l0) < 4.0 && std::abs (lh - l0) < 4.0, "space does not change overall loudness drastically");
        }

        beginTest ("CPU cost");
        {
            const int blocks = (int) (10 * kFs / kBlock);
            auto rtf = [&] (auto&& fn)
            {
                const auto t0 = Clock::now();
                for (int b = 0; b < blocks; ++b) fn();
                const double sec = std::chrono::duration<double> (Clock::now() - t0).count();
                return (blocks * (double) kBlock / kFs) / sec;
            };
            juce::AudioBuffer<float> buf (2, kBlock);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < kBlock; ++i) buf.setSample (c, i, 0.3f * (rnd.nextFloat() * 2.0f - 1.0f));
            wgfx::MasterChain mc;  mc.prepare (kFs, kBlock);
            wgfx::MasterParams p;  p.eqLowDb = 3;  p.eqHighDb = 2;  p.presenceDb = 2;  p.comp = 0.6f;  p.ensemble = 0.5f;  p.warmth = 0.5f;  p.width = 1.2f;
            mc.setParams (p);
            const double r1 = rtf ([&] { mc.process (buf); });
            wgfx::PlateReverb pr;  pr.prepare (kFs);  pr.setParams (0.6f, 0.7f, 0.4f, 20.0f);
            std::vector<float> L (kBlock, 0.1f), R (kBlock, 0.1f);
            const double r2 = rtf ([&] { std::fill (L.begin(), L.end(), 0.1f);  std::fill (R.begin(), R.end(), 0.1f);  pr.processStereo (L.data(), R.data(), kBlock); });
            wgfx::SpaceFx sp;  sp.prepare (kFs);
            const double r3 = rtf ([&] { std::fill (L.begin(), L.end(), 0.1f);  std::fill (R.begin(), R.end(), 0.1f);  sp.process (L.data(), R.data(), kBlock, 0.7f, 0.5f, 0.3f, 330.0f); });
            logMessage ("real-time factor (x faster than realtime, single core): MasterChain all-on " + juce::String (r1, 0)
                        + "x, PlateReverb " + juce::String (r2, 0) + "x, SpaceFx full " + juce::String (r3, 0) + "x");
            expect (r1 > 20.0 && r2 > 20.0 && r3 > 10.0, "cheap enough for real time");
        }
    }
};

static FxTests fxTests;
