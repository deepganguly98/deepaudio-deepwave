#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "WaveguideString.h"
#include "BodyResonator.h"
#include "SympatheticBank.h"
#include <atomic>

// Shared, lock-free-read parameter block (written by the processor from APVTS).
struct StringParams
{
    std::atomic<float> brightness   { 0.6f };
    std::atomic<float> sustain      { 0.7f };
    std::atomic<float> inharmonic   { 0.1f };
    std::atomic<float> pickPos      { 0.13f };
    std::atomic<float> drive        { 0.0f };
    std::atomic<float> glideMs      { 8.0f };
    std::atomic<float> pressureToBrightness { 0.5f };  // how much MPE pressure opens tone
    std::atomic<float> pressureToDrive      { 0.3f };
    std::atomic<float> bow          { 0.6f };   // sustain energy fed back while playing
    std::atomic<float> detune       { 0.35f };  // twin-string spread / stereo width
    std::atomic<float> attack       { 20.0f };  // ms swell of each note
    std::atomic<float> hold         { 0.8f };   // 1 = a held finger sustains forever, 0 = only motion does
    std::atomic<float> breath       { 0.0f };   // continuous noise excitation (flute / wind)
    std::atomic<float> strike       { 0.3f };   // strength of the initial pluck
    std::atomic<float> body         { 0.3f };   // acoustic body resonance amount 0..1
    std::atomic<float> sympathy     { 0.2f };   // sympathetic string resonance amount 0..1
};

// One MPE note = one touch = one string (plus a detuned twin).
class StringVoice : public juce::MPESynthesiserVoice
{
public:
    explicit StringVoice (StringParams& p) : params (p) {}

    void setCurrentSampleRate (double sr) override
    {
        MPESynthesiserVoice::setCurrentSampleRate (sr);
        string.prepare (sr);
        twin.prepare (sr);
        bodyRes.prepare (sr);
        sympBank.prepare (sr);
        motionDecay = std::exp (-1.0f / (0.30f * (float) sr));
        pressureSmooth = 1.0f - std::exp (-1.0f / (0.015f * (float) sr));
    }

    void noteStarted() override
    {
        const auto note = getCurrentlyPlayingNote();
        hz = (float) note.getFrequencyInHertz();
        pressure = pressureS = note.pressure.asUnsignedFloat();
        timbre   = note.timbre.asUnsignedFloat();
        releasing = false; motion = 0.0f;

        const float attMs = params.attack.load();
        attackCoef = attMs <= 1.5f ? 1.0f : 1.0f - std::exp (-1.0f / (attMs * 0.001f * (float) getSampleRate()));
        env = attackCoef >= 1.0f ? 1.0f : 0.0f;

        applyStaticParams();
        string.reset();  twin.reset();
        bodyRes.reset(); sympBank.reset();
        setFrequencies (true);              // jump straight to pitch on a fresh note

        // soft finger-strike: a floor keeps bowed voices able to start singing
        const float k = 0.10f + 0.90f * params.strike.load();
        const float v = note.noteOnVelocity.asUnsignedFloat() * k;
        string.pluck (v);
        twin.pluck (v * 0.9f);
    }

    void noteStopped (bool allowTailOff) override
    {
        releasing = true;
        if (! allowTailOff) { clearCurrentNote(); string.reset(); twin.reset(); }
    }

