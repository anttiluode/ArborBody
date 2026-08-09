#include "Voice.h"
#include <algorithm>

namespace ab {

void Voice::prepare(const ArborGraph& g, double sampleRate, float samplesPerUnit, float maxScale)
{
    g_ = &g; fs_ = sampleRate;
    mesh_.build(g, sampleRate, samplesPerUnit, maxScale);
    xfadeLen_ = (int)(0.020 * sampleRate);
    pa_ = pb_ = paOld_ = pbOld_ = 0; strike_ = 0;
    active_ = false; env_ = 0.f;
}

void Voice::applyBody(const Params& p)
{
    mesh_.setDecay(held_ ? p.decay : p.decay * p.release, p.damping);
    mesh_.setTension(p.tension);
}

void Voice::noteOn(int midiNote, float velocity, const Params& p)
{
    note_ = midiNote; held_ = true; active_ = true; age_ = 0; env_ = 0.f; peaked_ = false;
    dcA1_ = dcA2_ = dcB1_ = dcB2_ = 0.f;
    // All edge delays scale together, so the whole modal spectrum transposes
    // exactly. 60 = reference.
    float semis = (float)(midiNote - 60) + p.tune;
    mesh_.setTuning(std::pow(2.f, -semis / 12.f));
    mesh_.setDecay(p.decay, p.damping);
    mesh_.setTension(p.tension);
    mesh_.reset();
    xfade_ = 0;          // a fresh note has no previous pickup to fade from
    paOld_ = pa_; pbOld_ = pb_;
    exc_.start(p.exciter, velocity, p.hardness, fs_);
    gain_ = 1.f;
}

void Voice::noteOff(const Params& p)
{
    if (!held_) return;
    held_ = false;
    mesh_.setDecay(p.decay * p.release, p.damping);   // damper falls on the body
}

void Voice::setPickups(int a, int b)
{
    if (a == pa_ && b == pb_) return;
    paOld_ = pa_; pbOld_ = pb_;
    pa_ = a; pb_ = b;
    xfade_ = xfadeLen_;
}

bool Voice::render(float* L, float* R, int numSamples)
{
    if (!active_) return false;
    const float xinv = (xfadeLen_ > 0) ? 1.f / (float)xfadeLen_ : 0.f;

    for (int i = 0; i < numSamples; ++i) {
        mesh_.junction();
        if (!exc_.done()) {
            float v = mesh_.nodeVel(strike_);
            mesh_.inject(strike_, exc_.step(v));
        }

        float a = mesh_.pickup(pa_);
        float b = mesh_.pickup(pb_);
        if (xfade_ > 0) {
            float t = 1.f - (float)xfade_ * xinv;
            a = mesh_.pickup(paOld_) * (1.f - t) + a * t;
            b = mesh_.pickup(pbOld_) * (1.f - t) + b * t;
            --xfade_;
        }
        mesh_.scatter();

        // DC blockers: the junction pressure has no reason to be centred.
        float oa = a - dcA1_ + 0.9985f * dcA2_; dcA1_ = a; dcA2_ = oa;
        float ob = b - dcB1_ + 0.9985f * dcB2_; dcB1_ = b; dcB2_ = ob;

        L[i] += oa; R[i] += ob;
        float m = std::max(std::abs(oa), std::abs(ob));
        env_ = (m > env_) ? m : env_ + 0.00005f * (m - env_);
    }

    age_ += numSamples;
    // A voice must be allowed to actually start before it can be declared over.
    // At a 64-sample buffer the mallet contact is finished and the wavefront has
    // not yet reached the pickup, so a naive "exciter done and quiet" test kills
    // every note before it sounds. Wait for the envelope to have risen at least
    // once, and never retire a voice inside its first 150 ms.
    if (env_ > 1.0e-3f) peaked_ = true;
    if (exc_.done() && age_ > (int)(0.15 * fs_)) {
        bool over = peaked_ ? (env_ < 2.0e-5f) : (age_ > (int)(2.0 * fs_));
        if (over) { active_ = false; mesh_.reset(); }
    }
    return active_;
}

} // namespace ab
