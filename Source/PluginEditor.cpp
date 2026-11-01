#include "PluginEditor.h"
#include "gui/Fonts.h"

namespace ui
{
// v2.6: the look of ReKit 2 - a black device, monochrome with one orange, Inter / Space Mono
namespace col
{
const juce::Colour amber  { 0xffff5a1f };   // the one orange
const juce::Colour amberHi{ 0xfff2f2f0 };   // value text while touching = white
const juce::Colour violet { 0xffb0b0b2 };   // width (was violet): soft white
const juce::Colour green  { 0xffd9d9d7 };   // applied EQ / "like Suno": white
const juce::Colour cream  { 0xff8a8a8c };   // your voice curve
const juce::Colour text   { 0xfff2f2f0 };
const juce::Colour dim    { 0xff8a8a8c };
const juce::Colour faint  { 0xff5e5e61 };
const juce::Colour body0  { 0xff232326 }, body1 { 0xff17171a }, screen { 0xff050506 }, chip { 0xff141416 }, chip2 { 0xff1c1c1f },
                   chipOn { 0xff2a2a2d }, line { 0xff2e2e31 }, label { 0xffa6a6a8 }, soft { 0xffb0b0b2 }, ink { 0xff0b0b0c };
}

//==============================================================================
// v1.15.1//==============================================================================
// v2.6: no images any more (the faceplate, knobs and switches are drawn); the fonts are embedded
static juce::Typeface::Ptr face (int i)
{
    static juce::Typeface::Ptr t[4];
    if (t[i] == nullptr)
    {
        const unsigned char* d = fonts::InterRegular; std::size_t n = fonts::InterRegularSize;
        if (i == 1) { d = fonts::InterMedium; n = fonts::InterMediumSize; }
        if (i == 2) { d = fonts::InterSemiBold; n = fonts::InterSemiBoldSize; }
        if (i == 3) { d = fonts::SpaceMono; n = fonts::SpaceMonoSize; }
        t[i] = juce::Typeface::createSystemTypefaceFor (d, n);
    }
    return t[i];
}
juce::Font inter (float px, int weight)
{
    if (auto f = face (weight >= 600 ? 2 : weight >= 500 ? 1 : 0)) return juce::Font (juce::FontOptions (f).withPointHeight (px));
    return juce::Font (juce::FontOptions (px * 1.2f, weight >= 500 ? juce::Font::bold : juce::Font::plain));
}
juce::Font mono (float px)
{
    if (auto f = face (3)) return juce::Font (juce::FontOptions (f).withPointHeight (px));
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), px * 1.2f, juce::Font::plain));
}

// a soft shadow without a blur: a few wider, fainter shapes (as ReKit 2)
static void softShadow (juce::Graphics& g, juce::Rectangle<float> r, float alpha, float dy, float spread, bool round)
{
    for (int i = 4; i >= 1; --i)
    {
        const float e = spread * (float) i / 4.0f; const auto q = r.translated (0, dy * (float) i / 4.0f).expanded (e);
        g.setColour (juce::Colours::black.withAlpha (alpha * 0.22f));
        if (round) g.fillEllipse (q); else g.fillRoundedRectangle (q, 8.0f + e);
    }
}

// a rounded chip (ReKit's buttons): bg colour, optional outline
static void chipBg (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour bg, bool over, bool outline = false)
{
    const float rad = r.getHeight() * 0.5f;
    if (! bg.isTransparent()) { g.setColour (bg.brighter (over ? 0.08f : 0.0f)); g.fillRoundedRectangle (r, rad); }
    if (outline) { g.setColour (over ? col::soft : col::line); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f); }
    else if (over && bg.isTransparent()) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillRoundedRectangle (r, rad); }
}

// side module image (1024 x 1536): dividers at 24.2 / 48 / 71.6 % of the height -> logical y 194 / 384 / 573
constexpr float kSec[5] { 0.0f, 194.0f, 384.0f, 573.0f, 800.0f };

juce::Font capsFont (float size, bool bold) { return inter (size * 1.08f, bold ? 600 : 400); }

// v2.6: text is lower case Inter (the old engraved small caps are gone)
void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour c, float size,
               juce::Justification j, bool bold)
{
    g.setColour (c); g.setFont (capsFont (size, bold));
    g.drawText (text.toLowerCase().replace ("khz", "kHz").replace (" hz", " Hz").replace (" db", " dB"), area, j, false);
}

// v2.6: flat lines and dots (no glow)
static void glowLine (juce::Graphics& g, juce::Line<float> l, juce::Colour c, float core, float intensity = 1.0f)
{
    g.setColour (c.withAlpha (0.95f * intensity)); g.drawLine (l, core);
}

static void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float core)
{
    using PST = juce::PathStrokeType;
    g.setColour (c.withAlpha (0.95f)); g.strokePath (p, PST (core, PST::curved, PST::rounded));
}

static void lampDot (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, bool on)
{
    const auto e = juce::Rectangle<float> (r * 2, r * 2).withCentre (c);
    if (! on) { g.setColour (col::line); g.drawEllipse (e, 1.0f); return; }
    g.setColour (colour); g.fillEllipse (e);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, col::chip2);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::text);
    setColour (juce::PopupMenu::highlightedTextColourId, col::ink);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::Label::textColourId, col::label);
    setColour (juce::TooltipWindow::backgroundColourId, col::chip2);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::line);
    setColour (juce::TextEditor::highlightColourId, col::text.withAlpha (0.2f));
    setColour (juce::CaretComponent::caretColourId, col::amber);
}

