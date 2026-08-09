#include "PluginEditor.h"

using namespace juce;

// =========================================================== BodyView =======
BodyView::BodyView(ArborBodyAudioProcessor& p) : proc_(p) { startTimerHz(30); }

void BodyView::timerCallback()
{
    proc_.nodeEnergy(energy_);
    if (glow_.size() != energy_.size()) glow_.assign(energy_.size(), 0.f);
    for (size_t i = 0; i < glow_.size(); ++i) {
        float e = std::sqrt(energy_[i]) * 6.f;
        glow_[i] = (e > glow_[i]) ? e : glow_[i] + 0.14f * (e - glow_[i]);
    }
    repaint();
}

Point<float> BodyView::toScreen(float bx, float by) const
{
    return { ox_ + bx * sx_, oy_ + by * sy_ };
}

void BodyView::fromScreen(Point<float> p, float& nx, float& ny) const
{
    const auto& g = proc_.graph();
    float x0, y0, x1, y1; g.bounds(x0, y0, x1, y1);
    float bx = (p.x - ox_) / sx_, by = (p.y - oy_) / sy_;
    nx = jlimit(0.f, 1.f, (bx - x0) / jmax(1e-6f, x1 - x0));
    ny = jlimit(0.f, 1.f, (by - y0) / jmax(1e-6f, y1 - y0));
}

void BodyView::paint(Graphics& g)
{
    const auto& gr = proc_.graph();
    auto r = getLocalBounds().toFloat().reduced(18.f);
    float x0, y0, x1, y1; gr.bounds(x0, y0, x1, y1);
    float s = jmin(r.getWidth() / jmax(1e-6f, x1 - x0), r.getHeight() / jmax(1e-6f, y1 - y0));
    sx_ = sy_ = s;
    ox_ = r.getCentreX() - 0.5f * (x0 + x1) * s;
    oy_ = r.getCentreY() - 0.5f * (y0 + y1) * s;

    g.fillAll(ABColour::ground);

    // energy bloom, drawn under the structure
    if (glow_.size() == (size_t)gr.numNodes()) {
        for (int i = 0; i < gr.numNodes(); ++i) {
            float e = jlimit(0.f, 1.f, glow_[(size_t)i]);
            if (e < 0.01f) continue;
            auto pt = toScreen(gr.nodes()[(size_t)i].x, gr.nodes()[(size_t)i].y);
            float rad = 4.f + 26.f * e;
            g.setGradientFill(ColourGradient(ABColour::ember.withAlpha(0.30f * e), pt.x, pt.y,
                                             ABColour::ember.withAlpha(0.f), pt.x + rad, pt.y, true));
            g.fillEllipse(pt.x - rad, pt.y - rad, rad * 2.f, rad * 2.f);
        }
    }

    // structure: tree branches solid, added loops hairline, so the topology
    // control is legible at a glance
    for (const auto& e : gr.edges()) {
        auto a = toScreen(gr.nodes()[(size_t)e.a].x, gr.nodes()[(size_t)e.a].y);
        auto b = toScreen(gr.nodes()[(size_t)e.b].x, gr.nodes()[(size_t)e.b].y);
        g.setColour(e.loop ? ABColour::bone.withAlpha(0.30f) : ABColour::bone.withAlpha(0.85f));
        g.drawLine(a.x, a.y, b.x, b.y, e.loop ? 0.7f : 1.25f);
    }
    for (int i = 0; i < gr.numNodes(); ++i) {
        auto pt = toScreen(gr.nodes()[(size_t)i].x, gr.nodes()[(size_t)i].y);
        g.setColour(ABColour::boneHi.withAlpha(0.55f));
        g.fillEllipse(pt.x - 1.1f, pt.y - 1.1f, 2.2f, 2.2f);
    }

    // markers
    struct M { const char* label; Colour c; const char* xi; const char* yi; bool filled; };
    const M ms[3] = {
        { "STRIKE", ABColour::strike, "strikeX", "strikeY", true  },
        { "L",      ABColour::pickL,  "pickAX",  "pickAY",  false },
        { "R",      ABColour::pickR,  "pickBX",  "pickBY",  false },
    };
    for (const auto& m : ms) {
        float nx = proc_.apvts.getRawParameterValue(m.xi)->load();
        float ny = proc_.apvts.getRawParameterValue(m.yi)->load();
        int nd = proc_.nodeFor(nx, ny);
        if (nd < 0) continue;
        auto pt = toScreen(gr.nodes()[(size_t)nd].x, gr.nodes()[(size_t)nd].y);
        g.setColour(m.c);
        if (m.filled) g.fillEllipse(pt.x - 4.5f, pt.y - 4.5f, 9.f, 9.f);
        else { g.drawEllipse(pt.x - 6.f, pt.y - 6.f, 12.f, 12.f, 1.6f);
               g.fillEllipse(pt.x - 1.6f, pt.y - 1.6f, 3.2f, 3.2f); }
        g.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), 10.f, Font::plain)));
        g.drawText(m.label, (int)pt.x + 10, (int)pt.y - 8, 60, 16, Justification::centredLeft);
    }

    g.setColour(ABColour::text);
    g.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), 11.f, Font::plain)));
    g.drawText(String(gr.numNodes()) + " cells / " + String(gr.numEdges()) + " branches   "
               + String(proc_.activeVoices()) + " voices",
               12, getHeight() - 24, getWidth() - 24, 16, Justification::centredLeft);
    g.setColour(ABColour::text.withAlpha(0.6f));
    g.drawText("drag STRIKE / L / R", 12, getHeight() - 24, getWidth() - 24, 16, Justification::centredRight);
}

