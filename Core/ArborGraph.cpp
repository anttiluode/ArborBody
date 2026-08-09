#include "ArborGraph.h"
#include <algorithm>
#include <queue>

namespace ab {

void ArborGraph::grow(uint32_t seed, int cells, float loopiness)
{
    cells = std::max(8, std::min(cells, 600));
    loopiness = std::max(0.f, std::min(loopiness, 1.f));

    Rng rng(seed);
    n_.clear(); e_.clear();

    // Root, and one initial shoot so there is always a direction to continue.
    n_.push_back(Node{0.f, 0.f, -1, 0});
    {
        float a = rng.uni() * 6.2831853f;
        Node c; c.x = std::cos(a); c.y = std::sin(a); c.parent = 0; c.depth = 1;
        n_.push_back(c);
        e_.push_back(Edge{0, 1, 1.f, false});
    }

    // Frontier growth. Tips are preferred, but any node can sprout, which is
    // what produces the uneven, non-self-similar bodies.
    std::vector<int> childCount(n_.size(), 0);
    childCount[0] = 1;

    while ((int)n_.size() < cells) {
        // Pick a parent, weighted towards recent nodes (tips) with a long tail.
        int idx;
        {
            int m = (int)n_.size();
            float u = rng.uni();
            float bias = 1.f - u * u * u;              // skews towards the end
            idx = std::max(1, std::min(m - 1, (int)(bias * (float)m)));
        }
        // Limit fan-out so bodies stay dendritic rather than star-shaped.
        int maxKids = (childCount[(size_t)idx] == 0) ? 3 : 2;
        if (childCount[(size_t)idx] >= maxKids) continue;

        const Node& p = n_[(size_t)idx];
        float px = p.x, py = p.y;
        float dirx = 1.f, diry = 0.f;
        if (p.parent >= 0) {
            dirx = px - n_[(size_t)p.parent].x;
            diry = py - n_[(size_t)p.parent].y;
            float L = std::sqrt(dirx * dirx + diry * diry);
            if (L > 1e-6f) { dirx /= L; diry /= L; } else { dirx = 1.f; diry = 0.f; }
        } else {
            float a = rng.uni() * 6.2831853f; dirx = std::cos(a); diry = std::sin(a);
        }

        // Turn: small if continuing, large if this is a branch.
        bool branching = childCount[(size_t)idx] > 0;
        float turn = branching ? (0.5f + rng.uni() * 0.7f) * (rng.uni() < 0.5f ? -1.f : 1.f)
                               : rng.gauss() * 0.28f;
        float ca = std::cos(turn), sa = std::sin(turn);
        float nx = dirx * ca - diry * sa;
        float ny = dirx * sa + diry * ca;

        // Segment length shortens with depth: thick trunk, fine twigs.
        float len = (0.55f + rng.uni() * 0.6f) * std::exp(-0.035f * (float)p.depth);
        len = std::max(0.18f, len);

        Node c;
        c.x = px + nx * len; c.y = py + ny * len;
        c.parent = idx; c.depth = p.depth + 1;
        int ni = (int)n_.size();
        n_.push_back(c);
        childCount.push_back(0);
        childCount[(size_t)idx]++;
        e_.push_back(Edge{idx, ni, len, false});
    }

    // Loops: connect pairs that are close in space but far in the tree. These
    // are what turn a tree (which rings like a comb of separate paths) into a
    // mesh (which rings like a plate).
    rebuildAdjacency();
    int wanted = (int)(loopiness * 0.45f * (float)n_.size());
    int tries = 0;
    std::vector<int> h0 = hops(0);
    while (wanted > 0 && tries < wanted * 400) {
        ++tries;
        int a = 1 + rng.below((int)n_.size() - 1);
        int b = 1 + rng.below((int)n_.size() - 1);
        if (a == b) continue;
        float dx = n_[(size_t)a].x - n_[(size_t)b].x;
        float dy = n_[(size_t)a].y - n_[(size_t)b].y;
        float d  = std::sqrt(dx * dx + dy * dy);
        if (d > 1.6f || d < 0.05f) continue;                    // spatially near
        if (std::abs(h0[(size_t)a] - h0[(size_t)b]) < 2) continue; // topologically far
        bool dup = false;
        for (int ei : inc_[(size_t)a]) if (e_[(size_t)ei].a == b || e_[(size_t)ei].b == b) { dup = true; break; }
        if (dup) continue;
        e_.push_back(Edge{a, b, std::max(0.18f, d), true});
        inc_[(size_t)a].push_back((int)e_.size() - 1);
        inc_[(size_t)b].push_back((int)e_.size() - 1);
        --wanted;
    }

    rebuildAdjacency();
}

void ArborGraph::rebuildAdjacency()
{
    deg_.assign(n_.size(), 0);
    inc_.assign(n_.size(), {});
    for (int i = 0; i < (int)e_.size(); ++i) {
        deg_[(size_t)e_[(size_t)i].a]++;
        deg_[(size_t)e_[(size_t)i].b]++;
        inc_[(size_t)e_[(size_t)i].a].push_back(i);
        inc_[(size_t)e_[(size_t)i].b].push_back(i);
    }
}

void ArborGraph::bounds(float& x0, float& y0, float& x1, float& y1) const
{
    x0 = y0 = 1e30f; x1 = y1 = -1e30f;
    for (const auto& n : n_) { x0 = std::min(x0, n.x); y0 = std::min(y0, n.y); x1 = std::max(x1, n.x); y1 = std::max(y1, n.y); }
    if (x1 <= x0) { x0 -= 1.f; x1 += 1.f; }
    if (y1 <= y0) { y0 -= 1.f; y1 += 1.f; }
}

int ArborGraph::nearest(float x, float y) const
{
    int best = -1; float bd = 1e30f;
    for (int i = 0; i < (int)n_.size(); ++i) {
        float dx = n_[(size_t)i].x - x, dy = n_[(size_t)i].y - y;
        float d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

std::vector<int> ArborGraph::hops(int from) const
{
    std::vector<int> d(n_.size(), -1);
    if (n_.empty()) return d;
    std::queue<int> q; d[(size_t)from] = 0; q.push(from);
    while (!q.empty()) {
        int u = q.front(); q.pop();
        for (int ei : inc_[(size_t)u]) {
            int v = (e_[(size_t)ei].a == u) ? e_[(size_t)ei].b : e_[(size_t)ei].a;
            if (d[(size_t)v] < 0) { d[(size_t)v] = d[(size_t)u] + 1; q.push(v); }
        }
    }
    return d;
}

} // namespace ab
