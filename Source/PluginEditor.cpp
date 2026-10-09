#include "PluginEditor.h"

namespace col
{
const juce::Colour bg     { 0xff111316 };
const juce::Colour panel  { 0xff1a1d22 };
const juce::Colour edge   { 0xff2a2f37 };
const juce::Colour text   { 0xffe6e8eb };
const juce::Colour dim    { 0xff8a919c };
const juce::Colour accent { 0xffff7a45 };   // target / Suno
const juce::Colour blue   { 0xff6aa8ff };   // your voice
const juce::Colour green  { 0xff7bd88f };   // correction
}

//==============================================================================
SunoLookAndFeel::SunoLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::bg);
    setColour (juce::ToggleButton::textColourId, col::text);
}

void SunoLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4);
    float r = juce::jmin (b.getWidth(), b.getHeight()) / 2.0f, cx = b.getCentreX(), cy = b.getCentreY();
    float a = start + pos * (end - start);
    float th = juce::jmax (2.5f, r * 0.14f);
    juce::Path track; track.addCentredArc (cx, cy, r - th, r - th, 0, start, end, true);
    g.setColour (col::edge); g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path val; val.addCentredArc (cx, cy, r - th, r - th, 0, start, a, true);
    g.setColour (s.isEnabled() ? col::accent : col::dim);
    g.strokePath (val, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    float kr = r - th * 2.2f;
    g.setColour (col::panel.brighter (0.08f)); g.fillEllipse (cx - kr, cy - kr, kr * 2, kr * 2);
    juce::Point<float> tip (cx + (kr - 3) * std::sin (a), cy - (kr - 3) * std::cos (a));
    g.setColour (col::text); g.drawLine (cx + kr * 0.35f * std::sin (a), cy - kr * 0.35f * std::cos (a), tip.x, tip.y, 2.0f);
}

void SunoLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2);
    auto pill = r.removeFromLeft (34).withSizeKeepingCentre (34, 18);
    g.setColour (b.getToggleState() ? col::accent : col::edge); g.fillRoundedRectangle (pill, 9);
    float kx = b.getToggleState() ? pill.getRight() - 16 : pill.getX() + 2;
    g.setColour (col::text.withAlpha (hi ? 1.0f : 0.9f)); g.fillEllipse (kx, pill.getY() + 2, 14, 14);
    g.setColour (col::text); g.setFont (juce::FontOptions (13.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (6), juce::Justification::centredLeft);
}

void SunoLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1);
    auto c = b.getToggleState() ? col::accent : col::panel.brighter (hi ? 0.12f : 0.05f);
    if (down) c = c.brighter (0.1f);
    g.setColour (c); g.fillRoundedRectangle (r, 6);
    g.setColour (col::edge); g.drawRoundedRectangle (r, 6, 1);
}

