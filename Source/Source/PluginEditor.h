#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class SunoLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SunoLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

// EQ curves, or (WIDTH tab) the output width per band next to the Suno target
class CurveGraph : public juce::Component
{
public:
    void setData (const SunoChainProcessor::CurveSnapshot& s) { snap = s; repaint(); }
    void setWidth (const std::array<float, 6>& w) { widthBands = w; if (mode == 1) repaint(); }
    void setMode (int m) { mode = m; repaint(); }
    int getMode() const { return mode; }
    void paint (juce::Graphics&) override;
private:
    void paintEq (juce::Graphics&);
    void paintWidth (juce::Graphics&);
    SunoChainProcessor::CurveSnapshot snap;
    std::array<float, 6> widthBands { -100, -100, -100, -100, -100, -100 };
    int mode = 0;
};

class MeterPanel : public juce::Component
{
public:
    void update (const SunoChainProcessor::Meters& m);
    void paint (juce::Graphics&) override;
private:
    float in = -100, out = -100, gr1 = 0, gr2 = 0, duck = 0, width = -100, ds = 0;
    uint32_t lastBlocks = 0; int idleTicks = 0;
};

class SunoChainEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SunoChainEditor (SunoChainProcessor&);
    ~SunoChainEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void loadPreset();
    void savePreset();
    void toggleLearn();

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Toggle
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    Knob& addKnob (const juce::String& id, const juce::String& name, bool big = false);
    Toggle& addToggle (const juce::String& id, const juce::String& name);
    void place (const juce::String& id, int x, int y, int size);

    SunoChainProcessor& proc;
    SunoLookAndFeel lnf;
    CurveGraph graph;
    MeterPanel meters;
    juce::TextButton loadButton { "Load" }, saveButton { "Save" }, learnButton { "Learn My Voice" }, clearButton { "Clear" };
    juce::TextButton eqTab { "EQ" }, widthTab { "WIDTH" }, advancedButton { "ADVANCED" };
    void setAdvanced (bool open);
    juce::Label presetLabel, statusLabel, noteLabel, delayInfo;
    juce::ComboBox noteBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> noteAtt;
    std::map<juce::String, std::unique_ptr<Knob>> knobs;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String statusText;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunoChainEditor)
};
