#include "PluginEditor.h"
#include "gui/Assets.h"

namespace ui
{
namespace col
{
const juce::Colour amber  { 0xffffa640 };
const juce::Colour amberHi{ 0xffffd9a0 };
const juce::Colour violet { 0xffb48cff };
const juce::Colour green  { 0xff6fd18a };
const juce::Colour cream  { 0xffe8e1d6 };   // your voice curve
const juce::Colour text   { 0xffd2cbc1 };
const juce::Colour dim    { 0xff8a847c };
const juce::Colour faint  { 0xff4a4744 };
}

//==============================================================================
const Images& Images::get()
{
    static Images im = []
    {
        Images i;
        auto load = [] (const unsigned char* d, std::size_t n) { return juce::ImageCache::getFromMemory (d, (int) n); };
        i.faceplate  = load (assets::faceplate_jpg, assets::faceplate_jpgSize);
        i.sidePanel  = load (assets::side_panel_jpg, assets::side_panel_jpgSize);
        i.knob       = load (assets::knob_std_png, assets::knob_std_pngSize);
        i.knobLarge  = load (assets::knob_large_png, assets::knob_large_pngSize);
        i.pillLong   = load (assets::pill_long_png, assets::pill_long_pngSize);
        i.pillShort  = load (assets::pill_short_png, assets::pill_short_pngSize);
        i.buttonUp   = load (assets::button_up_png, assets::button_up_pngSize);
        i.buttonDown = load (assets::button_down_png, assets::button_down_pngSize);
        i.slot       = load (assets::slot_small_png, assets::slot_small_pngSize);
        return i;
    }();
    return im;
}

// side module image (1024 x 1536): dividers at 24.2 / 48 / 71.6 % of the height -> logical y 194 / 384 / 573
constexpr float kSec[5] { 0.0f, 194.0f, 384.0f, 573.0f, 800.0f };

juce::Font capsFont (float size, bool bold)
{
    juce::Font f (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    f.setExtraKerningFactor (0.16f);
    return f;
}

void drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour c, float size,
               juce::Justification j, bool bold)
{
    g.setColour (c); g.setFont (capsFont (size, bold));
    g.drawText (text.toUpperCase(), area, j, false);
}

// soft glow line: a lamp core with a halo
static void glowLine (juce::Graphics& g, juce::Line<float> l, juce::Colour c, float core, float intensity = 1.0f)
{
    g.setColour (c.withAlpha (0.10f * intensity)); g.drawLine (l, core * 5.0f);
    g.setColour (c.withAlpha (0.22f * intensity)); g.drawLine (l, core * 2.6f);
    g.setColour (c.interpolatedWith (juce::Colours::white, 0.25f).withAlpha (0.95f * intensity)); g.drawLine (l, core);
}

static void glowPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float core)
{
    using PST = juce::PathStrokeType;
    g.setColour (c.withAlpha (0.08f)); g.strokePath (p, PST (core * 6.0f, PST::curved, PST::rounded));
    g.setColour (c.withAlpha (0.18f)); g.strokePath (p, PST (core * 3.0f, PST::curved, PST::rounded));
    g.setColour (c.withAlpha (0.95f)); g.strokePath (p, PST (core, PST::curved, PST::rounded));
}

