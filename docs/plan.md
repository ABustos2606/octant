# Octant: plan

Started 28 October 2026. [design.md](design.md) is the spec; this is the order of work. Tick a step only when it has been checked the way it says.

## 1. Repository

- [x] Layout copied from Sway: `Makefile`, `LICENSE` (GPL-2.0-or-later: Octant is our own code), `.gitattributes` (LF), `.gitignore`, `AGENTS.md` and `CLAUDE.md`, `README.md`, `src/exports.map`.
- [x] Ports: `src/ports.hpp` and `octant.lv2/octant.ttl`, nine ports; `tests/ttl_check.py` compares them (run it on the source bundle until the plugin builds: `python3 tests/ttl_check.py octant.lv2`).
- [ ] The design reviewed by the user before any DSP code.
- [ ] The GitHub repository `https://github.com/ABustos2606/octant`, and the first commit (the user's).

## 2. The band bank (`src/bank.hpp`, `tests/test_bank.cpp`)

A class with no LV2 in it: `Bank(rate)` builds the bands; `step(float x)` updates every band's `z` and `w`.

- [ ] Band centres and widths from the design; coefficients at any sample rate (44.1, 48 and 96 kHz tested).
- [ ] Each band's measured response matches its formula (centre gain 1, width as designed).
- [ ] The delay of each band measured and written into this plan as a table, against the design's figures.
- [ ] A ringing band decays to zero with no denormals; a silent input costs the same as a loud one.

## 3. The voices (`src/voices.hpp`, `tests/test_voices.cpp`)

- [ ] First the plain sum (every band, no correction, no territory): pitch, level and purity measured and recorded as the baseline.
- [ ] Phase and level correction and territories: the same measurements, against the baseline and the design's targets.
- [ ] Unwrapped phase and the handoff: a sine swept slowly from 70 Hz to 2 kHz gives continuous down voices (no step larger than the sine itself could make).
- [ ] The design's delay targets, with plucked tones (a sharp start, a decay) at 82, 110, 165, 330 and 1000 Hz.
- [ ] The chord baseline (open E major, barre A) recorded.
- [ ] Swell: a note's voices reach full level in the Swell time; two notes of a chord swell on their own.
- [ ] Tone: the response at 200 Hz, 1 kHz and 16 kHz settings.
- [ ] Nothing in `step` allocates; extreme or non-finite controls are clamped; a non-finite input does not leave the filters stuck.

## 4. The plugin (`src/lv2_entry.cpp`, `tests/test_lv2.cpp`, `tools/render.cpp`)

- [ ] The glue: connect ports, clamp controls, `activate` clears the filters, `run` per sample.
- [ ] `test_lv2`: dlopen the built `.so`, one descriptor with the right URI, the same input sliced at random points and unsliced: the outputs must match sample for sample.
- [ ] `tools/render`: a WAV in, a WAV out, controls as `symbol=value`, to listen on the PC to recorded guitar (single notes, chords, bends, palm mutes).
- [ ] `make test` passes in WSL; `make lint` with lv2lint if it is installed.
- [ ] The CPU used per second of audio on the PC, recorded as a first guide to the Pi.

## 5. In WSL's Ingen

- [ ] Load it in the development engine (`tools/ingen-spike/env.sh`) between an audio input and output; move every control during play.

## 6. In Peepo (`C:\Pedal PI`)

- [ ] Pin the repository at a commit in `tools/ingen-spike/14-more-plugins.sh`.
- [ ] Add the URI to `config/catalogue/inventory.txt`.
- [ ] Overlay entry in `config/catalogue/overlay.json`: name Octant, category Pitch, the description from the Turtle, Tone logarithmic, and groups for the module page (for example Voices: 2 oct down, 1 oct down, Dry, 1 oct up, 2 oct up; Shape: Tone, Swell).
- [ ] Regenerate `docs/33-module-list.md` with `tools/document-catalogue.py`, and grep the catalogue export for the URI before saying it is offered.

## 7. On the Pi

- [ ] Build in the arm64 build root and install from there, so the next package does not wipe it.
- [ ] Played with a guitar: single notes on every string, chords, bends; the delay felt on the low strings noted.
- [ ] The design's CPU target, with xruns measured over 10 minutes at 128 frames.
- [ ] Then the version is done: update the README if anything changed, and record it in Peepo's docs.

## Later

The items under "Not in the first version" in [design.md](design.md), once the first version has been played.
