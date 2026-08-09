// ArborBody - Core/Instrument.h
// The whole instrument with no JUCE in sight, so it can be tested with one g++.
#pragma once
#include "Voice.h"
#include <vector>
#include <memory>

namespace ab {

class Instrument {
public:
    static constexpr int kMaxVoices = 12;
    static constexpr float kMaxScale = 8.f;   // lowest supported note = 60 - 36

    void prepare(double sampleRate, const Params& p);
    // Call whenever parameters change. Regrows only if the body changed.
    void setParams(const Params& p);

    void noteOn(int note, float velocity);
    void noteOff(int note);
    void allNotesOff();
    void panic();

    void render(float* L, float* R, int numSamples);

    const ArborGraph& graph() const { return graph_; }
    const Params&     params() const { return p_; }
    int   activeVoices() const;
    // Per-node activity summed over sounding voices, for the display.
    void  nodeEnergy(std::vector<float>& out) const;

private:
    ArborGraph graph_;
    Params p_;
    double fs_ = 48000.0;
    std::vector<std::unique_ptr<Voice>> voices_;
    int   counter_ = 0;
    bool  built_ = false;
    std::vector<float> scratchE_;

    void regrow();
    void pushPositions();
    int  defaultStrike() const;
    Voice* allocate();
};

} // namespace ab