static void lampDot (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool on)
{
    if (! on)
    {
        g.setColour (juce::Colour (0xff1a1918)); g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
        g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c), 0.8f);
        return;
    }
    juce::ColourGradient halo (col.withAlpha (0.45f), c.x, c.y, col.withAlpha (0.0f), c.x + r * 4.5f, c.y, true);
    g.setGradientFill (halo); g.fillEllipse (juce::Rectangle<float> (r * 9, r * 9).withCentre (c));
    g.setColour (col); g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (c));
    g.setColour (col.interpolatedWith (juce::Colours::white, 0.6f));
    g.fillEllipse (juce::Rectangle<float> (r * 0.9f, r * 0.9f).withCentre (c.translated (-r * 0.2f, -r * 0.2f)));
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff17171a));
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::amber.withAlpha (0.85f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff111111));
    setColour (juce::ComboBox::textColourId, col::amber);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff17171a));
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const bool big = s.getProperties().contains ("big");
    const bool off = (bool) s.getProperties().getWithDefault ("off", false);
    const auto& im = Images::get();
    auto b = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float margin = big ? 14.0f : 11.0f;
    const float d = juce::jmin (b.getWidth(), b.getHeight()) - 2 * margin;
    auto k = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
    const float r = d * 0.5f, cx = k.getCentreX(), cy = k.getCentreY();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // static drop shadow (light from above)
    {
        auto sh = k.expanded (d * 0.06f).translated (0, d * 0.09f);
        juce::ColourGradient grad (juce::Colours::black.withAlpha (0.55f), sh.getCentreX(), sh.getCentreY(),
                                   juce::Colours::black.withAlpha (0.0f), sh.getCentreX() + sh.getWidth() * 0.56f, sh.getCentreY(), true);
        g.setGradientFill (grad); g.fillEllipse (sh.expanded (d * 0.08f));
    }
    // static machined body (never rotates: the lighting stays put, only the indicator turns)
    g.drawImage (big ? im.knobLarge : im.knob, k, juce::RectanglePlacement::stretchToFit);

    const auto c = s.getProperties().contains ("violet") ? col::violet : col::amber;
    const float a = start + pos * (end - start);
    const float cap = big ? r * 0.74f : r * 0.80f;   // radius of the turning cap inside knurl / bezel
    auto pt = [&] (float rad) { return juce::Point<float> (cx + rad * std::sin (a), cy - rad * std::cos (a)); };

    if (big)
    {   // value arc around the bezel, glowing
        const float ar = r + 6.0f;
        juce::Path track; track.addCentredArc (cx, cy, ar, ar, 0, start, end, true);
        g.setColour (juce::Colours::white.withAlpha (0.05f)); g.strokePath (track, juce::PathStrokeType (2.0f));
        juce::Path val; val.addCentredArc (cx, cy, ar, ar, 0, start, a, true);
        if (! off) glowPath (g, val, c, 2.2f);
    }
    else
    {   // thin value arc: dim at rest, glowing like a lamp while you touch the knob.
        // Bipolar knobs (width bands, Output, Tone) start from their 0 dB point.
        const float ar = r + 4.0f;
        const bool hot = s.isMouseOverOrDragging() && ! off;
        juce::Path track; track.addCentredArc (cx, cy, ar, ar, 0, start, end, true);
        g.setColour (juce::Colours::white.withAlpha (0.045f)); g.strokePath (track, juce::PathStrokeType (1.4f));
        float from = start;
        if (s.getProperties().contains ("bipolar"))
            from = start + (float) s.valueToProportionOfLength (0.0) * (end - start);
        juce::Path val; val.addCentredArc (cx, cy, ar, ar, 0, juce::jmin (from, a), juce::jmax (from, a), true);
        if (off) { g.setColour (col::faint.withAlpha (0.6f)); g.strokePath (val, juce::PathStrokeType (1.4f)); }
        else if (hot) glowPath (g, val, c, 1.6f);
        else { g.setColour (c.withAlpha (0.10f)); g.strokePath (val, juce::PathStrokeType (4.0f));
               g.setColour (c.withAlpha (0.45f)); g.strokePath (val, juce::PathStrokeType (1.4f)); }
    }
    // lamp light spilling onto the metal around the indicator
    if (! off)
    {
        auto mid = pt (cap * 0.62f);
        juce::ColourGradient spill (c.withAlpha (0.20f), mid.x, mid.y, c.withAlpha (0.0f), mid.x + cap * 0.75f, mid.y, true);
        g.setGradientFill (spill); g.fillEllipse (juce::Rectangle<float> (cap * 1.5f, cap * 1.5f).withCentre (mid));
    }
    const float core = big ? 2.6f : juce::jmax (1.4f, d * 0.03f);
    juce::Line<float> ind (pt (cap * 0.30f), pt (cap * 0.94f));
    if (off) { g.setColour (col::faint); g.drawLine (ind, core); }
    else glowLine (g, ind, c, core);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto& im = Images::get();
    auto r = juce::Rectangle<float> ((float) w, (float) h);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (im.slot, r, juce::RectanglePlacement::stretchToFit);
    const float cx = r.getRight() - 14.0f, cy = r.getCentreY();
    juce::Path p; p.startNewSubPath (cx - 3.5f, cy - 1.5f); p.lineTo (cx, cy + 2.0f); p.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour ((box.isMouseOver() ? col::amberHi : col::amber).withAlpha (0.9f));
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& l)
{
    l.setBounds (4, 0, box.getWidth() - 20, box.getHeight());
    l.setFont (getComboBoxFont (box));
    l.setJustificationType (juce::Justification::centred);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return juce::Font (juce::FontOptions (14.0f)); }