// v2.6 knobs (ReKit 2): a dark cap with a soft shadow and a white mark; AMOUNT is the one orange cap.
// A thin value arc shows the setting (bipolar knobs from their 0 dB point), brighter while touched.
void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const bool big = s.getProperties().contains ("big");
    const bool off = (bool) s.getProperties().getWithDefault ("off", false);
    auto b = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float margin = big ? 14.0f : 11.0f;
    const float d = juce::jmin (b.getWidth(), b.getHeight()) - 2 * margin, r = d * 0.5f;
    const auto c = b.getCentre();
    const float a = start + pos * (end - start);
    auto cap = juce::Rectangle<float> (d, d).withCentre (c);
    if (big)
    {
        for (int i = 4; i >= 1; --i) { const float e = margin * 0.55f * (float) i / 4.0f; g.setColour (col::amber.withAlpha (0.09f)); g.fillEllipse (cap.translated (0, margin * 0.3f * (float) i / 4.0f).expanded (e)); }
        juce::ColourGradient gr (juce::Colour (0xffff7b47), c.x - r * 0.28f, c.y - r * 0.44f, juce::Colour (0xffd8440e), c.x + r * 0.7f, c.y + r * 0.9f, true);
        gr.addColour (0.55, juce::Colour (0xfff2541a));
        g.setGradientFill (gr); g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.25f)); g.drawEllipse (cap.reduced (1.0f), 1.0f);
    }
    else
    {
        softShadow (g, cap, 0.7f, r * 0.2f, r * 0.12f, true);
        juce::ColourGradient gr (juce::Colour (0xff3c3c3f), c.x - r * 0.28f, c.y - r * 0.44f, juce::Colour (0xff19191b), c.x + r * 0.7f, c.y + r * 0.9f, true);
        gr.addColour (0.55, juce::Colour (0xff242427));
        g.setGradientFill (gr); g.fillEllipse (cap);
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.drawEllipse (cap.reduced (1.0f), 1.0f);
    }
    // value arc
    {
        const float ar = r + (big ? 8.0f : 5.0f);
        const bool hot = s.isMouseOverOrDragging() && ! off;
        juce::Path track; track.addCentredArc (c.x, c.y, ar, ar, 0, start, end, true);
        g.setColour (col::line); g.strokePath (track, juce::PathStrokeType (big ? 2.0f : 1.4f));
        float from = start;
        if (s.getProperties().contains ("bipolar")) from = start + (float) s.valueToProportionOfLength (0.0) * (end - start);
        juce::Path val; val.addCentredArc (c.x, c.y, ar, ar, 0, juce::jmin (from, a), juce::jmax (from, a), true);
        const auto vc = big ? col::amber : col::text;
        g.setColour (off ? col::faint.withAlpha (0.5f) : vc.withAlpha (hot || big ? 0.95f : 0.45f));
        g.strokePath (val, juce::PathStrokeType (big ? 2.0f : 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // the mark
    juce::Path m; m.addRoundedRectangle (-2.0f, -r + (big ? 14.0f : 7.0f), 4.0f, r * 0.38f, 2.0f);
    g.setColour (off ? col::faint : juce::Colours::white);
    g.fillPath (m, juce::AffineTransform::rotation (a).translated (c.x, c.y));
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (1.0f, (float) h * 0.18f);
    chipBg (g, r, col::chip2, box.isMouseOver());
    const float cx = r.getRight() - 12.0f, cy = r.getCentreY();
    juce::Path p; p.startNewSubPath (cx - 3.5f, cy - 1.5f); p.lineTo (cx, cy + 2.0f); p.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (col::soft);
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (4, 0, box.getWidth() - 20, box.getHeight());
    l.setFont (getComboBoxFont (box));
    l.setJustificationType (juce::Justification::centred);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return inter (12.0f, 500); }

juce::PopupMenu::Options LookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& l)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, l).withStandardItemHeight (22);
}

//==============================================================================
void TextLink::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    auto c = on ? col::text : (over ? col::soft : col::dim);
    if (down) c = c.brighter (0.2f);
    g.setColour (c); g.setFont (inter (fontSize * 1.1f, on ? 600 : 400));
    g.drawText (getButtonText().toLowerCase(), r, juce::Justification::centred, false);
    if (recording)
    {
        const bool lit = (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
        const float tw = juce::GlyphArrangement::getStringWidth (inter (fontSize * 1.1f), getButtonText().toLowerCase());
        lampDot (g, { r.getCentreX() - tw * 0.5f - 9.0f, r.getCentreY() }, 3.0f, col::amber, lit);
    }
    if (underlineWhenOn && on)   // ReKit's page marker: a small dot under the word
        lampDot (g, { r.getCentreX(), r.getBottom() - 2.5f }, 1.8f, col::amber, true);
}

void Lamp::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.2f;
    lampDot (g, r.getCentre(), rad, col::amber, getToggleState());
    if (over) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillEllipse (r.reduced (r.getWidth() * 0.18f)); }
}

// round key (ReKit's keys): dark disc, soft shadow, orange dot when on
void PushLamp::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (8.0f);
    softShadow (g, r, 0.8f, 5.0f, 3.0f, true);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343437), r.getX() + r.getWidth() * 0.38f, r.getY() + r.getHeight() * 0.3f,
                                             juce::Colour (0xff1f1f22), r.getRight(), r.getBottom(), true));
    g.fillEllipse (r);
    if (over || down) { g.setColour (juce::Colours::white.withAlpha (down ? 0.1f : 0.06f)); g.fillEllipse (r); }
    g.setColour (juce::Colours::white.withAlpha (0.08f)); g.drawEllipse (r.reduced (0.5f), 1.0f);
    lampDot (g, r.getCentre(), r.getWidth() * 0.08f, col::amber, getToggleState());
}

void FlatButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    const bool on = getToggleState();
    chipBg (g, r, on ? col::text : col::chip2, over || down);
    g.setColour (on ? col::ink : col::soft); g.setFont (inter (12.0f, on ? 600 : 400));
    g.drawText (getButtonText().toLowerCase(), r, juce::Justification::centred, false);
    if (recording)
    {
        const bool lit = (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
        lampDot (g, { r.getX() + 12.0f, r.getCentreY() }, 3.0f, col::amber, lit);
    }
}

// two chips: suno | black
void LeverSwitch::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (0, getHeight() * 0.22f);
    const bool on = getToggleState();
    g.setColour (col::chip); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    auto left = r.withWidth (r.getWidth() * 0.5f), right = left.withX (left.getRight());
    chipBg (g, (on ? right : left).reduced (2.0f), col::text, over);
    g.setFont (inter (11.0f, 600));
    g.setColour (on ? col::dim : col::ink); g.drawText (offText.toLowerCase(), left, juce::Justification::centred, false);
    g.setColour (on ? col::ink : col::dim); g.drawText (onText.toLowerCase(), right, juce::Justification::centred, false);
}

// a chip with a dot (match loudness / freeze): the dot is orange while on, blinks while measuring
void GlassButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    bool on = lit;
    if (blinking) on = (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
    chipBg (g, r, col::chip2, over || down);
    lampDot (g, { r.getX() + 14.0f, r.getCentreY() }, 3.2f, col::amber, on);
    g.setColour (lit ? col::text : col::soft); g.setFont (inter (12.0f, lit ? 600 : 400));
    g.drawText (getButtonText().toLowerCase(), r.withTrimmedLeft (26.0f).withTrimmedRight (10.0f), juce::Justification::centredLeft, false);
}

// ReKit's switch: a track with a white knob
void SlideSwitch::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    auto sw = r.removeFromRight (r.getHeight() * 1.9f).reduced (0, 2.0f);
    const bool on = getToggleState();
    g.setColour (on ? col::amber : col::chip); g.fillRoundedRectangle (sw, sw.getHeight() * 0.5f);
    auto knob = juce::Rectangle<float> (sw.getHeight() - 4.0f, sw.getHeight() - 4.0f)
                    .withCentre ({ on ? sw.getRight() - sw.getHeight() * 0.5f : sw.getX() + sw.getHeight() * 0.5f, sw.getCentreY() });
    g.setColour (juce::Colours::white); g.fillEllipse (knob);
    g.setColour (on ? col::text : (over ? col::soft : col::dim)); g.setFont (inter (12.0f, on ? 600 : 400));
    g.drawText (getButtonText().toLowerCase(), r.withTrimmedRight (10.0f), juce::Justification::centredRight, false);
}

// listen: three chips (all / wet / width); the chosen one is white, orange while soloing
void Selector::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::dim); g.setFont (inter (12.0f));
    g.drawText ("listen", r.removeFromLeft (48.0f), juce::Justification::centredLeft, false);
    auto row = r.reduced (0, 2.0f);
    g.setColour (col::chip); g.fillRoundedRectangle (row, row.getHeight() * 0.5f);
    const char* names[3] { "all", "wet", "width" };
    const float w = row.getWidth() / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        auto seg = juce::Rectangle<float> (row.getX() + (float) i * w, row.getY(), w, row.getHeight());
        const bool sel = i == pos;
        if (sel) chipBg (g, seg.reduced (2.0f), i == 0 ? col::text : col::amber, false);
        g.setColour (sel ? (i == 0 ? col::ink : juce::Colours::white) : col::dim); g.setFont (inter (11.0f, sel ? 600 : 400));
        g.drawText (names[i], seg, juce::Justification::centred, false);
    }
}

