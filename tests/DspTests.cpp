#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "dsp/StringVoice.h"
#include <chrono>
#include <cmath>
#include <vector>

namespace
{
constexpr double kFs = 48000.0;

struct Setting { float bright, sustain, stiff; const char* label; int maxNote; };

// Autocorrelation f0 estimate, refined by locating the peak several periods out
// (long lag => fine frequency resolution) with parabolic interpolation.
double estimateF0 (const std::vector<float>& x, double fs, double guessHz)
{
    const int n = (int) x.size();
    auto ac = [&] (int lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < n; ++i) s += (double) x[(size_t) i] * x[(size_t) (i + lag)];
        return s;
    };
    auto peakNear = [&] (double lagGuess, int halfWidth)
    {
        int best = juce::jmax (2, (int) std::lround (lagGuess) - halfWidth);
        double bv = -1e30;
        for (int l = best; l <= (int) std::lround (lagGuess) + halfWidth; ++l)
            if (const double v = ac (l); v > bv) { bv = v; best = l; }
        const double a = ac (best - 1), b = ac (best), c = ac (best + 1);
        const double den = a - 2.0 * b + c;
        return (double) best + (std::abs (den) > 1e-30 ? 0.5 * (a - c) / den : 0.0);
    };

    const double p0 = fs / guessHz;
    const double coarse = peakNear (p0, juce::jmax (2, (int) (p0 * 0.06)));   // +-~1 semitone
    const int periods = juce::jmax (1, (int) (0.02 * fs / coarse));            // lag ~20 ms
    const double fine = peakNear (coarse * periods, juce::jmax (2, (int) (coarse * 0.5)));
    return fs * periods / fine;
}

double centsOff (double measured, double target) { return 1200.0 * std::log2 (measured / target); }

float runString (WaveguideString& s, int n, std::vector<float>* out = nullptr)
{
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float y = s.processSample();
        peak = juce::jmax (peak, std::abs (y));
        if (out != nullptr) (*out)[(size_t) i] = y;
    }
    return peak;
}
}


// Spectral estimate of the FUNDAMENTAL only (peak near hz): not fooled by slightly inharmonic partials.
static double estimateF0Spectral (const std::vector<float>& w, double fs, double hz)
{
    const int order = 17, N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const int L = (int) w.size();
    for (int i = 0; i < L; ++i)
        buf[(size_t) i] = w[(size_t) i] * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (L - 1)));
    fft.performRealOnlyForwardTransform (buf.data(), true);
    auto mag = [&] (int k) { return std::hypot (buf[(size_t) 2 * k], buf[(size_t) 2 * k + 1]); };
    const int lo = juce::jmax (1, (int) (0.94 * hz * N / fs)), hi = juce::jmin (N / 2 - 2, (int) (1.06 * hz * N / fs));
    int pk = lo;
    for (int k = lo; k <= hi; ++k) if (mag (k) > mag (pk)) pk = k;
    const double a = std::log (mag (pk - 1) + 1e-20), b = std::log (mag (pk) + 1e-20), c = std::log (mag (pk + 1) + 1e-20);
    const double d = 0.5 * (a - c) / (a - 2.0 * b + c);
    return ((double) pk + d) * fs / (double) N;
}

struct DspTests : public juce::UnitTest
{
    DspTests() : juce::UnitTest ("DSP", "WaveGrid") {}

    void runTest() override
    {
        pitchAccuracy();
        stability();
        denormals();
        benchmark();
    }