int BodyView::grabHandle(Point<float> p) const
{
    const auto& gr = proc_.graph();
    const char* xs[3] = { "strikeX", "pickAX", "pickBX" };
    const char* ys[3] = { "strikeY", "pickAY", "pickBY" };
    int best = -1; float bd = 18.f * 18.f;
    for (int i = 0; i < 3; ++i) {
        int nd = proc_.nodeFor(proc_.apvts.getRawParameterValue(xs[i])->load(),
                               proc_.apvts.getRawParameterValue(ys[i])->load());
        if (nd < 0) continue;
        auto pt = toScreen(gr.nodes()[(size_t)nd].x, gr.nodes()[(size_t)nd].y);
        float d = pt.getDistanceSquaredFrom(p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void BodyView::mouseDown(const MouseEvent& e)
{
    dragging_ = grabHandle(e.position);
    if (dragging_ < 0) dragging_ = e.mods.isShiftDown() ? 2 : 1;   // shift = right pickup
    mouseDrag(e);
}

void BodyView::mouseDrag(const MouseEvent& e)
{
    if (dragging_ < 0) return;
    const char* xs[3] = { "strikeX", "pickAX", "pickBX" };
    const char* ys[3] = { "strikeY", "pickAY", "pickBY" };
    float nx, ny; fromScreen(e.position, nx, ny);
    if (auto* px = proc_.apvts.getParameter(xs[dragging_])) { px->beginChangeGesture(); px->setValueNotifyingHost(nx); px->endChangeGesture(); }
    if (auto* py = proc_.apvts.getParameter(ys[dragging_])) { py->beginChangeGesture(); py->setValueNotifyingHost(ny); py->endChangeGesture(); }
    repaint();
}

void BodyView::mouseUp(const MouseEvent&) { dragging_ = -1; }

// ======================================================== Editor ============
void ArborBodyEditor::addKnob(Knob& k, const char* id, const char* name)
{
    k.s.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    k.s.setTextBoxStyle(Slider::TextBoxBelow, false, 62, 14);
    k.s.setColour(Slider::rotarySliderFillColourId, ABColour::ember);
    k.s.setColour(Slider::rotarySliderOutlineColourId, ABColour::rule);
    k.s.setColour(Slider::thumbColourId, ABColour::boneHi);
    k.s.setColour(Slider::textBoxTextColourId, ABColour::text);
    k.s.setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
    addAndMakeVisible(k.s);
    k.l.setText(name, dontSendNotification);
    k.l.setJustificationType(Justification::centred);
    k.l.setColour(Label::textColourId, ABColour::text);
    k.l.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), 10.f, Font::plain)));
    addAndMakeVisible(k.l);
    k.a = std::make_unique<SA>(proc_.apvts, id, k.s);
}