    void notePressureChanged() override
    {
        const float p = getCurrentlyPlayingNote().pressure.asUnsignedFloat();
        motion = juce::jmin (1.0f, motion + std::abs (p - pressure) * 1.5f);
        pressure = p;
        applyExpression();
    }
    void notePitchbendChanged() override
    {
        const float newHz = (float) getCurrentlyPlayingNote().getFrequencyInHertz();
        // finger speed (semitones moved) is what "bows" the string
        motion = juce::jmin (1.0f, motion + 12.0f * std::abs (std::log2 (newHz / hz)) * 0.5f);
        hz = newHz;
        setFrequencies (false);
    }
    void noteTimbreChanged() override
    {
        const float t = getCurrentlyPlayingNote().timbre.asUnsignedFloat();
        motion = juce::jmin (1.0f, motion + std::abs (t - timbre) * 1.5f);
        timbre = t;
        applyExpression();
    }
    void noteKeyStateChanged() override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isActive()) return;
        juce::ScopedNoDenormals noDenormals;
        applyStaticParams();
        const float bodyAmt = juce::jlimit (0.0f, 1.0f, params.body.load());
        const float sympAmt = juce::jlimit (0.0f, 1.0f, params.sympathy.load());
        sympBank.setNote (hz);
        if (bodyAmt <= 0.001f) bodyRes.reset();
        if (sympAmt <= 0.001f) sympBank.reset();

        // release: lift the finger — damp the string, stop feeding it
        if (releasing) { string.setSustain (0.0f); twin.setSustain (0.0f); motion = 0.0f; }

        const float bowAmt  = params.bow.load() * 0.03f;
        const float holdAmt = releasing ? 0.0f : params.hold.load();
        const float breathAmt = releasing ? 0.0f : params.breath.load();
        const int nch = out.getNumChannels();

        for (int i = 0; i < num; ++i)
        {
            motion *= motionDecay;
            pressureS += (pressure - pressureS) * pressureSmooth;
            env += (1.0f - env) * attackCoef;

            // held finger sustains (hold), moving finger adds more (motion)
            const float drive = holdAmt + (1.0f - holdAmt) * motion;
            const float lvl = releasing ? 0.0f : bowAmt * (0.3f + 0.7f * pressureS) * drive;
            const float br  = breathAmt * (0.2f + 0.8f * pressureS);
            string.setBow (lvl);  twin.setBow (lvl);
            string.setContinuousDrive (br);  twin.setContinuousDrive (br);

            const float g = 0.45f * env * (0.45f + 0.55f * pressureS);
            const float a = string.processSample() * g;
            const float b = twin.processSample()   * g;
            const float mono = 0.5f * (a + b);

            // body + sympathetic resonance act on the voice's mono sum, are added
            // equally to both channels, then a soft limiter bounds the voice level
            float extra = 0.0f;
            if (bodyAmt > 0.001f) extra += bodyRes.process (mono, bodyAmt);
            if (sympAmt > 0.001f) extra += sympBank.process (mono, sympAmt);
            float L = 0.65f * a + 0.35f * b + extra;
            float R = 0.35f * a + 0.65f * b + extra;
            if (! (std::abs (L) < 4.0f && std::abs (R) < 4.0f)) { L = R = 0.0f; bodyRes.reset(); sympBank.reset(); }
            L = std::tanh (L);  R = std::tanh (R);
            if (nch >= 2)
            {
                out.addSample (0, start + i, L);
                out.addSample (1, start + i, R);
                for (int ch = 2; ch < nch; ++ch) out.addSample (ch, start + i, 0.5f * (L + R));
            }
            else out.addSample (0, start + i, 0.5f * (L + R));
        }

        if (releasing && string.isSilent() && twin.isSilent())
        {
            clearCurrentNote();
            string.reset(); twin.reset();
            bodyRes.reset(); sympBank.reset();
        }
    }

private:
    void applyStaticParams()
    {
        for (auto* s : { &string, &twin })
        {
            s->setInharmonicity (params.inharmonic.load());
            s->setPickPosition (params.pickPos.load());
            s->setGlideSeconds (params.glideMs.load() * 0.001f);
            if (! releasing) s->setSustain (params.sustain.load());
        }
        setFrequencies (false);   // re-aim: loop filter changes shift the compensation
        applyExpression();
    }

    // twin string sits a few cents sharp of the main one
    void setFrequencies (bool immediate)
    {
        const float twinHz = hz * std::exp2 (params.detune.load() * 14.0f / 1200.0f);
        if (immediate) { string.setFrequencyImmediate (hz); twin.setFrequencyImmediate (twinHz); }
        else           { string.setFrequency (hz);          twin.setFrequency (twinHz); }
    }

    void applyExpression()
    {
        const float b = juce::jlimit (0.0f, 1.0f,
            params.brightness.load() + (pressure - 0.5f) * params.pressureToBrightness.load()
                                     + (timbre   - 0.5f) * 0.3f);
        const float d = juce::jlimit (0.0f, 1.0f, params.drive.load() + pressure * params.pressureToDrive.load());
        for (auto* s : { &string, &twin }) { s->setBrightness (b); s->setDrive (d); }
    }

    StringParams& params;
    WaveguideString string, twin;
    BodyResonator bodyRes;
    SympatheticBank sympBank;
    float hz = 440.0f, motion = 0.0f, motionDecay = 0.999f;
    float pressure = 0.5f, pressureS = 0.5f, pressureSmooth = 0.01f, timbre = 0.5f;
    float env = 1.0f, attackCoef = 1.0f;
    bool releasing = false;
};
