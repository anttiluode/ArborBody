#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;
using namespace juce;

static const char* kExciters[] = { "Mallet", "Pluck", "Noise", nullptr };

APVTS::ParameterLayout ArborBodyAudioProcessor::makeLayout()
{
    APVTS::ParameterLayout l;
    auto f = [](const char* id, const char* nm, float lo, float hi, float def, float skew = 1.f) {
        return std::make_unique<AudioParameterFloat>(
            ParameterID{ id, 1 }, nm, NormalisableRange<float>(lo, hi, 0.f, skew), def);
    };
    // body
    l.add(std::make_unique<AudioParameterInt>(ParameterID{ "seed", 1 }, "Seed", 1, 999, 7));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{ "cells", 1 }, "Cells", 24, 220, 90));
    l.add(f("loops", "Loops", 0.f, 1.f, 0.25f));
    l.add(f("size", "Size", 10.f, 90.f, 34.f));
    l.add(std::make_unique<AudioParameterInt>(ParameterID{ "voices", 1 }, "Voices", 1, 12, 6));
    // material
    l.add(f("decay", "Decay", 0.2f, 30.f, 4.f, 0.4f));
    l.add(f("damping", "Damping", 0.f, 0.9f, 0.30f));
    l.add(f("tension", "Tension", 0.f, 1.f, 0.f));
    l.add(f("release", "Release", 0.02f, 1.f, 0.10f, 0.5f));
    l.add(f("tune", "Tune", -12.f, 12.f, 0.f));
    // exciter
    l.add(std::make_unique<AudioParameterChoice>(ParameterID{ "exciter", 1 }, "Exciter",
                                                 StringArray(kExciters), 0));
    l.add(f("hardness", "Hardness", 0.f, 1.f, 0.45f));
    // positions, normalised to the body's bounding box so they survive regrowth
    l.add(f("strikeX", "Strike X", 0.f, 1.f, 0.35f));
    l.add(f("strikeY", "Strike Y", 0.f, 1.f, 0.45f));
    l.add(f("pickAX", "Pickup L X", 0.f, 1.f, 0.30f));
    l.add(f("pickAY", "Pickup L Y", 0.f, 1.f, 0.70f));
    l.add(f("pickBX", "Pickup R X", 0.f, 1.f, 0.75f));
    l.add(f("pickBY", "Pickup R Y", 0.f, 1.f, 0.35f));
    l.add(f("level", "Level", 0.f, 1.5f, 0.8f));
    return l;
}

ArborBodyAudioProcessor::ArborBodyAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ArborBody", makeLayout())
{
    startTimerHz(12);
}

int ArborBodyAudioProcessor::nodeFor(float nx, float ny) const
{
    const auto& g = live_.load()->graph();
    float x0, y0, x1, y1; g.bounds(x0, y0, x1, y1);
    return g.nearest(x0 + nx * (x1 - x0), y0 + ny * (y1 - y0));
}

void ArborBodyAudioProcessor::normalisedFor(int node, float& nx, float& ny) const
{
    const auto& g = live_.load()->graph();
    if (node < 0 || node >= g.numNodes()) { nx = ny = 0.5f; return; }
    float x0, y0, x1, y1; g.bounds(x0, y0, x1, y1);
    nx = (g.nodes()[(size_t)node].x - x0) / std::max(1e-6f, x1 - x0);
    ny = (g.nodes()[(size_t)node].y - y0) / std::max(1e-6f, y1 - y0);
}

ab::Params ArborBodyAudioProcessor::readParams() const
{
    auto g = [&](const char* id) { return apvts.getRawParameterValue(id)->load(); };
    ab::Params p;
    p.seed      = (int)g("seed");
    p.cells     = (int)g("cells");
    p.loops     = g("loops");
    p.size      = g("size");
    p.maxVoices = (int)g("voices");
    p.decay     = g("decay");
    p.damping   = g("damping");
    p.tension   = g("tension");
    p.release   = g("release");
    p.tune      = g("tune");
    p.exciter   = (ab::ExciterType)(int)g("exciter");
    p.hardness  = g("hardness");
    p.level     = g("level");
    return p;
}

