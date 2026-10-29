#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

class XquilerAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kBands = 8;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    using Filter = juce::dsp::IIR::Filter<float>;

    // Analizador de espectro (FFT) que lee la interfaz
    struct Analyzer
    {
        static constexpr int order = 11;
        static constexpr int size  = 1 << order;

        juce::dsp::FFT fft { order };
        juce::dsp::WindowingFunction<float> win { (size_t) size, juce::dsp::WindowingFunction<float>::hann };
        std::array<float, size>     fifo {};
        std::array<float, size * 2> data {};
        std::array<std::atomic<float>, size / 2> mags;
        int idx = 0;

        Analyzer() { for (auto& m : mags) m.store (-120.f); }

        void push (float s)
        {
            fifo[(size_t) idx++] = s;
            if (idx < size) return;
            idx = 0;
            std::fill (data.begin(), data.end(), 0.f);
            std::copy (fifo.begin(), fifo.end(), data.begin());
            win.multiplyWithWindowingTable (data.data(), (size_t) size);
            fft.performFrequencyOnlyForwardTransform (data.data());
            for (int k = 0; k < size / 2; ++k)
                mags[(size_t) k].store (juce::Decibels::gainToDecibels (data[(size_t) k] / ((float) size * 0.25f), -120.f));
        }
    };

    XquilerAudioProcessor();
    ~XquilerAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "xQUILER"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::String bandId (int band, const char* name) { return "b" + juce::String (band) + name; }

    // Tipos: 0 Bell, 1 Low Shelf, 2 High Shelf, 3 Low Cut, 4 High Cut, 5 Notch
    static std::array<Coeffs::Ptr, 2> makeBandCoeffs (double sr, float freq, float gainDb, float q, int type);

    juce::AudioProcessorValueTreeState apvts;

    struct BandPtrs { std::atomic<float> *on, *freq, *gain, *q, *type; };
    std::array<BandPtrs, kBands> bands;
    std::atomic<float>* outGain = nullptr;

    Analyzer inAnalyzer, outAnalyzer;

private:
    struct Cache { bool on = false; float f = -1.f, g = -999.f, q = -1.f; int type = -1; bool two = false; };

    double sr = 44100.0;
    Filter filt[kBands][2][2];   // banda, etapa, canal
    std::array<Cache, kBands> cache;
    float lastOut = 1.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XquilerAudioProcessor)
};
