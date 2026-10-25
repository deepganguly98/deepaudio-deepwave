#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

WaveGridProcessor::WaveGridProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    // MPE lower zone: master ch 1, members 2–16, 48-semitone per-note bend.
    juce::MPEZoneLayout layout;
    layout.setLowerZone (15, kBendRangeSemis, 2);
    mpe.setZoneLayout (layout);
    synth.setZoneLayout (layout);

    for (int i = 0; i < kNumVoices; ++i)
        synth.addVoice (new StringVoice (params));
    synth.setVoiceStealingEnabled (true);

    auto r = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    cache = { r (ParamID::brightness), r (ParamID::sustain), r (ParamID::inharmonic), r (ParamID::pickPos),
              r (ParamID::drive), r (ParamID::glide), r (ParamID::pressBright), r (ParamID::pressDrive),
              r (ParamID::bow), r (ParamID::detune), r (ParamID::attack), r (ParamID::hold),
              r (ParamID::breath), r (ParamID::strike), r (ParamID::space), r (ParamID::output),
              r (ParamID::voice), r (ParamID::body), r (ParamID::sympathy), r (ParamID::shimmer),
              r (ParamID::delayMix), r (ParamID::delayTime),
              r (ParamID::arpOn), r (ParamID::arpPattern), r (ParamID::arpRate), r (ParamID::arpOct),
              r (ParamID::arpGate), r (ParamID::arpSwing), r (ParamID::arpLatch), r (ParamID::arpBpm),
              r (ParamID::eqLow), r (ParamID::eqHigh), r (ParamID::comp), r (ParamID::ensemble),
              r (ParamID::warmth) };
}

APVTS::ParameterLayout WaveGridProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using I = juce::AudioParameterInt;
    auto u = juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f);
    APVTS::ParameterLayout l;
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::brightness, 1 }, "Brightness", u, kVoicePresets[0].bright));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::sustain, 1 },    "Sustain",    u, kVoicePresets[0].sustain));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::inharmonic, 1 }, "Stiffness",  u, kVoicePresets[0].stiff));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::pickPos, 1 },    "Pick Pos",   juce::NormalisableRange<float> (0.02f, 0.5f, 0.001f), kVoicePresets[0].pick));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::drive, 1 },      "Drive",      u, kVoicePresets[0].drive));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::glide, 1 },      "Glide (ms)", juce::NormalisableRange<float> (0.0f, 200.0f, 0.1f, 0.5f), kVoicePresets[0].glide));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::pressBright, 1 },"Press→Bright", u, 0.5f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::pressDrive, 1 }, "Press→Drive",  u, 0.3f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::snap, 1 },       "Fret Snap",  juce::NormalisableRange<float> (0.0f, 0.49f, 0.01f), 0.04f));
    l.add (std::make_unique<I> (juce::ParameterID { ParamID::rows, 1 },       "Rows", 3, 8, 6));
    l.add (std::make_unique<I> (juce::ParameterID { ParamID::baseNote, 1 },   "Base Note", 24, 72, 40));
    l.add (std::make_unique<I> (juce::ParameterID { ParamID::rowInterval, 1 },"Row Interval", 1, 7, 5));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::output, 1 },     "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), -6.0f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::bow, 1 },    "Bow",    u, kVoicePresets[0].bow));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::detune, 1 }, "Width",  u, kVoicePresets[0].width));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::space, 1 },  "Space",  u, kVoicePresets[0].space));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::attack, 1 }, "Attack (ms)", juce::NormalisableRange<float> (1.0f, 1000.0f, 0.1f, 0.4f), kVoicePresets[0].attack));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::hold, 1 },   "Hold",   u, kVoicePresets[0].hold));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::breath, 1 }, "Breath", u, kVoicePresets[0].breath));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::strike, 1 }, "Strike", u, kVoicePresets[0].strike));
    juce::StringArray names;
    for (auto& p : kVoicePresets) names.add (p.name);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::voice, 1 }, "Voice", names, 0));

    l.add (std::make_unique<F> (juce::ParameterID { ParamID::body, 1 },     "Body",     u, kVoicePresets[0].body));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::sympathy, 1 }, "Sympathy", u, kVoicePresets[0].sympathy));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::shimmer, 1 },  "Shimmer",  u, kVoicePresets[0].shimmer));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::delayMix, 1 }, "Delay Mix", u, kVoicePresets[0].delayMix));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::vibrato, 1 },  "Vibrato",  u, 0.0f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::delayTime, 1 }, "Delay Time (ms)",
                                juce::NormalisableRange<float> (60.0f, 1000.0f, 1.0f, 0.5f), 330.0f));
    juce::StringArray scaleNames;
    for (auto& sc : kScales) scaleNames.add (sc.name);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::scale, 1 }, "Scale", scaleNames, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::root, 1 }, "Root",
           juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamID::hammer, 1 }, "Hammer-On", true));

    // arpeggiator
    juce::StringArray patNames, rateNames;
    for (int i = 0; i < wgfx::kNumArpPatterns; ++i) patNames.add (wgfx::kArpPatternNames[i]);
    for (int i = 0; i < wgfx::kNumArpRates; ++i)    rateNames.add (wgfx::kArpRateNames[i]);
    l.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { ParamID::arpOn, 1 },      "Arp On", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::arpPattern, 1 }, "Arp Pattern", patNames, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ParamID::arpRate, 1 },    "Arp Rate", rateNames, 3));
    l.add (std::make_unique<I> (juce::ParameterID { ParamID::arpOct, 1 }, "Arp Octaves", 1, 4, 1));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::arpGate, 1 },  "Arp Gate",  juce::NormalisableRange<float> (0.05f, 1.0f, 0.01f), 0.6f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::arpSwing, 1 }, "Arp Swing", juce::NormalisableRange<float> (0.0f, 0.6f, 0.01f), 0.0f));
    l.add (std::make_unique<juce::AudioParameterBool>   (juce::ParameterID { ParamID::arpLatch, 1 },   "Arp Latch", false));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::arpBpm, 1 },   "Arp Tempo (no host)", juce::NormalisableRange<float> (40.0f, 240.0f, 0.1f), 110.0f));

    // master polish
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::eqLow, 1 },  "EQ Low (dB)",  juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::eqHigh, 1 }, "EQ High (dB)", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::comp, 1 },     "Glue Comp", u, 0.0f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::ensemble, 1 }, "Ensemble",  u, 0.0f));
    l.add (std::make_unique<F> (juce::ParameterID { ParamID::warmth, 1 },   "Warmth",    u, 0.0f));
    return l;
}