//==============================================================================
void CurveGraph::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel); g.fillRoundedRectangle (r, 8);
    auto plot = r.reduced (34, 14).withTrimmedBottom (10);
    const float fLo = 40, fHi = 20000, dLo = -30, dHi = 18;
    auto X = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / fLo) / std::log (fHi / fLo); };
    auto Y = [&] (float d) { return plot.getBottom() - plot.getHeight() * (juce::jlimit (dLo, dHi, d) - dLo) / (dHi - dLo); };

    g.setFont (juce::FontOptions (11.0f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        g.setColour (col::edge); g.drawVerticalLine ((int) X (f), plot.getY(), plot.getBottom());
        g.setColour (col::dim); g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) X (f) - 15, (int) plot.getBottom() + 2, 30, 12, juce::Justification::centred);
    }
    for (float d : { -24.0f, -12.0f, 0.0f, 12.0f })
    {
        g.setColour (d == 0 ? col::dim.withAlpha (0.6f) : col::edge); g.drawHorizontalLine ((int) Y (d), plot.getX(), plot.getRight());
        g.setColour (col::dim); g.drawText (juce::String ((int) d), (int) plot.getX() - 32, (int) Y (d) - 6, 28, 12, juce::Justification::centredRight);
    }
    const auto& fc = sc::bandCenters();
    auto drawCurve = [&] (const std::array<float, sc::kNumBands>& c, juce::Colour colour, float thick)
    {
        juce::Path p;
        for (int i = 0; i < sc::kNumBands; ++i)
        {
            auto pt = juce::Point<float> (X (fc[(size_t) i]), Y (c[(size_t) i]));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (colour); g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    if (snap.hasTarget) drawCurve (snap.target, col::accent.withAlpha (0.9f), 1.8f);
    if (snap.hasSource) drawCurve (snap.source, col::blue.withAlpha (0.9f), 1.8f);
    if (snap.hasTarget && snap.hasSource) drawCurve (snap.correction, col::green, 2.6f);

    auto legend = r.reduced (40, 8).removeFromTop (14);
    auto item = [&] (juce::Colour c, const juce::String& s, bool on)
    {
        auto a = legend.removeFromLeft (150);
        g.setColour (on ? c : col::edge); g.fillRect (a.removeFromLeft (14).withSizeKeepingCentre (14, 3));
        g.setColour (on ? col::text : col::dim); g.drawText (s, a.withTrimmedLeft (6), juce::Justification::centredLeft);
    };
    item (col::accent, "Suno target", snap.hasTarget);
    item (col::blue, "Your voice (Learn)", snap.hasSource);
    item (col::green, "Applied EQ", snap.hasTarget && snap.hasSource);
}

//==============================================================================
SunoChainEditor::Knob& SunoChainEditor::addKnob (const juce::String& id, const juce::String& name, bool big)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 80 : 70, 16);
    k->slider.setColour (juce::Slider::textBoxTextColourId, col::text);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, col::dim);
    k->label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    auto& ref = *k; knobs[id] = std::move (k); return ref;
}

SunoChainEditor::Toggle& SunoChainEditor::addToggle (const juce::String& id, const juce::String& name)
{
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText (name);
    addAndMakeVisible (t->button);
    t->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, t->button);
    auto& ref = *t; toggles[id] = std::move (t); return ref;
}

SunoChainEditor::SunoChainEditor (SunoChainProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (graph);
    for (auto* b : { &loadButton, &learnButton, &clearButton }) addAndMakeVisible (*b);
    loadButton.onClick = [this] { loadPreset(); };
    learnButton.onClick = [this] { toggleLearn(); };
    clearButton.onClick = [this] { proc.clearLearn(); statusText = "Learned voice cleared."; };
    presetLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    presetLabel.setColour (juce::Label::textColourId, col::accent);
    statusLabel.setFont (juce::FontOptions (12.5f));
    statusLabel.setColour (juce::Label::textColourId, col::dim);
    addAndMakeVisible (presetLabel); addAndMakeVisible (statusLabel);

    addKnob ("amount", "AMOUNT", true); addKnob ("output", "OUTPUT", true);
    addKnob ("eqAmount", "Match EQ"); addKnob ("eqLow", "Low Match"); addKnob ("compAmount", "Compression");
    addKnob ("deess", "De-ess"); addKnob ("satDrive", "Sat Drive"); addKnob ("satMix", "Sat Mix");
    addToggle ("revOn", "Reverb");
    addKnob ("revLevel", "Level"); addKnob ("predelay", "Pre-delay"); addKnob ("decay", "Decay"); addKnob ("width", "Width");
    addKnob ("duck", "Ducking"); addKnob ("duckRel", "Duck Release"); addKnob ("wetHpf", "HPF"); addKnob ("wetLpf", "LPF");
    addToggle ("dlyOn", "Delay");
    addKnob ("dlyLevel", "Level"); addKnob ("dlyTime", "Time"); addKnob ("dlyFb", "Feedback");

    lastDir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    setSize (980, 610);
    timerCallback();
    startTimerHz (15);
}

SunoChainEditor::~SunoChainEditor() { stopTimer(); setLookAndFeel (nullptr); }

void SunoChainEditor::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Suno Chain preset", lastDir, "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              lastDir = f.getParentDirectory();
                              juce::String err;
                              statusText = proc.loadPresetFile (f, err) ? "Preset loaded: " + f.getFileName() : err;
                          });
}

