# ArborBody

A physical-modelling instrument where the resonator is a body you grow, and the
microphones are two points you drag around on it.

VST3 / AU / Standalone, built with JUCE. The DSP core is JUCE-free and builds
with one `g++` line.

---

## What it is

A scattering-junction digital waveguide mesh laid on a grown dendritic graph.
Each branch is a pair of delay lines; each cell is a lossless scattering
junction. MIDI notes scale every delay together, so the whole modal spectrum
transposes exactly. You strike it at one point and listen at two others.

The three controls that do not exist in other physical-modelling instruments:

- **Seed / Cells / Loops** — the body itself. `Loops = 0` is a pure tree, which
  rings like a bundle of separate paths. `Loops = 1` is a mesh, which rings like
  a plate. Everything between is available and audible.
- **Strike** — where the mallet lands, dragged on the picture of the body.
- **L / R pickups** — where you listen, dragged independently, live, while the
  note is still ringing. Two pickups on the same body are essentially
  uncorrelated (measured: r = −0.03), so the stereo image is real, not a widener.

---

## Why an instrument and not a reverb

An adjoint fit, a convolution, and a sampled impulse response all reproduce a
target that already exists. There is no room to win there — for an exact copy of
a room, convolution is exact and free.

An instrument is different, because the thing being reproduced has not happened
yet. It depends on how hard you hit it.

That is what the mallet exciter is for. It is a real two-way contact: a mass
meets the body through a Hertzian spring, the body pushes back through the
junction, and contact ends when the mass leaves. Because the spring stiffens with
compression, a hard strike is in contact for *less time* than a soft one, so it
excites a different spectrum rather than a louder one.

Measured, in `Tests/core_test.cpp`:

```
contact time      v=0.1  2.292 ms    spectral centroid   203 Hz
                  v=0.3  1.854 ms                        227 Hz
                  v=0.6  1.292 ms                        284 Hz
                  v=1.0  0.917 ms                        341 Hz
```

Soft to hard is a **+68% centroid shift** at constant body. The negative control
is in the same test: the Pluck exciter is linear, and it produces **−0.00%**.

**Honest limit:** a sampler with velocity layers approximates this, and does it
well. What it cannot do is what the pickups and `Tension` do — change the
instrument continuously *while a note rings*.

---

## Verification

`bash Tests/build_and_run.sh` builds the core with no JUCE, runs the tests, and
renders the demo WAVs. Current state: **8 sections, all pass.**

1. **Growth is deterministic** — same seed, identical body; `loops=0` gives
   exactly `n−1` edges (a tree); `loops=1` gives 172 edges for 120 cells.
2. **Stability** — the scattering junction is passive, so the body is
   unconditionally stable for any topology and any edge gain ≤ 1. There is no
   eigenvalue check anywhere in the code because there does not need to be one.
   Tested at the worst settings the UI allows (`T60 = 60 s`, damping 0, tension 1,
   6 simultaneous voices, 20 s): peak over 2–4 s `0.00452`, peak over 18–20 s
   `0.00033`.
3. **Pitch** — measured by whitening both log-spectra and finding the shift that
   aligns them, so it tests the *whole spectrum* moving, not one partial:
   `48→60 = 2.0000×`, `60→72 = 2.0002×`.
4. **The velocity claim** above, with its negative control.
5. **Tension** — with a *linear* exciter, tension 0 gives a 0.00% centroid change
   between quiet and loud (the body is LTI, as it should be) and tension 1 gives
   175% (it is not).
6. **Pickups** — two distant pickups correlate at −0.031, and moving one
   mid-ring produces no step larger than 3.7× the mean sample step.
7. **Cost** — 6 voices, one core, 48 kHz: 60 cells 24%, 90 cells 37%,
   140 cells 60%.
8. **Block-size independence** — output is *bit-identical* at 32 / 64 / 128 /
   512 sample buffers.

Two real bugs were caught by these tests rather than by ear, and both would have
shipped otherwise:

- **The damping filter is a delay.** A one-pole `y = (1−d)x + d·y[−1]` has a
  group delay of `d/(1−d)` samples, and that is *constant per traversal* — it does
  not scale with the note. Uncompensated, every octave came out as 1.983 instead
  of 2.000. `Mesh::recompute()` now subtracts it from the delay line.
- **Voices died before they sounded.** At a 64-sample buffer the mallet contact
  is over and the wavefront has not yet reached the pickup, so a naive
  "exciter finished and quiet ⇒ retire" test silenced every note. Section 8
  exists specifically to catch this class of bug.

---

## Building

**Plugin (VST3 / AU / Standalone):**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE 8.0.6 is fetched automatically. `COPY_PLUGIN_AFTER_BUILD` installs to the
system plugin folder.

**Core only, no dependencies:**

```bash
bash Tests/build_and_run.sh
```

---

## Demos

Rendered by `Tests/render_demo.cpp` with no audio device involved:

| file | what to listen for |
|---|---|
| `01_five_bodies.wav` | five seeds, same note, same strike — from pure tree to full mesh |
| `02_walk_the_pickup.wav` | one body, one struck note, the pickups walked root → tip |
| `03_soft_to_hard.wav` | eight strikes, ppp to fff, on one body |
| `04_tension_off_then_on.wav` | same two notes, tension 0 then 0.85 |
| `05_phrase.wav` | played, rather than demonstrated |

---

## Parameters

| control | what it does |
|---|---|
| Seed / Cells / Loops | the body. Changing these regrows it (crossfaded, ~6 ms) |
| Size | delay samples per growth unit — the coarse pitch and the brightness |
| Tune | semitone trim |
| Decay | T60 at low frequency, set by the Jot attenuator rule per branch |
| Damping | high-frequency loss per traversal — wood to metal |
| Tension | junction saturation. `tanh(g·p)/g` is contractive, so passivity survives it |
| Release | T60 multiplier once the key lifts, i.e. how hard the damper falls |
| Exciter | Mallet (nonlinear, two-way), Pluck (linear), Noise |
| Hardness | felt exponent and contact stiffness of the mallet |
| Voices | 1–12. See the cost table |
| Level | output trim. Above about −6 dBFS a soft-clip stage engages |

A preset is ~18 numbers. The body is regrown from the seed rather than stored,
so a patch is under 1 KB against a sampled instrument's hundreds of megabytes.

---

## What this is not

- **The algorithm is not novel.** Scattering waveguide meshes are standard
  (Schroeder, Jot, Smith), and modal/waveguide instruments are a mature category
  — Chromaphone, Kaivo, Plasmonic. What is unoccupied is parameterising the
  resonator as a *grown spatial graph* with the topology as the control surface
  and every cell a free listening point.
- **Branch delays are integers set by geometry**, so topology is not
  differentiable. To search the body, reroll the seed.
- **The bodies are inharmonic.** Transposition is exact; the spectrum is not a
  harmonic series and never will be. This is a struck-object instrument — bars,
  plates, bells, unnameable things — not a piano.
- **No bowing yet.** Continuous excitation needs a stick-slip friction solve at
  the junction, which is a real piece of work and is not in this version.

---

## Layout

```
Core/            JUCE-free DSP. builds with one g++ line
  ArborGraph     deterministic growth from a seed
  Mesh           the scattering waveguide mesh
  Exciter        mallet / pluck / noise
  Voice          one note
  Instrument     voice pool, topology ownership
Source/          JUCE plugin wrapper and editor
Tests/           core_test.cpp, render_demo.cpp, build_and_run.sh
```

Made with the arbor line from FunctionalArbors / GeometricNeuronPlusField.
Do not hype. Do not lie. Just show.
