// ArborBody - Core/Exciter.h
//
// The exciter is the reason this is an instrument and not a reverb. A reverb's
// input is a signal; an instrument's input is a *collision*. The mallet below is
// a real two-way contact: a mass meets the body through a Hertzian spring, the
// body pushes back, and the contact ends when the mass leaves. Because the
// spring stiffens with compression, a hard strike is in contact for less time
// than a soft one, so it excites a different spectrum -- not a louder one.
//
// That is the property a sampler or a convolution has to fake with velocity
// layers, and the property that makes topology worth hearing.
#pragma once
#include <cmath>
#include <cstdint>

namespace ab {

enum class ExciterType { Mallet = 0, Pluck = 1, Noise = 2 };

class Exciter {
public:
    void start(ExciterType type, float velocity, float hardness, double sampleRate)
    {
        type_ = type; fs_ = sampleRate; dt_ = 1.0f / (float)sampleRate;
        vel_ = std::max(0.02f, std::min(velocity, 1.f));
        hard_ = std::max(0.f, std::min(hardness, 1.f));
        done_ = false; n_ = 0; s_ = 0.f;

        switch (type_) {
        case ExciterType::Mallet: {
            float fc    = 220.f * std::pow(9.f, hard_);        // 220 Hz .. ~2 kHz
            float omega = 6.2831853f * fc;
            alpha_ = 1.6f + 2.0f * hard_;                       // felt exponent
            K_     = omega * omega * std::pow(uref_, 1.f - alpha_);
            h_     = 0.f;
            w_     = -(0.35f + 3.1f * vel_ * vel_);             // approach speed
            maxN_  = (int)(0.05 * fs_);
            break;
        }
        case ExciterType::Pluck: {
            width_ = std::max(4, (int)((48.f - 40.f * hard_) * (float)(fs_ / 48000.0)));
            maxN_  = width_;
            break;
        }
        case ExciterType::Noise: {
            tau_   = (float)std::exp(-1.0 / (0.006 * fs_ * (1.0 + 2.0 * (1.f - hard_))));
            lpc_   = 0.85f - 0.8f * hard_;
            env_   = 1.f; rng_ = 0x2545F491u + (uint32_t)(vel_ * 100000.f);
            maxN_  = (int)(0.12 * fs_);
            break;
        }
        }
    }

    // nodeVel = junction pressure at the strike point, this sample.
    // Returns the value to add to that junction.
    float step(float nodeVel)
    {
        if (done_) return 0.f;
        ++n_;
        if (n_ > maxN_) { done_ = true; return 0.f; }

        switch (type_) {
        case ExciterType::Mallet: {
            s_ += nodeVel * dt_;                 // body surface displacement
            s_ *= 0.9999f;                       // no DC wander over a long note
            float u = s_ - h_;
            if (u <= 0.f) {
                if (n_ > 4) { done_ = true; }    // hammer has left the body
                h_ += w_ * dt_;
                return 0.f;
            }
            float F = K_ * std::pow(u, alpha_);
            if (!(F < 1e12f)) { done_ = true; return 0.f; }
            w_ += F * dt_;                       // contact decelerates and rebounds it
            h_ += w_ * dt_;
            return F * dt_ * inj_;
        }
        case ExciterType::Pluck: {
            float p = (float)n_ / (float)width_;
            float v = 0.5f * (1.f - std::cos(6.2831853f * p));
            if (n_ >= width_) done_ = true;
            return v * vel_ * 4.0f;
        }
        case ExciterType::Noise: {
            rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
            float r = (float)((int32_t)rng_) * (1.f / 2147483648.f);
            lpz_ = r + lpc_ * (lpz_ - r);
            env_ *= tau_;
            if (env_ < 1e-4f) done_ = true;
            return lpz_ * env_ * vel_ * 4.4f;
        }
        }
        return 0.f;
    }

    bool  done() const { return done_; }
    // Diagnostics used by the tests.
    int   samplesElapsed() const { return n_; }

private:
    ExciterType type_ = ExciterType::Mallet;
    double fs_ = 48000.0; float dt_ = 1.f / 48000.f;
    float vel_ = 1.f, hard_ = 0.5f;
    bool  done_ = true;
    int   n_ = 0, maxN_ = 0;

    // mallet
    static constexpr float uref_ = 1e-3f;
    static constexpr float inj_  = 9.0f;
    float K_ = 0.f, alpha_ = 2.f, h_ = 0.f, w_ = 0.f, s_ = 0.f;
    // pluck
    int   width_ = 32;
    // noise
    float tau_ = 0.999f, lpc_ = 0.5f, lpz_ = 0.f, env_ = 0.f;
    uint32_t rng_ = 1u;
};

} // namespace ab
