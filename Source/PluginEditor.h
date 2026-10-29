#pragma once
#include "PluginProcessor.h"

// Look monocromatico: los colores salen del tema claro/oscuro
struct XLAF : public juce::LookAndFeel_V4
{
    juce::Colour fg { juce::Colours::white }, dim { juce::Colours::grey }, track { 0xff333333 };

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (10.f);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = startAngle + pos * (endAngle - startAngle);
        const float alpha = s.isEnabled() ? 1.f : 0.3f;

        juce::Path trackP, valP, ptr;
        trackP.addCentredArc (c.x, c.y, r, r, 0.f, startAngle, endAngle, true);
        valP.addCentredArc   (c.x, c.y, r, r, 0.f, startAngle, ang, true);
        g.setColour (track);
        g.strokePath (trackP, juce::PathStrokeType (2.f));
        g.setColour (fg.withAlpha (alpha));
        g.strokePath (valP, juce::PathStrokeType (2.f));

        ptr.startNewSubPath (0.f, -r * 0.45f);
        ptr.lineTo (0.f, -r * 0.8f);
        g.strokePath (ptr, juce::PathStrokeType (2.f), juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }
};

class XquilerAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit XquilerAudioProcessorEditor (XquilerAudioProcessor&);
    ~XquilerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    static constexpr int kBins = XquilerAudioProcessor::Analyzer::size / 2;

    struct Pal { juce::Colour bg, fg, dim, grid; };
    Pal  pal() const;
    bool isDark() const;
    int  rangeIndex() const;
    int  rangeDb() const;

    float freqToX (float f) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    juce::Point<float> pointPos (int band) const;
    int  hitTest (juce::Point<float> p) const;

    float getParam (int band, const char* name) const;
    void  setParam (const juce::String& id, float value);
    void  addBand (juce::Point<float> p);
    void  removeBand (int band);
    void  selectBand (int band);
    void  bindControls();
    void  applyTheme();
    void  syncState();
    void  timerCallback() override;

    juce::Path spectrumPath (const std::array<float, kBins>& sm) const;
    juce::Path curvePath (const std::vector<std::array<XquilerAudioProcessor::Coeffs::Ptr, 2>>& cs) const;

    XquilerAudioProcessor& proc;
    XLAF laf;

    juce::Slider freqKnob, gainKnob, qKnob, outKnob;
    juce::ComboBox typeBox;
    juce::TextButton themeBtn, rangeBtn, removeBtn;
    std::unique_ptr<SliderAtt> freqAtt, gainAtt, qAtt, outAtt;
    std::unique_ptr<ComboAtt> typeAtt;

    int selected = -1, hover = -1, dragBand = -1;
    bool lastDark = true;
    int lastRange = -1, lastType = -1;

    std::array<float, kBins> smIn {}, smOut {};
    juce::Rectangle<int> graph;
};
