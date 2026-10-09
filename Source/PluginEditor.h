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
};

class CurveGraph : public juce::Component
{
public:
    void setData (const SunoChainProcessor::CurveSnapshot& s) { snap = s; repaint(); }
    void paint (juce::Graphics&) override;
private:
    SunoChainProcessor::CurveSnapshot snap;
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

    SunoChainProcessor& proc;
    SunoLookAndFeel lnf;
    CurveGraph graph;
    juce::TextButton loadButton { "Load Preset" }, learnButton { "Learn My Voice" }, clearButton { "Clear" };
    juce::Label presetLabel, statusLabel;
    std::map<juce::String, std::unique_ptr<Knob>> knobs;
    std::map<juce::String, std::unique_ptr<Toggle>> toggles;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastDir;
    juce::String statusText;
    int graphTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunoChainEditor)
};
