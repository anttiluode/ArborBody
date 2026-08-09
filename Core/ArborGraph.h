// ArborBody - Core/ArborGraph.h
// Deterministic growth of the body. No JUCE, no dependencies.
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>

namespace ab {

struct Node { float x = 0.f, y = 0.f; int parent = -1; int depth = 0; };
struct Edge { int a = 0, b = 0; float len = 1.f; bool loop = false; };

// Small deterministic PRNG so a seed gives the same body on every machine.
struct Rng {
    uint64_t s;
    explicit Rng(uint32_t seed) : s(0x9E3779B97F4A7C15ull ^ (uint64_t)seed * 0xBF58476D1CE4E5B9ull) { next(); next(); }
    uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
    float uni() { return (float)((next() >> 11) * (1.0 / 9007199254740992.0)); }   // [0,1)
    float sym() { return uni() * 2.f - 1.f; }
    float gauss() { float u = uni() * 0.999998f + 1e-6f, v = uni(); return std::sqrt(-2.f * std::log(u)) * std::cos(6.2831853f * v); }
    int   below(int n) { return (int)(next() % (uint64_t)(n > 0 ? n : 1)); }
};

class ArborGraph {
public:
    // cells: number of nodes. loopiness 0 = pure tree, 1 = heavily meshed.
    void grow(uint32_t seed, int cells, float loopiness);

    const std::vector<Node>& nodes() const { return n_; }
    const std::vector<Edge>& edges() const { return e_; }
    int   numNodes() const { return (int)n_.size(); }
    int   numEdges() const { return (int)e_.size(); }
    int   degree(int i) const { return deg_[(size_t)i]; }
    // Node indices incident on i, and the edge index for each.
    const std::vector<int>& incidentEdges(int i) const { return inc_[(size_t)i]; }

    // Bounding box in growth units (for drawing).
    void bounds(float& x0, float& y0, float& x1, float& y1) const;
    // Nearest node to a point in growth units. Returns -1 if empty.
    int  nearest(float x, float y) const;
    // Graph distance in hops from node i to every other node.
    std::vector<int> hops(int from) const;

private:
    std::vector<Node> n_;
    std::vector<Edge> e_;
    std::vector<int>  deg_;
    std::vector<std::vector<int>> inc_;
    void rebuildAdjacency();
};

} // namespace ab