juce::PopupMenu::Options LookAndFeel::getOptionsForComboBoxPopupMenu (juce::ComboBox& box, juce::Label& l)
{
    return LookAndFeel_V4::getOptionsForComboBoxPopupMenu (box, l).withStandardItemHeight (22);
}

//==============================================================================
void TextLink::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    auto c = on ? onColour : (over ? col::cream : col::text.withAlpha (0.85f));
    if (down) c = c.brighter (0.2f);
    drawCaps (g, getButtonText(), r, c, fontSize);
    if (recording)
    {   // blinking "recording" lamp left of the text
        const bool lit = (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
        const float tw = (float) getButtonText().length() * fontSize * 0.78f;
        lampDot (g, { r.getCentreX() - tw * 0.5f - 10.0f, r.getCentreY() }, 3.6f, juce::Colour (0xffff5a3c), lit);
    }
    if (underlineWhenOn && on)
    {
        const float w = juce::jmin (r.getWidth() * 0.6f, (float) getButtonText().length() * fontSize * 0.75f);
        glowLine (g, { r.getCentreX() - w / 2, r.getBottom() - 2.0f, r.getCentreX() + w / 2, r.getBottom() - 2.0f }, onColour, 1.0f, 0.8f);
    }
}

void Lamp::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat();
    lampDot (g, r.getCentre(), juce::jmin (r.getWidth(), r.getHeight()) * 0.16f, colour, getToggleState());
    if (over) { g.setColour (juce::Colours::white.withAlpha (0.05f)); g.fillEllipse (r.reduced (r.getWidth() * 0.15f)); }
}

void PushLamp::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto& im = Images::get();
    auto r = getLocalBounds().toFloat();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    juce::ColourGradient sh (juce::Colours::black.withAlpha (0.5f), r.getCentreX(), r.getCentreY() + r.getHeight() * 0.08f,
                             juce::Colours::black.withAlpha (0.0f), r.getCentreX() + r.getWidth() * 0.55f, r.getCentreY(), true);
    g.setGradientFill (sh); g.fillEllipse (r.reduced (2).translated (0, 3));
    g.drawImage (getToggleState() || down ? im.buttonDown : im.buttonUp, r.reduced (4), juce::RectanglePlacement::stretchToFit);
    lampDot (g, r.getCentre(), r.getWidth() * 0.07f, col::amber, getToggleState());
    if (over) { g.setColour (juce::Colours::white.withAlpha (0.04f)); g.fillEllipse (r.reduced (r.getWidth() * 0.14f)); }
}