void Selector::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < 48) return;
    const int p = juce::jlimit (0, 2, (int) ((float) (e.x - 48) / ((float) (getWidth() - 48) / 3.0f)));
    if (p == pos) return;
    pos = p; repaint();
    if (onChange) onChange (pos);
}

void Lcd::tick()
{
    juce::Font f (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::bold));
    const float tw = juce::GlyphArrangement::getStringWidth (f, text.toUpperCase());
    const float box = (float) getWidth() * 0.80f;
    if (tw <= box) { offset = 0; return; }
    if (hold > 0) { --hold; return; }
    offset += 1.6f;
    if (offset > tw + 40.0f) { offset = 0; hold = 30; }
    repaint();
}

void Lcd::paint (juce::Graphics& g)
{
    g.setColour (col::text); g.setFont (mono (11.0f));
    g.drawText (text.toLowerCase(), getLocalBounds().toFloat().translated (-offset, 0), juce::Justification::centredLeft, false);
}

//==============================================================================
void PresetPill::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0, 4.0f);
    g.setColour (col::chip); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (col::line); g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    auto inner = r.reduced (r.getHeight() * 0.9f, 0);
    g.setColour (col::text); g.setFont (inter (15.0f, 500));
    g.drawText (name, inner, juce::Justification::centred, true);
    auto chev = [&] (float x, float dir)
    {
        const float cy = r.getCentreY();
        juce::Path p; p.startNewSubPath (x + 2.5f * dir, cy - 5); p.lineTo (x - 2.5f * dir, cy); p.lineTo (x + 2.5f * dir, cy + 5);
        g.setColour (col::soft);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    chev (r.getX() + r.getHeight() * 0.62f, 1.0f);
    chev (r.getRight() - r.getHeight() * 0.62f, -1.0f);
}

void PresetPill::mouseUp (const juce::MouseEvent& e)
{
    const float x = (float) e.x, w = (float) getWidth(), h = (float) getHeight();
    if (x < h * 1.1f) { if (onPrev) onPrev(); }
    else if (x > w - h * 1.1f) { if (onNext) onNext(); }
    else if (onClick) onClick();
}

//==============================================================================
static juce::Path smoothPath (const std::vector<juce::Point<float>>& pts)
{
    juce::Path p;
    if (pts.empty()) return p;
    p.startNewSubPath (pts[0]);
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {   // Catmull-Rom -> cubic Bezier
        auto p0 = pts[i > 0 ? i - 1 : i], p1 = pts[i], p2 = pts[i + 1], p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
        auto c1 = p1 + (p2 - p0) / 6.0f, c2 = p2 - (p3 - p1) / 6.0f;
        p.cubicTo (c1, c2, p2);
    }
    return p;
}

void Display::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::Path p; p.addRoundedRectangle (r.reduced (0.5f), 16.0f);
    g.setColour (col::screen); g.fillPath (p);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.8f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 14.0f, false));
    g.fillPath (p);
    g.setColour (juce::Colour (0xff2a2a2c)); g.strokePath (p, juce::PathStrokeType (1.0f));
    g.setColour (col::line); g.drawHorizontalLine ((int) (r.getBottom() - bottomInset), r.getX() + 18, r.getRight() - 18);
    r = r.withTrimmedBottom (bottomInset);
    if (mode >= 3)
    {   // a section page: its name and a note; the knobs sit on top
        g.setColour (col::text); g.setFont (inter (20.0f, 500));
        g.drawText (pageTitle, juce::Rectangle<float> (r.getX() + 24, r.getY() + 16, 400, 26), juce::Justification::centredLeft, false);
        g.setColour (col::dim); g.setFont (inter (11.0f));
        g.drawText (pageNote, juce::Rectangle<float> (r.getX() + 24, r.getY() + 42, r.getWidth() - 48, 16), juce::Justification::centredLeft, false);
        return;
    }
    if (mode == 1) paintWidth (g, r); else if (mode == 2) paintMatch (g, r); else paintEq (g, r);
}

void Display::paintEq (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto plot = r.reduced (18, 0).withTrimmedTop (30).withTrimmedBottom (44);
    const float fLo = 40, fHi = 20000, dLo = -30, dHi = 18;
    auto X = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / fLo) / std::log (fHi / fLo); };
    auto Y = [&] (float d) { return plot.getBottom() - plot.getHeight() * (juce::jlimit (dLo, dHi, d) - dLo) / (dHi - dLo); };
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.035f)); g.drawVerticalLine ((int) X (f), plot.getY(), plot.getBottom());
        drawCaps (g, f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                  { X (f) - 20, plot.getBottom() + 3, 40, 14 }, col::dim.withAlpha (0.8f), 10.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.05f)); g.drawHorizontalLine ((int) Y (0), plot.getX(), plot.getRight());

    const auto& fc = sc::bandCenters();
    auto points = [&] (const std::array<float, sc::kNumBands>& c)
    {
        std::vector<juce::Point<float>> v;
        for (int i = 0; i < sc::kNumBands; ++i) v.push_back ({ X (fc[(size_t) i]), Y (c[(size_t) i]) });
        return v;
    };
    const bool corr = snap.hasTarget && snap.hasSource;
    if (corr)
    {   // applied EQ: green, filled toward the 0 dB line
        auto pts = points (snap.correction);
        auto line = smoothPath (pts);
        juce::Path fill = line;
        fill.lineTo (pts.back().x, Y (0)); fill.lineTo (pts.front().x, Y (0)); fill.closeSubPath();
        juce::ColourGradient gf (col::green.withAlpha (0.16f), 0, plot.getY(), col::green.withAlpha (0.02f), 0, plot.getBottom(), false);
        g.setGradientFill (gf); g.fillPath (fill);
        glowPath (g, line, col::green, 1.4f);
    }
    if (snap.hasSource) glowPath (g, smoothPath (points (snap.source)), col::cream, 1.5f);
    if (snap.hasTarget) glowPath (g, smoothPath (points (snap.target)), col::amber, 1.8f);

    auto leg = juce::Rectangle<float> (r.getX() + 18, r.getBottom() - 20, r.getWidth() - 36, 14);
    auto item = [&] (juce::Colour c, const juce::String& s, bool on)
    {
        auto a = leg.removeFromLeft ((float) s.length() * 9.0f + 36.0f);
        lampDot (g, { a.getX() + 5, a.getCentreY() }, 2.6f, c, on);
        drawCaps (g, s, a.withTrimmedLeft (14), on ? col::text.withAlpha (0.85f) : col::faint, 10.5f, juce::Justification::centredLeft);
    };
    item (col::amber, "Target", snap.hasTarget);
    item (col::cream, "Your voice", snap.hasSource);
    item (col::green, "Applied EQ", corr);
    if (! snap.hasSource)
        drawCaps (g, "Learn your voice to see the match", plot.withTrimmedTop (plot.getHeight() * 0.62f).withHeight (16), col::dim.withAlpha (0.8f), 10.0f);
}

