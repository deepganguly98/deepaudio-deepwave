// WaveGrid for Android: standalone JUCEApplication hosting WaveGridProcessor + WaveGridEditor.
// Built only through cmake/WaveGridAndroid.cmake (target WaveGridAndroid). The audio path is
//   GridSurface / USB-BT MIDI -> AudioProcessorPlayer -> WaveGridProcessor::processBlock -> Oboe.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../PluginEditor.h"

namespace
{
    // The editor lays itself out for at least this many logical pixels; on smaller screens
    // (phones) we give it this much room and scale the whole thing down with a transform.
    constexpr int kMinEditorW = 760;
    constexpr int kMinEditorH = 560;
}

//==============================================================================
// Full-screen root: owns the editor, fits it to the screen, copes with rotation / resizing.
class RootComponent : public juce::Component
{
public:
    explicit RootComponent (juce::AudioProcessorEditor* e) : editor (e)
    {
        setOpaque (true);
        editor->setResizable (false, false);   // no resize corner on a touch screen
        addAndMakeVisible (*editor);
    }

    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }

    void resized() override
    {
        auto area = getLocalBounds();

        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds()))
            area = d->safeAreaInsets.subtractedFrom (area);   // notches / gesture bars

        if (area.isEmpty())
            return;

        const auto w = (float) area.getWidth();
        const auto h = (float) area.getHeight();
        const auto scale = juce::jlimit (0.3f, 1.6f, juce::jmin (w / (float) kMinEditorW,
                                                                  h / (float) kMinEditorH));

        // Editor works in its own (virtual) pixels; the transform maps them onto the screen.
        editor->setTransform (juce::AffineTransform::scale (scale)
                                  .translated ((float) area.getX(), (float) area.getY()));
        editor->setBounds (0, 0, juce::roundToInt (w / scale), juce::roundToInt (h / scale));
    }

private:
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RootComponent)
};

//==============================================================================
class WaveGridAndroidApp final : public juce::JUCEApplication,
                                 private juce::Timer
{
public:
    const juce::String getApplicationName() override    { return "WaveGrid"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::Desktop::getInstance().setScreenSaverEnabled (false);   // keep the screen on while playing

        processor = std::make_unique<WaveGridProcessor>();
        player.setProcessor (processor.get());

        // Output only (0 in, 2 out): no RECORD_AUDIO permission needed. Oboe picks a low-latency
        // stream; we leave the buffer size at the device default (its optimal burst multiple).
        auto err = deviceManager.initialise (0, 2, nullptr, true, {}, nullptr);
        if (err.isNotEmpty())
            juce::Logger::writeToLog ("WaveGrid audio init: " + err);

        deviceManager.addAudioCallback (&player);
        deviceManager.addMidiInputDeviceCallback ({}, &player);   // {} = all enabled MIDI inputs

        // USB MIDI needs no permission; Bluetooth MIDI does (BLUETOOTH_SCAN / location on older Androids).
        juce::RuntimePermissions::request (juce::RuntimePermissions::bluetoothMidi,
                                           [this] (bool) { refreshMidiInputs(); });
        refreshMidiInputs();
        startTimer (3000);   // pick up devices plugged in / paired later

        root = std::make_unique<RootComponent> (processor->createEditor());
        root->addToDesktop (0);   // 0 = no title bar / borders: a plain full-screen window
        root->setVisible (true);
        juce::Desktop::getInstance().setKioskModeComponent (root.get(), false);
        root->setBounds (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->totalArea);
    }

    void shutdown() override
    {
        stopTimer();
        juce::Desktop::getInstance().setKioskModeComponent (nullptr);
        root.reset();   // editor must die before the processor
        deviceManager.removeMidiInputDeviceCallback ({}, &player);
        deviceManager.removeAudioCallback (&player);
        player.setProcessor (nullptr);
        deviceManager.closeAudioDevice();
        processor.reset();
    }

    void systemRequestedQuit() override { quit(); }

    // App went to background: release the audio device (no sound, no battery drain), reopen on return.
    void suspended() override { deviceManager.closeAudioDevice(); }
    void resumed() override   { deviceManager.restartLastAudioDevice(); refreshMidiInputs(); }

private:
    void timerCallback() override { refreshMidiInputs(); }

    void refreshMidiInputs()
    {
        for (auto& dev : juce::MidiInput::getAvailableDevices())
            if (! deviceManager.isMidiInputDeviceEnabled (dev.identifier))
                deviceManager.setMidiInputDeviceEnabled (dev.identifier, true);
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    std::unique_ptr<WaveGridProcessor> processor;
    std::unique_ptr<RootComponent> root;
};

START_JUCE_APPLICATION (WaveGridAndroidApp)