//==============================================================================
void PresetPill::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (Images::get().pillLong, r, juce::RectanglePlacement::stretchToFit);
    auto inner = r.reduced (r.getHeight() * 0.9f, 0);
    g.setColour (col::amber); g.setFont (juce::Font (juce::FontOptions (17.0f)));
    g.drawText (name, inner, juce::Justification::centred, true);
    auto chev = [&] (float x, float dir)
    {
        const float cy = r.getCentreY();
        juce::Path p; p.startNewSubPath (x + 2.5f * dir, cy - 5); p.lineTo (x - 2.5f * dir, cy); p.lineTo (x + 2.5f * dir, cy + 5);
        g.setColour (col::amber.withAlpha (0.75f));
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
    if (mode == 1) paintWidth (g, r); else paintEq (g, r);
    // glass: reflection, inner shadow at the top and left, faint bottom highlight
    juce::ColourGradient refl (juce::Colours::white.withAlpha (0.055f), r.getX(), r.getY(),
                               juce::Colours::white.withAlpha (0.0f), r.getX() + r.getWidth() * 0.45f, r.getY() + r.getHeight() * 0.9f, false);
    g.setGradientFill (refl); g.fillRect (r);
    juce::ColourGradient top (juce::Colours::black.withAlpha (0.45f), 0, r.getY(), juce::Colours::black.withAlpha (0.0f), 0, r.getY() + 14, false);
    g.setGradientFill (top); g.fillRect (r.withHeight (14));
    juce::ColourGradient left (juce::Colours::black.withAlpha (0.35f), r.getX(), 0, juce::Colours::black.withAlpha (0.0f), r.getX() + 10, 0, false);
    g.setGradientFill (left); g.fillRect (r.withWidth (10));
    g.setColour (juce::Colours::white.withAlpha (0.06f)); g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX() + 6, r.getRight() - 6);
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
        juce::ColourGradient gf (col::green.withAlpha (0.32f), 0, plot.getY(), col::green.withAlpha (0.04f), 0, plot.getBottom(), false);
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
                auto cc = c == 0 ? col::violet : col::violet.withAlpha (0.5f);
                g.setColour (cc.withAlpha (0.15f)); g.fillRoundedRectangle (bar.expanded (3), 4);
                juce::ColourGradient bg (cc, 0, bar.getY(), cc.withAlpha (0.25f), 0, bar.getBottom(), false);
                g.setGradientFill (bg); g.fillRoundedRectangle (bar, 2);
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
    auto labels = r.removeFromBottom (24);
    r.removeFromTop (12);
    const int n = 5; const float w = r.getWidth() / (float) n;
    auto bar = [&] (int i, const juce::String& name, float v, float lo, float hi, bool down, juce::Colour c1, juce::Colour c2)
    {
        auto lane = juce::Rectangle<float> (r.getX() + (float) i * w, r.getY(), w, r.getHeight());
        auto track = lane.withSizeKeepingCentre (5.0f, lane.getHeight());
        g.setColour (juce::Colour (0xff050505)); g.fillRoundedRectangle (track.expanded (1), 3);
        g.setColour (juce::Colours::white.withAlpha (0.04f)); g.drawRoundedRectangle (track.expanded (1), 3, 0.6f);
        const float t = juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
        if (t > 0.01f)
        {
            auto fill = down ? track.withHeight (track.getHeight() * t) : track.withTrimmedTop (track.getHeight() * (1 - t));
            g.setColour (c2.withAlpha (0.18f)); g.fillRoundedRectangle (fill.expanded (2.5f), 3);
            juce::ColourGradient grad (c2, 0, track.getY(), c1, 0, track.getBottom(), false);
            g.setGradientFill (grad); g.fillRoundedRectangle (fill, 2);
        }
        drawCaps (g, name, juce::Rectangle<float> (lane.getX() - 6, labels.getY() + 1, lane.getWidth() + 12, 16), col::dim.brighter (0.1f), 9.0f);
    };
    bar (0, "In", in, -60, 0, false, col::green, col::amber);
    bar (1, "GR", gr, 0, 15, true, col::amber, col::amber);
    bar (2, "S/Z", ds, 0, 18, true, col::amber, col::amber);
    bar (3, "Duck", duck, 0, 24, true, col::green, col::green);
    bar (4, "Out", out, -60, 0, false, col::green, col::amber);
}

