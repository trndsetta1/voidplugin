#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

namespace vc
{
namespace col
{
    const juce::Colour bg       { 0xff07060d };
    const juce::Colour panel    { 0xff110f1c };
    const juce::Colour panelHi  { 0xff1b1830 };
    const juce::Colour cyan     { 0xff00f0ff };
    const juce::Colour magenta  { 0xffff2bd6 };
    const juce::Colour violet   { 0xff8a5cff };
    const juce::Colour lime     { 0xff7dff6b };
    const juce::Colour amber    { 0xffffb340 };
    const juce::Colour text     { 0xffe9e6ff };
    const juce::Colour textDim  { 0xff7b7699 };
}

//==============================================================================
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
};

//==============================================================================
class NeonKnob : public juce::Component
{
public:
    NeonKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramId,
              const juce::String& title, juce::Colour accent, const juce::String& tip);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::String title;
    juce::Colour accent;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class SpectrumView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit SpectrumView (VoidCarveProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    float getMaxGr() const { return maxGr; }
    bool sidechainActive() const { return scActive; }

private:
    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    float freqToX (float hz) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    int handleAt (float x) const;

    VoidCarveProcessor& proc;
    juce::RangedAudioParameter* lowParam;
    juce::RangedAudioParameter* highParam;
    std::array<float, voidcarve::CarveEngine::numBands> sc {}, mainLv {}, gr {};
    float maxGr = 0.0f;
    bool scActive = false;
    int hover = -1, dragging = -1;
};

} // namespace vc

//==============================================================================
class VoidCarveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VoidCarveEditor (VoidCarveProcessor&);
    ~VoidCarveEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    VoidCarveProcessor& proc;
    vc::NeonLookAndFeel lnf;
    vc::SpectrumView spectrum;
    std::vector<std::unique_ptr<vc::NeonKnob>> knobs;
    juce::TooltipWindow tooltips { this, 600 };
    float pulse = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoidCarveEditor)
};
