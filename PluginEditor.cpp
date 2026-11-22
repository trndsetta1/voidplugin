#include "PluginEditor.h"

using namespace vc;
using Engine = voidcarve::CarveEngine;

namespace
{
juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
{
    g.setColour (c.withAlpha (0.10f)); g.strokePath (p, juce::PathStrokeType (width * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c.withAlpha (0.22f)); g.strokePath (p, juce::PathStrokeType (width * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (c);                   g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::String hzLabel (float hz)
{
    return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + "k" : juce::String (juce::roundToInt (hz));
}
} // namespace

//==============================================================================
NeonLookAndFeel::NeonLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, col::violet.withAlpha (0.5f));
    setColour (juce::TooltipWindow::backgroundColourId, col::panelHi);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::violet);
}

juce::Label* NeonLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (13.0f, true));
    l->setJustificationType (juce::Justification::centred);
    return l;
}

void NeonLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float a0, float a1, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging();

    // outer track
    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 3, r - 3, 0.0f, a0, a1, true);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc with glow
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, r - 3, r - 3, 0.0f, a0, ang, true);
    glowStroke (g, arc, hot ? accent.brighter (0.3f) : accent, 4.0f);

    // knob body
    const float kr = r * 0.70f;
    juce::ColourGradient body (col::panelHi.brighter (0.25f), c.x - kr, c.y - kr,
                               col::bg, c.x + kr, c.y + kr, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (accent.withAlpha (hot ? 0.6f : 0.3f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.2f);

    // pointer
    juce::Path ptr;
    const float pl = kr * 0.78f;
    ptr.startNewSubPath (c.x + std::sin (ang) * kr * 0.25f, c.y - std::cos (ang) * kr * 0.25f);
    ptr.lineTo (c.x + std::sin (ang) * pl, c.y - std::cos (ang) * pl);
    glowStroke (g, ptr, accent, 2.5f);
}

//==============================================================================
NeonKnob::NeonKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id,
                    const juce::String& t, juce::Colour a, const juce::String& tip)
    : title (t), accent (a)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 20);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTooltip (tip + "\n(Double-click to reset)");
    slider.setDoubleClickReturnValue (true, (double) s.getParameter (id)->convertFrom0to1 (s.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
}

void NeonKnob::resized()
{
    auto b = getLocalBounds();
    b.removeFromTop (20);
    slider.setBounds (b);
}

void NeonKnob::paint (juce::Graphics& g)
{
    g.setColour (accent.withAlpha (0.9f));
    g.setFont (font (12.0f, true).withExtraKerningFactor (0.18f));
    g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (20), juce::Justification::centred);
}

//==============================================================================
SpectrumView::SpectrumView (VoidCarveProcessor& p)
    : proc (p),
      lowParam (p.apvts.getParameter ("low")),
      highParam (p.apvts.getParameter ("high"))
{
    sc.fill (-100.0f);
    mainLv.fill (-100.0f);
    gr.fill (0.0f);
    setTooltip ("Drag the glowing handles to choose which frequencies get carved.");
    startTimerHz (40);
}

juce::Rectangle<float> SpectrumView::plotArea() const { return getLocalBounds().toFloat().reduced (14.0f, 12.0f).withTrimmedBottom (14.0f); }

float SpectrumView::freqToX (float hz) const
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * std::log (hz / Engine::minHz) / std::log (Engine::maxHz / Engine::minHz);
}

float SpectrumView::xToFreq (float x) const
{
    const auto a = plotArea();
    const float t = juce::jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth());
    return Engine::minHz * std::pow (Engine::maxHz / Engine::minHz, t);
}

float SpectrumView::dbToY (float db) const
{
    const auto a = plotArea();
    const float t = juce::jlimit (0.0f, 1.0f, (db + 72.0f) / 72.0f);
    return a.getBottom() - t * a.getHeight();
}