void Display::paintWidth (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto plot = r.reduced (46, 0).withTrimmedTop (34).withTrimmedBottom (40);
    const float dLo = -36, dHi = 6;
    auto Y = [&] (float d) { return plot.getBottom() - plot.getHeight() * (juce::jlimit (dLo, dHi, d) - dLo) / (dHi - dLo); };
    for (float d : { -30.0f, -20.0f, -10.0f, 0.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (d == 0 ? 0.07f : 0.035f)); g.drawHorizontalLine ((int) Y (d), plot.getX(), plot.getRight());
        drawCaps (g, juce::String ((int) d), { plot.getX() - 40, Y (d) - 6, 32, 12 }, col::dim.withAlpha (0.7f), 9.0f, juce::Justification::centredRight);
    }
    drawCaps (g, "wide", { plot.getRight() - 60, plot.getY() - 2, 60, 12 }, col::dim.withAlpha (0.6f), 8.5f, juce::Justification::centredRight);
    drawCaps (g, "narrow", { plot.getRight() - 60, plot.getBottom() - 12, 60, 12 }, col::dim.withAlpha (0.6f), 8.5f, juce::Justification::centredRight);
    const char* names[3] { "Low  < 300", "Mid  300 - 4k", "High  > 4k" };
    const float gw = plot.getWidth() / 3.0f;
    for (int b = 0; b < 3; ++b)
    {
        auto grp = juce::Rectangle<float> (plot.getX() + (float) b * gw, plot.getY(), gw, plot.getHeight()).reduced (gw * 0.2f, 0);
        const float bw = grp.getWidth() / 2.0f;
        for (int c = 0; c < 2; ++c)   // 0 singing, 1 pauses
        {
            auto colR = juce::Rectangle<float> (grp.getX() + (float) c * bw, grp.getY(), bw, grp.getHeight()).reduced (bw * 0.22f, 0);
            const float v = widthBands[(size_t) (c * 3 + b)];
            if (v > -99.0f)
            {
                auto bar = colR.withTop (Y (v));
                auto cc = c == 0 ? col::text : col::text.withAlpha (0.45f);
                g.setColour (cc); g.fillRoundedRectangle (bar.withSizeKeepingCentre (juce::jmin (bar.getWidth(), 10.0f), bar.getHeight()), 2);
                drawCaps (g, juce::String (v, 1), colR.withY (Y (v) - 15).withHeight (12).expanded (10, 0), col::text, 9.0f);
            }
            if (snap.hasWidthTarget)
            {
                const float t = c == 0 ? snap.widthTargetSing[(size_t) b] : snap.widthTargetGap[(size_t) b];
                glowLine (g, { colR.getX() - 4, Y (t), colR.getRight() + 4, Y (t) }, col::amber, 1.6f);
            }
            drawCaps (g, c == 0 ? "sing" : "pause", colR.withY (plot.getBottom() + 3).withHeight (11).expanded (12, 0), col::dim, 8.0f);
        }
        drawCaps (g, names[b], { plot.getX() + (float) b * gw, plot.getBottom() + 16, gw, 12 }, col::text.withAlpha (0.85f), 9.0f);
    }
    auto leg = juce::Rectangle<float> (r.getX() + 18, r.getY() + 10, 300, 14);
    lampDot (g, { leg.getX() + 5, leg.getCentreY() }, 2.6f, col::violet, true);
    drawCaps (g, "Your output", leg.withTrimmedLeft (14).withWidth (110), col::text.withAlpha (0.8f), 9.0f, juce::Justification::centredLeft);
    lampDot (g, { leg.getX() + 125, leg.getCentreY() }, 2.6f, col::amber, snap.hasWidthTarget);
    drawCaps (g, snap.hasWidthTarget ? "Suno" : "Suno (not in preset)", leg.withTrimmedLeft (134).withWidth (170),
              col::text.withAlpha (0.8f), 9.0f, juce::Justification::centredLeft);
}

// v2.5 SUNO MATCH: one row per aspect, a dot on a -6..+6 dB scale whose centre is the preset's Suno measurement
void Display::paintMatch (juce::Graphics& g, juce::Rectangle<float> r)
{
    auto area = r.reduced (22, 0).withTrimmedTop (34).withTrimmedBottom (26);
    struct Row { const char* name; const char* plus; const char* minus; };
    const Row rows[6] { { "Tone", "brighter", "darker" }, { "Width", "wider", "narrower" }, { "Space", "more space", "drier" },
                        { "Dynamics", "more dynamic", "flatter" }, { "S / Z", "louder s", "softer s" }, { "Loudness", "louder", "quieter" } };
    const float rh = area.getHeight() / 6.0f, nameW = 86, textW = 132;
    const float sx = area.getX() + nameW, sw = area.getWidth() - nameW - textW - 10;
    auto X = [&] (float v) { return sx + sw * (juce::jlimit (-6.0f, 6.0f, v) + 6.0f) / 12.0f; };
    drawCaps (g, "Suno", { X (0) - 30, area.getBottom() + 4, 60, 12 }, col::amber.withAlpha (0.9f), 9.0f);
    for (float d : { -6.0f, -3.0f, 3.0f, 6.0f })
        drawCaps (g, (d > 0 ? "+" : "") + juce::String ((int) d), { X (d) - 16, area.getBottom() + 4, 32, 12 }, col::dim.withAlpha (0.6f), 8.5f);
    g.setColour (col::amber.withAlpha (0.22f)); g.drawVerticalLine ((int) X (0), area.getY(), area.getBottom());
    g.setColour (juce::Colours::white.withAlpha (0.04f)); g.fillRect (juce::Rectangle<float> (X (-1.5f), area.getY(), X (1.5f) - X (-1.5f), area.getHeight()));
    for (int i = 0; i < 6; ++i)
    {
        const float cy = area.getY() + rh * ((float) i + 0.5f);
        drawCaps (g, rows[i].name, { area.getX(), cy - 7, nameW, 14 }, col::text.withAlpha (0.85f), 9.5f, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) cy, sx, sx + sw);
        auto txt = juce::Rectangle<float> (sx + sw + 10, cy - 7, textW, 14);
        if (! match.ok[(size_t) i])
        {
            drawCaps (g, i == 0 || i == 3 || i == 4 ? "listening" : "no data", txt, col::faint, 9.0f, juce::Justification::centredLeft);
            continue;
        }
        const float v = match.v[(size_t) i], a = std::abs (v);
        const auto c = a <= 1.5f ? col::text : (a <= 3.0f ? col::amber.withAlpha (0.75f) : col::amber);
        lampDot (g, { X (v), cy }, 3.4f, c, true);
        const juce::String word = a <= 1.5f ? juce::String ("like Suno") : juce::String (v > 0 ? rows[i].plus : rows[i].minus);
        drawCaps (g, (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB  " + word, txt, c.withAlpha (0.95f), 9.0f, juce::Justification::centredLeft);
    }
    if (match.ok[0])
        drawCaps (g, "Tone error " + juce::String (match.toneErr, 1) + " dB  (vowels, 250 Hz - 12 kHz)", { r.getX() + 18, r.getY() + 10, 300, 14 },
                  col::dim.withAlpha (0.8f), 8.5f, juce::Justification::centredLeft);
}

//==============================================================================
void Meters::update (const SunoChainProcessor::Meters& m)
{
    idleTicks = m.blocks == lastBlocks ? idleTicks + 1 : 0;
    lastBlocks = m.blocks;
    const bool idle = idleTicks > 3, silent = idle || m.in < -70.0f;
    auto fall = [] (float& cur, float v, float rate, float floor) { cur = std::max (v, std::max (floor, cur - rate)); };
    fall (in,  idle ? -100.0f : m.in,  1.5f, -100.0f);
    fall (out, idle ? -100.0f : m.out, 1.5f, -100.0f);
    fall (gr,  silent ? 0.0f : m.gr1 + m.gr2, 0.4f, 0.0f);
    fall (ds,  silent ? 0.0f : m.deess, 0.8f, 0.0f);
    fall (duck, silent ? 0.0f : m.duck, 0.6f, 0.0f);
    repaint();
}

void Meters::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    {
        juce::Path p; p.addRoundedRectangle (r.reduced (0.5f), 16.0f);
        g.setColour (col::screen); g.fillPath (p);
        g.setColour (juce::Colour (0xff2a2a2c)); g.strokePath (p, juce::PathStrokeType (1.0f));
    }
    auto labels = r.removeFromBottom (24);
    r.removeFromTop (12);
    const int n = 5; const float w = r.getWidth() / (float) n;
    auto bar = [&] (int i, const juce::String& name, float v, float lo, float hi, bool down, juce::Colour c1, juce::Colour c2)
    {
        auto lane = juce::Rectangle<float> (r.getX() + (float) i * w, r.getY(), w, r.getHeight());
        auto track = lane.withSizeKeepingCentre (5.0f, lane.getHeight());
        g.setColour (col::chip2); g.fillRoundedRectangle (track, 2.5f);
        const float t = juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
        if (t > 0.01f)
        {
            auto fill = down ? track.withHeight (track.getHeight() * t) : track.withTrimmedTop (track.getHeight() * (1 - t));
            juce::ignoreUnused (c1);
            g.setColour (c2); g.fillRoundedRectangle (fill, 2.5f);
        }
        drawCaps (g, name, juce::Rectangle<float> (lane.getX() - 6, labels.getY() + 1, lane.getWidth() + 12, 16), col::dim.brighter (0.1f), 9.0f);
    };
    bar (0, "In", in, -60, 0, false, col::text, col::text);
    bar (1, "GR", gr, 0, 15, true, col::amber, col::amber);
    bar (2, "S/Z", ds, 0, 18, true, col::amber, col::amber);
    bar (3, "Duck", duck, 0, 24, true, col::soft, col::soft);
    bar (4, "Out", out, -60, 0, false, col::text, col::text);
}

