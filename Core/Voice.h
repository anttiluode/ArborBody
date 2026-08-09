// ArborBody - Core/Voice.h
#pragma once
#include "Mesh.h"
#include "Exciter.h"
#include "Params.h"

namespace ab {

class Voice {
public:
    void prepare(const ArborGraph& g, double sampleRate, float samplesPerUnit, float maxScale);
    void applyBody(const Params& p);          // decay / damping / tension, live
    void noteOn(int midiNote, float velocity, const Params& p);
    void noteOff(const Params& p);
    void setPickups(int a, int b);            // crossfaded, safe while ringing
    void setStrike(int s) { strike_ = s; }
    void kill() { active_ = false; mesh_.reset(); }

    // Adds into L/R. Returns false once the voice has fallen silent.
    bool render(float* L, float* R, int numSamples);

    bool  isActive() const { return active_; }
    int   note() const { return note_; }
    bool  held() const { return held_; }
    int   age() const { return age_; }
    float loudness() const { return env_; }
    const Mesh& mesh() const { return mesh_; }

private:
    Mesh  mesh_;
    Exciter exc_;
    const ArborGraph* g_ = nullptr;
    double fs_ = 48000.0;
    bool  active_ = false, held_ = false;
    int   note_ = 60, age_ = 0;
    int   strike_ = 0, pa_ = 0, pb_ = 0, paOld_ = 0, pbOld_ = 0;
    int   xfade_ = 0, xfadeLen_ = 0;
    float env_ = 0.f;
    bool  peaked_ = false;
    float dcA1_ = 0.f, dcA2_ = 0.f, dcB1_ = 0.f, dcB2_ = 0.f;
    float gain_ = 1.f;
};

} // namespace ab
