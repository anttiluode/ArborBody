#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// ---- palette -------------------------------------------------------------
// Warm near-black ground, bone structure, ember energy. The three markers are
// the only saturated colour in the plugin, and each one means something:
// where you hit it, and the two places you are listening from.
namespace ABColour {
    const juce::Colour ground   { 0xff14110e };
    const juce::Colour panel    { 0xff1d1915 };
    const juce::Colour rule     { 0xff2e2822 };
    const juce::Colour bone     { 0xff9c9282 };
    const juce::Colour boneHi   { 0xffd8cdb9 };
    const juce::Colour ember    { 0xfff2a03d };
    const juce::Colour strike   { 0xffff6b35 };
    const juce::Colour pickL    { 0xff6fd6c2 };
    const juce::Colour pickR    { 0xffd96fa8 };
    const juce::Colour text     { 0xff8b8275 };
}

class BodyView : public juce::Component, private juce::Timer
{
public:
    explicit BodyView(ArborBodyAudioProcessor& p);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Point<float> toScreen(float bx, float by) const;
    void fromScreen(juce::Point<float> p, float& nx, float& ny) const;
    int  grabHandle(juce::Point<float> p) const;   // 0 strike, 1 L, 2 R, -1 none

    ArborBodyAudioProcessor& proc_;
    std::vector<float> energy_, glow_;
    int dragging_ = -1;
    float sx_ = 1.f, sy_ = 1.f, ox_ = 0.f, oy_ = 0.f;   // body -> screen
};

class ArborBodyEditor : public juce::AudioProcessorEditor
{
public:
    explicit ArborBodyEditor(ArborBodyAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob {
        juce::Slider s;
        juce::Label  l;
        std::unique_ptr<SA> a;
    };
    void addKnob(Knob& k, const char* id, const char* name);

    ArborBodyAudioProcessor& proc_;
    BodyView view_;
    Knob seed_, cells_, loops_, size_, decay_, damping_, tension_, hardness_,
         release_, tune_, level_, voices_;
    juce::ComboBox exciter_;
    juce::Label exciterLabel_;
    std::unique_ptr<CA> exciterAtt_;
    juce::TextButton reroll_ { "Regrow" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArborBodyEditor)
};