//==============================================================================
Root::Root (SunoChainProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    addAndMakeVisible (display); addAndMakeVisible (meters); addAndMakeVisible (pill);
    for (auto* b : std::initializer_list<juce::Component*> { &loadB, &saveB, &learnB, &clearB, &matchB, &freezeB, &listenSel, &blackSw, &lcd }) addAndMakeVisible (*b);
    loadB.onClick = [this] { loadPreset(); };
    saveB.onClick = [this] { savePreset(); };
    learnB.onClick = [this] { toggleLearn(); };
    clearB.onClick = [this] { proc.clearLearn(); statusText = "Learned voice cleared."; };
    matchB.onClick = [this] { toggleLoudMatch(); };
    listenSel.onChange = [this] (int p) { proc.listenMode = p; proc.markDirty(); };
    listenSel.setTooltip ("LISTEN: ALL = normal, WET = reverb + echo only (with the ducking), WIDTH = width layer only. Not saved.");
    blackSw.setTooltip ("Reverb: SUNO (fitted to the Suno vocals) or BLACK (long, dense, wide, blooming)");
    blackSw.setClickingTogglesState (true);
    matchB.setTooltip ("Measures your output while you play, then holds the gain that matches the Suno vocal's loudness");
    pill.onPrev = [this] { stepPreset (-1); };
    pill.onNext = [this] { stepPreset (1); };
    pill.onClick = [this] { loadPreset(); };

    for (auto* s : { &status, &delayInfo })
    {
        s->setColour (juce::Label::textColourId, col::dim);
        s->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*s);
    }
    status.setFont (inter (11.5f));
    delayInfo.setJustificationType (juce::Justification::centredRight);
    delayInfo.setFont (capsFont (10.5f));

    // v2.7: the face keeps AMOUNT and two knobs per section; every control is on a page of the display
    addKnob ("amount", "Amount", 120, false, true);
    for (auto [id, name] : std::vector<std::pair<const char*, const char*>> {
            { "eqAmount", "Match EQ" }, { "compAmount", "Compression" }, { "revLevel", "Level" }, { "decay", "Decay" },
            { "wLevel", "Width" }, { "wMotion", "Held Notes" }, { "dlyLevel", "Level" }, { "dlyFb", "Feedback" } })
        addKnob (id, name, 64, false, false, juce::String ("m:") + id);
    struct D { const char* id; const char* name; };
    const std::vector<std::vector<D>> pages {
        { { "eqAmount", "Match EQ" }, { "compAmount", "Compression" }, { "deess", "S/Z" }, { "harm", "Saturation" },
          { "eqLow", "Low Boost" }, { "air", "Air" }, { "loudGain", "Loudness" }, { "output", "Output" } },
        { { "dynPeak", "Peak" }, { "dynLeveler", "Rider" }, { "breath", "Breath" }, { "dynSpeed", "Speed" }, { "dynPunch", "Attack" } },
        { { "revLevel", "Level" }, { "decay", "Decay" }, { "predelay", "Pre-delay" }, { "duck", "Ducking" }, { "duckRel", "Duck Rel" },
          { "width", "Width" }, { "wetHpf", "HPF" }, { "wetLpf", "LPF" }, { "rwLow", "Width Low" }, { "rwMid", "Width Mid" }, { "rwHigh", "Width High" } },
        { { "wLevel", "Width" }, { "wTone", "Tone" }, { "wMotion", "Held Notes" }, { "lwLow", "Width Low" }, { "lwMid", "Width Mid" }, { "lwHigh", "Width High" } },
        { { "dlyLevel", "Level" }, { "dlyFb", "Feedback" }, { "dlyTime", "Time" }, { "dlyToRev", "Into Reverb" },
          { "dwLow", "Width Low" }, { "dwMid", "Width Mid" }, { "dwHigh", "Width High" } } };
    pageKnobs.assign (8, {});
    for (size_t pg = 0; pg < pages.size(); ++pg)
        for (const auto& d : pages[pg]) { addKnob (d.id, d.name, 46); pageKnobs[pg + 3].push_back (d.id); }

    {   // page names in the display's bottom strip, section names on the face (click = open its page)
        const char* names[8] { "eq", "stereo", "match", "tone", "dynamics", "space", "width", "echo" };
        for (int i = 0; i < 8; ++i)
        {
            auto* t = pageTabs.add (new TextLink (names[i], 11.0f));
            t->underlineWhenOn = true; t->onClick = [this, i] { setPage (i); };
            addAndMakeVisible (t);
        }
        const char* secs[4] { "tone", "space", "width", "echo" }; const int secPage[4] { 3, 5, 6, 7 };
        for (int i = 0; i < 4; ++i)
        {
            auto* t = sectionTitles.add (new TextLink (secs[i], 12.5f));
            t->setTooltip ("All of " + juce::String (secs[i]) + " on the display");
            const int pg = secPage[i]; t->onClick = [this, pg] { setPage (pg); };
            addAndMakeVisible (t);
        }
    }
    for (auto* l : { &noteL, &syncL })
    {
        l->setFont (inter (10.5f)); l->setColour (juce::Label::textColourId, col::label);
        l->setJustificationType (juce::Justification::centred); l->setInterceptsMouseClicks (false, false); addChildComponent (*l);
    }
    noteL.setText ("note", juce::dontSendNotification); syncL.setText ("sync", juce::dontSendNotification);

    for (auto id : { "rwLow", "rwMid", "rwHigh", "lwLow", "lwMid", "lwHigh", "dwLow", "dwMid", "dwHigh", "output", "wTone", "loudGain", "air" })
        knobs[id]->slider.getProperties().set ("bipolar", true);
    revLamp.colour = echoLamp.colour = col::amber; widthLamp.colour = col::violet;
    for (auto* l : { &revLamp, &widthLamp, &echoLamp }) addAndMakeVisible (*l);
    addAndMakeVisible (syncB);
    revAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "revOn", revLamp);
    widthAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "wOn", widthLamp);
    echoAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "dlyOn", echoLamp);
    syncAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "dlySync", syncB);
    blackAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "revBlack", blackSw);
    freezeB.setClickingTogglesState (true);
    freezeB.setTooltip ("FREEZE: the reverb tail holds forever and nothing new enters it (automatable)");
    freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "revFreeze", freezeB);
    revLamp.setTooltip ("Reverb on/off"); widthLamp.setTooltip ("Vocal width on/off"); echoLamp.setTooltip ("Echo on/off");
    syncB.setTooltip ("Sync the echo to the project tempo");

    noteBox.addItemList ({ "1/4", "1/8 dotted", "1/8", "1/8 triplet", "1/16" }, 1);
    addAndMakeVisible (noteBox);
    noteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "dlyNote", noteBox);

    setSize (kW, kH);
    setPage (0);
}