void SunoChainEditor::toggleLearn()
{
    if (! proc.isLearning())
    {
        proc.startLearn();
        statusText = "Learning... play your dry vocal (10-60 s), then press Stop.";
    }
    else
    {
        juce::String msg;
        proc.stopLearn (msg);
        statusText = msg;
    }
}

void SunoChainEditor::timerCallback()
{
    const bool learning = proc.isLearning();
    learnButton.setButtonText (learning ? "Stop (" + juce::String (proc.learnSeconds(), 0) + " s)" : (proc.hasLearned() ? "Re-learn Voice" : "Learn My Voice"));
    learnButton.setToggleState (learning, juce::dontSendNotification);
    if (! learning && statusText.startsWith ("Learning")) // auto-stopped (buffer full)
    {
        juce::String msg; proc.stopLearn (msg); statusText = msg;
    }
    presetLabel.setText (proc.getPresetName(), juce::dontSendNotification);
    if (statusText.isEmpty())
        statusText = ! proc.hasPreset() ? "Load a preset made from a Suno vocal, then press Learn My Voice while your vocal plays."
                   : ! proc.hasLearned() ? "Press Learn My Voice and play your dry vocal so the EQ and compression can adapt to it."
                   : "Ready.";
    statusLabel.setText (statusText, juce::dontSendNotification);
    if (++graphTick % 3 == 0) graph.setData (proc.getCurves());
}

void SunoChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::text); g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("SUNO CHAIN", 20, 14, 200, 28, juce::Justification::centredLeft);
    auto section = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (col::panel); g.fillRoundedRectangle (r.toFloat(), 8);
        g.setColour (col::dim); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, r.getX() + 12, r.getY() + 6, 200, 14, juce::Justification::centredLeft);
    };
    section ({ 20, 312, 290, 280 }, "TONE & DYNAMICS");
    section ({ 320, 312, 450, 280 }, "SPACE (REVERB)");
    section ({ 780, 312, 180, 280 }, "ECHO (DELAY)");
    section ({ 780, 64, 180, 238 }, "MASTER");
}

void SunoChainEditor::resized()
{
    presetLabel.setBounds (190, 14, 330, 28);
    loadButton.setBounds (530, 14, 120, 28);
    learnButton.setBounds (660, 14, 190, 28);
    clearButton.setBounds (860, 14, 100, 28);
    statusLabel.setBounds (20, 44, 940, 18);
    graph.setBounds (20, 64, 750, 238);

    auto place = [this] (const juce::String& id, int x, int y, int size)
    {
        auto& k = *knobs[id];
        k.label.setBounds (x - 10, y, size + 20, 14);
        k.slider.setBounds (x, y + 14, size, size + 18);
    };
    place ("amount", 830, 80, 80);
    place ("output", 830, 190, 80);

    const int s = 64;
    // tone & dynamics: 3 x 2 grid
    int tx[3] = { 38, 128, 218 };
    place ("eqAmount", tx[0], 334, s); place ("eqLow", tx[1], 334, s); place ("compAmount", tx[2], 334, s);
    place ("deess", tx[0], 464, s);    place ("satDrive", tx[1], 464, s); place ("satMix", tx[2], 464, s);
    // space: toggle + 4 x 2 grid
    toggles["revOn"]->button.setBounds (650, 316, 110, 22);
    int sx[4] = { 340, 448, 556, 664 };
    place ("revLevel", sx[0], 334, s); place ("predelay", sx[1], 334, s); place ("decay", sx[2], 334, s); place ("width", sx[3], 334, s);
    place ("duck", sx[0], 464, s);     place ("duckRel", sx[1], 464, s);  place ("wetHpf", sx[2], 464, s); place ("wetLpf", sx[3], 464, s);
    // echo
    toggles["dlyOn"]->button.setBounds (868, 316, 90, 22);
    place ("dlyLevel", 798, 334, 56); place ("dlyTime", 884, 334, 56);
    place ("dlyFb", 840, 464, 56);
}
