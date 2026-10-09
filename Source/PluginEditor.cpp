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
const juce::Colour violet { 0xffb48cff };   // width
}

//==============================================================================
SunoLookAndFeel::SunoLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::bg);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::ComboBox::backgroundColourId, col::panel.brighter (0.06f));
    setColour (juce::ComboBox::outlineColourId, col::edge);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::arrowColourId, col::dim);
    setColour (juce::PopupMenu::backgroundColourId, col::panel);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
}

juce::Label* SunoLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (juce::FontOptions (12.0f));
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    return l;
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
    auto c = s.getProperties().contains ("violet") ? col::violet : col::accent;
    g.setColour (s.isEnabled() ? c : col::dim);
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
    g.setColour (col::text); g.setFont (juce::FontOptions (12.5f));
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
    auto plot = r.reduced (34, 14).withTrimmedBottom (10).withTrimmedTop (12);
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

    auto legend = r.reduced (40, 6).removeFromTop (14);
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
void MeterPanel::update (const SunoChainProcessor::Meters& m)
{
    auto fall = [] (float& cur, float v, float rate) { cur = v > cur ? v : cur - rate; };
    fall (in, m.in, 1.5f); fall (out, m.out, 1.5f);
    fall (gr1, m.gr1, 0.4f); fall (gr2, m.gr2, 0.4f); fall (duck, m.duck, 0.6f);
    width = m.width > -99 ? (width < -99 ? m.width : 0.8f * width + 0.2f * m.width) : width - 1.0f;
    repaint();
}

void MeterPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::panel); g.fillRoundedRectangle (r, 8);
    g.setColour (col::dim); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText ("METERS", r.removeFromTop (22).withTrimmedLeft (12), juce::Justification::centredLeft);
    auto area = r.reduced (8, 4);
    const int n = 6; const float w = area.getWidth() / n;
    auto bar = [&] (int i, const juce::String& name, float v, float lo, float hi, bool down, juce::Colour c, const juce::String& val)
    {
        auto b = juce::Rectangle<float> (area.getX() + i * w, area.getY(), w, area.getHeight());
        auto lab = b.removeFromBottom (28);
        auto track = b.reduced (w * 0.28f, 2);
        g.setColour (col::edge); g.fillRoundedRectangle (track, 3);
        float t = juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
        auto fill = down ? track.withHeight (track.getHeight() * t) : track.withTrimmedTop (track.getHeight() * (1 - t));
        g.setColour (c); g.fillRoundedRectangle (fill, 3);
        g.setColour (col::dim); g.setFont (juce::FontOptions (10.0f));
        g.drawText (name, lab.removeFromTop (13), juce::Justification::centred);
        g.setColour (col::text); g.drawText (val, lab, juce::Justification::centred);
    };
    auto dbs = [] (float v) { return v < -99 ? juce::String ("-inf") : juce::String (v, 1); };
    bar (0, "IN", in, -60, 0, false, col::blue, dbs (in));
    bar (1, "OUT", out, -60, 0, false, col::blue, dbs (out));
    bar (2, "COMP", gr1, 0, 12, true, col::accent, juce::String (gr1, 1));
    bar (3, "LEVEL", gr2, 0, 12, true, col::accent, juce::String (gr2, 1));
    bar (4, "DUCK", duck, 0, 24, true, col::green, juce::String (duck, 1));
    bar (5, "WIDTH", width, -50, -10, false, col::violet, dbs (width));
}