//==============================================================================
Root::Root (SunoChainProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    addAndMakeVisible (display); addAndMakeVisible (meters); addAndMakeVisible (pill);
    for (auto* b : { &loadB, &saveB, &learnB, &clearB, &eqTab, &widthTab, &advancedB }) addAndMakeVisible (*b);
    eqTab.underlineWhenOn = widthTab.underlineWhenOn = true;
    eqTab.setToggleState (true, juce::dontSendNotification);
    eqTab.onClick = [this] { display.setMode (0); eqTab.setToggleState (true, juce::dontSendNotification); widthTab.setToggleState (false, juce::dontSendNotification); };
    widthTab.onClick = [this] { display.setMode (1); eqTab.setToggleState (false, juce::dontSendNotification); widthTab.setToggleState (true, juce::dontSendNotification); };
    loadB.onClick = [this] { loadPreset(); };
    saveB.onClick = [this] { savePreset(); };
    learnB.onClick = [this] { toggleLearn(); };
    clearB.onClick = [this] { proc.clearLearn(); statusText = "Learned voice cleared."; };
    advancedB.onClick = [this] { setAdvanced (! proc.advancedOpen); };
    pill.onPrev = [this] { stepPreset (-1); };
    pill.onNext = [this] { stepPreset (1); };
    pill.onClick = [this] { loadPreset(); };

    for (auto* s : { &status, &delayInfo })
    {
        s->setColour (juce::Label::textColourId, col::dim);
        s->setInterceptsMouseClicks (false, false);
        addAndMakeVisible (*s);
    }
    status.setFont (juce::Font (juce::FontOptions (13.5f)));
    delayInfo.setJustificationType (juce::Justification::centredRight);
    delayInfo.setFont (capsFont (10.5f));

    // main controls
    addKnob ("amount", "Amount", 150, false, true);
    addKnob ("eqAmount", "Match EQ", 64); addKnob ("compAmount", "Compression", 64);
    addKnob ("deess", "S/Z", 64);         addKnob ("satMix", "Saturation", 64);
    addKnob ("revLevel", "Level", 64);    addKnob ("decay", "Decay", 64);
    addKnob ("predelay", "Pre-delay", 64); addKnob ("duck", "Ducking", 64);
    addKnob ("wLevel", "Width", 64, true); addKnob ("wTone", "Tone", 64, true); addKnob ("wMotion", "Held Notes", 64, true);
    addKnob ("dlyLevel", "Level", 64);    addKnob ("dlyFb", "Feedback", 64); addKnob ("dlyTime", "Time", 64);
    // Advanced drawer
    struct D { const char* id; const char* name; bool violet; };
    for (const auto& d : std::vector<D> {
            { "eqLow", "Low Boost", false }, { "satDrive", "Sat Drive", false }, { "output", "Output", false },
            { "dynPeak", "Peak", false }, { "dynLeveler", "Leveler", false }, { "dynSpeed", "Speed", false }, { "dynPunch", "Attack", false },
            { "width", "Width", false }, { "wetHpf", "HPF", false }, { "wetLpf", "LPF", false }, { "duckRel", "Duck Rel", false },
            { "rwLow", "Low", false }, { "rwMid", "Mid", false }, { "rwHigh", "High", false },
            { "lwLow", "Low", true }, { "lwMid", "Mid", true }, { "lwHigh", "High", true },
            { "dlyToRev", "> Reverb", false }, { "dwLow", "Low", false }, { "dwMid", "Mid", false }, { "dwHigh", "High", false } })
    {
        addKnob (d.id, d.name, 42, d.violet);
        drawerIds.push_back (d.id);
    }

    for (auto id : { "rwLow", "rwMid", "rwHigh", "lwLow", "lwMid", "lwHigh", "dwLow", "dwMid", "dwHigh", "output", "wTone" })
        knobs[id]->slider.getProperties().set ("bipolar", true);
    revLamp.colour = echoLamp.colour = col::amber; widthLamp.colour = col::violet;
    for (auto* l : { &revLamp, &widthLamp, &echoLamp }) addAndMakeVisible (*l);
    addAndMakeVisible (syncB);
    revAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "revOn", revLamp);
    widthAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "wOn", widthLamp);
    echoAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "dlyOn", echoLamp);
    syncAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "dlySync", syncB);
    revLamp.setTooltip ("Reverb on/off"); widthLamp.setTooltip ("Vocal width on/off"); echoLamp.setTooltip ("Echo on/off");
    syncB.setTooltip ("Sync the echo to the project tempo");

    noteBox.addItemList ({ "1/4", "1/8 dotted", "1/8", "1/8 triplet", "1/16" }, 1);
    addAndMakeVisible (noteBox);
    noteAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "dlyNote", noteBox);

    setSize (kW, kH);
    setAdvanced (proc.advancedOpen);
}

Root::~Root() { setLookAndFeel (nullptr); }

