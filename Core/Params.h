// ArborBody - Core/Params.h
// Everything a preset is. A whole instrument is these ~14 numbers, because the
// body is regrown deterministically from the seed rather than stored.
#pragma once
#include "Exciter.h"

namespace ab {

struct Params {
    // --- body (changing any of these regrows the topology) ---
    int   seed = 7;
    int   cells = 90;
    float loops = 0.25f;      // 0 = tree, 1 = mesh

    // --- voice ---
    float size = 34.f;        // samples per growth unit at the reference pitch
    float tune = 0.f;         // semitones trim
    float decay = 4.0f;       // T60 seconds
    float damping = 0.30f;    // HF loss per traversal, 0..0.98
    float tension = 0.0f;     // junction saturation, 0..1
    float release = 0.10f;    // T60 multiplier once the key is released

    // --- exciter ---
    ExciterType exciter = ExciterType::Mallet;
    float hardness = 0.45f;

    // --- positions, as node indices into the current body ---
    int   strike = -1;        // -1 = auto (a mid-depth node)
    int   pickA = -1;
    int   pickB = -1;

    float level = 0.8f;
    int   maxVoices = 6;
};

} // namespace ab