    void pitchAccuracy()
    {
        beginTest ("pitch accuracy (+-5 cents, MIDI 28..100)");
        const Setting settings[] = {
            { 1.0f, 1.0f, 0.0f, "bright/stiff0", 100 },
            { 0.5f, 1.0f, 0.1f, "mid/stiff.1", 100 },
            // dark: above C6 this tone rings for only milliseconds, so there is no measurable pitch
            { 0.0f, 1.0f, 0.0f, "dark/stiff0", 84 },
            { 0.5f, 1.0f, 0.6f, "mid/stiff.6", 100 },
        };

        int failures = 0;
        double worst = 0.0;
        for (auto& st : settings)
        {
            juce::String table = "\nPitch deviation (cents) [" + juce::String (st.label) + "]\n";
            juce::String row;
            for (int note = 28; note <= st.maxNote; ++note)
            {
                const double hz = 440.0 * std::pow (2.0, (note - 69) / 12.0);
                WaveguideString s;
                s.prepare (kFs);
                s.setNoiseSeed (12345 + note);
                s.setBrightness (st.bright); s.setSustain (st.sustain); s.setInharmonicity (st.stiff);
                s.setPickPosition (0.13f);   s.setGlideSeconds (0.0f);
                s.setFrequencyImmediate ((float) hz);
                s.pluck (1.0f);
                const int skip = juce::jmax ((int) (0.004 * kFs), (int) (4.0 * kFs / hz)), len = juce::jmax ((int) (0.08 * kFs), (int) (12.0 * kFs / hz));   // >= 12 periods so low notes are resolvable
                std::vector<float> y ((size_t) (skip + len));
                runString (s, skip + len, &y);
                std::vector<float> w (y.begin() + skip, y.end());
                float wp = 0.0f;
                for (float v : w) wp = juce::jmax (wp, std::abs (v));
                if (wp < 1.0e-3f) { row << juce::String (note) << ":decayed  "; continue; }   // below -60 dB the window is noise, not a pitch
                for (auto& v : w) v /= wp;
                // assert on the FUNDAMENTAL (spectral peak); the whole-waveform autocorrelation is pulled sharp by the
                // slightly inharmonic partials of a strongly low-passed loop, so it is only a debug cross-check.
                const double cents = centsOff (estimateF0Spectral (w, kFs, hz), hz);
                if (getenv ("WG_PITCH_DEBUG"))
                    logMessage ("  [" + juce::String (st.label) + "] note " + juce::String (note) + " spectral " + juce::String (cents, 2) + "  autocorr " + juce::String (centsOff (estimateF0 (w, kFs, hz), hz), 2));
                worst = juce::jmax (worst, std::abs (cents));
                if (std::abs (cents) > 5.0) ++failures;
                row << (note % 12 == 4 ? "\n" : "") << juce::String (note) << ":" << juce::String (cents, 1) << "  ";
            }
            table << row;
            logMessage (table);
        }
        logMessage ("Worst |deviation|: " + juce::String (worst, 2) + " cents; out-of-tolerance cells: " + juce::String (failures));
        expect (failures == 0, juce::String (failures) + " (note, setting) cells exceed 5 cents; worst " + juce::String (worst, 2));
    }

