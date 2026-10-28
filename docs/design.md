# Octant: design

Adds octaves to what you play: two down, one down, one up and two up, each with its own level beside the dry signal. It follows chords as well as single notes: a phase vocoder finds every partial in the spectrum and moves each one on its own, rather than finding one pitch for the whole signal. Written for Peepo (Ingen on JACK, Raspberry Pi 5). This document is the spec: ports and behaviour change here first.

Plugin: **Octant** (`https://github.com/ABustos2606/octant#octant`), an `lv2:PitchPlugin`. Peepo lists it under Pitch.

## Ports

Indices and symbols are permanent once a patch is saved with them; new ports go at the end.

| Index | Symbol | Kind | Range, default | What it is |
| --- | --- | --- | --- | --- |
| 0 | `in` | audio in | | The guitar |
| 1 | `out` | audio out | | Dry plus the four octave voices |
| 2 | `dry` | control | 0 to 1; 1 | Level of the input, passed straight through with no delay |
| 3 | `down_2` | control | 0 to 1; 0 | Level of the voice two octaves down |
| 4 | `down_1` | control | 0 to 1; 0.6 | Level of the voice one octave down |
| 5 | `up_1` | control | 0 to 1; 0 | Level of the voice one octave up |
| 6 | `up_2` | control | 0 to 1; 0 | Level of the voice two octaves up |
| 7 | `tone` | control | 200 to 16000 Hz, logarithmic; 16000 | Low-pass on the four voices together (not on dry) |
| 8 | `swell` | control | 0 to 2 s; 0 | How long each new note's voices take to fade in; 0 is instant |

Port names on screen: In, Out, Dry, 2 oct down, 1 oct down, 1 oct up, 2 oct up, Tone, Swell.

Levels are plain gains. At level 1, a sine of amplitude A at the input gives a sine of amplitude A at the voice's pitch, within 1 dB from 70 Hz to 4 kHz (but see Limits for two octaves down from the lowest notes). Mono in, mono out.

## How it works

The first version (28 October 2026) shifted each note in its own narrow band of a bank of complex filters. Measured, and then heard, it was unusable on real notes and chords: neighbouring bands overlapped, and every band carried two partials into the shift (docs/plan.md, "The band bank, abandoned"). It was replaced the same day by this phase vocoder.

### 1. Short-time spectra, with a short delay

Every **hop** the newest samples are windowed and transformed. Two windows, as in Mauler and Martin's low-delay STFT:

- The **analysis window** is long, about 85 ms (4096 samples at 44.1 and 48 kHz; a power of 2 at any rate), so neighbouring partials of a low note (82 Hz apart for a low E) are told apart. It is asymmetric: it rises over all but the last 8 ms and falls over those, so the newest samples weigh most.
- The **synthesis window** covers only the last 16 ms (768 samples at 48 kHz), and the two windows multiply to a Hann window of that length. Only that part is resynthesised and overlap-added, four frames to a window (a hop of 4 ms). The voices' delay is the synthesis window, **16 ms**, the same for every note. 85 ms of analysis with a plain window would delay them by 85 ms. Prototypes at 10.7, 21 and 43 ms were rendered for listening; 16 ms is a compromise between the first two (10.7 ms measured rougher on low notes), still to be confirmed by playing.
- The FFT is **twice the analysis window** (zero-padded), so its bins are half as far apart, about 5.9 Hz.
- **Zero phase at the centre of the synthesis window:** each frame is turned so that sample sits at index 0, and every phase is measured and set there.

### 2. Partials

- **Peaks:** local maxima of the power spectrum, higher than every bin within two unpadded bins either side, within 70 dB of the hop's loudest and above -100 dBFS, from 40 Hz to 6 kHz. Each peak's **region** runs from the lowest bin between it and the peak below to the lowest between it and the peak above.
- **Frequency:** from the peak bin's phase turn since the last hop, against the turn its bin's own frequency would make: exact to a fraction of a cent on a steady partial.
- **Phase:** the peak bin's phase, less the window's own phase at the peak's offset from that bin (tabulated in `instantiate`). Without that, a partial halfway between two bins changed phase whenever its peak moved from one to the other.
- **Carried on:** a peak within two unpadded bins of a peak of the last hop is the same partial, carried on; the lists are matched in one pass, both rising.

### 3. The four voices

For each voice with ratio `r` (1/4, 1/2, 2 or 4) and each partial of frequency `f`:

