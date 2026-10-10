#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Suno Chain v1.4 GUI: image faceplate + knobs (made in ChatGPT, see design/), everything that moves or
// carries text drawn in code. All coordinates are logical (1200 x 800 main view, +366 for the Advanced
// drawer); the whole UI is scaled as one piece, so it stays sharp and proportional at any size.
namespace ui
{
constexpr int kW = 1200, kH = 800, kSideW = 480;   // Advanced = side module on the right (v1.5)

struct Images
{
    juce::Image faceplate, sidePanel, knob, knobLarge, pillLong, pillShort, buttonUp, buttonDown, slot;
    static const Images& get();
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::PopupMenu::Options getOptionsForComboBoxPopupMenu (juce::ComboBox&, juce::Label&) override;
};

// letter-spaced small caps
juce::Font capsFont (float size, bool bold = false);
void drawCaps (juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Colour, float size,
               juce::Justification = juce::Justification::centred, bool bold = false);

// a text button in the faceplate style (LOAD, SAVE, tabs, ADVANCED ...)
class TextLink : public juce::Button
{
public:
    explicit TextLink (const juce::String& t, float size = 15.0f) : juce::Button (t), fontSize (size) {}
    bool underlineWhenOn = false, recording = false;
    juce::Colour onColour { 0xffffb35c };
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    float fontSize;
};

// small lamp toggle next to a section title (reverb / width / echo on-off)
class Lamp : public juce::ToggleButton
{
public:
    juce::Colour colour { 0xffffa640 };
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// round machined push button with a lamp (Sync)
class PushLamp : public juce::ToggleButton
{
public:
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

// preset name in the long glass pill, with < > to step through the preset folder
class PresetPill : public juce::Component
{
public:
    std::function<void()> onPrev, onNext, onClick;
    juce::String name;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

// the glass display: EQ curves or width per band
class Display : public juce::Component
{
public:
    void setCurves (const SunoChainProcessor::CurveSnapshot& s) { snap = s; repaint(); }
    void setWidth (const std::array<float, 6>& w) { widthBands = w; if (mode == 1) repaint(); }
    void setMode (int m) { mode = m; repaint(); }
    int getMode() const { return mode; }
    void paint (juce::Graphics&) override;
private:
    void paintEq (juce::Graphics&, juce::Rectangle<float>);
    void paintWidth (juce::Graphics&, juce::Rectangle<float>);
    SunoChainProcessor::CurveSnapshot snap;
    std::array<float, 6> widthBands { -100, -100, -100, -100, -100, -100 };
    int mode = 0;
};

// slim meters in the meter well
class Meters : public juce::Component
{
public:
    void update (const SunoChainProcessor::Meters&);
    void paint (juce::Graphics&) override;
private:
    float in = -100, out = -100, gr = 0, ds = 0, duck = 0;
    uint32_t lastBlocks = 0; int idleTicks = 0;
};

struct Knob
{
    juce::Slider slider;
    juce::Label label;
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

// everything at logical size; the editor scales this one component
class Root : public juce::Component
{
public:
    explicit Root (SunoChainProcessor&);
    ~Root() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();
    std::function<void (bool)> onAdvanced;

private:
    Knob& addKnob (const juce::String& id, const juce::String& name, int size, bool violet = false, bool big = false);
    void placeKnob (const juce::String& id, float cx, float cy, int size);
    void loadPreset();
    void savePreset();
    void stepPreset (int dir);
    void toggleLearn();
    void toggleLoudMatch();
    void setAdvanced (bool open);

    SunoChainProcessor& proc;
    LookAndFeel lnf;
    Display display;
    Meters meters;
    PresetPill pill;
    TextLink loadB { "LOAD" }, saveB { "SAVE" }, learnB { "LEARN" }, clearB { "CLEAR" };
    TextLink eqTab { "EQ", 11.0f }, widthTab { "WIDTH", 11.0f }, advancedB { "ADVANCED  +", 11.0f }, matchB { "MATCH LOUDNESS", 11.0f };
    Lamp revLamp, widthLamp, echoLamp;
    PushLamp syncB;
    juce::ComboBox noteBox;
    juce::Label status, delayInfo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> revAtt, widthAtt, echoAtt, syncAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> noteAtt;
    std::map<juce::String, std::unique_ptr<Knob>> knobs;
    std::vector<juce::String> drawerIds;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String statusText;
    int ticks = 0;
};
} // namespace ui

class SunoChainEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SunoChainEditor (SunoChainProcessor&);
    ~SunoChainEditor() override;
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0d0d0e)); }

private:
    void timerCallback() override { root.tick(); }
    void applyLayout (bool advanced);
    int logicalW = ui::kW;
    SunoChainProcessor& proc;
    ui::Root root;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SunoChainEditor)
};