ArborBodyEditor::ArborBodyEditor(ArborBodyAudioProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), view_(p)
{
    addAndMakeVisible(view_);
    addKnob(seed_,     "seed",     "SEED");
    addKnob(cells_,    "cells",    "CELLS");
    addKnob(loops_,    "loops",    "LOOPS");
    addKnob(size_,     "size",     "SIZE");
    addKnob(decay_,    "decay",    "DECAY");
    addKnob(damping_,  "damping",  "DAMPING");
    addKnob(tension_,  "tension",  "TENSION");
    addKnob(hardness_, "hardness", "HARDNESS");
    addKnob(release_,  "release",  "RELEASE");
    addKnob(tune_,     "tune",     "TUNE");
    addKnob(level_,    "level",    "LEVEL");
    addKnob(voices_,   "voices",   "VOICES");

    exciter_.addItemList({ "Mallet", "Pluck", "Noise" }, 1);
    exciter_.setColour(ComboBox::backgroundColourId, ABColour::panel);
    exciter_.setColour(ComboBox::textColourId, ABColour::boneHi);
    exciter_.setColour(ComboBox::outlineColourId, ABColour::rule);
    addAndMakeVisible(exciter_);
    exciterLabel_.setText("EXCITER", dontSendNotification);
    exciterLabel_.setJustificationType(Justification::centred);
    exciterLabel_.setColour(Label::textColourId, ABColour::text);
    exciterLabel_.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), 10.f, Font::plain)));
    addAndMakeVisible(exciterLabel_);
    exciterAtt_ = std::make_unique<CA>(proc_.apvts, "exciter", exciter_);

    reroll_.setColour(TextButton::buttonColourId, ABColour::panel);
    reroll_.setColour(TextButton::textColourOffId, ABColour::ember);
    reroll_.onClick = [this] {
        if (auto* sp = proc_.apvts.getParameter("seed")) {
            sp->beginChangeGesture();
            sp->setValueNotifyingHost(Random::getSystemRandom().nextFloat());
            sp->endChangeGesture();
        }
    };
    addAndMakeVisible(reroll_);

    setResizable(true, true);
    setResizeLimits(760, 460, 2200, 1400);
    setSize(940, 560);
}

void ArborBodyEditor::paint(Graphics& g)
{
    g.fillAll(ABColour::ground);
    auto strip = getLocalBounds().removeFromBottom(148);
    g.setColour(ABColour::panel); g.fillRect(strip);
    g.setColour(ABColour::rule);  g.drawLine((float)strip.getX(), (float)strip.getY(),
                                             (float)strip.getRight(), (float)strip.getY(), 1.f);
    g.setColour(ABColour::boneHi);
    g.setFont(Font(FontOptions(Font::getDefaultMonospacedFontName(), 13.f, Font::bold)));
    g.drawText("ARBORBODY", 16, 12, 240, 18, Justification::centredLeft);
}

void ArborBodyEditor::resized()
{
    auto r = getLocalBounds();
    auto strip = r.removeFromBottom(148).reduced(10, 8);
    view_.setBounds(r);

    Knob* ks[12] = { &seed_, &cells_, &loops_, &size_, &decay_, &damping_,
                     &tension_, &hardness_, &release_, &tune_, &voices_, &level_ };
    const int cols = 13;                       // 12 knobs + the exciter column
    const int w = strip.getWidth() / cols;
    for (int i = 0; i < 12; ++i) {
        auto c = strip.removeFromLeft(w);
        ks[i]->l.setBounds(c.removeFromTop(14));
        ks[i]->s.setBounds(c.reduced(2));
    }
    exciterLabel_.setBounds(strip.removeFromTop(14));
    exciter_.setBounds(strip.removeFromTop(24).reduced(2));
    reroll_.setBounds(strip.removeFromTop(26).reduced(2, 3));
}
