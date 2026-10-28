# Octant: plan

Started 28 October 2026. [design.md](design.md) is the spec; this is the order of work. Tick a step only when it has been checked the way it says.

## 1. Repository

- [x] Layout copied from Sway: `Makefile`, `LICENSE` (GPL-2.0-or-later: Octant is our own code), `.gitattributes` (LF), `.gitignore`, `AGENTS.md` and `CLAUDE.md`, `README.md`, `src/exports.map`.
- [x] Ports: `src/ports.hpp` and `octant.lv2/octant.ttl`, nine ports; `tests/ttl_check.py` compares them (run it on the source bundle until the plugin builds: `python3 tests/ttl_check.py octant.lv2`).
- [x] The design reviewed by the user before any DSP code (28 October 2026: "start implementation").
- [ ] The GitHub repository `https://github.com/ABustos2606/octant`, and the first commit (the user's).

## 2. The band bank, abandoned

The first design: 30 complex 3-pole bands from 65 Hz to 4.7 kHz, each band's phase multiplied for the voices (with phase and level correction, territories and a handoff between bands). Built and tested on 28 October 2026: on steady sines it met every target (pitch exact, level within 0.02 dB, purity 46 to 58 dB, delay 13 ms at 82 Hz). But neighbouring bands heard each other's centre partial only 7.5 dB down, so a real note carried two partials in a band into the shift: single notes with eight harmonics measured 7 to 28 dB, six-string chords about -5 to 6 dB. Six stages instead of three, more smoothing of the measured frequency, and a rule keeping a band silent between two partials changed that by a few dB at most. Rendered from three guitar recordings, the user judged it unusable ("horrible, completely useless"). `src/bank.hpp` and its test were removed; the measurements are in this file's history.

The lesson kept: a core that misses its targets by that much is reported before tests, glue and documents are built around it.

## 3. The phase vocoder (`src/fft.hpp`, `src/voices.hpp`, `tests/test_fft.cpp`, `tests/test_voices.cpp`)

- [x] A prototype rendered from the three recordings with a plain 43 ms window and with asymmetric windows of 10.7 and 21 ms delay: judged "much better" by the user (28 October 2026).
- [x] Our own real FFT, its butterflies four at a time: matches a direct DFT (16 to 1024 points) and round-trips exactly (to 8192); 4096 points forward and back take 27 to 37 us on the PC.
- [x] The voices, delay 16 ms: pitch, level and purity of sines at 24, 44.1, 48 and 96 kHz, against the design's targets.
- [x] Single notes with eight harmonics and the chord baseline (open E major, barre A) recorded; `test_voices` fails if a chord falls more than 1 dB below it.
- [x] A sine swept slowly from 70 Hz to 2 kHz: no voice steps further than its own sine could, by half again.
- [x] Delay with plucked tones at 82, 110, 165, 330 and 1000 Hz.
- [x] Swell, Tone, Dry exact, a level moved at once, extreme and non-finite controls and input; nothing in `step` allocates.
- [ ] Listened to through the plugin: the renders in `tmp/audio/out/` (below).
- [ ] The delay confirmed by playing (16 ms; 10.7 or 21 ms are one constant away, `Voices::synthesis`).

Measured at 48 kHz on 28 October 2026 (`test_voices` prints it all).

**Sines**, amplitude 0.5, 70 Hz to 4 kHz, each voice alone. Worst per voice (2 oct down, 1 oct down, 1 oct up, 2 oct up):

| Rate | Pitch, cents | Level, dB | Purity, dB |
| --- | --- | --- | --- |
| 48 kHz (and 24, 96) | 0.03, 0.00, 0.00, 0.00 | 1.35, 0.69, 0.10, 0.10 | 34, 34, 35, 35 |
| 44.1 kHz | 0.00, 0.01, 0.00, 0.00 | 1.39, 0.67, 0.13, 0.13 | 32, 33, 33, 33 |

Levels are within 0.1 dB from 250 Hz up. Below, 2 oct down loses up to 1.4 dB from notes under 100 Hz (outputs of 17 to 25 Hz, the region running into 0 Hz) and 1 oct down up to 0.7 dB at 70 Hz; the test allows 1.5 dB for 2 oct down below 100 Hz and 1 dB elsewhere.

Three things in the design came out of these measurements. Without zero phase at the centre of the synthesis window, each peak's phase was set 16 ms from where it was used, and purity on notes was 14 to 44 dB. Without the window's own phase taken out, a sine halfway between two bins (70 Hz at 44.1 kHz) came out at 7 dB of purity, its phase jumping whenever its peak changed bin. And whole-bin shifts err by up to half a bin: chosen from the peak's bin, they flipped as the peak moved; chosen from its frequency, with hysteresis, they were steady but still half a bin off at worst (up voices 11 and 17 dB at 70 Hz, 44.1 kHz), until the FFT was zero-padded to twice the window. Padding doubled the CPU and cost the low E's up voices 4 dB on notes with harmonics; it was kept because the half-bin cases otherwise come back wherever a partial lands.

**Single notes and chords**, eight harmonics of amplitude 1/h, purity in dB:

| Input | 2 oct down | 1 oct down | 1 oct up | 2 oct up |
| --- | --- | --- | --- | --- |
| 82.4 Hz | 29.3 | 27.0 | 18.3 | 18.3 |
| 110 Hz | 35.3 | 36.7 | 29.9 | 29.2 |
| 196 Hz | 38.2 | 39.8 | 39.8 | 40.2 |
| 329.6 Hz | 44.1 | 44.3 | 45.5 | 44.9 |
| Open E major | 9.4 | 7.7 | 4.8 | 3.5 |
| Barre A | 11.2 | 10.3 | 7.1 | 3.4 |

Against the band bank: single notes 7 to 28 dB then, 18 to 45 now; chords -5 to 6 then, 3 to 11 now. Chords are still the weak spot: in equal temperament, partials of different strings nearly coincide (the third harmonic of E2 and the fundamental of B3, 0.3 Hz apart) and share a peak.

**Sweep**, 70 Hz to 2 kHz in 10 s: biggest step 1.06, 1.02, 1.02 and 1.02 times what a sine at each voice's pitch could make.

**Delay to half level**, plucked tones with six harmonics, ms (2 oct down, 1 oct down, 1 oct up, 2 oct up):

| Note | | | | |
| ---: | ---: | ---: | ---: | ---: |
| 82.4 Hz | 17.3 | 19.5 | 20.3 | 20.9 |
| 110 Hz | 12.8 | 17.1 | 17.5 | 18.1 |
| 164.8 Hz | 16.4 | 17.2 | 18.7 | 18.6 |
| 330 Hz | 17.5 | 18.6 | 18.3 | 18.2 |
| 1000 Hz | 18.5 | 18.0 | 18.5 | 18.3 |

The same at every pitch, as the design intends: the synthesis window (16 ms) and a few ms for a note to build up.

**Swell** 0.5 s: a note at half of full scale reaches 90 % of its level in 0.52 s; a second note added later is 33 dB down a tenth of the Swell time in and reaches its level, while the first stays within 0.9 dB. **Tone**: within 0.5 dB of a 2-pole Butterworth at 200 Hz and 1 kHz; flat at 16 kHz. **Dry** 1 with the voices at 0 gives Out equal to In, sample for sample.

**Recordings** (`audio/`: flamenco chords and a chord progression at 44.1 kHz, strummed electric chords at 24 kHz; MP3, decoded with libsndfile to `tmp/audio/`), rendered through the built plugin to `tmp/audio/out/`: the default settings, each voice alone, a mix (dry 0.8, 2 oct down 0.3, 1 oct down 0.6, 1 oct up 0.4, Tone 6 kHz) and a Swell of 0.6 s. Measured from the files (`tmp/tools/analyse.py`): every voice's long-term spectrum is the input's at x0.25, x0.5, x2 and x4, correlation of the log spectra 0.89 to 0.98 (the band bank: 0.77 to 0.96); levels 0.8 to 1.6 dB below the input's; no clicks; the voices 16 to 17 ms behind; 1.4 to 3 % of a PC core.

## 4. The plugin (`src/lv2_entry.cpp`, `tests/test_lv2.cpp`, `tools/render.cpp`)

- [x] The glue: connect ports, clamp controls, `activate` clears the buffers, `run` per sample. Unchanged by the move to the phase vocoder.
- [x] `test_lv2`: dlopen the built `.so`, one descriptor with the right URI, the same input sliced at random points and unsliced: the outputs must match sample for sample. Also with In and Out on one buffer, and with fourteen control changes (some out of range or not finite) acting at their own frames, against the voices stepped directly.
- [x] `tools/render`: a WAV in, a WAV out, controls as `symbol=value`, to listen on the PC to recorded guitar. Used on the three recordings in `audio/` (step 3). Still wanted: recordings of single notes on every string, bends and palm mutes.
- [x] `make test` passes in WSL; `make lint` with lv2lint if it is installed. lv2lint 0.17.151: one warning, no author e-mail (`foaf:mbox`).
- [x] The CPU used per second of audio on the PC, recorded as a first guide to the Pi: 2.7 % of one core at 48 kHz (AMD Ryzen 3 7330U, WSL, GCC 14, -O2), a strummed chord with all four voices on. The worst single sample (a hop's work) took 168 us, against 2.7 ms in a 128-frame period. The Pi 5 is slower per core; whether it stays under the design's 5 % is step 7.

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
