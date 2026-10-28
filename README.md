# Octant

An LV2 plugin that adds octaves to what you play, two down to two up, and follows chords as well as single
notes, written for Peepo and other LV2 hosts.
GPL-2.0-or-later, Copyright (c) 2026 Alejandro Bustos; see [LICENSE](LICENSE).

**Octant** (`https://github.com/ABustos2606/octant#octant`) has one audio input and one audio output. Its
controls:

| Control | What it does |
| --- | --- |
| Dry | Level of the input, passed straight through |
| 2 oct down, 1 oct down, 1 oct up, 2 oct up | Level of each octave voice |
| Tone | Low-pass on the octave voices, 200 Hz to 16 kHz (open) |
| Swell | How long each new note's voices take to fade in, 0 to 2 s |

Each note is shifted in its own narrow band, so chords stay in tune. Low notes respond later than high ones:
a band narrow enough to separate low notes needs time to settle. [docs/design.md](docs/design.md) is the
spec and [docs/plan.md](docs/plan.md) the plan.

**Status:** design only; there is no DSP code yet, so `make` does not build. The ports can be checked on the
source bundle with `python3 tests/ttl_check.py octant.lv2`.

## Build and test

Needs a C++17 compiler, `pkg-config` and the LV2 headers (`lv2-dev`); the tests need `python3-rdflib`.

```sh
make            # build/octant.lv2
make test       # Turtle against the ports, the band bank, the voices, and the plugin sliced as Ingen runs it
make render     # build/render: a WAV through Octant, to listen on the PC
make install PREFIX=/usr LV2DIR=/usr/lib/lv2
```