Root::~Root() { setLookAndFeel (nullptr); }

Knob& Root::addKnob (const juce::String& id, const juce::String& name, int size, bool violet, bool big, const juce::String& keyIn)
{
    const auto key = keyIn.isEmpty() ? id : keyIn;
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->slider.setMouseDragSensitivity (big ? 320 : 220);
    if (violet) k->slider.getProperties().set ("violet", true);
    if (big) k->slider.getProperties().set ("big", true);
    k->label.setText (name.toLowerCase(), juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (inter (size >= 60 ? (big ? 14.0f : 12.0f) : 10.5f, big ? 500 : 400));
    k->label.setColour (juce::Label::textColourId, col::label);
    // v1.12: click the text under a knob to type a value ("300", "-6", "12k"), Enter to set, Esc to cancel
    k->label.setEditable (true, false, true);
    k->label.setMouseCursor (juce::MouseCursor::IBeamCursor);
    k->label.setColour (juce::Label::textWhenEditingColourId, col::amberHi);
    k->label.setColour (juce::Label::backgroundWhenEditingColourId, col::chip);
    k->label.setColour (juce::Label::outlineWhenEditingColourId, col::amber.withAlpha (0.6f));
    {
        auto* sl = &k->slider; auto* lb = &k->label;
        k->label.onEditorShow = [sl, lb]
        {
            if (auto* ed = lb->getCurrentTextEditor())
            {
                auto t = sl->getTextFromValue (sl->getValue()).upToFirstOccurrenceOf (" ", false, false);
                ed->setText (t, false); ed->selectAll(); ed->setJustification (juce::Justification::centred);
            }
        };
        k->label.onTextChange = [sl, lb]
        {
            auto t = lb->getText().trim().toLowerCase().replace (",", ".");
            const bool kilo = t.containsChar ('k');
            const auto num = t.retainCharacters ("-+.0123456789");
            if (num.isNotEmpty())
            {
                double v = num.getDoubleValue() * (kilo ? 1000.0 : 1.0);
                if (sl->getMaximum() <= 24.0 && kilo) v /= 1000.0;   // "k" only means kHz on frequency knobs
                sl->setValue (juce::jlimit (sl->getMinimum(), sl->getMaximum(), v), juce::sendNotificationSync);
            }
            lb->setText ({}, juce::dontSendNotification);   // tick() puts the name back
        };
    }
    addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    auto* prm = proc.apvts.getParameter (id);
    k->slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    k->slider.setTooltip (prm->getName (64) + "  (double-click: default)");
    auto& ref = *k; knobs[key] = std::move (k); return ref;
}

void Root::placeKnob (const juce::String& key, float cx, float cy, int size)
{
    auto& k = *knobs[key];
    const bool big = k.slider.getProperties().contains ("big");
    const int box = size + (big ? 28 : 22);
    k.slider.setBounds (juce::Rectangle<int> (box, box).withCentre ({ (int) cx, (int) cy }));
    const int ly = (int) (cy + (float) size * 0.5f + (big ? 16.0f : 9.0f));
    k.label.setBounds ((int) cx - 64, ly, 128, 18);
}

void Root::setAdvanced (bool) {}   // v2.7: no Advanced module (every control is on a page of the display)

void Root::setPage (int p)
{
    page = p;
    display.setMode (p);
    static const char* titles[8] { "", "", "", "tone", "dynamics", "space", "width", "echo" };
    static const char* notes[8] { "", "", "", "100 % = as learned.  s/z = sibilant match, saturation = harmonics, loudness = the match loudness gain.",
                                  "100 % = as learned.  rider evens the words, breath lowers the breaths between phrases.",
                                  "the reverb.  width low / mid / high = side only (mono stays untouched): < 300 Hz / 300 Hz - 4 kHz / > 4 kHz.",
                                  "the width layer: side only, mono stays untouched.  width low / mid / high = per band.",
                                  "the echo, ducked with the reverb.  width low / mid / high = its width per band." };
    display.pageTitle = titles[p]; display.pageNote = notes[p];
    for (int i = 0; i < pageTabs.size(); ++i) pageTabs[i]->setToggleState (i == p, juce::dontSendNotification);
    const int secPage[4] { 3, 5, 6, 7 };
    for (int i = 0; i < sectionTitles.size(); ++i) sectionTitles[i]->setToggleState (secPage[i] == p || (i == 0 && p == 4), juce::dontSendNotification);
    for (auto& [key, k] : knobs)
    {
        if (key == "amount" || key.startsWith ("m:")) continue;
        bool on = false;
        for (auto& id : pageKnobs[(size_t) p]) on = on || id == key;
        if (key == "dlyTime" && proc.apvts.getRawParameterValue ("dlySync")->load() > 0.5f) on = false;
        k->slider.setVisible (on); k->label.setVisible (on);
    }
    const bool echo = p == 7, sync = proc.apvts.getRawParameterValue ("dlySync")->load() > 0.5f;
    noteBox.setVisible (echo && sync); noteL.setVisible (echo && sync); syncB.setVisible (echo); syncL.setVisible (echo);
    resized(); repaint();
}

void Root::paint (juce::Graphics& g)
{
    // v2.6 / v2.7: the black device (ReKit 2): one body, thin lines between the parts, no wood / screws / metal
    g.setGradientFill (juce::ColourGradient (col::body0, 0, 0, col::body1, 0, (float) kH, false));
    g.fillAll();
    g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawHorizontalLine (0, 0, (float) getWidth());
    g.setColour (col::line);
    g.drawHorizontalLine (140, 48, (float) kW - 48);                       // header
    g.drawHorizontalLine (486, 48, (float) kW - 48);                       // display row
    g.drawHorizontalLine (722, 48, (float) kW - 48);                       // bottom bar
    for (float x : { 324.0f, 600.0f, 876.0f }) g.drawVerticalLine ((int) x, 500.0f, 706.0f);

    // logo: small waveform mark + name
    {
        const float x = 116, cy = 76;
        const float hs[] { 10, 22, 34, 22, 14 };
        g.setColour (col::text);
        for (int i = 0; i < 5; ++i)
        {
            const float bx = x + (float) i * 6.0f, h = hs[i];
            g.fillRoundedRectangle (bx - 1.0f, cy - h / 2, 2.0f, h, 1.0f);
        }
        g.setFont (inter (21.0f, 500)); g.drawText ("suno chain", juce::Rectangle<float> (x + 44, cy - 18, 230, 28), juce::Justification::centredLeft, false);
        g.setColour (col::faint); g.setFont (mono (10.0f)); g.drawText ("2.8.0", juce::Rectangle<float> (x + 174, cy - 12, 60, 16), juce::Justification::centredLeft, false);
        g.setColour (col::dim); g.setFont (inter (10.5f)); g.drawText ("by pouria motabean", juce::Rectangle<float> (x + 45, cy + 9, 230, 14), juce::Justification::centredLeft, false);
    }
}

void Root::resized()
{
    pill.setBounds (410, 51, 300, 50);
    loadB.setBounds (716, 60, 80, 32);
    saveB.setBounds (802, 60, 80, 32);
    learnB.setBounds (888, 60, 112, 32);
    clearB.setBounds (1006, 60, 80, 32);

    // the display (graphs and the pages of every section), meters, AMOUNT
    const juce::Rectangle<int> disp (48, 160, 804, 306);
    display.setBounds (disp);
    {   // page names in the display's bottom strip
        int x = disp.getX() + 18; const int y = disp.getBottom() - 32;
        for (int i = 0; i < pageTabs.size(); ++i)
        {
            const int w = (int) juce::GlyphArrangement::getStringWidth (inter (12.1f, 600), pageTabs[i]->getButtonText()) + 26;
            if (i == 3) x += 30;   // graphs | sections
            pageTabs[i]->setBounds (x, y, w, 28); x += w;
        }
    }
    meters.setBounds (872, 160, 112, 306);
    placeKnob ("amount", 1078, 300, 116);

    // the face: four sections, two knobs each; the name opens the section's page
    const int x0[4] { 48, 324, 600, 876 };
    for (int i = 0; i < 4; ++i)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (inter (13.75f, 600), sectionTitles[i]->getButtonText()) + 16;
        sectionTitles[i]->setBounds (x0[i] + 14, 498, w, 26);
    }
    revLamp.setBounds (x0[1] + 76, 501, 20, 20);
    widthLamp.setBounds (x0[2] + 76, 501, 20, 20);
    echoLamp.setBounds (x0[3] + 70, 501, 20, 20);
    blackSw.setBounds (x0[1] + 150, 494, 110, 34);
    delayInfo.setBounds (x0[3] + 90, 502, 172, 18);
    const float ky = 618;
    const char* face[4][2] { { "m:eqAmount", "m:compAmount" }, { "m:revLevel", "m:decay" }, { "m:wLevel", "m:wMotion" }, { "m:dlyLevel", "m:dlyFb" } };
    for (int i = 0; i < 4; ++i) { placeKnob (face[i][0], (float) x0[i] + 78.0f, ky, 64); placeKnob (face[i][1], (float) x0[i] + 198.0f, ky, 64); }

    // pages: one or two rows of knobs inside the display
    {
        auto row = [&] (const std::vector<juce::String>& keys, float y)
        {
            const float left = (float) disp.getX() + 24.0f, w = (float) disp.getWidth() - 48.0f, step = w / (float) juce::jmax (1, (int) keys.size());
            for (size_t i = 0; i < keys.size(); ++i)
            {
                const float cx = left + step * ((float) i + 0.5f);
                if (keys[i] == "#time")
                {
                    if (knobs.count ("dlyTime")) placeKnob ("dlyTime", cx, y, 46);
                    noteBox.setBounds ((int) cx - 44, (int) y - 16, 88, 32); noteL.setBounds ((int) cx - 50, (int) y + 32, 100, 16);
                }
                else if (keys[i] == "#sync") { syncB.setBounds ((int) cx - 26, (int) y - 26, 52, 52); syncL.setBounds ((int) cx - 50, (int) y + 32, 100, 16); }
                else placeKnob (keys[i], cx, y, 46);
            }
        };
        const float one = (float) disp.getY() + 150.0f, r1 = (float) disp.getY() + 112.0f, r2 = (float) disp.getY() + 214.0f;
        row ({ "eqAmount", "compAmount", "deess", "harm" }, r1);       row ({ "eqLow", "air", "loudGain", "output" }, r2);
        row ({ "dynPeak", "dynLeveler", "breath", "dynSpeed", "dynPunch" }, one);
        row ({ "revLevel", "decay", "predelay", "duck", "duckRel" }, r1); row ({ "width", "wetHpf", "wetLpf", "rwLow", "rwMid", "rwHigh" }, r2);
        row ({ "wLevel", "wTone", "wMotion" }, r1);                    row ({ "lwLow", "lwMid", "lwHigh" }, r2);
        row ({ "dlyLevel", "dlyFb", "#time", "#sync" }, r1);           row ({ "dlyToRev", "dwLow", "dwMid", "dwHigh" }, r2);
    }

    status.setBounds (70, 742, 400, 20);
    freezeB.setBounds (520, 738, 110, 30);
    lcd.setVisible (false);
    listenSel.setBounds (642, 738, 186, 30);
    matchB.setBounds (842, 738, 210, 30);
    advancedB.setVisible (false);
}

