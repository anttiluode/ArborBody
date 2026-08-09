// ArborBody - Core/Mesh.h
// A scattering-junction digital waveguide mesh laid on the grown arbor.
//
// Two delay lines per edge (a->b and b->a). At each node the junction pressure
// is pJ = (2/deg) * sum(incoming); the outgoing wave on a port is pJ minus the
// incoming wave on that port. That scattering matrix is passive, so the body is
// unconditionally stable for any topology and any edge gain <= 1. There is no
// eigenvalue check anywhere in this file because there does not need to be one.
//
// The only nonlinearity in the body is a contractive saturation on the junction
// pressure (tension modulation). tanh(g*p)/g satisfies |f(p)| <= |p| for all
// g > 0, so passivity survives it.
#pragma once
#include "ArborGraph.h"
#include <vector>
#include <cmath>

namespace ab {

class Mesh {
public:
    // maxScale = longest delay multiplier that will ever be requested
    // (i.e. the lowest note). Buffers are allocated once, for that worst case.
    void build(const ArborGraph& g, double sampleRate, float samplesPerUnit, float maxScale);

    void  setTuning(float scale);                 // 1.0 = reference pitch
    void  setDecay(float t60Seconds, float damping); // damping 0..1, HF loss
    void  setTension(float t) { tension_ = t; tenG_ = t * 14.f; }
    void  reset();

    // --- one sample, in three phases so an exciter can sit inside the loop ---
    void  junction();                             // reads delays -> pJ[]
    float nodeVel(int i) const { return pJ_[(size_t)i]; }
    void  inject(int i, float f) { pJ_[(size_t)i] += f; }
    float pickup(int i) const { return pJ_[(size_t)i]; }
    void  scatter();                              // writes delays

    int   numNodes() const { return nN_; }
    int   numEdges() const { return nE_; }
    float energy() const;                         // cheap activity meter
    void  nodeEnergy(std::vector<float>& out) const;

private:
    const ArborGraph* g_ = nullptr;
    int   nN_ = 0, nE_ = 0;
    double fs_ = 48000.0;
    float spu_ = 30.f, maxScale_ = 8.f, tuning_ = 1.f;
    float t60_ = 3.f, damping_ = 0.3f;
    float tension_ = 0.f, tenG_ = 0.f;

    // Flat storage: line index = edge*2 + dir (dir 0 = a->b, 1 = b->a).
    std::vector<float> buf_;
    std::vector<int>   off_, cap_, wr_;
    std::vector<float> rawDelay_; // geometric, before filter compensation
    std::vector<float> delay_;    // fractional, in samples, as actually read
    std::vector<float> gain_, lp_, lpState_;
    std::vector<float> arr_;      // what arrived this sample, per line
    std::vector<float> pJ_;
    std::vector<float> invDeg2_;  // 2/degree

    void recompute();
    inline float readLine(int L) const
    {
        float d = delay_[(size_t)L];
        int   c = cap_[(size_t)L], o = off_[(size_t)L];
        float rp = (float)wr_[(size_t)L] - d;
        while (rp < 0.f) rp += (float)c;
        int i0 = (int)rp; float fr = rp - (float)i0;
        int i1 = i0 + 1; if (i1 >= c) i1 -= c;
        return buf_[(size_t)(o + i0)] * (1.f - fr) + buf_[(size_t)(o + i1)] * fr;
    }
    inline void writeLine(int L, float v)
    {
        buf_[(size_t)(off_[(size_t)L] + wr_[(size_t)L])] = v;
        if (++wr_[(size_t)L] >= cap_[(size_t)L]) wr_[(size_t)L] = 0;
    }
};

} // namespace ab