- The output phase is the carried partial's last one, turned on by `r * f` over a hop; a new partial starts from `r` times its own phase. So the voice turns at exactly `r` times the partial, through bends and vibrato, and the down voices, whose phase is a running total, have no jumps.
- The whole region is moved by a whole number of bins, chosen from `r * f - f` (not from the peak's bin), and a carried partial keeps its last shift while that is within 0.75 of a bin: the shift does not flip from hop to hop. The region's bins are all turned by the same angle, the output phase less the partial's phase (identity phase locking), so the partial keeps its shape.
- A voice skips what would land above 0.4 of the sample rate, or below half a bin.
- Every voice goes into one output spectrum, times its level: one FFT in and one out per hop, whatever the number of voices.
- The levels are smoothed per sample over about 10 ms, and a level is applied at each hop: the overlapping windows fade from one hop's level to the next.

### 4. Swell and Tone

- **Swell:** each partial has a gain. A new partial starts at -60 dB and rises at a steady rate in dB, 60 dB in the Swell time; a partial carried on keeps rising from where it was, so a note played again while the last one still rings does not swell again. Each note of a chord swells on its own. With Swell at 0 the gain is 1.
- **Tone:** a 2-pole low-pass (12 dB per octave, Butterworth) on the sum of the four voices, set by Tone. At 16000 Hz it is open: from 8 kHz up the filter fades into the voices unfiltered.

**Out** = Dry level x input (no delay) + the four voices x their levels, through Tone. Nothing limits the sum: four voices at full level beside dry can exceed the input's level.

## Targets

These are measured by the tests in the plan, on the PC, then on the Pi.

- **Pitch:** a steady sine in, each voice within 1 cent of its ratio, from 70 Hz to 1.3 kHz (and 2 kHz for the down voices).
- **Level:** within 1 dB across 70 Hz to 4 kHz, at the ratios above; 1.5 dB for two octaves down from below 100 Hz (see Limits).
- **Purity:** a steady sine, everything at the voice's output but the shifted sine at least 30 dB down. Single notes with harmonics and chords are measured and recorded, and kept from getting worse.
- **Delay**, from a note's start to its voice reaching half its settled level: within the synthesis window (16 ms) and 6 ms, at every pitch.
- **Chords:** an open E major and a barre A, synthesised from harmonic tones, measured as the energy at the expected shifted partials against everything else. Recorded as a baseline, then kept from getting worse.
- **CPU:** at most 5 % of one Pi 5 core at 48 kHz and 128 frames, with no xruns over 10 minutes.

## Host limits that shape the code

- **Ingen runs a block in slices** when a control input changes during a period, moving the audio pointers to each slice. Octant takes one sample at a time; a hop's work falls on the sample that completes it, wherever the slices fall, so a slice simply carries on. The plugin test runs it sliced at random points and unsliced and needs the same output, sample for sample.
- **One hop's work in one sample.** The FFTs and the partials of a hop all run on the sample that completes it, so a period with a hop in it costs more than one without; at 128 frames and a 192-sample hop, about two periods in three have one. The worst hop is what has to fit in a period.
- **No allocation, locks or I/O in `run()`.** Windows, tables and every buffer are made in `instantiate`, for whatever sample rate the host gives.
- **Denormals:** state (in float) below 1e-15, 300 dB down, is flushed to zero.
- **Our own FFT** (`src/fft.hpp`): a real-input FFT as a complex one of half the size, real and imaginary parts in separate arrays, its butterflies four at a time with GCC's vector types (SSE on a PC, NEON on the Pi), whatever the optimisation flags. Nothing is vendored.
- **Built with -O2.** Peepo builds the project's own plugins with `OPT="-O2 -mcpu=cortex-a76"`; the Makefile adds `-fno-fast-math -fno-math-errno -fno-trapping-math` after it: IEEE arithmetic, NaN and Inf kept, and loops free to vectorise.
- **Blocks may run on several threads;** instances share nothing.
- **No LV2 state.** Everything is in control ports, so patches save it.
- **No latency is reported.** Dry is not delayed, and the voices are a voice beside it, not a replacement for it. Ingen does not compensate latency in any case.

## Limits

- **Close partials mix.** Two partials closer than the analysis window can tell apart (about 25 Hz) share a peak, and their shift is rough. In dense chords, notes a semitone or a tone apart in the low register, and partials of different strings that nearly coincide, do this most: synthesised six-string chords measure 4 to 11 dB of purity, single notes with eight harmonics 18 to 45 dB (docs/plan.md).
- **Two octaves down from the lowest notes** (17 to 25 Hz from notes below 100 Hz) loses up to 1.4 dB, its region running into 0 Hz. It is felt more than heard.
- **Attacks** are likely to be softened in the voices, since a note's partials build up over the analysis window (its plucked-tone delay is measured, its sound not yet judged). Dry carries the attack.
- A sound with no clear partials (noise, a heavily distorted signal) shifts as noise. Octant goes before drive, not after it.

## Not in the first version

- Any interval other than octaves: a fifth, a third, a detune or a bend. The voices already move each partial to `r * f`, so another ratio is a new port, not a new design.
- A CV input for the levels or for a bend, so a foot switch or Sway can move them.
- A resonance control for Tone.
- Stereo, for example the down voices on one side.
- A choice of delay (10 or 21 ms instead of 16): a new port, if playing asks for it.