Knob& Root::addKnob (const juce::String& id, const juce::String& name, int size, bool violet, bool big)
{
    auto k = std::make_unique<Knob>();
    k->name = name;
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k->slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    k->slider.setMouseDragSensitivity (big ? 320 : 220);
    if (violet) k->slider.getProperties().set ("violet", true);
    if (big) k->slider.getProperties().set ("big", true);
    k->label.setText (name.toUpperCase(), juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (capsFont (size >= 60 ? (big ? 14.0f : 12.5f) : 10.5f));
    k->label.setColour (juce::Label::textColourId, col::text.withAlpha (0.82f));
    k->label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (k->slider); addAndMakeVisible (k->label);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k->slider);
    auto* prm = proc.apvts.getParameter (id);
    k->slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    k->slider.setTooltip (prm->getName (64) + "  (double-click: default)");
    auto& ref = *k; knobs[id] = std::move (k); return ref;
}

void Root::placeKnob (const juce::String& id, float cx, float cy, int size)
{
    auto& k = *knobs[id];
    const bool big = k.slider.getProperties().contains ("big");
    const int box = size + (big ? 28 : 22);
    k.slider.setBounds (juce::Rectangle<int> (box, box).withCentre ({ (int) cx, (int) cy }));
    const int ly = (int) (cy + (float) size * 0.5f + (big ? 16.0f : 9.0f));
    k.label.setBounds ((int) cx - 64, ly, 128, 18);
}

void Root::setAdvanced (bool open)
{
    proc.advancedOpen = open;
    advancedB.setButtonText (open ? "ADVANCED  -" : "ADVANCED  +");
    advancedB.setToggleState (open, juce::dontSendNotification);
    for (auto& id : drawerIds) { knobs[id]->slider.setVisible (open); knobs[id]->label.setVisible (open); }
    if (onAdvanced) onAdvanced (open);
    repaint();
}

void Root::paint (juce::Graphics& g)
{
    const auto& im = Images::get();
    g.fillAll (juce::Colour (0xff0d0d0e));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (im.faceplate, juce::Rectangle<float> (0, 0, (float) kW, (float) kH), juce::RectanglePlacement::stretchToFit);
    if (proc.advancedOpen)
    {
        g.drawImage (im.sidePanel, juce::Rectangle<float> ((float) kW, 0, (float) kSideW, (float) kH), juce::RectanglePlacement::stretchToFit);
        juce::ColourGradient seam (juce::Colours::black.withAlpha (0.6f), (float) kW, 0, juce::Colours::black.withAlpha (0.0f), (float) kW + 16.0f, 0, false);
        g.setGradientFill (seam); g.fillRect (kW, 0, 16, kH);
    }

    // logo: small waveform mark + name
    {
        const float x = 116, cy = 76;
        const float hs[] { 10, 22, 34, 22, 14 };
        for (int i = 0; i < 5; ++i)
        {
            const float bx = x + (float) i * 6.0f, h = hs[i];
            glowLine (g, { bx, cy - h / 2, bx, cy + h / 2 }, col::cream, 1.6f, 0.75f);
        }
        drawCaps (g, "Suno Chain", { x + 44, cy - 16, 230, 28 }, col::cream.withAlpha (0.92f), 21.0f, juce::Justification::centredLeft);
        drawCaps (g, "by Pouria Motabean", { x + 45, cy + 12, 230, 14 }, col::amber.withAlpha (0.8f), 10.0f, juce::Justification::centredLeft);
        drawCaps (g, "v1.7", { x + 214, cy - 7, 44, 14 }, col::dim.withAlpha (0.85f), 9.5f, juce::Justification::centredLeft);
    }
    // section titles
    drawCaps (g, "Tone", { 70, 442, 120, 18 }, col::dim.brighter (0.15f), 11.5f, juce::Justification::centredLeft);
    drawCaps (g, "Space", { 366, 442, 120, 18 }, col::dim.brighter (0.15f), 11.5f, juce::Justification::centredLeft);
    drawCaps (g, "Width", { 621, 442, 120, 18 }, col::dim.brighter (0.15f), 11.5f, juce::Justification::centredLeft);
    drawCaps (g, "Echo", { 876, 442, 120, 18 }, col::dim.brighter (0.15f), 11.5f, juce::Justification::centredLeft);
    const bool sync = proc.apvts.getRawParameterValue ("dlySync")->load() > 0.5f;
    if (sync) drawCaps (g, "Note", { 883, 701, 120, 18 }, col::text.withAlpha (0.82f), 12.5f);
    drawCaps (g, "Sync", { 1001, 701, 120, 18 }, col::text.withAlpha (0.82f), 12.5f);
    if (proc.advancedOpen)
    {   // side module: four sections stacked (the image's dividers)
        const float x = (float) kW + 40.0f;
        auto title = [&] (int sec, const juce::String& t, const juce::String& sub)
        {
            drawCaps (g, t, { x + 26, kSec[sec] + 10, 220, 18 }, col::dim.brighter (0.15f), 11.5f, juce::Justification::centredLeft);
            if (sub.isNotEmpty()) drawCaps (g, sub, { x + 150, kSec[sec] + 10, 220, 18 }, col::dim.withAlpha (0.75f), 9.5f, juce::Justification::centredRight);
        };
        title (0, "Tone  /  Dynamics", "100 % = learned");
        title (1, "Reverb", "width per band");
        title (2, "Vocal width", "per band");
        title (3, "Echo", "width per band");
        drawCaps (g, "Side only: mono and the centre stay untouched", { x, kSec[2] + 128, 400, 16 }, col::dim.withAlpha (0.7f), 9.5f);
        drawCaps (g, "Low < 300 Hz   Mid 300 Hz - 4 kHz   High > 4 kHz", { x, kSec[2] + 146, 400, 16 }, col::dim.withAlpha (0.7f), 9.5f);
    }
}

