// ArborBody - Tests/core_test.cpp
// Builds and runs with no JUCE:  see build_and_run.sh
#include "../Core/Instrument.h"
#include <cstdio>
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>

using namespace ab;
// MSVC's <cmath> does not define M_PI unless _USE_MATH_DEFINES is set before
// the include, which is fragile across build systems. Use our own constant.
static constexpr double kPi = 3.14159265358979323846;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) ++failures;
}

// ---------- tiny radix-2 FFT ----------
static void fft(std::vector<std::complex<double>>& a)
{
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * kPi / (double)len;
        std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}
static std::vector<double> spectrum(const float* x, int n, int N)
{
    std::vector<std::complex<double>> a((size_t)N, {0.0, 0.0});
    for (int i = 0; i < std::min(n, N); ++i) {
        double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (double)std::min(n, N));
        a[(size_t)i] = { (double)x[i] * w, 0.0 };
    }
    fft(a);
    std::vector<double> m((size_t)(N / 2));
    for (int i = 0; i < N / 2; ++i) m[(size_t)i] = std::abs(a[(size_t)i]);
    return m;
}
static double centroid(const std::vector<double>& m, double fs, int N, double fmin = 60.0)
{
    double num = 0, den = 0;
    int i0 = (int)(fmin * N / fs);
    for (int i = i0; i < (int)m.size(); ++i) { double f = i * fs / N; num += f * m[(size_t)i]; den += m[(size_t)i]; }
    return den > 0 ? num / den : 0.0;
}
// Measuring "did it transpose" by tracking one partial is unreliable: damping
// is fixed in absolute frequency, so which partial is loudest changes with the
// note. Instead, resample the magnitude spectrum onto a log-frequency axis and
// find the shift that best aligns two notes. That measures the claim directly --
// the WHOLE spectrum moving by a constant ratio.
static std::vector<double> logSpec(const std::vector<double>& m, double fs, int N,
                                   double f0 = 60.0, double f1 = 9000.0, int bpo = 96)
{
    int nb = (int)(bpo * std::log2(f1 / f0));
    std::vector<double> o((size_t)nb, 0.0);
    for (int i = 0; i < nb; ++i) {
        double f = f0 * std::pow(2.0, (double)i / bpo);
        double bin = f * N / fs;
        int b0 = (int)bin; double fr = bin - b0;
        if (b0 + 1 < (int)m.size()) o[(size_t)i] = m[(size_t)b0] * (1 - fr) + m[(size_t)b0 + 1] * fr;
    }
    double mx = 0; for (double v : o) mx = std::max(mx, v);
    for (auto& v : o) v = std::log10(v / (mx + 1e-30) + 1e-4);
    // Whiten: the damping filter is fixed in absolute frequency, so high notes
    // have a genuinely darker envelope. That is a real timbral fact but it is
    // not what this test is asking about. Subtract a one-octave moving average
    // so only the PATTERN of partials is compared.
    std::vector<double> s((size_t)nb, 0.0);
    int w = bpo / 2;
    for (int i = 0; i < nb; ++i) {
        double acc = 0; int n = 0;
        for (int j = i - w; j <= i + w; ++j) if (j >= 0 && j < nb) { acc += o[(size_t)j]; ++n; }
        s[(size_t)i] = acc / std::max(1, n);
    }
    for (int i = 0; i < nb; ++i) o[(size_t)i] -= s[(size_t)i];
    return o;
}
static double transposeRatio(const std::vector<double>& a, const std::vector<double>& b, int bpo = 96)
{
    // best shift of b relative to a, in log bins
    int maxShift = bpo * 3;
    double best = -1e30; int bs = 0; std::vector<double> sc;
    for (int s = -maxShift; s <= maxShift; ++s) {
        double num = 0; int n = 0;
        for (int i = 0; i < (int)a.size(); ++i) {
            int j = i + s;
            if (j < 0 || j >= (int)b.size()) continue;
            num += a[(size_t)i] * b[(size_t)j]; ++n;
        }
        if (n < 20) { sc.push_back(-1e30); continue; }
        double v = num / n; sc.push_back(v);
        if (v > best) { best = v; bs = s; }
    }
    int k = bs + maxShift;
    double d = 0;
    if (k > 0 && k + 1 < (int)sc.size()) {
        double A = sc[(size_t)k - 1], B = sc[(size_t)k], C = sc[(size_t)k + 1];
        double den = 2 * (A - 2 * B + C);
        if (std::abs(den) > 1e-30) { d = (A - C) / den; if (std::abs(d) > 1) d = 0; }
    }
    return std::pow(2.0, ((double)bs + d) / bpo);
}
static double globalPeak(const std::vector<double>& m, double fs, int N, double fmin)
{
    int i0 = std::max(2, (int)(fmin * N / fs)); int bi = i0; double bv = -1;
    for (int i = i0; i < (int)m.size(); ++i) if (m[(size_t)i] > bv) { bv = m[(size_t)i]; bi = i; }
    return bi * fs / (double)N;
}
static double corr(const std::vector<float>& a, const std::vector<float>& b)
{
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0; int n = (int)a.size();
    for (int i = 0; i < n; ++i) { sa += a[(size_t)i]; sb += b[(size_t)i]; }
    sa /= n; sb /= n;
    for (int i = 0; i < n; ++i) { double x = a[(size_t)i] - sa, y = b[(size_t)i] - sb; saa += x * x; sbb += y * y; sab += x * y; }
    return sab / (std::sqrt(saa * sbb) + 1e-30);
}