void WaveGridProcessor::applyVoicePreset (int i)
{
    if (i < 0 || i >= kNumVoicePresets) return;
    const auto& p = kVoicePresets[i];
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    };
    set (ParamID::brightness, p.bright);  set (ParamID::sustain, p.sustain);
    set (ParamID::inharmonic, p.stiff);   set (ParamID::pickPos, p.pick);
    set (ParamID::drive, p.drive);        set (ParamID::glide, p.glide);
    set (ParamID::bow, p.bow);            set (ParamID::hold, p.hold);
    set (ParamID::attack, p.attack);      set (ParamID::strike, p.strike);
    set (ParamID::breath, p.breath);      set (ParamID::detune, p.width);
    set (ParamID::space, p.space);
    set (ParamID::body, p.body);          set (ParamID::sympathy, p.sympathy);
    set (ParamID::shimmer, p.shimmer);    set (ParamID::delayMix, p.delayMix);
    set (ParamID::delayTime, p.delayTime);
    set (ParamID::eqLow, p.eqLowDb);      set (ParamID::eqHigh, p.eqHighDb);
    set (ParamID::comp, p.comp);          set (ParamID::ensemble, p.ensemble);
    set (ParamID::warmth, p.warmth);
    set (ParamID::arpOn, (float) p.arpOn);          set (ParamID::arpPattern, (float) p.arpPattern);
    set (ParamID::arpRate, (float) p.arpRate);      set (ParamID::arpOct, (float) p.arpOct);
    set (ParamID::arpGate, p.arpGate);
}

void WaveGridProcessor::prepareToPlay (double sr, int block)
{
    currentSampleRate = sr;
    synth.setCurrentPlaybackSampleRate (sr);
    gridMidi.reset (sr);
    space.prepare (sr);
    limiter.prepare (sr);
    arp.prepare (sr);
    master.prepare (sr, block);
    outGain.reset (sr, 0.02);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (cache.output->load() + currentTrimDb()));
    peakState = 0.0f;
    outputPeak.store (0.0f);
    activeVoiceCount.store (0);
    juce::ignoreUnused (block);
}

void WaveGridProcessor::reset()
{
    space.reset();
    limiter.reset();
    arp.reset();
    master.reset();
    peakState = 0.0f;
}

float WaveGridProcessor::currentTrimDb() const
{
    const int v = (int) (cache.voice->load() + 0.5f);
    return (v >= 0 && v < kNumVoicePresets) ? kVoicePresets[v].trimDb : 0.0f;
}

bool WaveGridProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void WaveGridProcessor::pushParams()
{
    params.brightness.store (cache.brightness->load());
    params.sustain.store    (cache.sustain->load());
    params.inharmonic.store (cache.inharmonic->load());
    params.pickPos.store    (cache.pickPos->load());
    params.drive.store      (cache.drive->load());
    params.glideMs.store    (cache.glide->load());
    params.pressureToBrightness.store (cache.pressBright->load());
    params.pressureToDrive.store      (cache.pressDrive->load());
    params.bow.store    (cache.bow->load());
    params.detune.store (cache.detune->load());
    params.attack.store (cache.attack->load());
    params.hold.store   (cache.hold->load());
    params.breath.store (cache.breath->load());
    params.strike.store (cache.strike->load());
    params.body.store     (cache.body->load());
    params.sympathy.store (cache.sympathy->load());
}

void WaveGridProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    buffer.clear();
    pushParams();
    const int n = buffer.getNumSamples();

    // Merge grid-generated MPE into the incoming MIDI stream. The merged
    // stream both drives the synth and leaves the plugin as MIDI out, so the
    // grid works as a standalone MPE controller for other instruments.
    gridMidi.removeNextBlockOfMessages (midi, n);

    // arpeggiator rewrites the merged MIDI (grid + keyboard + external MPE) before the synth sees it;
    // it follows the host tempo/transport when there is one, else the "Arp Tempo" knob.
    {
        wgfx::Arpeggiator::Settings as;
        as.enabled = cache.arpOn->load() > 0.5f;
        as.pattern = juce::jlimit (0, wgfx::kNumArpPatterns - 1, (int) (cache.arpPattern->load() + 0.5f));
        as.rate    = juce::jlimit (0, wgfx::kNumArpRates - 1, (int) (cache.arpRate->load() + 0.5f));
        as.octaves = juce::jlimit (1, 4, (int) (cache.arpOct->load() + 0.5f));
        as.gate    = cache.arpGate->load();
        as.swing   = cache.arpSwing->load();
        as.latch   = cache.arpLatch->load() > 0.5f;

        double bpm = (double) cache.arpBpm->load();
        std::optional<double> ppq;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (pos->getBpm().hasValue()) bpm = *pos->getBpm();
                if (pos->getIsPlaying() && pos->getPpqPosition().hasValue()) ppq = *pos->getPpqPosition();
            }
        arp.process (midi, n, as, bpm, ppq);
    }

    synth.renderNextBlock (buffer, midi, 0, n);

    int active = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = synth.getVoice (i))
            if (v->isActive()) ++active;
    activeVoiceCount.store (active);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() >= 2 ? buffer.getWritePointer (1) : nullptr;

    // delay -> shimmer -> big, dark hall: the "space" around the strings
    space.process (L, R, n, cache.space->load(), cache.shimmer->load(),
                   cache.delayMix->load(), cache.delayTime->load());

    // master polish: warmth -> EQ -> ensemble -> glue comp
    wgfx::MasterParams mp;
    mp.eqLowDb  = cache.eqLow->load();
    mp.eqHighDb = cache.eqHigh->load();
    mp.comp     = cache.comp->load();
    mp.ensemble = cache.ensemble->load();
    mp.warmth   = cache.warmth->load();
    master.setParams (mp);
    master.process (buffer);

    // output level + per-preset loudness trim (smoothed together)
    outGain.setTargetValue (juce::Decibels::decibelsToGain (cache.output->load() + currentTrimDb()));
    const float g0 = outGain.getCurrentValue();
    outGain.skip (n);
    buffer.applyGainRamp (0, n, g0, outGain.getCurrentValue());

    limiter.process (L, R, n);

    float blockPeak = 0.0f;
    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
        blockPeak = juce::jmax (blockPeak, buffer.getMagnitude (ch, 0, n));
    peakState = juce::jmax (blockPeak, peakState * std::exp (-(float) n / (0.3f * (float) currentSampleRate)));
    outputPeak.store (peakState);
}

//==============================================================================
juce::File WaveGridProcessor::getPresetDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("WaveGrid").getChildFile ("Presets");
}

juce::String WaveGridProcessor::sanitisePresetName (const juce::String& raw)
{
    juce::String out;
    for (auto c : raw.trim())
        if (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')')
            out << juce::String::charToString (c);
    return out.trim().substring (0, 64);
}

juce::File WaveGridProcessor::presetFile (const juce::String& name) const
{
    return getPresetDir().getChildFile (sanitisePresetName (name) + ".wgpreset");
}

juce::StringArray WaveGridProcessor::getUserPresetNames()
{
    juce::StringArray names;
    for (auto& f : getPresetDir().findChildFiles (juce::File::findFiles, false, "*.wgpreset"))
        names.add (f.getFileNameWithoutExtension());
    names.sortNatural();
    return names;
}

bool WaveGridProcessor::saveUserPreset (const juce::String& name)
{
    if (sanitisePresetName (name).isEmpty()) return false;
    auto dir = getPresetDir();
    if (! dir.createDirectory().wasOk()) return false;
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return false;
    return xml->writeTo (presetFile (name));
}

bool WaveGridProcessor::loadUserPreset (const juce::String& name)
{
    if (sanitisePresetName (name).isEmpty()) return false;
    auto f = presetFile (name);
    if (! f.existsAsFile()) return false;
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    return true;
}

bool WaveGridProcessor::deleteUserPreset (const juce::String& name)
{
    if (sanitisePresetName (name).isEmpty()) return false;
    auto f = presetFile (name);
    return f.existsAsFile() && f.deleteFile();
}

void WaveGridProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}
void WaveGridProcessor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* WaveGridProcessor::createEditor() { return new WaveGridEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WaveGridProcessor(); }