void Root::tick()
{
    const bool learning = proc.isLearning();
    learnB.setButtonText (learning ? "STOP " + juce::String (proc.learnSeconds(), 0) + " S" : (proc.hasLearned() ? "RE-LEARN" : "LEARN"));
    learnB.setToggleState (learning, juce::dontSendNotification);
    learnB.recording = learning;
    if (learning) learnB.repaint();
    if (! learning && statusText.startsWith ("Learning")) { juce::String msg; proc.stopLearn (msg); statusText = msg; }
    {   // LISTEN: the word blinks while soloing so a bounce is never made by accident
        const int lm = proc.listenMode.load();
        listenSel.setPosition (lm);
        if (lm != 0) listenSel.repaint();
    }
    {   // MATCH LOUDNESS: lamp + seconds while measuring, then the held gain
        const bool matching = proc.isLoudMatching();
        const float lg = proc.apvts.getRawParameterValue ("loudGain")->load();
        const auto t = matching ? "MEASURING " + juce::String (proc.loudSeconds(), 0) + " S - STOP"
                                : (std::abs (lg) > 0.05f ? "LOUDNESS " + juce::String (lg >= 0 ? "+" : "") + juce::String (lg, 1) + " DB" : juce::String ("MATCH LOUDNESS"));
        if (matchB.getButtonText() != t) matchB.setButtonText (t);
        const bool lit = matching || std::abs (lg) > 0.05f;
        if (matchB.lit != lit || matchB.blinking != matching) { matchB.lit = lit; matchB.blinking = matching; matchB.repaint(); }
        if (matching) matchB.repaint();
    }
    const auto pn = proc.hasPreset() ? proc.getPresetName() : juce::String ("Suno Lead Reference");
    if (pill.name != pn) { pill.name = pn; pill.repaint(); }
    if (statusText.isEmpty())
        statusText = ! proc.hasLearned() ? "Press LEARN and play 20 s or more of your dry vocal (with some s/z)." : "Ready.";
    {   // v1.14: no status screen; a message shows as a quiet line for a few seconds (the first-run hint and
        // LEARN / MATCH in progress stay while they matter)
        if (statusText != lastStatus) { lastStatus = statusText; statusAge = 0; }
        else ++statusAge;
        const bool sticky = ! proc.hasLearned() || proc.isLearning() || proc.isLoudMatching();
        const bool show = statusText != "Ready." && (sticky || statusAge < 20 * 8);
        status.setText (show ? statusText : juce::String(), juce::dontSendNotification);
    }
    advancedB.setToggleState (proc.advancedOpen, juce::dontSendNotification);
    if (freezeB.lit != freezeB.getToggleState()) { freezeB.lit = freezeB.getToggleState(); freezeB.repaint(); }

    proc.pumpMeters();   // v2.0.1: the WIDTH analysis runs here, not in the audio callback
    auto m = proc.getMeters();
    meters.update (m);
    display.setWidth (m.widthBands);
    const bool sync = proc.apvts.getRawParameterValue ("dlySync")->load() > 0.5f;
    if (sync != lastSync) { lastSync = sync; setPage (page); }   // echo page: note box or time knob
    delayInfo.setText (juce::String ((int) std::round (m.delayMs)) + " ms" + (sync ? "  @ " + juce::String (m.bpm, 1) + " bpm" : juce::String()),
                       juce::dontSendNotification);

    // sections that are off: their knobs go dark (still adjustable)
    auto setOff = [this] (std::initializer_list<const char*> ids, bool off)
    {
        for (auto* id : ids)
        {
            for (const auto& key : { juce::String (id), "m:" + juce::String (id) })   // the page knob and its copy on the face
            {
                auto it = knobs.find (key);
                if (it == knobs.end()) continue;
                auto& s = it->second->slider;
                if ((bool) s.getProperties().getWithDefault ("off", false) != off) { s.getProperties().set ("off", off); s.repaint(); }
            }
        }
    };
    setOff ({ "revLevel", "decay", "predelay", "duck", "width", "wetHpf", "wetLpf", "duckRel" }, ! revLamp.getToggleState());
    setOff ({ "rwLow", "rwMid", "rwHigh" }, ! revLamp.getToggleState() || blackSw.getToggleState());   // BLACK has its own width per band
    setOff ({ "wLevel", "wTone", "wMotion", "lwLow", "lwMid", "lwHigh" }, ! widthLamp.getToggleState());
    setOff ({ "dlyLevel", "dlyFb", "dlyTime", "dlyToRev", "dwLow", "dwMid", "dwHigh" }, ! echoLamp.getToggleState());

    // knob labels show the value while you touch them
    for (auto& [id, k] : knobs)
    {
        if (k->label.isBeingEdited()) continue;
        const bool show = k->slider.isMouseButtonDown() || k->slider.isMouseOver() || k->label.isMouseOver();
        const auto t = show ? k->slider.getTextFromValue (k->slider.getValue()) : k->name.toLowerCase();
        if ((bool) k->slider.getProperties().getWithDefault ("hov", false) != show)
        { k->slider.getProperties().set ("hov", show); k->slider.repaint(); }   // arc glows while touched
        if (k->label.getText() != t)
        {
            k->label.setText (t, juce::dontSendNotification);
            k->label.setColour (juce::Label::textColourId, show ? col::text : col::label);
        }
    }
    if (++ticks % 4 == 0) { display.setCurves (proc.getCurves()); display.setMatch (proc.getMatch()); }
}