void Root::resized()
{
    pill.setBounds (410, 51, 300, 50);
    loadB.setBounds (722, 60, 78, 32);
    saveB.setBounds (800, 60, 78, 32);
    learnB.setBounds (878, 60, 132, 32);
    clearB.setBounds (1010, 60, 84, 32);

    // glass display (inside the faceplate well) + tabs, meters, AMOUNT
    display.setBounds (144, 182, 580, 207);
    eqTab.setBounds (600, 188, 48, 24); widthTab.setBounds (648, 188, 70, 24);
    meters.setBounds (785, 182, 145, 207);
    placeKnob ("amount", 1046, 262, 150);

    // columns: Tone 48-344, Space 344-599, Width 599-854, Echo 854-1150 (the faceplate's dividers)
    const float r1 = 530, r2 = 660;
    placeKnob ("eqAmount", 137, r1, 64); placeKnob ("compAmount", 255, r1, 64);
    placeKnob ("deess", 137, r2, 64);    placeKnob ("satMix", 255, r2, 64);
    placeKnob ("revLevel", 420, r1, 64); placeKnob ("decay", 523, r1, 64);
    placeKnob ("predelay", 420, r2, 64); placeKnob ("duck", 523, r2, 64);
    placeKnob ("wLevel", 676, r1, 64);   placeKnob ("wTone", 777, r1, 64);
    placeKnob ("wMotion", 726, r2, 64);
    placeKnob ("dlyLevel", 943, r1, 64); placeKnob ("dlyFb", 1061, r1, 64);
    placeKnob ("dlyTime", 943, r2, 64);
    noteBox.setBounds (906, 642, 74, 40);
    syncB.setBounds (1061 - 28, (int) r2 - 28, 56, 56);
    revLamp.setBounds (412, 441, 20, 20);
    widthLamp.setBounds (664, 441, 20, 20);
    echoLamp.setBounds (910, 441, 20, 20);
    delayInfo.setBounds (940, 442, 192, 18);

    status.setBounds (120, 742, 700, 20);
    advancedB.setBounds (960, 738, 140, 28);

    // Advanced side module (x 1200-1680): columns and rows inside its four sections
    const float c[4] { kW + 80.0f, kW + 180.0f, kW + 280.0f, kW + 380.0f };
    const int ks = 40;
    placeKnob ("eqLow", c[0], 64, ks); placeKnob ("satDrive", c[1], 64, ks); placeKnob ("output", c[3], 64, ks);
    placeKnob ("dynPeak", c[0], 144, ks); placeKnob ("dynLeveler", c[1], 144, ks); placeKnob ("dynSpeed", c[2], 144, ks); placeKnob ("dynPunch", c[3], 144, ks);
    placeKnob ("width", c[0], kSec[1] + 62, ks); placeKnob ("wetHpf", c[1], kSec[1] + 62, ks); placeKnob ("wetLpf", c[2], kSec[1] + 62, ks); placeKnob ("duckRel", c[3], kSec[1] + 62, ks);
    placeKnob ("rwLow", c[0], kSec[1] + 140, ks); placeKnob ("rwMid", c[1], kSec[1] + 140, ks); placeKnob ("rwHigh", c[2], kSec[1] + 140, ks);
    placeKnob ("lwLow", c[0], kSec[2] + 72, ks); placeKnob ("lwMid", c[1], kSec[2] + 72, ks); placeKnob ("lwHigh", c[2], kSec[2] + 72, ks);
    placeKnob ("dlyToRev", c[0], kSec[3] + 72, ks);
    placeKnob ("dwLow", c[1], kSec[3] + 72, ks); placeKnob ("dwMid", c[2], kSec[3] + 72, ks); placeKnob ("dwHigh", c[3], kSec[3] + 72, ks);
}

