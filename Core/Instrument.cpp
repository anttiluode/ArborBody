#include "Instrument.h"
#include <algorithm>
#include <cstring>
#include <cmath>

namespace ab {

void Instrument::prepare(double sampleRate, const Params& p)
{
    fs_ = sampleRate;
    p_ = p;
    built_ = false;
    voices_.clear();
    regrow();
}

void Instrument::regrow()
{
    graph_.grow((uint32_t)p_.seed, p_.cells, p_.loops);

    int nv = std::max(1, std::min(p_.maxVoices, kMaxVoices));
    voices_.clear();
    for (int i = 0; i < nv; ++i) {
        auto v = std::make_unique<Voice>();
        v->prepare(graph_, fs_, p_.size, kMaxScale);
        voices_.push_back(std::move(v));
    }
    built_ = true;
    pushPositions();
}

int Instrument::defaultStrike() const
{
    // A node partway out: struck at the root everything is symmetric and dull.
    int n = graph_.numNodes();
    return std::max(0, std::min(n - 1, n / 5));
}

void Instrument::pushPositions()
{
    int n = graph_.numNodes();
    int s  = (p_.strike >= 0 && p_.strike < n) ? p_.strike : defaultStrike();
    int a  = (p_.pickA  >= 0 && p_.pickA  < n) ? p_.pickA  : std::min(n - 1, n / 2);
    int b  = (p_.pickB  >= 0 && p_.pickB  < n) ? p_.pickB  : std::min(n - 1, (3 * n) / 4);
    for (auto& v : voices_) { v->setStrike(s); v->setPickups(a, b); }
}

void Instrument::setParams(const Params& p)
{
    bool bodyChanged = (p.seed != p_.seed) || (p.cells != p_.cells)
                     || (std::abs(p.loops - p_.loops) > 1e-6f)
                     || (std::abs(p.size - p_.size) > 1e-6f)
                     || (p.maxVoices != p_.maxVoices);
    // Recomputing edge gains costs a pow() per delay line, so only do it when a
    // material parameter has actually moved. setParams() is called every block.
    bool materialChanged = std::abs(p.decay - p_.decay) > 1e-6f
                        || std::abs(p.damping - p_.damping) > 1e-6f
                        || std::abs(p.tension - p_.tension) > 1e-6f
                        || std::abs(p.release - p_.release) > 1e-6f;
    p_ = p;
    if (bodyChanged || !built_) { regrow(); return; }
    pushPositions();
    if (materialChanged)
        for (auto& v : voices_) if (v->isActive()) v->applyBody(p_);
}

Voice* Instrument::allocate()
{
    // Prefer a free voice; otherwise steal the quietest released one, and only
    // then the quietest held one.
    Voice* best = nullptr;
    for (auto& v : voices_) if (!v->isActive()) return v.get();
    float q = 1e30f;
    for (auto& v : voices_) if (!v->held() && v->loudness() < q) { q = v->loudness(); best = v.get(); }
    if (best) return best;
    q = 1e30f;
    for (auto& v : voices_) if (v->loudness() < q) { q = v->loudness(); best = v.get(); }
    return best;
}

void Instrument::noteOn(int note, float velocity)
{
    if (!built_ || voices_.empty()) return;
    Voice* v = allocate();
    if (!v) return;
    v->noteOn(note, velocity, p_);
    pushPositions();
    ++counter_;
}

void Instrument::noteOff(int note)
{
    for (auto& v : voices_) if (v->isActive() && v->held() && v->note() == note) v->noteOff(p_);
}

void Instrument::allNotesOff() { for (auto& v : voices_) if (v->held()) v->noteOff(p_); }
void Instrument::panic()      { for (auto& v : voices_) v->kill(); }

void Instrument::render(float* L, float* R, int numSamples)
{
    std::memset(L, 0, sizeof(float) * (size_t)numSamples);
    std::memset(R, 0, sizeof(float) * (size_t)numSamples);
    if (!built_) return;
    for (auto& v : voices_) if (v->isActive()) v->render(L, R, numSamples);
    // Output safety. A resonator with a 60 s decay and six voices can be driven
    // past 0 dBFS by an enthusiastic player; this is transparent below about
    // -6 dBFS and soft above it, so the instrument never clips hard.
    const float g = p_.level;
    for (int i = 0; i < numSamples; ++i) {
        float a = L[i] * g, b = R[i] * g;
        L[i] = (std::abs(a) < 0.5f) ? a : std::tanh(a * 2.f) * 0.5f;
        R[i] = (std::abs(b) < 0.5f) ? b : std::tanh(b * 2.f) * 0.5f;
    }
}

int Instrument::activeVoices() const
{
    int c = 0; for (const auto& v : voices_) if (v->isActive()) ++c; return c;
}

void Instrument::nodeEnergy(std::vector<float>& out) const
{
    out.assign((size_t)graph_.numNodes(), 0.f);
    std::vector<float> tmp;
    for (const auto& v : voices_) {
        if (!v->isActive()) continue;
        v->mesh().nodeEnergy(tmp);
        for (size_t i = 0; i < out.size() && i < tmp.size(); ++i) out[i] += tmp[i];
    }
}

} // namespace ab