    void stability()
    {
        beginTest ("stability: sustain=1, bow/pluck sweep, 10 s");
        const float freqs[] = { 55.0f, 440.0f, 2000.0f };
        const float bows[] = { 0.0f, 0.03f, 1.0f };
        const float vels[] = { 0.2f, 1.0f };
        const float brights[] = { 0.0f, 1.0f };
        const float drives[] = { 0.0f, 1.0f };
        int combos = 0, bad = 0;
        const int n = (int) (10.0 * kFs);

        for (float f : freqs) for (float bw : bows) for (float v : vels) for (float br : brights) for (float dr : drives)
        {
            WaveguideString s;
            s.prepare (kFs);
            s.setBrightness (br); s.setSustain (1.0f); s.setInharmonicity (0.3f);
            s.setDrive (dr);      s.setGlideSeconds (0.0f);
            s.setFrequencyImmediate (f);
            s.setBow (bw);
            s.pluck (v);
            bool finite = true; float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float y = s.processSample();
                if (! std::isfinite (y)) { finite = false; break; }
                peak = juce::jmax (peak, std::abs (y));
            }
            // release: lift finger, stop feeding
            s.setBow (0.0f); s.setSustain (0.0f);
            float tail = 0.0f;
            for (int i = 0; i < (int) (3.0 * kFs); ++i)
            {
                const float y = s.processSample();
                if (! std::isfinite (y)) { finite = false; break; }
                if (i > (int) (2.5 * kFs)) tail = juce::jmax (tail, std::abs (y));
            }
            ++combos;
            const bool ok = finite && peak < 2.0f && tail < 1.0e-3f;
            if (! ok)
            {
                ++bad;
                logMessage ("FAIL f=" + juce::String (f) + " bow=" + juce::String (bw) + " vel=" + juce::String (v)
                            + " bright=" + juce::String (br) + " drive=" + juce::String (dr)
                            + " finite=" + juce::String ((int) finite) + " peak=" + juce::String (peak)
                            + " tail=" + juce::String (tail));
            }
        }
        logMessage ("Stability combos: " + juce::String (combos) + ", failures: " + juce::String (bad));
        expect (bad == 0, juce::String (bad) + " of " + juce::String (combos) + " combos unstable / not silent after release");
    }

    void denormals()
    {
        beginTest ("no NaN/Inf/denormals in output through a long decay");
        long nan = 0, sub = 0;
        for (float f : { 110.0f, 880.0f })
        {
            WaveguideString s;
            s.prepare (kFs);
            s.setBrightness (0.3f); s.setSustain (0.2f); s.setGlideSeconds (0.0f);
            s.setFrequencyImmediate (f);
            s.pluck (1.0f);
            for (int i = 0; i < (int) (10.0 * kFs); ++i)
            {
                const float y = s.processSample();
                const int c = std::fpclassify (y);
                if (c == FP_NAN || c == FP_INFINITE) ++nan;
                if (c == FP_SUBNORMAL) ++sub;
            }
        }
        expectEquals ((int) nan, 0);
        expect (sub == 0, "subnormal output samples: " + juce::String ((juce::int64) sub)
                          + " (processBlock uses ScopedNoDenormals, but the tail should flush itself)");
    }

    void benchmark()
    {
        beginTest ("CPU benchmark: 15 voices x 10 s @ 48 kHz");
        StringParams params;
        params.hold = 1.0f; params.sustain = 0.9f; params.bow = 0.6f;
        juce::MPESynthesiser synth;
        synth.setVoiceStealingEnabled (false);
        for (int i = 0; i < 15; ++i) synth.addVoice (new StringVoice (params));
        juce::MPEZoneLayout layout;
        layout.setLowerZone (15, 48);
        synth.setZoneLayout (layout);
        synth.setCurrentPlaybackSampleRate (kFs);
        synth.setMinimumRenderingSubdivisionSize (32);

        juce::MidiBuffer notes;
        for (int i = 0; i < 15; ++i)
        {
            notes.addEvent (juce::MidiMessage::pitchWheel (i + 2, 8192), 0);
            notes.addEvent (juce::MidiMessage::controllerEvent (i + 2, 74, 64), 0);
            notes.addEvent (juce::MidiMessage::channelPressureChange (i + 2, 80), 0);
            notes.addEvent (juce::MidiMessage::noteOn (i + 2, 40 + i * 3, (juce::uint8) 100), 0);
        }
        const int block = 256, total = (int) (10.0 * kFs);
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer empty;
        float peak = 0.0f; bool finite = true;

        const auto t0 = std::chrono::steady_clock::now();
        for (int pos = 0; pos < total; pos += block)
        {
            buf.clear();
            synth.renderNextBlock (buf, pos == 0 ? notes : empty, 0, block);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = buf.getReadPointer (ch);
                for (int i = 0; i < block; ++i)
                {
                    finite = finite && std::isfinite (d[i]);
                    peak = juce::jmax (peak, std::abs (d[i]));
                }
            }
        }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        logMessage ("Rendered 15 voices (30 strings) x 10 s in " + juce::String (sec, 3) + " s -> real-time factor "
                    + juce::String (10.0 / sec, 1) + "x, CPU load " + juce::String (100.0 * sec / 10.0, 1)
                    + " % of one core, peak " + juce::String (peak, 3));
        expect (finite, "benchmark output must be finite");
        expect (peak > 0.001f, "benchmark must actually produce sound");
        expect (peak < 4.0f, "benchmark output unreasonably loud");
        expect (sec < 10.0, "slower than real time");
    }
};
static DspTests dspTests;
