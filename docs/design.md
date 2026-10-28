# Octant: design

Adds octaves to what you play: two down, one down, one up and two up, each with its own level beside the dry signal. It follows chords as well as single notes, because every note is shifted in its own narrow band rather than by finding one pitch for the whole signal. Written for Peepo (Ingen on JACK, Raspberry Pi 5). This document is the spec: ports and behaviour change here first.

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

Levels are plain gains. At level 1, a sine of amplitude A at the input gives a sine of amplitude A at the voice's pitch, within 1 dB from 70 Hz to 4 kHz. Mono in, mono out.

## How it works

### 1. The band bank

The input runs through a bank of complex band-pass filters (each a cascade of complex one-pole filters, `N` stages). A complex filter gives, for every band and every sample, a complex value `z = a * e^(i*phi)`: the band's amplitude `a` and phase `phi`. When one partial dominates a band, `phi` turns at that partial's frequency.

- Range: about 65 Hz (below the low E of a guitar in drop tuning) to about 5 kHz. Fundamentals of a guitar end near 1.3 kHz; the bands above that carry harmonics, so the shifted voices keep the guitar's colour.
- Width: **wider at the bottom, narrower above.** About 1/3 octave below 250 Hz, narrowing to about 1/6 octave above 500 Hz. Starting values: the tests in the plan set the final ones. About 30 bands in all.
- Why not narrow everywhere: a narrow filter takes long to respond, and its delay grows as the band gets lower. For `N = 3`, the delay at the centre of a band is about `0.49 / B` seconds, where `B` is its width in Hz. At 82 Hz a 1/6-octave band (`B` = 9.5 Hz) would delay its voice by about 50 ms; a 1/3-octave band (19 Hz) by about 25 ms. At 1 kHz a 1/6-octave band delays by about 4 ms. Low notes respond later than high ones whatever the design. This is the price of following chords, not a defect to fix later.
- The cost of wide low bands: two low notes less than a third of an octave apart share a band and their shifts mix (see Limits). Low-register chords on a guitar are mostly fifths and octaves, which stay in separate bands.

### 2. One partial, one band

A partial sits between two neighbouring bands and both see it, with different phases (one filter lags, the other leads). Summing both copies after shifting would cancel part of the partial, and doubling a phase doubles the difference. So each band also measures its **instantaneous frequency**, `w = arg(z[n] * conj(z[n-1]))`, and:

- **Phase and level correction:** the band's own response at `w` is known exactly, so its phase lag and gain at `w` are taken out. Every band that sees a partial then reports the same phase and amplitude for it.
- **Territory:** each band owns the frequencies between the midpoints to its neighbours. A band's weight is 1 when `w` is well inside its territory, 0 when outside, with a short crossfade at the edges; the weights of neighbours add to 1. A partial is voiced by the band that owns it, not by every band that hears it.

The plan builds the plain sum first and measures it, so the effect of this step is shown by numbers, not assumed.

### 3. The four voices

Each band keeps its phase **unwrapped**: a running total of `w`, never folded back into one turn. A voice with ratio `k` (1/4, 1/2, 2 or 4) turns that total into `k * phi` and outputs `a * cos(k * phi)`, summed over the bands with their weights.

- **Up** (2 and 4): `k * phi` is exact, and the voice follows every bend and vibrato of the note at the new pitch.
- **Down** (1/2 and 1/4): halving an unwrapped phase has no jumps, so the voice is continuous, with no clicks. Two bands that hear the same partial can disagree by a whole turn in their totals, which halving turns into opposite signs. So when a band's weight rises from 0, its total is first matched to the neighbour that owns the partial (the handoff), and they then agree.
- A voice skips a band whose shifted centre would be above 0.4 of the sample rate (19.2 kHz at 48 kHz), so nothing folds back from above the top.
- The levels are smoothed per sample over about 10 ms, so moving a knob does not click.

### 4. Swell and Tone

- **Swell:** each band's amplitude for the voices may rise no faster than Swell allows (rising from silence to full level takes the Swell time) and falls freely. Each note of a chord swells on its own. A note played again while the last one still rings does not swell again.
- **Tone:** a 2-pole low-pass (12 dB per octave) on the sum of the four voices, set by Tone. At 16000 Hz it is open.

**Out** = Dry level x input (no delay) + the four voices x their levels, through Tone. Nothing limits the sum: four voices at full level beside dry can exceed the input's level.

## Targets

These are measured by the tests in the plan, on the PC, then on the Pi.

- **Pitch:** a steady sine in, each voice within 1 cent of its ratio, from 70 Hz to 1.3 kHz (and 2 kHz for the down voices).
- **Level:** within 1 dB across 70 Hz to 4 kHz, at the ratios above.
- **Purity:** a steady single note, everything at the voice's output except the shifted partials at least 30 dB down.
- **Delay**, from a note's start to its voice reaching half its settled level: at most 25 ms at 82 Hz, 8 ms at 330 Hz, 3 ms at 1 kHz. A low note's up voices start sooner than its fundamental's band would suggest, because its harmonics sit in faster bands.
- **Chords:** an open E major and a barre A, synthesised from harmonic tones, measured as the energy at the expected shifted partials against everything else. Recorded first as a baseline, then kept from getting worse.
- **CPU:** at most 5 % of one Pi 5 core at 48 kHz and 128 frames, with no xruns over 10 minutes.

## Host limits that shape the code

- **Ingen runs a block in slices** when a control input changes during a period, moving the audio pointers to each slice. Everything in Octant is per sample (filters, phases, smoothing), so a slice simply carries on: the plugin test runs it sliced at random points and unsliced and needs the same output, sample for sample.
- **No allocation, locks or I/O in `run()`.** Band coefficients are computed in `instantiate`, for whatever sample rate the host gives; all state is fixed-size.
- **Denormals:** a filter ringing down to silence must not reach denormal numbers; state below about 1e-20 is flushed to zero.
- **Blocks may run on several threads;** instances share nothing.
- **No LV2 state.** Everything is in control ports, so patches save it.
- **No latency is reported.** Dry is not delayed, and the voices' delay depends on the note. Ingen does not compensate latency in any case.

## Limits

- Two notes that share a band (closer than its width) mix: their shift carries a sum-and-difference tone that sounds rough or out of tune. Most likely at the bottom, where bands are widest, and with dense chords.
- High harmonics of one note are closer together than a band from about the 6th harmonic up; their shift is less pure. It colours the up voices rather than detuning them.
- A sound with no clear partials (noise, a heavily distorted signal) shifts as noise. Octant goes before drive, not after it.

## Not in the first version

- Any interval other than octaves: a fifth, a third, a detune or a bend. The voices already use `k * phi`, so another ratio is a new port, not a new design.
- A CV input for the levels or for a bend, so a foot switch or Sway can move them.
- A resonance control for Tone.
- Stereo, for example the down voices on one side.