//==============================================================================
SunoChainEditor::Knob& SunoChainEditor::addKnob (const juce::String& id, const juce::String& name, bool big)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, big ? 90 : 76, 16);
    k->label.setText (name, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setColour (juce::Label::textColourId, col::dim);
    k->label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    k->slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
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
    addAndMakeVisible (graph); addAndMakeVisible (meters);
    for (auto* b : { &loadButton, &saveButton, &learnButton, &clearButton }) addAndMakeVisible (*b);
    loadButton.onClick = [this] { loadPreset(); };
    saveButton.onClick = [this] { savePreset(); };
    learnButton.onClick = [this] { toggleLearn(); };
    clearButton.onClick = [this] { proc.clearLearn(); statusText = "Learned voice cleared."; };
    presetLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    presetLabel.setColour (juce::Label::textColourId, col::accent);
    statusLabel.setFont (juce::FontOptions (12.5f));
    statusLabel.setColour (juce::Label::textColourId, col::dim);
    delayInfo.setFont (juce::FontOptions (11.0f)); delayInfo.setColour (juce::Label::textColourId, col::dim);
    delayInfo.setJustificationType (juce::Justification::centred);
    noteLabel.setText ("Note", juce::dontSendNotification); noteLabel.setFont (juce::FontOptions (12.0f));
    noteLabel.setColour (juce::Label::textColourId, col::dim); noteLabel.setJustificationType (juce::Justification::centred);
    for (auto* l : { &presetLabel, &statusLabel, &delayInfo, &noteLabel }) addAndMakeVisible (*l);
    noteBox.addItemList ({ "1/4", "1/8 dotted", "1/8", "1/8 triplet", "1/16" }, 1);
    addAndMakeVisible (noteBox);
    noteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "dlyNote", noteBox);

    addKnob ("amount", "AMOUNT", true); addKnob ("output", "OUTPUT", true);
    addKnob ("eqAmount", "Match EQ"); addKnob ("eqLow", "Low Boost"); addKnob ("compAmount", "Compression");
    addKnob ("deess", "De-ess"); addKnob ("satDrive", "Sat Drive"); addKnob ("satMix", "Sat Mix");
    addToggle ("revOn", "On");
    addKnob ("revLevel", "Level"); addKnob ("predelay", "Pre-delay"); addKnob ("decay", "Decay"); addKnob ("width", "Width");
    addKnob ("duck", "Ducking"); addKnob ("duckRel", "Duck Release"); addKnob ("wetHpf", "HPF"); addKnob ("wetLpf", "LPF");
    addToggle ("wOn", "On");
    for (auto id : { "wLevel", "wTone", "wMotion" })
    {
        auto& k = addKnob (id, juce::String (id) == "wLevel" ? "Width" : juce::String (id) == "wTone" ? "Tone" : "Held Notes");
        k.slider.getProperties().set ("violet", true);
    }
    addToggle ("dlyOn", "On"); addToggle ("dlySync", "Sync");
    addKnob ("dlyLevel", "Level"); addKnob ("dlyTime", "Time"); addKnob ("dlyFb", "Feedback"); addKnob ("dlyToRev", "> Reverb");

    setSize (1140, 640);
    timerCallback();
    startTimerHz (20);
}

SunoChainEditor::~SunoChainEditor() { stopTimer(); setLookAndFeel (nullptr); }

void SunoChainEditor::loadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Load Suno Chain preset", SunoChainProcessor::presetFolder(), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              juce::String err;
                              statusText = proc.loadPresetFile (f, err) ? "Preset loaded: " + f.getFileName() : err;
                          });
}

void SunoChainEditor::savePreset()
{
    auto start = SunoChainProcessor::presetFolder().getChildFile (proc.getPresetName().replaceCharacters ("/\\:*?\"<>|", "_________") + ".json");
    chooser = std::make_unique<juce::FileChooser> ("Save Suno Chain preset", start, "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("json")) f = f.withFileExtension ("json");
                              juce::String err;
                              statusText = proc.savePresetFile (f, err) ? "Preset saved: " + f.getFileName() : err;
                          });
}

