#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout XquilerAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;

    static const float defFreq[kBands] { 60.f, 150.f, 400.f, 1000.f, 2500.f, 6000.f, 10000.f, 16000.f };

    for (int i = 0; i < kBands; ++i)
    {
        const String n = "Band " + String (i + 1) + " ";
        l.add (std::make_unique<AudioParameterBool>   (ParameterID { bandId (i, "on"), 1 }, n + "On", false));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { bandId (i, "freq"), 1 }, n + "Freq",
                                                       NormalisableRange<float> (20.f, 20000.f, 0.1f, 0.3f), defFreq[i]));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { bandId (i, "gain"), 1 }, n + "Gain",
                                                       NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f));
        l.add (std::make_unique<AudioParameterFloat>  (ParameterID { bandId (i, "q"), 1 }, n + "Q",
                                                       NormalisableRange<float> (0.1f, 18.f, 0.01f, 0.4f), 1.f));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { bandId (i, "type"), 1 }, n + "Type",
                                                       StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" }, 0));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "out", 1 }, "Output",
                                                  NormalisableRange<float> (-24.f, 24.f, 0.1f), 0.f));
    return l;
}

XquilerAudioProcessor::XquilerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int i = 0; i < kBands; ++i)
        bands[(size_t) i] = { apvts.getRawParameterValue (bandId (i, "on")),
                              apvts.getRawParameterValue (bandId (i, "freq")),
                              apvts.getRawParameterValue (bandId (i, "gain")),
                              apvts.getRawParameterValue (bandId (i, "q")),
                              apvts.getRawParameterValue (bandId (i, "type")) };
    outGain = apvts.getRawParameterValue ("out");
}

bool XquilerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto o = layouts.getMainOutputChannelSet();
    if (o != juce::AudioChannelSet::mono() && o != juce::AudioChannelSet::stereo()) return false;
    return o == layouts.getMainInputChannelSet();
}

std::array<XquilerAudioProcessor::Coeffs::Ptr, 2>
XquilerAudioProcessor::makeBandCoeffs (double sr, float f, float gainDb, float q, int type)
{
    f = juce::jlimit (20.f, 0.45f * (float) sr, f);
    const float g = juce::Decibels::decibelsToGain (gainDb);
    switch (type)
    {
        case 1:  return { Coeffs::makeLowShelf  (sr, f, q, g), nullptr };
        case 2:  return { Coeffs::makeHighShelf (sr, f, q, g), nullptr };
        case 3:  return { Coeffs::makeHighPass  (sr, f, 0.5412f), Coeffs::makeHighPass (sr, f, 1.3066f) }; // 24 dB/oct
        case 4:  return { Coeffs::makeLowPass   (sr, f, 0.5412f), Coeffs::makeLowPass  (sr, f, 1.3066f) }; // 24 dB/oct
        case 5:  return { Coeffs::makeNotch     (sr, f, q), nullptr };
        default: return { Coeffs::makePeakFilter (sr, f, q, g), nullptr };
    }
}

void XquilerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    auto init = Coeffs::makePeakFilter (sr, 1000.f, 1.f, 1.f);
    for (int b = 0; b < kBands; ++b)
        for (int s = 0; s < 2; ++s)
            for (int c = 0; c < 2; ++c)
            {
                filt[b][s][c].coefficients = init;
                filt[b][s][c].reset();
            }
    for (auto& c : cache) c = Cache();
    lastOut = juce::Decibels::decibelsToGain (outGain->load());
}

void XquilerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2);
    const int n = buffer.getNumSamples();
    if (numCh == 0) return;

    // espectro de entrada
    for (int i = 0; i < n; ++i)
    {
        float m = buffer.getReadPointer (0)[i];
        if (numCh > 1) m = 0.5f * (m + buffer.getReadPointer (1)[i]);
        inAnalyzer.push (m);
    }

    // bandas
    for (int b = 0; b < kBands; ++b)
    {
        auto& c = cache[(size_t) b];
        const auto& p = bands[(size_t) b];
        if (p.on->load() < 0.5f) { c.on = false; continue; }

        const float f = p.freq->load(), g = p.gain->load(), q = p.q->load();
        const int   t = (int) std::round (p.type->load());

        if (! c.on || f != c.f || g != c.g || q != c.q || t != c.type)
        {
            auto cs = makeBandCoeffs (sr, f, g, q, t);
            const bool reset = (! c.on) || (t != c.type);
            for (int ch = 0; ch < 2; ++ch)
            {
                filt[b][0][ch].coefficients = cs[0];
                if (cs[1] != nullptr) filt[b][1][ch].coefficients = cs[1];
                if (reset) { filt[b][0][ch].reset(); filt[b][1][ch].reset(); }
            }
            c.two = (cs[1] != nullptr);
            c.on = true; c.f = f; c.g = g; c.q = q; c.type = t;
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i) d[i] = filt[b][0][ch].processSample (d[i]);
            if (c.two)
                for (int i = 0; i < n; ++i) d[i] = filt[b][1][ch].processSample (d[i]);
        }
    }

    // ganancia de salida
    const float og = juce::Decibels::decibelsToGain (outGain->load());
    buffer.applyGainRamp (0, n, lastOut, og);
    lastOut = og;

    // espectro de salida
    for (int i = 0; i < n; ++i)
    {
        float m = buffer.getReadPointer (0)[i];
        if (numCh > 1) m = 0.5f * (m + buffer.getReadPointer (1)[i]);
        outAnalyzer.push (m);
    }
}

//==============================================================================
juce::AudioProcessorEditor* XquilerAudioProcessor::createEditor()
{
    return new XquilerAudioProcessorEditor (*this);
}

void XquilerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void XquilerAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new XquilerAudioProcessor();
}
