#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "dsp/StringVoice.h"
#include "fx/Effects.h"
#include "fx/Arpeggiator.h"
#include "Presets.h"
#include "ui/Scales.h"

namespace ParamID
{
    inline constexpr auto brightness = "brightness";
    inline constexpr auto sustain    = "sustain";
    inline constexpr auto inharmonic = "inharmonic";
    inline constexpr auto pickPos    = "pickPos";
    inline constexpr auto drive      = "drive";
    inline constexpr auto glide      = "glide";
    inline constexpr auto pressBright= "pressBright";
    inline constexpr auto pressDrive = "pressDrive";
    inline constexpr auto snap       = "snap";
    inline constexpr auto rows       = "rows";
    inline constexpr auto baseNote   = "baseNote";
    inline constexpr auto rowInterval= "rowInterval";
    inline constexpr auto output     = "output";
    inline constexpr auto bow        = "bow";
    inline constexpr auto detune     = "detune";
    inline constexpr auto space      = "space";
    inline constexpr auto attack     = "attack";
    inline constexpr auto hold       = "hold";
    inline constexpr auto breath     = "breath";
    inline constexpr auto strike     = "strike";
    inline constexpr auto voice      = "voice";
    inline constexpr auto body       = "body";
    inline constexpr auto sympathy   = "sympathy";
    inline constexpr auto shimmer    = "shimmer";
    inline constexpr auto delayMix   = "delayMix";
    inline constexpr auto delayTime  = "delayTime";
    inline constexpr auto vibrato    = "vibrato";   // UI only
    inline constexpr auto scale      = "scale";
    inline constexpr auto root       = "root";
    inline constexpr auto hammer     = "hammer";
    inline constexpr auto arpOn      = "arpOn";
    inline constexpr auto arpPattern = "arpPattern";
    inline constexpr auto arpRate    = "arpRate";
    inline constexpr auto arpOct     = "arpOct";
    inline constexpr auto arpGate    = "arpGate";
    inline constexpr auto arpSwing   = "arpSwing";
    inline constexpr auto arpLatch   = "arpLatch";
    inline constexpr auto arpBpm     = "arpBpm";      // tempo when the host gives none (Standalone)
    inline constexpr auto eqLow      = "eqLow";
    inline constexpr auto eqHigh     = "eqHigh";
    inline constexpr auto comp       = "comp";
    inline constexpr auto ensemble   = "ensemble";
    inline constexpr auto warmth     = "warmth";
}

inline constexpr int kBendRangeSemis = 48;
inline constexpr int kNumVoices = 15;   // one per MPE member channel


class WaveGridProcessor : public juce::AudioProcessor
{
public:
    WaveGridProcessor();
    ~WaveGridProcessor() override = default;

    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }   // MPE out from the grid
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiMessageCollector& getGridCollector() { return gridMidi; }
    void applyVoicePreset (int index);   // message thread

    // User presets (.wgpreset XML in <userAppData>/WaveGrid/Presets). Message thread only.
    juce::StringArray getUserPresetNames();
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::String& name);
    bool deleteUserPreset (const juce::String& name);

    // Metering for the UI (written on the audio thread)
    std::atomic<float> outputPeak { 0.0f };       // linear, post-limiter, ~300 ms decay
    std::atomic<int>   activeVoiceCount { 0 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pushParams();
    float currentTrimDb() const;
    static juce::File getPresetDir();
    static juce::String sanitisePresetName (const juce::String&);
    juce::File presetFile (const juce::String& name) const;

    StringParams params;
    juce::MPEInstrument   mpe;
    juce::MPESynthesiser  synth { mpe };
    juce::MidiMessageCollector gridMidi;   // UI → audio thread
    wgfx::SpaceFx space;
    wgfx::PeakLimiter limiter;
    wgfx::Arpeggiator arp;
    wgfx::MasterChain master;

    struct Cache
    {
        std::atomic<float> *brightness, *sustain, *inharmonic, *pickPos, *drive, *glide, *pressBright,
                           *pressDrive, *bow, *detune, *attack, *hold, *breath, *strike, *space, *output,
                           *voice, *body, *sympathy, *shimmer, *delayMix, *delayTime,
                           *arpOn, *arpPattern, *arpRate, *arpOct, *arpGate, *arpSwing, *arpLatch, *arpBpm,
                           *eqLow, *eqHigh, *comp, *ensemble, *warmth;
    } cache {};
    float peakState = 0.0f;
    double currentSampleRate = 44100.0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveGridProcessor)
};