// Render one note into L/R.
static void strike(Instrument& ins, int note, float vel, std::vector<float>& L, std::vector<float>& R, int n)
{
    L.assign((size_t)n, 0.f); R.assign((size_t)n, 0.f);
    ins.panic();
    ins.noteOn(note, vel);
    const int B = 512;
    for (int i = 0; i < n; i += B) {
        int k = std::min(B, n - i);
        ins.render(L.data() + i, R.data() + i, k);
    }
}

int main()
{
    const double fs = 48000.0;
    const int N = 16384;

    std::printf("\n=== ArborBody core tests ===\n");

    // ---------------------------------------------------------------- 1
    std::printf("\n1. Growth is deterministic and well formed\n");
    {
        ArborGraph a, b, c;
        a.grow(7, 90, 0.25f); b.grow(7, 90, 0.25f); c.grow(8, 90, 0.25f);
        bool same = a.numNodes() == b.numNodes() && a.numEdges() == b.numEdges();
        for (int i = 0; same && i < a.numNodes(); ++i)
            same = (a.nodes()[(size_t)i].x == b.nodes()[(size_t)i].x);
        check(same, "same seed gives an identical body");
        check(a.numEdges() != c.numEdges() || a.nodes()[5].x != c.nodes()[5].x, "different seed gives a different body");
        check(a.numNodes() == 90, "cell count is honoured");
        auto h = a.hops(0);
        bool conn = true; for (int d : h) if (d < 0) conn = false;
        check(conn, "body is connected");
        ArborGraph t, m; t.grow(3, 120, 0.f); m.grow(3, 120, 1.f);
        check(t.numEdges() == t.numNodes() - 1, "loops=0 is exactly a tree");
        check(m.numEdges() > t.numEdges() + 10, "loops=1 adds a real mesh");
        std::printf("       tree %d edges, mesh %d edges\n", t.numEdges(), m.numEdges());
    }

    // ---------------------------------------------------------------- 2
    std::printf("\n2. Stability at the worst settings the UI allows\n");
    {
        Params p; p.seed = 11; p.cells = 120; p.loops = 1.0f;
        p.decay = 60.f; p.damping = 0.f; p.tension = 1.f; p.level = 1.f; p.maxVoices = 6;
        Instrument ins; ins.prepare(fs, p);
        std::vector<float> L, R;
        double worst = 0; bool nan = false;
        for (int ex = 0; ex < 3; ++ex) {
            p.exciter = (ExciterType)ex; ins.setParams(p);
            for (int rep = 0; rep < 6; ++rep) {
                strike(ins, 36 + rep * 6, 1.f, L, R, (int)(fs * 2));
                for (int i = 0; i < (int)L.size(); ++i) {
                    if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) nan = true;
                    worst = std::max(worst, (double)std::max(std::abs(L[(size_t)i]), std::abs(R[(size_t)i])));
                }
            }
        }
        check(!nan, "no NaN or inf across all exciters and pitches");
        check(worst < 12.0, "output stays bounded");
        std::printf("       worst peak %.3f\n", worst);

        // 6 voices piled on, no decay, 12 s: the passivity claim under load
        ins.panic();
        for (int v = 0; v < 6; ++v) ins.noteOn(40 + v * 5, 1.f);
        std::vector<float> l(512), r(512);
        int nb = (int)(fs * 20 / 512);
        int b1 = (int)(fs * 2 / 512), b2 = (int)(fs * 4 / 512);
        double early = 0, late = 0;
        for (int blk = 0; blk < nb; ++blk) {
            ins.render(l.data(), r.data(), 512);
            double pk = 0; for (int i = 0; i < 512; ++i) pk = std::max(pk, (double)std::abs(l[(size_t)i]));
            if (blk >= b1 && blk < b2) early = std::max(early, pk);
            if (blk >= nb - (b2 - b1)) late = std::max(late, pk);
        }
        check(late <= early + 1e-9, "6 voices at T60=60s: peak at 18-20 s does not exceed 2-4 s");
        std::printf("       peak over 2-4 s %.5f, peak over 18-20 s %.5f\n", early, late);
    }

    // ---------------------------------------------------------------- 3
    std::printf("\n3. Pitch: all delays scale together, so the spectrum transposes\n");
    {
        Params p; p.seed = 7; p.cells = 90; p.loops = 0.25f; p.decay = 3.f;
        p.damping = 0.30f; p.exciter = ExciterType::Mallet; p.hardness = 0.5f;
        Instrument ins; ins.prepare(fs, p);
        std::vector<float> L, R;
        std::vector<std::vector<double>> ls;
        int notes[3] = { 48, 60, 72 };
        for (int i = 0; i < 3; ++i) {
            strike(ins, notes[i], 0.9f, L, R, N);
            auto m = spectrum(L.data(), N, N);
            ls.push_back(logSpec(m, fs, N));
            if (i == 1) std::printf("       loudest partial at note 60: %.1f Hz\n", globalPeak(m, fs, N, 70.0));
        }
        double r1 = transposeRatio(ls[0], ls[1]);
        double r2 = transposeRatio(ls[1], ls[2]);
        std::printf("       measured spectral shift 48->60 = %.4fx, 60->72 = %.4fx (want 2.0000)\n", r1, r2);
        check(std::abs(r1 - 2.0) < 0.03, "48 -> 60 transposes the whole spectrum by an octave");
        check(std::abs(r2 - 2.0) < 0.03, "60 -> 72 transposes the whole spectrum by an octave");

        // and the same measurement with damping off, where the compensation is
        // not needed, to show the compensation is not doing the work by accident
        p.damping = 0.f; ins.setParams(p);
        std::vector<std::vector<double>> l2;
        for (int i = 0; i < 2; ++i) { strike(ins, notes[i], 0.9f, L, R, N); l2.push_back(logSpec(spectrum(L.data(), N, N), fs, N)); }
        double r0 = transposeRatio(l2[0], l2[1]);
        std::printf("       undamped control: %.4fx\n", r0);
        check(std::abs(r0 - 2.0) < 0.03, "undamped body transposes too");
    }

    // ---------------------------------------------------------------- 4
    std::printf("\n4. THE CLAIM: a hard strike is a different sound, not a louder one\n");
    {
        // (a) the hammer alone: contact time must shorten with velocity
        double prevT = 1e9; bool monotone = true;
        std::printf("       contact time vs velocity (mallet, hardness 0.45):\n");
        for (float v : { 0.1f, 0.3f, 0.6f, 1.0f }) {
            Exciter e; e.start(ExciterType::Mallet, v, 0.45f, fs);
            int n = 0; while (!e.done() && n < 20000) { e.step(0.f); ++n; }
            double ms = 1000.0 * n / fs;
            std::printf("         v=%.1f  %6.3f ms\n", v, ms);
            if (ms > prevT + 1e-9) monotone = false;
            prevT = ms;
        }
        check(monotone, "contact time falls monotonically with strike velocity");

        // (b) in the instrument: spectral centroid must rise with velocity
        Params p; p.seed = 7; p.cells = 90; p.loops = 0.3f; p.decay = 3.f;
        p.damping = 0.25f; p.tension = 0.f; p.exciter = ExciterType::Mallet; p.hardness = 0.45f;
        // Low output level on purpose: the instrument's output safety stage is
        // itself a nonlinearity, and it would manufacture exactly the timbre
        // shift this test is trying to attribute to the hammer.
        p.level = 0.05f;
        Instrument ins; ins.prepare(fs, p);
        std::vector<float> L, R;
        double prevC = 0; bool rising = true; double c10 = 0, c01 = 0;
        std::printf("       spectral centroid vs velocity:\n");
        for (float v : { 0.1f, 0.3f, 0.6f, 1.0f }) {
            strike(ins, 60, v, L, R, N);
            double c = centroid(spectrum(L.data(), N, N), fs, N);
            std::printf("         v=%.1f  %7.1f Hz\n", v, c);
            if (c < prevC - 1e-9) rising = false;
            prevC = c;
            if (v == 0.1f) c01 = c;
            if (v == 1.0f) c10 = c;
        }
        check(rising, "centroid rises monotonically with velocity");
        check(c10 > c01 * 1.15, "the timbre change is large enough to hear (>15%)");
        std::printf("       soft -> hard centroid shift: %+.1f%%\n", 100.0 * (c10 / c01 - 1.0));

        // (c) control: the pluck exciter is linear, so it must NOT do this
        p.exciter = ExciterType::Pluck; ins.setParams(p);
        strike(ins, 60, 0.1f, L, R, N); double pc01 = centroid(spectrum(L.data(), N, N), fs, N);
        strike(ins, 60, 1.0f, L, R, N); double pc10 = centroid(spectrum(L.data(), N, N), fs, N);
        std::printf("       linear-pluck control: %.1f Hz -> %.1f Hz (%+.2f%%)\n", pc01, pc10, 100.0 * (pc10 / pc01 - 1.0));
        check(std::abs(pc10 / pc01 - 1.0) < 0.02, "linear exciter shows no timbre shift (negative control)");
    }

    // ---------------------------------------------------------------- 5
    std::printf("\n5. Tension makes the BODY nonlinear too\n");
    {
        Params p; p.seed = 21; p.cells = 90; p.loops = 0.4f; p.decay = 3.f;
        p.damping = 0.2f; p.exciter = ExciterType::Pluck; p.hardness = 0.5f;
        p.level = 0.05f;   // keep the output safety stage out of the measurement
        Instrument ins;
        auto centroidAt = [&](float tension, float vel) {
            p.tension = tension; ins.prepare(fs, p);
            std::vector<float> L, R; strike(ins, 60, vel, L, R, N);
            return centroid(spectrum(L.data(), N, N), fs, N);
        };
        double lin = std::abs(centroidAt(0.f, 1.0f) / centroidAt(0.f, 0.1f) - 1.0);
        double non = std::abs(centroidAt(1.f, 1.0f) / centroidAt(1.f, 0.1f) - 1.0);
        std::printf("       centroid shift with a LINEAR exciter: tension 0 %.2f%%, tension 1 %.2f%%\n",
                    100.0 * lin, 100.0 * non);
        check(lin < 0.02, "tension 0: body is LTI, amplitude does not change timbre");
        check(non > lin * 3.0 + 0.01, "tension 1: body is amplitude dependent");
    }

    // ---------------------------------------------------------------- 6
    std::printf("\n6. Pickups: position is a real control, and moving it is silent\n");
    {
        Params p; p.seed = 7; p.cells = 90; p.loops = 0.3f; p.decay = 4.f; p.damping = 0.25f;
        Instrument ins; ins.prepare(fs, p);
        int n = ins.graph().numNodes();
        std::vector<float> L, R;

        p.pickA = n / 6; p.pickB = (5 * n) / 6; ins.setParams(p);
        strike(ins, 60, 0.8f, L, R, (int)(fs * 0.5));
        double c = corr(L, R);
        std::printf("       correlation between two pickups on the same body: %+.3f\n", c);
        check(std::abs(c) < 0.5, "distant pickups are substantially decorrelated");

        // move the pickup mid-ring and look for a click
        ins.panic(); ins.noteOn(60, 0.8f);
        std::vector<float> l(256), r(256), all;
        for (int b = 0; b < 200; ++b) {
            if (b == 60) { p.pickA = (2 * n) / 3; ins.setParams(p); }
            ins.render(l.data(), r.data(), 256);
            all.insert(all.end(), l.begin(), l.end());
        }
        double maxStep = 0, typStep = 0;
        for (size_t i = 1; i < all.size(); ++i) {
            double d = std::abs(all[i] - all[i - 1]);
            typStep += d;
            if (i > 256 * 55 && i < 256 * 75) maxStep = std::max(maxStep, d);
        }
        typStep /= (double)all.size();
        std::printf("       largest step during the move %.5f vs mean step %.5f\n", maxStep, typStep);
        check(maxStep < typStep * 40.0, "no discontinuity when the pickup moves while ringing");
    }

    // ---------------------------------------------------------------- 7
    std::printf("\n7. Cost\n");
    {
        for (int cells : { 60, 90, 140 }) {
            Params p; p.seed = 5; p.cells = cells; p.loops = 0.3f; p.decay = 4.f; p.maxVoices = 6;
            Instrument ins; ins.prepare(fs, p);
            for (int v = 0; v < 6; ++v) ins.noteOn(48 + v * 4, 0.9f);
            std::vector<float> l(512), r(512);
            int blocks = (int)(fs * 4 / 512);
            clock_t t0 = clock();
            for (int b = 0; b < blocks; ++b) ins.render(l.data(), r.data(), 512);
            double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
            std::printf("       %3d cells / %3d edges, 6 voices: %.2f s cpu for 4.0 s audio  (%.1fx realtime, %.0f%% of a core)\n",
                        cells, ins.graph().numEdges(), sec, 4.0 / sec, 100.0 * sec / 4.0);
        }
    }

    // ---------------------------------------------------------------- 8
    std::printf("\n8. Block size does not change the output (DAW buffer independence)\n");
    {
        Params p; p.seed = 7; p.cells = 90; p.loops = 0.3f; p.decay = 4.f; p.damping = 0.3f;
        std::vector<std::vector<float>> outs;
        for (int B : { 32, 64, 128, 512 }) {
            Instrument ins; ins.prepare(fs, p);
            int n = (int)(fs * 1.5);
            std::vector<float> L((size_t)n, 0.f), R((size_t)n, 0.f);
            ins.panic(); ins.noteOn(57, 0.9f);
            for (int i = 0; i < n; i += B) ins.render(L.data() + i, R.data() + i, std::min(B, n - i));
            outs.push_back(L);
        }
        double worst = 0, pk = 0;
        for (size_t k = 1; k < outs.size(); ++k)
            for (size_t i = 0; i < outs[0].size(); ++i) {
                worst = std::max(worst, (double)std::abs(outs[k][i] - outs[0][i]));
                pk = std::max(pk, (double)std::abs(outs[0][i]));
            }
        std::printf("       peak %.4f, largest difference across 32/64/128/512 = %.2e\n", pk, worst);
        check(pk > 0.05, "the note actually sounds at every block size");
        check(worst < 1e-9, "output is bit-identical regardless of buffer size");
    }

    std::printf("\n=== %s (%d failure%s) ===\n\n", failures ? "FAILED" : "ALL PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