void ArborBodyAudioProcessor::prepareToPlay(double sampleRate, int)
{
    fs_ = sampleRate;
    auto p = readParams();
    slotA_.prepare(sampleRate, p);
    slotB_.prepare(sampleRate, p);
    live_.store(&slotA_);
    lastBody_ = p;
    fadeLen_ = juce::jmax(64, (int)(0.006 * sampleRate));
    needsRegrow_ = false;
}

// Growth allocates, so it happens here on the message thread, into whichever
// slot is not currently sounding. The audio thread only ever swaps a pointer.
void ArborBodyAudioProcessor::timerCallback()
{
    auto p = readParams();
    bool bodyChanged = p.seed != lastBody_.seed || p.cells != lastBody_.cells
                    || std::abs(p.loops - lastBody_.loops) > 1e-6f
                    || std::abs(p.size - lastBody_.size) > 1e-6f
                    || p.maxVoices != lastBody_.maxVoices;
    if (!bodyChanged && !needsRegrow_) return;
    if (pending_.load() != nullptr) return;      // a swap is already queued

    ab::Instrument* other = (live_.load() == &slotA_) ? &slotB_ : &slotA_;
    other->prepare(fs_, p);
    lastBody_ = p;
    needsRegrow_ = false;
    pending_.store(other);
}

void ArborBodyAudioProcessor::swapBody()
{
    if (auto* n = pending_.load()) { live_.store(n); pending_.store(nullptr); }
}

void ArborBodyAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    auto* L = buffer.getWritePointer(0);
    auto* R = buffer.getWritePointer(1);

    ab::Instrument* ins = live_.load();

    // Live (non-allocating) parameters, plus positions resolved to nodes.
    ab::Params p = readParams();
    // Keep whatever body the audio thread currently holds. Body changes only
    // ever arrive through swapBody(), never through setParams() here, because
    // regrowing allocates.
    const ab::Params& cur = ins->params();
    p.seed = cur.seed; p.cells = cur.cells; p.loops = cur.loops;
    p.size = cur.size; p.maxVoices = cur.maxVoices;
    {
        const auto& g = ins->graph();
        float x0, y0, x1, y1; g.bounds(x0, y0, x1, y1);
        auto rp = [&](const char* xi, const char* yi) {
            float nx = apvts.getRawParameterValue(xi)->load();
            float ny = apvts.getRawParameterValue(yi)->load();
            return g.nearest(x0 + nx * (x1 - x0), y0 + ny * (y1 - y0));
        };
        p.strike = rp("strikeX", "strikeY");
        p.pickA  = rp("pickAX",  "pickAY");
        p.pickB  = rp("pickBX",  "pickBY");
    }
    ins->setParams(p);

    // Render in sub-blocks split at MIDI events so timing is sample accurate.
    int pos = 0;
    for (const auto meta : midi) {
        const int t = juce::jlimit(0, n, meta.samplePosition);
        if (t > pos) { ins->render(L + pos, R + pos, t - pos); pos = t; }
        const auto m = meta.getMessage();
        if (m.isNoteOn())        ins->noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())  ins->noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) ins->allNotesOff();
    }
    if (pos < n) ins->render(L + pos, R + pos, n - pos);

    // Fade around a body swap: growth changes the whole delay network, so the
    // signal has to go through zero or it clicks.
    if (pending_.load() != nullptr && fadeState_ == Fade::none) { fadeState_ = Fade::out; fade_ = fadeLen_; }
    if (fadeState_ != Fade::none) {
        for (int i = 0; i < n; ++i) {
            float g;
            if (fadeState_ == Fade::out) {
                g = (float)fade_ / (float)fadeLen_;
                if (--fade_ <= 0) { swapBody(); fadeState_ = Fade::in; fade_ = 0; }
            } else {
                g = (float)fade_ / (float)fadeLen_;
                if (++fade_ >= fadeLen_) { fadeState_ = Fade::none; }
            }
            L[i] *= g; R[i] *= g;
        }
    }
}

AudioProcessorEditor* ArborBodyAudioProcessor::createEditor() { return new ArborBodyEditor(*this); }

void ArborBodyAudioProcessor::getStateInformation(MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}

void ArborBodyAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) {
            apvts.replaceState(ValueTree::fromXml(*xml));
            needsRegrow_ = true;
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ArborBodyAudioProcessor(); }
