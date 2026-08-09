#include "Mesh.h"
#include <algorithm>

namespace ab {

void Mesh::build(const ArborGraph& g, double sampleRate, float samplesPerUnit, float maxScale)
{
    g_ = &g; fs_ = sampleRate; spu_ = samplesPerUnit; maxScale_ = maxScale;
    nN_ = g.numNodes(); nE_ = g.numEdges();

    off_.assign((size_t)nE_ * 2, 0);
    cap_.assign((size_t)nE_ * 2, 0);
    wr_.assign((size_t)nE_ * 2, 0);
    rawDelay_.assign((size_t)nE_ * 2, 2.f);
    delay_.assign((size_t)nE_ * 2, 2.f);
    gain_.assign((size_t)nE_ * 2, 0.99f);
    lp_.assign((size_t)nE_ * 2, 0.3f);
    lpState_.assign((size_t)nE_ * 2, 0.f);
    arr_.assign((size_t)nE_ * 2, 0.f);

    int total = 0;
    for (int e = 0; e < nE_; ++e) {
        int c = (int)std::ceil(g.edges()[(size_t)e].len * spu_ * maxScale_) + 4;
        c = std::max(c, 8);
        for (int d = 0; d < 2; ++d) { int L = e * 2 + d; off_[(size_t)L] = total; cap_[(size_t)L] = c; total += c; }
    }
    buf_.assign((size_t)total, 0.f);

    pJ_.assign((size_t)nN_, 0.f);
    invDeg2_.assign((size_t)nN_, 0.f);
    for (int i = 0; i < nN_; ++i) {
        int d = g.degree(i);
        invDeg2_[(size_t)i] = (d > 0) ? 2.f / (float)d : 0.f;
    }
    setTuning(1.f);
}

void Mesh::setTuning(float scale)
{
    tuning_ = scale;
    for (int e = 0; e < nE_; ++e) {
        float d = g_->edges()[(size_t)e].len * spu_ * scale;
        for (int k = 0; k < 2; ++k) {
            int L = e * 2 + k;
            rawDelay_[(size_t)L] = std::max(3.f, std::min(d, (float)cap_[(size_t)L] - 2.f));
        }
    }
    recompute();
}

void Mesh::setDecay(float t60Seconds, float damping)
{
    t60_ = std::max(0.02f, t60Seconds);
    damping_ = std::max(0.f, std::min(damping, 0.98f));
    recompute();
}

void Mesh::recompute()
{
    // The damping filter is itself a delay. A one-pole y = (1-d)x + d*y[-1] has
    // group delay d/(1-d) samples at DC, and that is a CONSTANT per traversal --
    // it does not scale with the note. Left uncompensated it flattens every
    // octave (measured: 1.983 instead of 2.000 at damping 0.30). So take it out
    // of the delay line, and the spectrum transposes exactly again.
    const float gd = damping_ / (1.f - damping_);
    for (int L = 0; L < nE_ * 2; ++L) {
        float D = std::max(2.f, rawDelay_[(size_t)L] - gd);
        delay_[(size_t)L] = D;
        // Jot attenuator design: a wave losing g per traversal of D samples
        // decays 60 dB in t60 seconds when g = 10^(-3 D / (t60 fs)). Same rule
        // per edge, so long and short branches ring down together.
        float g = std::pow(10.f, -3.f * rawDelay_[(size_t)L] / (float)(t60_ * fs_));
        gain_[(size_t)L] = std::min(g, 0.99999f);
        lp_[(size_t)L]   = damping_;
    }
}

void Mesh::reset()
{
    std::fill(buf_.begin(), buf_.end(), 0.f);
    std::fill(lpState_.begin(), lpState_.end(), 0.f);
    std::fill(arr_.begin(), arr_.end(), 0.f);
    std::fill(pJ_.begin(), pJ_.end(), 0.f);
    std::fill(wr_.begin(), wr_.end(), 0);
}

void Mesh::junction()
{
    for (int L = 0; L < nE_ * 2; ++L) arr_[(size_t)L] = readLine(L);
    std::fill(pJ_.begin(), pJ_.end(), 0.f);
    const auto& E = g_->edges();
    for (int e = 0; e < nE_; ++e) {
        // line 2e   travels a->b, so it arrives at b
        // line 2e+1 travels b->a, so it arrives at a
        pJ_[(size_t)E[(size_t)e].b] += arr_[(size_t)(e * 2)];
        pJ_[(size_t)E[(size_t)e].a] += arr_[(size_t)(e * 2 + 1)];
    }
    for (int i = 0; i < nN_; ++i) {
        float p = pJ_[(size_t)i] * invDeg2_[(size_t)i];
        if (tenG_ > 1e-4f) p = std::tanh(tenG_ * p) / tenG_;   // contractive
        pJ_[(size_t)i] = p;
    }
}

void Mesh::scatter()
{
    const auto& E = g_->edges();
    for (int e = 0; e < nE_; ++e) {
        int a = E[(size_t)e].a, b = E[(size_t)e].b;
        int L0 = e * 2, L1 = e * 2 + 1;
        float outA = pJ_[(size_t)a] - arr_[(size_t)L1];   // leaves a, enters line a->b
        float outB = pJ_[(size_t)b] - arr_[(size_t)L0];   // leaves b, enters line b->a
        float s0 = lpState_[(size_t)L0] = outA + lp_[(size_t)L0] * (lpState_[(size_t)L0] - outA);
        float s1 = lpState_[(size_t)L1] = outB + lp_[(size_t)L1] * (lpState_[(size_t)L1] - outB);
        writeLine(L0, gain_[(size_t)L0] * s0);
        writeLine(L1, gain_[(size_t)L1] * s1);
    }
}

float Mesh::energy() const
{
    float s = 0.f;
    for (int i = 0; i < nN_; ++i) s += pJ_[(size_t)i] * pJ_[(size_t)i];
    return s / (float)std::max(1, nN_);
}

void Mesh::nodeEnergy(std::vector<float>& out) const
{
    out.resize((size_t)nN_);
    for (int i = 0; i < nN_; ++i) out[(size_t)i] = pJ_[(size_t)i] * pJ_[(size_t)i];
}

} // namespace ab
