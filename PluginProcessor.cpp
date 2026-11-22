#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float s, float e, float t) { return s * std::pow (e / s, t); },
             [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); } };
}

juce::String hzText (float v, int)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz"
                        : juce::String (juce::roundToInt (v)) + " Hz";
}
} // namespace

VoidCarveProcessor::VoidCarveProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VOIDCARVE", createLayout())
{
    pAmount  = apvts.getRawParameterValue ("amount");
    pSens    = apvts.getRawParameterValue ("sens");
    pAttack  = apvts.getRawParameterValue ("attack");
    pRelease = apvts.getRawParameterValue ("release");
    pLow     = apvts.getRawParameterValue ("low");
    pHigh    = apvts.getRawParameterValue ("high");
    pMix     = apvts.getRawParameterValue ("mix");
    pOut     = apvts.getRawParameterValue ("output");
}

juce::AudioProcessorValueTreeState::ParameterLayout VoidCarveProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> ps;

    auto pct = A().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; });
    auto db  = A().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB"; });
    auto ms  = A().withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10 ? 1 : 0) + " ms"; });
    auto hz  = A().withStringFromValueFunction (hzText);

    juce::NormalisableRange<float> attR (1.0f, 200.0f, 0.0f, 0.4f);
    juce::NormalisableRange<float> relR (10.0f, 1000.0f, 0.0f, 0.4f);

    ps.push_back (std::make_unique<P> (juce::ParameterID { "amount", 1 },  "Amount",      juce::NormalisableRange<float> (0, 100), 50.0f, pct));
    ps.push_back (std::make_unique<P> (juce::ParameterID { "sens", 1 },    "Sensitivity", juce::NormalisableRange<float> (-24, 24), 0.0f, db));
    ps.push_back (std::make_unique<P> (juce::ParameterID { "attack", 1 },  "Attack",      attR, 10.0f, ms));
    ps.push_back (std::make_unique<P> (juce::ParameterID { "release", 1 }, "Release",     relR, 150.0f, ms));
    ps.push_back (std::make_unique<P> (juce::ParameterID { "low", 1 },     "Low Cut",     logRange (20, 20000), 20.0f, hz));
    ps.push_back (std::make_unique<P> (juce::ParameterID { "high", 1 },    "High Cut",    logRange (20, 20000), 20000.0f, hz));
    ps.push_back (std::make_unique<P> (juce::ParameterID { "mix", 1 },     "Mix",         juce::NormalisableRange<float> (0, 100), 100.0f, pct));
    ps.push_back (std::make_unique<P> (juce::ParameterID { "output", 1 },  "Output",      juce::NormalisableRange<float> (-24, 24), 0.0f, db));
    return { ps.begin(), ps.end() };
}

bool VoidCarveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void VoidCarveProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, juce::jmax (1, getMainBusNumInputChannels()));
    setLatencySamples (engine.getLatencySamples());
}

void VoidCarveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    voidcarve::CarveParams p;
    p.amount        = pAmount->load() * 0.01f;
    p.sensitivityDb = pSens->load();
    p.attackMs      = pAttack->load();
    p.releaseMs     = pRelease->load();
    p.lowHz         = pLow->load();
    p.highHz        = pHigh->load();
    p.mix           = pMix->load() * 0.01f;
    p.outputDb      = pOut->load();
    engine.setParams (p);

    auto mainBuf = getBusBuffer (buffer, true, 0);

    const bool scOn = getBusCount (true) > 1 && getBus (true, 1) != nullptr
                      && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0;
    auto scBuf = scOn ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    const int scCh = scOn ? scBuf.getNumChannels() : 0;
    const float* const* scPtr = scCh > 0 ? scBuf.getArrayOfReadPointers() : nullptr;

    engine.process (mainBuf.getArrayOfWritePointers(), mainBuf.getNumChannels(),
                    scPtr, scCh, buffer.getNumSamples());

    // silence any extra output channels the host may have given us
    for (int ch = getMainBusNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

void VoidCarveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VoidCarveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VoidCarveProcessor::createEditor() { return new VoidCarveEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoidCarveProcessor(); }
