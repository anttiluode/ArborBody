#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Core/Instrument.h"
#include <atomic>

class ArborBodyAudioProcessor : public juce::AudioProcessor,
                                private juce::Timer
{
public:
    ArborBodyAudioProcessor();
    ~ArborBodyAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override
    { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ArborBody"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }
    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();

    // --- for the editor ---
    // The live body. Safe to read from the message thread: it is only ever
    // replaced by the message thread itself, in swapBody().
    const ab::ArborGraph& graph() const { return live_.load()->graph(); }
    void  nodeEnergy(std::vector<float>& out) const { live_.load()->nodeEnergy(out); }
    int   activeVoices() const { return live_.load()->activeVoices(); }
    int   nodeFor(float nx, float ny) const;      // normalised body coords -> node
    void  normalisedFor(int node, float& nx, float& ny) const;
    void  requestRegrow() { needsRegrow_ = true; }

private:
    void timerCallback() override;
    void swapBody();
    ab::Params readParams() const;

    ab::Instrument slotA_, slotB_;
    std::atomic<ab::Instrument*> live_ { &slotA_ };
    std::atomic<ab::Instrument*> pending_ { nullptr };
    std::atomic<bool> needsRegrow_ { true };
    double fs_ = 48000.0;

    // short fade around a body swap so growth never clicks
    int fade_ = 0, fadeLen_ = 256;
    enum class Fade { none, out, in } fadeState_ = Fade::none;

    ab::Params lastBody_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArborBodyAudioProcessor)
};
