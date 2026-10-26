// Renders the real DeepWave editor offscreen to PNG files for the README (no screen-recording permission needed).
// Usage: WaveGrid_Screenshot <output-dir>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
void pump (int ms)   // let async parameter/layout updates and the grid's 60 Hz timer run
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}

void save (juce::Component& c, const juce::File& file, float scale)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (out.openedOk()) juce::PNGImageFormat().writeImageToStream (img, out);
    juce::Logger::writeToLog ("wrote " + file.getFullPathName() + "  " + juce::String (img.getWidth()) + "x" + juce::String (img.getHeight()));
}

// a synthetic finger: press, then slide so the string bends and a trail appears
void finger (GridSurface& grid, juce::Point<float> from, juce::Point<float> to)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    auto ev = [&] (juce::Point<float> p, bool dragged)
    {
        return juce::MouseEvent (src, p, juce::ModifierKeys::leftButtonModifier, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &grid, &grid, juce::Time::getCurrentTime(), from, juce::Time::getCurrentTime(), 1, dragged);
    };
    grid.mouseDown (ev (from, false));
    for (int i = 1; i <= 14; ++i)
    {
        grid.mouseDrag (ev (from + (to - from) * ((float) i / 14.0f), true));
        pump (12);
    }
}
}

class ScreenshotApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "DeepWave Screenshot"; }
    const juce::String getApplicationVersion() override { return "1.0"; }

    void initialise (const juce::String& cmd) override
    {
        const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (cmd.unquoted().trim().isEmpty() ? "." : cmd.unquoted().trim());
        outDir.createDirectory();

        WaveGridProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        auto shot = [&] (const juce::String& name, int voice, int w, int h, const std::function<void (WaveGridEditor&)>& play)
        {
            // same as picking it in the UI: select the preset's name, then apply its values
            if (auto* vp = proc.apvts.getParameter (ParamID::voice)) vp->setValueNotifyingHost (vp->convertTo0to1 ((float) voice));
            proc.applyVoicePreset (voice);
            WaveGridEditor ed (proc);
            ed.setSize (w, h);
            pump (350);
            play (ed);
            pump (80);
            save (ed, outDir.getChildFile (name), 2.0f);
        };

        // 1. hero: Glass Continuum, a chord held on the keyboard plus one finger sliding on the grid
        shot ("deepwave-main.png", 0, 1280, 860, [] (WaveGridEditor& ed)
        {
            auto& g = ed.getGrid();
            for (int n : { 52, 59, 64 }) g.keyNote (n, true);
            const auto b = g.getLocalBounds().toFloat();
            finger (g, { b.getWidth() * 0.22f, b.getHeight() * 0.40f }, { b.getWidth() * 0.46f, b.getHeight() * 0.36f });
        });

        // 2. arpeggiator preset: Crystal Arp (arp on, pink ARP badge)
        shot ("deepwave-arp.png", 30, 1280, 860, [] (WaveGridEditor& ed)
        {
            auto& g = ed.getGrid();
            for (int n : { 48, 55, 64 }) g.keyNote (n, true);
            const auto b = g.getLocalBounds().toFloat();
            finger (g, { b.getWidth() * 0.62f, b.getHeight() * 0.72f }, { b.getWidth() * 0.80f, b.getHeight() * 0.68f });
        });

        // 3. a darker, wind-style voice at a tablet-ish size (touch layout)
        shot ("deepwave-tablet.png", 3, 1024, 768, [] (WaveGridEditor& ed)
        {
            auto& g = ed.getGrid();
            const auto b = g.getLocalBounds().toFloat();
            finger (g, { b.getWidth() * 0.30f, b.getHeight() * 0.58f }, { b.getWidth() * 0.55f, b.getHeight() * 0.54f });
        });

        quit();
    }

    void shutdown() override {}
};

START_JUCE_APPLICATION (ScreenshotApp)