void Root::tick()
{
    const bool learning = proc.isLearning();
    learnB.setButtonText (learning ? "STOP " + juce::String (proc.learnSeconds(), 0) + " S" : (proc.hasLearned() ? "RE-LEARN" : "LEARN"));
    learnB.setToggleState (learning, juce::dontSendNotification);
    learnB.recording = learning;
    if (learning) learnB.repaint();
    if (! learning && statusText.startsWith ("Learning")) { juce::String msg; proc.stopLearn (msg); statusText = msg; }
    const auto pn = proc.hasPreset() ? proc.getPresetName() : juce::String ("Suno Lead 01");
    if (pill.name != pn) { pill.name = pn; pill.repaint(); }
    if (statusText.isEmpty())
        statusText = ! proc.hasLearned() ? "Press LEARN and play 20 s or more of your dry vocal (with some s/z)." : "Ready.";
    status.setText (statusText, juce::dontSendNotification);

    auto m = proc.getMeters();
    meters.update (m);
    display.setWidth (m.widthBands);
    const bool sync = proc.apvts.getRawParameterValue ("dlySync")->load() > 0.5f;
    const bool wasSync = noteBox.isVisible();
    noteBox.setVisible (sync);
    knobs["dlyTime"]->slider.setVisible (! sync); knobs["dlyTime"]->label.setVisible (! sync);
    if (wasSync != sync) repaint();
    delayInfo.setText (juce::String ((int) std::round (m.delayMs)) + " ms" + (sync ? "  @ " + juce::String (m.bpm, 1) + " bpm" : juce::String()),
                       juce::dontSendNotification);

    // sections that are off: their knobs go dark (still adjustable)
    auto setOff = [this] (std::initializer_list<const char*> ids, bool off)
    {
        for (auto* id : ids)
        {
            auto& s = knobs[id]->slider;
            if ((bool) s.getProperties().getWithDefault ("off", false) != off) { s.getProperties().set ("off", off); s.repaint(); }
        }
    };
    setOff ({ "revLevel", "decay", "predelay", "duck", "width", "wetHpf", "wetLpf", "duckRel", "rwLow", "rwMid", "rwHigh" }, ! revLamp.getToggleState());
    setOff ({ "wLevel", "wTone", "wMotion", "lwLow", "lwMid", "lwHigh" }, ! widthLamp.getToggleState());
    setOff ({ "dlyLevel", "dlyFb", "dlyTime", "dlyToRev", "dwLow", "dwMid", "dwHigh" }, ! echoLamp.getToggleState());

    // knob labels show the value while you touch them
    for (auto& [id, k] : knobs)
    {
        const bool show = k->slider.isMouseButtonDown() || k->slider.isMouseOver();
        const auto t = show ? k->slider.getTextFromValue (k->slider.getValue()) : k->name.toUpperCase();
        if ((bool) k->slider.getProperties().getWithDefault ("hov", false) != show)
        { k->slider.getProperties().set ("hov", show); k->slider.repaint(); }   // arc glows while touched
        if (k->label.getText() != t)
        {
            k->label.setText (t, juce::dontSendNotification);
            k->label.setColour (juce::Label::textColourId, show ? col::amberHi : col::text.withAlpha (0.82f));
        }
    }
    if (++ticks % 4 == 0) display.setCurves (proc.getCurves());
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
    logicalW = ui::kW + (advanced ? ui::kSideW : 0);
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
