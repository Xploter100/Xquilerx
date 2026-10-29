#include "PluginEditor.h"

using namespace juce;
using Coeffs = XquilerAudioProcessor::Coeffs;

static String fmtFreq (float f)
{
    return f >= 1000.f ? String (f / 1000.f, 2) + " kHz" : String ((int) std::round (f)) + " Hz";
}

//==============================================================================
XquilerAudioProcessorEditor::XquilerAudioProcessorEditor (XquilerAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (900, 545);
    setLookAndFeel (&laf);
    smIn.fill (-120.f);
    smOut.fill (-120.f);

    for (auto* s : { &freqKnob, &gainKnob, &qKnob, &outKnob })
    {
        s->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (Slider::TextBoxBelow, false, 84, 16);
        addAndMakeVisible (*s);
    }

    typeBox.addItemList (StringArray { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" }, 1);
    addAndMakeVisible (typeBox);

    removeBtn.setButtonText ("REMOVE BAND");
    removeBtn.onClick = [this] { if (selected >= 0) removeBand (selected); };
    addAndMakeVisible (removeBtn);

    themeBtn.onClick = [this]
    {
        proc.apvts.state.setProperty ("dark", ! isDark(), nullptr);
        applyTheme();
    };
    addAndMakeVisible (themeBtn);

    rangeBtn.onClick = [this]
    {
        proc.apvts.state.setProperty ("range", (rangeIndex() + 1) % 3, nullptr);
        syncState();
    };
    addAndMakeVisible (rangeBtn);

    outAtt = std::make_unique<SliderAtt> (proc.apvts, "out", outKnob);
    outKnob.textFromValueFunction = [] (double v) { return String (v, 1) + " dB"; };
    outKnob.updateText();

    bindControls();
    applyTheme();
    syncState();
    startTimerHz (30);
}

XquilerAudioProcessorEditor::~XquilerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
// Estado / tema
bool XquilerAudioProcessorEditor::isDark() const
{
    return (bool) proc.apvts.state.getProperty ("dark", true);
}

int XquilerAudioProcessorEditor::rangeIndex() const
{
    return jlimit (0, 2, (int) proc.apvts.state.getProperty ("range", 1));
}

int XquilerAudioProcessorEditor::rangeDb() const
{
    static const int r[3] { 6, 12, 24 };
    return r[rangeIndex()];
}

XquilerAudioProcessorEditor::Pal XquilerAudioProcessorEditor::pal() const
{
    if (isDark()) return { Colour (0xff0c0c0c), Colours::white,   Colour (0xff7a7a7a), Colour (0xff272727) };
    return               { Colour (0xfff2f2f0), Colour (0xff111111), Colour (0xff808080), Colour (0xffd8d8d5) };
}

void XquilerAudioProcessorEditor::applyTheme()
{
    const auto p = pal();
    laf.fg = p.fg; laf.dim = p.dim; laf.track = p.grid;

    laf.setColour (Slider::textBoxTextColourId, p.fg);
    laf.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    laf.setColour (ComboBox::backgroundColourId, p.bg);
    laf.setColour (ComboBox::textColourId, p.fg);
    laf.setColour (ComboBox::outlineColourId, p.dim);
    laf.setColour (ComboBox::arrowColourId, p.fg);
    laf.setColour (TextButton::buttonColourId, p.bg);
    laf.setColour (TextButton::buttonOnColourId, p.bg);
    laf.setColour (TextButton::textColourOffId, p.fg);
    laf.setColour (TextButton::textColourOnId, p.fg);
    laf.setColour (PopupMenu::backgroundColourId, p.bg);
    laf.setColour (PopupMenu::textColourId, p.fg);
    laf.setColour (PopupMenu::highlightedBackgroundColourId, p.fg);
    laf.setColour (PopupMenu::highlightedTextColourId, p.bg);
    laf.setColour (Label::textColourId, p.fg);

    themeBtn.setButtonText (isDark() ? "LIGHT" : "DARK");
    lastDark = isDark();
    sendLookAndFeelChange();
    repaint();
}

void XquilerAudioProcessorEditor::syncState()
{
    if (isDark() != lastDark) applyTheme();
    if (rangeIndex() != lastRange)
    {
        lastRange = rangeIndex();
        rangeBtn.setButtonText ("+/- " + String (rangeDb()) + " dB");
        repaint();
    }
}

//==============================================================================
// Coordenadas
float XquilerAudioProcessorEditor::freqToX (float f) const
{
    return (float) graph.getX() + (float) graph.getWidth() * std::log (f / 20.f) / std::log (1000.f);
}
float XquilerAudioProcessorEditor::xToFreq (float x) const
{
    return 20.f * std::pow (1000.f, (x - (float) graph.getX()) / (float) graph.getWidth());
}
float XquilerAudioProcessorEditor::dbToY (float db) const
{
    return (float) graph.getY() + (float) graph.getHeight() * (0.5f - db / (2.f * (float) rangeDb()));
}
float XquilerAudioProcessorEditor::yToDb (float y) const
{
    return (0.5f - (y - (float) graph.getY()) / (float) graph.getHeight()) * 2.f * (float) rangeDb();
}

float XquilerAudioProcessorEditor::getParam (int b, const char* name) const
{
    const auto& p = proc.bands[(size_t) b];
    const String n (name);
    if (n == "on")   return p.on->load();
    if (n == "freq") return p.freq->load();
    if (n == "gain") return p.gain->load();
    if (n == "q")    return p.q->load();
    return p.type->load();
}

Point<float> XquilerAudioProcessorEditor::pointPos (int b) const
{
    const int t = (int) std::round (getParam (b, "type"));
    const float g = (t <= 2) ? getParam (b, "gain") : 0.f;
    const float x = jlimit ((float) graph.getX(), (float) graph.getRight(), freqToX (getParam (b, "freq")));
    const float y = jlimit ((float) graph.getY(), (float) graph.getBottom(), dbToY (g));
    return { x, y };
}

int XquilerAudioProcessorEditor::hitTest (Point<float> pos) const
{
    int best = -1;
    float bd = 14.f;
    for (int b = 0; b < XquilerAudioProcessor::kBands; ++b)
    {
        if (getParam (b, "on") < 0.5f) continue;
        const float d = pos.getDistanceFrom (pointPos (b));
        if (d < bd) { bd = d; best = b; }
    }
    return best;
}

void XquilerAudioProcessorEditor::setParam (const String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

//==============================================================================
// Edicion de bandas
void XquilerAudioProcessorEditor::addBand (Point<float> pos)
{
    for (int b = 0; b < XquilerAudioProcessor::kBands; ++b)
    {
        if (getParam (b, "on") >= 0.5f) continue;
        setParam (XquilerAudioProcessor::bandId (b, "type"), 0.f);
        setParam (XquilerAudioProcessor::bandId (b, "freq"), jlimit (20.f, 20000.f, xToFreq (pos.x)));
        setParam (XquilerAudioProcessor::bandId (b, "gain"), jlimit (-24.f, 24.f, yToDb (pos.y)));
        setParam (XquilerAudioProcessor::bandId (b, "q"), 1.f);
        setParam (XquilerAudioProcessor::bandId (b, "on"), 1.f);
        selectBand (b);
        return;
    }
}

void XquilerAudioProcessorEditor::removeBand (int b)
{
    setParam (XquilerAudioProcessor::bandId (b, "on"), 0.f);
    if (selected == b) selectBand (-1);
    if (hover == b) hover = -1;
    repaint();
}

void XquilerAudioProcessorEditor::selectBand (int b)
{
    selected = b;
    bindControls();
    repaint();
}

void XquilerAudioProcessorEditor::bindControls()
{
    freqAtt.reset(); gainAtt.reset(); qAtt.reset(); typeAtt.reset();

    const bool has = selected >= 0;
    Component* comps[] { &freqKnob, &gainKnob, &qKnob, &typeBox, &removeBtn };
    for (auto* c : comps) c->setVisible (has);
    lastType = -1;
    if (! has) return;

    freqAtt = std::make_unique<SliderAtt> (proc.apvts, XquilerAudioProcessor::bandId (selected, "freq"), freqKnob);
    gainAtt = std::make_unique<SliderAtt> (proc.apvts, XquilerAudioProcessor::bandId (selected, "gain"), gainKnob);
    qAtt    = std::make_unique<SliderAtt> (proc.apvts, XquilerAudioProcessor::bandId (selected, "q"), qKnob);
    typeAtt = std::make_unique<ComboAtt>  (proc.apvts, XquilerAudioProcessor::bandId (selected, "type"), typeBox);

    freqKnob.textFromValueFunction = [] (double v) { return fmtFreq ((float) v); };
    freqKnob.valueFromTextFunction = [] (const String& t)
    {
        auto s = t.trim().toLowerCase();
        double v = s.getDoubleValue();
        if (s.contains ("k")) v *= 1000.0;
        return v;
    };
    gainKnob.textFromValueFunction = [] (double v) { return String (v, 1) + " dB"; };
    qKnob.textFromValueFunction    = [] (double v) { return "Q " + String (v, 2); };
    freqKnob.updateText(); gainKnob.updateText(); qKnob.updateText();
}

//==============================================================================
// Raton
void XquilerAudioProcessorEditor::mouseDown (const MouseEvent& e)
{
    if (! graph.contains (e.getPosition())) return;
    const int h = hitTest (e.position);

    if (e.mods.isPopupMenu()) { if (h >= 0) removeBand (h); return; }

    if (h >= 0) { selectBand (h); dragBand = h; }
    else        { dragBand = -1; if (selected >= 0) selectBand (-1); }
}

void XquilerAudioProcessorEditor::mouseDrag (const MouseEvent& e)
{
    if (dragBand < 0) return;
    const int t = (int) std::round (getParam (dragBand, "type"));
    const float x = jlimit ((float) graph.getX(), (float) graph.getRight(), e.position.x);
    const float y = jlimit ((float) graph.getY(), (float) graph.getBottom(), e.position.y);
    setParam (XquilerAudioProcessor::bandId (dragBand, "freq"), jlimit (20.f, 20000.f, xToFreq (x)));
    if (t <= 2)
        setParam (XquilerAudioProcessor::bandId (dragBand, "gain"), jlimit (-24.f, 24.f, yToDb (y)));
}

void XquilerAudioProcessorEditor::mouseUp (const MouseEvent&) { dragBand = -1; }

void XquilerAudioProcessorEditor::mouseMove (const MouseEvent& e)
{
    hover = graph.contains (e.getPosition()) ? hitTest (e.position) : -1;
    setMouseCursor (hover >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
}

void XquilerAudioProcessorEditor::mouseDoubleClick (const MouseEvent& e)
{
    if (! graph.contains (e.getPosition())) return;
    const int h = hitTest (e.position);
    if (h >= 0) removeBand (h);
    else        addBand (e.position);
}

void XquilerAudioProcessorEditor::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int b = hitTest (e.position);
    if (b < 0) return;
    const int t = (int) std::round (getParam (b, "type"));
    if (t == 3 || t == 4) return;   // los cortes tienen pendiente fija
    float q = getParam (b, "q") * std::exp (w.deltaY * 3.f * (w.isReversed ? -1.f : 1.f));
    setParam (XquilerAudioProcessor::bandId (b, "q"), jlimit (0.1f, 18.f, q));
}

//==============================================================================
void XquilerAudioProcessorEditor::timerCallback()
{
    for (int k = 0; k < kBins; ++k)
    {
        const float a = proc.inAnalyzer.mags[(size_t) k].load();
        const float o = proc.outAnalyzer.mags[(size_t) k].load();
        smIn[(size_t) k]  = a > smIn[(size_t) k]  ? smIn[(size_t) k]  + (a - smIn[(size_t) k])  * 0.6f : jmax (-120.f, smIn[(size_t) k]  - 1.5f);
        smOut[(size_t) k] = o > smOut[(size_t) k] ? smOut[(size_t) k] + (o - smOut[(size_t) k]) * 0.6f : jmax (-120.f, smOut[(size_t) k] - 1.5f);
    }

    syncState();

    if (selected >= 0)
    {
        if (getParam (selected, "on") < 0.5f) { selectBand (-1); }
        else
        {
            const int t = (int) std::round (getParam (selected, "type"));
            if (t != lastType)
            {
                lastType = t;
                gainKnob.setEnabled (t <= 2);
                qKnob.setEnabled (t <= 2 || t == 5);
                repaint();
            }
        }
    }
    repaint();
}

void XquilerAudioProcessorEditor::resized()
{
    graph = { 56, 52, getWidth() - 56 - 24, 350 };
    themeBtn.setBounds (getWidth() - 24 - 80, 14, 80, 24);
    rangeBtn.setBounds (getWidth() - 24 - 80 - 8 - 96, 14, 96, 24);
    typeBox.setBounds (56, 452, 140, 24);
    removeBtn.setBounds (56, 486, 140, 24);
    freqKnob.setBounds (230, 440, 110, 92);
    gainKnob.setBounds (350, 440, 110, 92);
    qKnob.setBounds (470, 440, 110, 92);
    outKnob.setBounds (getWidth() - 24 - 110, 440, 110, 92);
}

//==============================================================================
Path XquilerAudioProcessorEditor::spectrumPath (const std::array<float, kBins>& sm) const
{
    Path p;
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    const float binHz = (float) sr / (float) XquilerAudioProcessor::Analyzer::size;
    const float x0 = (float) graph.getX(), x1 = (float) graph.getRight();
    const float bot = (float) graph.getBottom(), h = (float) graph.getHeight();
    bool started = false;

    for (float x = x0; x <= x1; x += 2.f)
    {
        const int k0 = jlimit (1, kBins - 1, (int) std::floor (xToFreq (x - 1.f) / binHz));
        const int k1 = jlimit (k0, kBins - 1, (int) std::ceil (xToFreq (x + 1.f) / binHz));
        float db;
        if (k1 - k0 <= 1)
        {
            const float fc = xToFreq (x) / binHz;
            const int k = jlimit (1, kBins - 2, (int) fc);
            const float fr = jlimit (0.f, 1.f, fc - (float) k);
            db = sm[(size_t) k] + fr * (sm[(size_t) k + 1] - sm[(size_t) k]);
        }
        else
        {
            db = -120.f;
            for (int k = k0; k <= k1; ++k) db = jmax (db, sm[(size_t) k]);
        }
        const float tilt = 3.f * std::log2 (xToFreq (x) / 1000.f);   // compensa la caida natural del espectro
        const float y = bot - jlimit (0.f, 1.f, (db + tilt + 100.f) / 94.f) * h;
        if (! started) { p.startNewSubPath (x, y); started = true; }
        else           p.lineTo (x, y);
    }
    return p;
}

Path XquilerAudioProcessorEditor::curvePath (const std::vector<std::array<Coeffs::Ptr, 2>>& cs) const
{
    Path p;
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    bool started = false;
    for (float x = (float) graph.getX(); x <= (float) graph.getRight(); x += 2.f)
    {
        const double f = xToFreq (x);
        double mag = 1.0;
        for (auto& c : cs)
            for (auto& ptr : c)
                if (ptr != nullptr) mag *= ptr->getMagnitudeForFrequency (f, sr);
        const float db = Decibels::gainToDecibels ((float) mag, -120.f);
        const float y = jlimit ((float) graph.getY(), (float) graph.getBottom(), dbToY (db));
        if (! started) { p.startNewSubPath (x, y); started = true; }
        else           p.lineTo (x, y);
    }
    return p;
}

void XquilerAudioProcessorEditor::paint (Graphics& g)
{
    const auto p = pal();
    g.fillAll (p.bg);

    // titulo
    g.setColour (p.fg);
    g.setFont (22.f);
    g.drawText ("xQUILER", 56, 12, 240, 28, Justification::centredLeft);

    // ---- rejilla ----
    const float gx0 = (float) graph.getX(), gx1 = (float) graph.getRight();
    const float gy0 = (float) graph.getY(), gy1 = (float) graph.getBottom();
    g.setFont (10.f);

    const float fl[] { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : fl)
    {
        const float x = freqToX (f);
        g.setColour (p.grid);
        g.drawVerticalLine ((int) x, gy0, gy1);
        g.setColour (p.dim);
        const String t = f >= 1000.f ? String ((int) (f / 1000.f)) + "k" : String ((int) f);
        g.drawText (t, (int) x - 20, graph.getBottom() + 3, 40, 12, Justification::centred);
    }
    const int R = rangeDb();
    for (int db = -R; db <= R; db += R / 2)
    {
        const float y = dbToY ((float) db);
        g.setColour (db == 0 ? p.dim.withAlpha (0.7f) : p.grid);
        g.drawHorizontalLine ((int) y, gx0, gx1);
        g.setColour (p.dim);
        g.drawText ((db > 0 ? "+" : "") + String (db), 4, (int) y - 6, 46, 12, Justification::centredRight);
    }
    g.setColour (p.grid);
    g.drawRect (graph);

    // ---- contenido recortado a la grafica ----
    {
        Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (graph);

        // espectro de entrada (linea tenue)
        g.setColour (p.dim.withAlpha (0.55f));
        g.strokePath (spectrumPath (smIn), PathStrokeType (1.f));

        // espectro de salida (relleno)
        Path outLine = spectrumPath (smOut);
        Path outFill = outLine;
        outFill.lineTo (gx1, gy1);
        outFill.lineTo (gx0, gy1);
        outFill.closeSubPath();
        g.setColour (p.fg.withAlpha (0.14f));
        g.fillPath (outFill);
        g.setColour (p.fg.withAlpha (0.5f));
        g.strokePath (outLine, PathStrokeType (1.f));

        // curva del EQ
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
        std::vector<std::array<Coeffs::Ptr, 2>> all, one;
        for (int b = 0; b < XquilerAudioProcessor::kBands; ++b)
        {
            if (getParam (b, "on") < 0.5f) continue;
            auto cs = XquilerAudioProcessor::makeBandCoeffs (sr, getParam (b, "freq"), getParam (b, "gain"),
                                                             getParam (b, "q"), (int) std::round (getParam (b, "type")));
            all.push_back (cs);
            if (b == selected) one.push_back (cs);
        }
        const float y0 = dbToY (0.f);

        if (! one.empty())
        {
            Path bp = curvePath (one);
            bp.lineTo (gx1, y0);
            bp.lineTo (gx0, y0);
            bp.closeSubPath();
            g.setColour (p.fg.withAlpha (0.12f));
            g.fillPath (bp);
        }
        if (! all.empty())
        {
            Path cp = curvePath (all);
            g.setColour (p.fg);
            g.strokePath (cp, PathStrokeType (2.f));
        }
        else
        {
            g.setColour (p.fg.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y0, gx0, gx1);
        }
    }

    // ---- puntos ----
    for (int b = 0; b < XquilerAudioProcessor::kBands; ++b)
    {
        if (getParam (b, "on") < 0.5f) continue;
        const auto pt = pointPos (b);
        const bool sel = (b == selected);
        const float r = 8.f;

        if (b == hover || b == dragBand)
        {
            g.setColour (p.fg.withAlpha (0.2f));
            g.fillEllipse (pt.x - 13.f, pt.y - 13.f, 26.f, 26.f);
        }
        g.setColour (sel ? p.fg : p.bg);
        g.fillEllipse (pt.x - r, pt.y - r, 2.f * r, 2.f * r);
        g.setColour (p.fg);
        g.drawEllipse (pt.x - r, pt.y - r, 2.f * r, 2.f * r, 1.5f);
        g.setColour (sel ? p.bg : p.fg);
        g.setFont (10.f);
        g.drawText (String (b + 1), Rectangle<float> (pt.x - r, pt.y - r, 2.f * r, 2.f * r), Justification::centred);
    }

    // etiqueta de la banda bajo el cursor
    const int tip = dragBand >= 0 ? dragBand : hover;
    if (tip >= 0)
    {
        const auto pt = pointPos (tip);
        const int t = (int) std::round (getParam (tip, "type"));
        String txt = fmtFreq (getParam (tip, "freq"));
        if (t <= 2) txt += "   " + String (getParam (tip, "gain"), 1) + " dB";
        auto box = Rectangle<float> (pt.x - 62.f, pt.y - 36.f, 124.f, 18.f)
                       .constrainedWithin (graph.toFloat());
        g.setColour (p.bg);
        g.fillRoundedRectangle (box, 3.f);
        g.setColour (p.dim);
        g.drawRoundedRectangle (box, 3.f, 1.f);
        g.setColour (p.fg);
        g.setFont (11.f);
        g.drawText (txt, box, Justification::centred);
    }

    // ---- panel inferior ----
    g.setFont (11.f);
    g.setColour (p.dim);
    if (selected >= 0)
    {
        g.setColour (p.fg);
        g.drawText ("BAND " + String (selected + 1), 56, 424, 140, 18, Justification::centredLeft);
        g.setColour (p.dim);
        g.drawText ("FREQ", freqKnob.getX(), 426, freqKnob.getWidth(), 12, Justification::centred);
        g.drawText ("GAIN", gainKnob.getX(), 426, gainKnob.getWidth(), 12, Justification::centred);
        g.drawText ("Q",    qKnob.getX(),    426, qKnob.getWidth(),    12, Justification::centred);
    }
    else
    {
        g.drawText ("Double-click on the graph to add a band", 56, 440, 360, 16, Justification::centredLeft);
        g.drawText ("Drag to move  -  wheel = Q  -  double-click or right-click on a point = remove", 56, 460, 520, 16, Justification::centredLeft);
    }
    g.setColour (p.dim);
    g.drawText ("OUTPUT", outKnob.getX(), 426, outKnob.getWidth(), 12, Justification::centred);
}