void SunoChainEditor::toggleLearn()
{
    if (! proc.isLearning())
    {
        proc.startLearn();
        statusText = "Learning... play your dry vocal (10-60 s, with your tuning plugin on), then press Stop.";
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
    if (! learning && statusText.startsWith ("Learning")) { juce::String msg; proc.stopLearn (msg); statusText = msg; }
    presetLabel.setText (proc.hasPreset() ? proc.getPresetName() : "Suno Lead 01 (built-in)", juce::dontSendNotification);
    if (statusText.isEmpty())
        statusText = ! proc.hasLearned() ? "Press Learn My Voice and play your dry vocal so EQ and compression adapt to it." : "Ready.";
    statusLabel.setText (statusText, juce::dontSendNotification);

    auto m = proc.getMeters();
    meters.update (m);
    const bool sync = proc.apvts.getRawParameterValue ("dlySync")->load() > 0.5f;
    knobs["dlyTime"]->slider.setVisible (! sync); knobs["dlyTime"]->label.setVisible (! sync);
    noteBox.setVisible (sync); noteLabel.setVisible (sync);
    delayInfo.setText (juce::String ((int) std::round (m.delayMs)) + " ms" + (sync ? " @ " + juce::String (m.bpm, 1) + " BPM" : ""), juce::dontSendNotification);
    if (++tick % 4 == 0) graph.setData (proc.getCurves());
}

void SunoChainEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::text); g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("SUNO CHAIN", 20, 12, 170, 28, juce::Justification::centredLeft);
    g.setColour (col::dim); g.setFont (juce::FontOptions (11.0f));
    g.drawText ("v1.1", 160, 20, 40, 14, juce::Justification::centredLeft);
    auto section = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (col::panel); g.fillRoundedRectangle (r.toFloat(), 8);
        g.setColour (col::dim); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (title, r.getX() + 12, r.getY() + 7, 200, 14, juce::Justification::centredLeft);
    };
    section ({ 20, 318, 290, 300 }, "TONE & DYNAMICS");
    section ({ 320, 318, 450, 300 }, "SPACE (REVERB)");
    section ({ 780, 318, 160, 300 }, "VOCAL WIDTH");
    section ({ 950, 318, 170, 300 }, "ECHO (DELAY)");
    section ({ 950, 64, 170, 244 }, "MASTER");
}

void SunoChainEditor::place (const juce::String& id, int x, int y, int size)
{
    auto& k = *knobs[id];
    k.label.setBounds (x - 14, y, size + 28, 14);
    k.slider.setBounds (x - 8, y + 14, size + 16, size + 20);
}

void SunoChainEditor::resized()
{
    presetLabel.setBounds (210, 14, 330, 28);
    loadButton.setBounds (560, 14, 90, 28);
    saveButton.setBounds (656, 14, 90, 28);
    learnButton.setBounds (760, 14, 220, 28);
    clearButton.setBounds (986, 14, 134, 28);
    statusLabel.setBounds (20, 44, 1100, 18);
    graph.setBounds (20, 64, 640, 244);
    meters.setBounds (670, 64, 270, 244);

    place ("amount", 995, 90, 80);
    place ("output", 995, 196, 64);

    const int s = 58;
    // tone & dynamics
    int tx[3] = { 40, 132, 224 };
    place ("eqAmount", tx[0], 342, s); place ("eqLow", tx[1], 342, s); place ("compAmount", tx[2], 342, s);
    place ("deess", tx[0], 472, s);    place ("satDrive", tx[1], 472, s); place ("satMix", tx[2], 472, s);
    // space
    toggles["revOn"]->button.setBounds (700, 322, 64, 20);
    int sx[4] = { 344, 452, 560, 668 };
    place ("revLevel", sx[0], 342, s); place ("predelay", sx[1], 342, s); place ("decay", sx[2], 342, s); place ("width", sx[3], 342, s);
    place ("duck", sx[0], 472, s);     place ("duckRel", sx[1], 472, s);  place ("wetHpf", sx[2], 472, s); place ("wetLpf", sx[3], 472, s);
    // vocal width
    toggles["wOn"]->button.setBounds (872, 322, 64, 20);
    place ("wLevel", 796, 342, s); place ("wTone", 870, 342, 50);
    place ("wMotion", 830, 472, s);
    // echo
    toggles["dlyOn"]->button.setBounds (1052, 322, 64, 20);
    toggles["dlySync"]->button.setBounds (962, 346, 80, 20);
    noteLabel.setBounds (1040, 346, 74, 14);
    noteBox.setBounds (1042, 362, 72, 22);
    place ("dlyTime", 1050, 342, 50);
    place ("dlyLevel", 972, 392, 50); delayInfo.setBounds (1030, 450, 90, 14);
    place ("dlyFb", 972, 492, 50); place ("dlyToRev", 1052, 492, 50);
}