void Root::loadPreset()
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

void Root::savePreset()
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

void Root::stepPreset (int dir)
{
    auto files = SunoChainProcessor::presetFolder().findChildFiles (juce::File::findFiles, false, "*.json");
    if (files.isEmpty()) { statusText = "No presets in " + SunoChainProcessor::presetFolder().getFullPathName(); return; }
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    int idx = files.indexOf (proc.currentPresetFile);
    idx = idx < 0 ? (dir > 0 ? 0 : files.size() - 1) : (idx + dir + files.size()) % files.size();
    juce::String err;
    statusText = proc.loadPresetFile (files[idx], err) ? "Preset loaded: " + files[idx].getFileName() : err;
}

void Root::toggleLoudMatch()
{
    if (! proc.isLoudMatching())
    {
        proc.startLoudMatch();
        statusText = "Measuring loudness... play the vocal (10 s or more, a loud part too), then press STOP.";
    }
    else
    {
        juce::String msg; proc.stopLoudMatch (msg); statusText = msg;
    }
}

void Root::toggleLearn()
{
    if (! proc.isLearning())
    {
        proc.startLearn();
        statusText = "Learning... play your dry vocal (20-60 s, tuning plugin on), then press STOP.";
    }
    else
    {
        juce::String msg; proc.stopLearn (msg); statusText = msg;
    }
}
} // namespace ui

//==============================================================================
SunoChainEditor::SunoChainEditor (SunoChainProcessor& p) : AudioProcessorEditor (&p), proc (p), root (p)
{
    addAndMakeVisible (root);
    root.onAdvanced = [this] (bool open) { applyLayout (open); };
    setResizable (true, true);
    setConstrainer (&constrainer);
    applyLayout (proc.advancedOpen);
    startTimerHz (20);
}

SunoChainEditor::~SunoChainEditor() { stopTimer(); }

void SunoChainEditor::applyLayout (bool advanced)
{
    const int lh = ui::kH;
    juce::ignoreUnused (advanced); logicalW = ui::kW;   // v2.7: one size
    root.setSize (logicalW, lh);
    const float s = juce::jlimit (0.6f, 1.6f, proc.uiScale);
    constrainer.setFixedAspectRatio ((double) logicalW / (double) lh);
    constrainer.setSizeLimits ((int) ((float) logicalW * 0.6f), (int) ((float) lh * 0.6f), (int) ((float) logicalW * 1.6f), (int) ((float) lh * 1.6f));
    setSize (juce::roundToInt ((float) logicalW * s), juce::roundToInt ((float) lh * s));
    resized();
}

void SunoChainEditor::resized()
{
    const float s = (float) getWidth() / (float) logicalW;
    root.setTransform (juce::AffineTransform::scale (s));
    if (getWidth() > 0) proc.uiScale = s;
}
