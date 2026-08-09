// ArborBody - Tests/render_demo.cpp
// Renders the demo WAVs. No JUCE, no audio device: g++ and go.
#include "../Core/Instrument.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

using namespace ab;

static void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int fs)
{
    int n = (int)std::min(L.size(), R.size());
    std::vector<int16_t> pcm((size_t)n * 2);
    for (int i = 0; i < n; ++i) {
        auto c = [](float v) { v = std::max(-1.f, std::min(1.f, v)); return (int16_t)std::lrint(v * 32700.f); };
        pcm[(size_t)i * 2] = c(L[(size_t)i]); pcm[(size_t)i * 2 + 1] = c(R[(size_t)i]);
    }
    uint32_t dataBytes = (uint32_t)(pcm.size() * 2);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::printf("cannot write %s\n", path.c_str()); return; }
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); u32(36 + dataBytes); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); u32(16); u16(1); u16(2); u32((uint32_t)fs);
    u32((uint32_t)fs * 4); u16(4); u16(16);
    std::fwrite("data", 1, 4, f); u32(dataBytes);
    std::fwrite(pcm.data(), 1, dataBytes, f);
    std::fclose(f);
    std::printf("  wrote %-34s %5.1f s\n", path.c_str(), (double)n / fs);
}

struct Ev { double t; int note; float vel; double dur; };

static void play(Instrument& ins, const std::vector<Ev>& evs, double seconds,
                 std::vector<float>& L, std::vector<float>& R, int fs)
{
    int n = (int)(seconds * fs);
    L.assign((size_t)n, 0.f); R.assign((size_t)n, 0.f);
    std::vector<std::pair<int,int>> offs;   // sample, note
    ins.panic();
    const int B = 64;
    size_t next = 0;
    for (int i = 0; i < n; i += B) {
        int k = std::min(B, n - i);
        while (next < evs.size() && (int)(evs[next].t * fs) <= i) {
            ins.noteOn(evs[next].note, evs[next].vel);
            offs.push_back({ (int)((evs[next].t + evs[next].dur) * fs), evs[next].note });
            ++next;
        }
        for (auto& o : offs) if (o.first >= 0 && o.first <= i) { ins.noteOff(o.second); o.first = -1; }
        ins.render(L.data() + i, R.data() + i, k);
    }
}

int main(int argc, char** argv)
{
    const int fs = 48000;
    std::string dir = (argc > 1) ? argv[1] : ".";
    std::printf("\nRendering ArborBody demos into %s\n\n", dir.c_str());
    std::vector<float> L, R;

    auto run = [&](const char* name, Params p, std::vector<Ev> evs, double secs) {
        Instrument ins; ins.prepare(fs, p);
        play(ins, evs, secs, L, R, fs);
        writeWav(dir + "/" + name, L, R, fs);
    };

    // 1. Five bodies, same note, same strike: the topology IS the timbre.
    {
        Instrument ins;
        std::vector<float> all_l, all_r;
        int seeds[5] = { 3, 11, 29, 47, 91 };
        float loops[5] = { 0.0f, 0.15f, 0.35f, 0.6f, 1.0f };
        for (int i = 0; i < 5; ++i) {
            Params p; p.seed = seeds[i]; p.cells = 100; p.loops = loops[i];
            p.decay = 5.f; p.damping = 0.22f; p.exciter = ExciterType::Mallet; p.hardness = 0.55f;
            ins.prepare(fs, p);
            play(ins, { {0.0, 55, 0.85f, 4.0} }, 2.4, L, R, fs);
            all_l.insert(all_l.end(), L.begin(), L.end());
            all_r.insert(all_r.end(), R.begin(), R.end());
        }
        writeWav(dir + "/01_five_bodies.wav", all_l, all_r, fs);
    }

    // 2. One body, pickup walked from root to tip while it rings.
    {
        Params p; p.seed = 11; p.cells = 110; p.loops = 0.45f; p.decay = 9.f;
        p.damping = 0.18f; p.exciter = ExciterType::Mallet; p.hardness = 0.6f; p.maxVoices = 4;
        Instrument ins; ins.prepare(fs, p);
        int nn = ins.graph().numNodes();
        int n = (int)(9.0 * fs);
        L.assign((size_t)n, 0.f); R.assign((size_t)n, 0.f);
        ins.panic();
        const int B = 64;
        int stops[6] = { 2, nn / 6, nn / 3, nn / 2, (2 * nn) / 3, nn - 2 };
        for (int i = 0; i < n; i += B) {
            int k = std::min(B, n - i);
            if (i % (int)(1.5 * fs) < B) ins.noteOn(50, 0.8f);
            int s = std::min(5, i / (int)(1.5 * fs));
            p.pickA = stops[s]; p.pickB = stops[5 - s]; ins.setParams(p);
            ins.render(L.data() + i, R.data() + i, k);
        }
        writeWav(dir + "/02_walk_the_pickup.wav", L, R, fs);
    }

    // 3. Velocity: eight strikes from ppp to fff on one body.
    {
        Params p; p.seed = 7; p.cells = 90; p.loops = 0.3f; p.decay = 4.f;
        p.damping = 0.3f; p.exciter = ExciterType::Mallet; p.hardness = 0.5f;
        std::vector<Ev> e;
        for (int i = 0; i < 8; ++i) e.push_back({ i * 0.8, 57, 0.08f + 0.13f * i, 3.0 });
        run("03_soft_to_hard.wav", p, e, 8.5);
    }

    // 4. Tension: same phrase, tension off then on.
    {
        Params p; p.seed = 33; p.cells = 120; p.loops = 0.75f; p.decay = 7.f;
        p.damping = 0.14f; p.exciter = ExciterType::Mallet; p.hardness = 0.75f; p.maxVoices = 4;
        std::vector<Ev> e = { {0.0, 45, 1.0f, 3.0}, {1.6, 45, 0.25f, 3.0} };
        Instrument a; a.prepare(fs, p); play(a, e, 3.4, L, R, fs);
        std::vector<float> l1 = L, r1 = R;
        p.tension = 0.85f;
        Instrument b; b.prepare(fs, p); play(b, e, 3.4, L, R, fs);
        l1.insert(l1.end(), L.begin(), L.end()); r1.insert(r1.end(), R.begin(), R.end());
        writeWav(dir + "/04_tension_off_then_on.wav", l1, r1, fs);
    }

    // 5. A short played phrase, to hear it as an instrument rather than a demo.
    {
        Params p; p.seed = 61; p.cells = 96; p.loops = 0.4f; p.decay = 6.f;
        p.damping = 0.24f; p.exciter = ExciterType::Mallet; p.hardness = 0.42f;
        p.maxVoices = 6; p.tension = 0.15f;
        int mel[12] = { 52, 59, 64, 67, 59, 64, 71, 67, 52, 64, 59, 55 };
        std::vector<Ev> e;
        for (int i = 0; i < 12; ++i) e.push_back({ i * 0.42, mel[i], 0.45f + 0.4f * ((i % 3) == 0), 1.8 });
        e.push_back({ 5.4, 40, 0.9f, 4.0 }); e.push_back({ 5.4, 52, 0.7f, 4.0 }); e.push_back({ 5.4, 59, 0.6f, 4.0 });
        run("05_phrase.wav", p, e, 10.0);
    }

    std::printf("\n");
    return 0;
}