void SpectrumView::timerCallback()
{
    auto& e = proc.engine;
    maxGr = 0.0f;
    for (int b = 0; b < Engine::numBands; ++b)
    {
        auto smooth = [] (float& v, float target, float up, float down) { v += (target - v) * (target > v ? up : down); };
        smooth (sc[(size_t) b], e.meterSc[(size_t) b].load(), 0.6f, 0.15f);
        smooth (mainLv[(size_t) b], e.meterMain[(size_t) b].load(), 0.6f, 0.15f);
        smooth (gr[(size_t) b], e.meterGr[(size_t) b].load(), 0.5f, 0.2f);
        maxGr = juce::jmax (maxGr, gr[(size_t) b]);
    }
    scActive = e.sidechainPeakDb.load() > -65.0f;
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    const auto a = plotArea();

    // panel
    g.setGradientFill (juce::ColourGradient (col::panel, 0, full.getY(), col::bg, 0, full.getBottom(), false));
    g.fillRoundedRectangle (full, 14.0f);
    g.setColour (col::violet.withAlpha (0.25f));
    g.drawRoundedRectangle (full.reduced (0.5f), 14.0f, 1.0f);

    // grid
    g.setFont (font (10.5f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (col::textDim);
        g.drawText (hzLabel (f), juce::Rectangle<float> (x - 20, a.getBottom() + 1, 40, 13), juce::Justification::centred);
    }
    for (float db : { -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.drawHorizontalLine ((int) dbToY (db), a.getX(), a.getRight());
    }

    // bands
    const float lo = lowParam->convertFrom0to1 (lowParam->getValue());
    const float hi = highParam->convertFrom0to1 (highParam->getValue());
    for (int b = 0; b < Engine::numBands; ++b)
    {
        const float x0 = freqToX (Engine::bandEdgeHz (b)) + 1.5f;
        const float x1 = freqToX (Engine::bandEdgeHz (b + 1)) - 1.5f;
        const float w = x1 - x0;

        // music (main) level - violet
        const float ym = dbToY (mainLv[(size_t) b]);
        g.setGradientFill (juce::ColourGradient (col::violet.withAlpha (0.55f), 0, ym, col::violet.withAlpha (0.05f), 0, a.getBottom(), false));
        g.fillRect (x0, ym, w, a.getBottom() - ym);

        // sidechain level - cyan outline cap
        const float ys = dbToY (sc[(size_t) b]);
        g.setColour (col::cyan.withAlpha (0.18f));
        g.fillRect (x0, ys, w, a.getBottom() - ys);
        g.setColour (col::cyan);
        g.fillRect (x0, ys, w, 2.0f);

        // gain reduction - magenta, hanging from the top
        const float grH = a.getHeight() * 0.45f * juce::jlimit (0.0f, 1.0f, gr[(size_t) b] / Engine::maxDepthDb);
        if (grH > 0.5f)
        {
            g.setGradientFill (juce::ColourGradient (col::magenta.withAlpha (0.15f), 0, a.getY(), col::magenta.withAlpha (0.85f), 0, a.getY() + grH, false));
            g.fillRect (x0, a.getY(), w, grH);
        }
    }

    // gain-reduction curve
    juce::Path curve;
    for (int b = 0; b < Engine::numBands; ++b)
    {
        const float x = freqToX (Engine::bandCenterHz (b));
        const float y = a.getY() + a.getHeight() * 0.45f * juce::jlimit (0.0f, 1.0f, gr[(size_t) b] / Engine::maxDepthDb);
        if (b == 0) curve.startNewSubPath (a.getX(), y);
        curve.lineTo (x, y);
    }
    curve.lineTo (a.getRight(), a.getY() + a.getHeight() * 0.45f * juce::jlimit (0.0f, 1.0f, gr.back() / Engine::maxDepthDb));
    glowStroke (g, curve, col::magenta, 2.0f);

    // out-of-range shading + handles
    const float xl = freqToX (juce::jmin (lo, hi)), xh = freqToX (juce::jmax (lo, hi));
    g.setColour (col::bg.withAlpha (0.65f));
    g.fillRect (juce::Rectangle<float> (a.getX(), a.getY(), xl - a.getX(), a.getHeight()));
    g.fillRect (juce::Rectangle<float> (xh, a.getY(), a.getRight() - xh, a.getHeight()));

    auto drawHandle = [&] (float x, float hz, int id)
    {
        const bool hot = (hover == id || dragging == id);
        juce::Path line; line.startNewSubPath (x, a.getY()); line.lineTo (x, a.getBottom());
        glowStroke (g, line, col::amber.withAlpha (hot ? 1.0f : 0.7f), hot ? 2.0f : 1.4f);
        const float r = hot ? 7.0f : 5.5f;
        g.setColour (col::amber);
        g.fillEllipse (x - r, a.getY() + 10 - r, r * 2, r * 2);
        g.setColour (col::text);
        g.setFont (font (11.0f, true));
        const bool left = id == 1;
        g.drawText (hzLabel (hz) + "Hz", juce::Rectangle<float> (left ? x - 66 : x + 8, a.getY() + 2, 58, 16),
                    left ? juce::Justification::centredRight : juce::Justification::centredLeft);
    };
    drawHandle (xl, juce::jmin (lo, hi), 0);
    drawHandle (xh, juce::jmax (lo, hi), 1);

    // legend
    g.setFont (font (10.5f, true));
    auto leg = juce::Rectangle<float> (a.getRight() - 300, a.getBottom() - 18, 300, 14);
    auto item = [&] (juce::Colour c, const juce::String& t)
    {
        auto r = leg.removeFromLeft (100);
        g.setColour (c); g.fillRoundedRectangle (r.removeFromLeft (10).reduced (0, 2), 2.0f);
        g.setColour (col::textDim); g.drawText (" " + t, r, juce::Justification::centredLeft);
    };
    item (col::violet, "MUSIC");
    item (col::cyan, "SIDECHAIN");
    item (col::magenta, "CARVE");
}

int SpectrumView::handleAt (float x) const
{
    const float lo = lowParam->convertFrom0to1 (lowParam->getValue());
    const float hi = highParam->convertFrom0to1 (highParam->getValue());
    const float dl = std::abs (x - freqToX (juce::jmin (lo, hi)));
    const float dh = std::abs (x - freqToX (juce::jmax (lo, hi)));
    if (juce::jmin (dl, dh) > 12.0f) return -1;
    return dl <= dh ? 0 : 1;
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    const int h = handleAt ((float) e.x);
    if (h != hover) { hover = h; setMouseCursor (h >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
}

void SpectrumView::mouseExit (const juce::MouseEvent&) { hover = -1; }

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    dragging = handleAt ((float) e.x);
    if (dragging < 0)
    {   // click anywhere: grab the nearest handle
        const float f = xToFreq ((float) e.x);
        const float lo = lowParam->convertFrom0to1 (lowParam->getValue());
        const float hi = highParam->convertFrom0to1 (highParam->getValue());
        dragging = std::abs (std::log (f / lo)) < std::abs (std::log (f / hi)) ? 0 : 1;
    }
    (dragging == 0 ? lowParam : highParam)->beginChangeGesture();
    mouseDrag (e);
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    auto* p = dragging == 0 ? lowParam : highParam;
    auto* other = dragging == 0 ? highParam : lowParam;
    float f = xToFreq ((float) e.x);
    const float o = other->convertFrom0to1 (other->getValue());
    f = dragging == 0 ? juce::jmin (f, o / 1.06f) : juce::jmax (f, o * 1.06f);
    p->setValueNotifyingHost (p->convertTo0to1 (f));
}

void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        (dragging == 0 ? lowParam : highParam)->endChangeGesture();
    dragging = -1;
}

//==============================================================================
VoidCarveEditor::VoidCarveEditor (VoidCarveProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (spectrum);

    struct K { const char* id; const char* title; juce::Colour c; const char* tip; };
    const K defs[] = {
        { "amount",  "Amount",   col::magenta, "How deep the carve goes. This is the main control." },
        { "sens",    "Sense",    col::cyan,    "Boosts or trims the sidechain into the detector.\nTurn up if the sidechain is quiet." },
        { "attack",  "Attack",   col::cyan,    "How fast the carve reacts when the sidechain hits." },
        { "release", "Release",  col::cyan,    "How fast the music comes back after the sidechain stops." },
        { "low",     "Low Cut",  col::amber,   "Lowest frequency that gets carved (also draggable on the display)." },
        { "high",    "High Cut", col::amber,   "Highest frequency that gets carved (also draggable on the display)." },
        { "mix",     "Mix",      col::violet,  "Blend between the untouched and the carved signal." },
        { "output",  "Output",   col::violet,  "Output level." },
    };
    for (const auto& d : defs)
    {
        knobs.push_back (std::make_unique<NeonKnob> (p.apvts, d.id, d.title, d.c, d.tip));
        addAndMakeVisible (*knobs.back());
    }

    setSize (980, 580);
    startTimerHz (30);
}

VoidCarveEditor::~VoidCarveEditor() { setLookAndFeel (nullptr); }

void VoidCarveEditor::timerCallback()
{
    pulse += 0.08f;
    repaint (getLocalBounds().removeFromTop (78));
}

void VoidCarveEditor::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // background with soft neon blooms
    g.fillAll (col::bg);
    g.setGradientFill (juce::ColourGradient (col::violet.withAlpha (0.20f), b.getWidth() * 0.15f, 0,
                                             juce::Colours::transparentBlack, b.getWidth() * 0.15f, b.getHeight() * 0.7f, true));
    g.fillRect (b);
    g.setGradientFill (juce::ColourGradient (col::magenta.withAlpha (0.12f), b.getWidth() * 0.9f, b.getHeight(),
                                             juce::Colours::transparentBlack, b.getWidth() * 0.9f, b.getHeight() * 0.3f, true));
    g.fillRect (b);

    // title
    auto header = getLocalBounds().removeFromTop (78).reduced (24, 10);
    const auto titleArea = header.removeFromLeft (420).toFloat();
    const auto titleFont = font (44.0f, true).withExtraKerningFactor (0.12f);
    g.setFont (titleFont);
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (col::magenta.withAlpha (0.08f * (float) i));
        g.drawText ("VOIDCARVE", titleArea.translated (0, 0).expanded ((float) i * 1.5f, 0), juce::Justification::centredLeft);
    }
    juce::ColourGradient tg (col::cyan, titleArea.getX(), 0, col::magenta, titleArea.getX() + 300, 0, false);
    g.setGradientFill (tg);
    g.drawText ("VOIDCARVE", titleArea, juce::Justification::centredLeft);

    g.setFont (font (11.0f, true).withExtraKerningFactor (0.35f));
    g.setColour (col::textDim);
    g.drawText ("SPECTRAL SIDECHAIN CARVER  //  v1.0", juce::Rectangle<float> (titleArea.getX() + 3, titleArea.getBottom() - 8, 400, 14),
                juce::Justification::centredLeft);

    // sidechain status pill
    const bool on = spectrum.sidechainActive();
    auto pill = header.removeFromRight (300).toFloat().withSizeKeepingCentre (300, 34);
    g.setColour (col::panel);
    g.fillRoundedRectangle (pill, 17.0f);
    g.setColour ((on ? col::lime : col::amber).withAlpha (0.5f));
    g.drawRoundedRectangle (pill, 17.0f, 1.0f);
    const float glow = on ? 0.6f + 0.4f * std::sin (pulse * 2.0f) : 1.0f;
    auto dot = pill.removeFromLeft (34).withSizeKeepingCentre (12, 12);
    g.setColour ((on ? col::lime : col::amber).withAlpha (0.25f * glow)); g.fillEllipse (dot.expanded (5));
    g.setColour (on ? col::lime : col::amber); g.fillEllipse (dot);
    g.setColour (col::text);
    g.setFont (font (12.0f, true));
    if (on)
        g.drawText ("SIDECHAIN LIVE   -" + juce::String (spectrum.getMaxGr(), 1) + " dB", pill, juce::Justification::centredLeft);
    else
        g.drawText ("NO SIDECHAIN - route a track in", pill, juce::Justification::centredLeft);

    // knob tray
    auto tray = getLocalBounds().removeFromBottom (190).reduced (16, 12).toFloat();
    g.setColour (col::panel.withAlpha (0.85f));
    g.fillRoundedRectangle (tray, 14.0f);
    g.setColour (col::violet.withAlpha (0.2f));
    g.drawRoundedRectangle (tray.reduced (0.5f), 14.0f, 1.0f);
}

void VoidCarveEditor::resized()
{
    auto b = getLocalBounds();
    b.removeFromTop (78);
    auto tray = b.removeFromBottom (190).reduced (28, 20);
    spectrum.setBounds (b.reduced (16, 4));

    // Amount gets a big knob, the rest share the remaining space
    const int bigW = 150;
    knobs[0]->setBounds (tray.removeFromLeft (bigW));
    tray.removeFromLeft (8);
    const int w = tray.getWidth() / ((int) knobs.size() - 1);
    for (size_t i = 1; i < knobs.size(); ++i)
        knobs[i]->setBounds (tray.removeFromLeft (w).withTrimmedTop (18));
}
