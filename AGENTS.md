# Octant

Octant is an LV2 plugin that adds octaves to what you play, two down to two up, and follows chords as well as single notes, because a phase vocoder moves every partial of the spectrum on its own. It is a plugin written for **Peepo**, and Peepo is the host that matters. Read `docs/design.md` before changing behaviour or ports: it is the spec. `docs/plan.md` says what is done and what is next.

## Peepo

Peepo is a guitar pedal built on a Raspberry Pi 5 (2 GB, Debian 12 arm64) running the **Ingen** modular host on JACK, with an LVGL touch UI. Users patch LV2 modules together with audio, control (CV) and MIDI cables. It was called "Pedal PI" before; old names (`pedalpi`, `urn:pedalpi:ns#`) still turn up.

- Repository: `C:\Pedal PI` (https://github.com/ABustos2606/Peepo). It is readable from here.
- Docs worth reading there:
    - `docs/22-plugin-catalogue.md` and `config/catalogue/overlay.json`: how plugins are named, grouped and described in the UI;
    - `reference/upstream/ingen/`: Ingen's source.
- `C:\LV2-own-plugins\sway`, `C:\LV2-own-plugins\discipline` and `C:\LV2-own-plugins\loo-pi` are sister projects: same host limits, same kind of build and tests. Sway's layout is the one copied here.
- Peepo builds every third-party plugin from its own repository at a pinned commit (`tools/ingen-spike/14-more-plugins.sh`). Keep the build plain (`make`, no network access, works natively on arm64).

## Host limits that shape the code

- **Ingen runs a block in slices** when a control changes during a period, moving the audio pointers to each slice. Everything is per sample (a hop's FFT work falls on the sample that completes it) and carries on across slices; `tests/test_lv2.cpp` runs it that way. Never assume one `run()` per period.
- **xruns are the limit.** In `run()`: no allocation, no locks, no file or console I/O. Windows, tables and buffers are made in `instantiate`, for the host's sample rate. State is flushed before it reaches denormals. The worst single hop has to fit in a period.
- **Blocks run on several threads.** Instances share nothing.
- **No LV2 state.** Everything is in control ports, so patches save it.

## Rules

- Plugin URI: `https://github.com/ABustos2606/octant#octant`. Never use Peepo's namespace. Don't change a URI, port index or port symbol once a patch has been saved with it; new ports go at the end.
- Port names, value names and descriptions appear on Peepo's small screen: short, plain ASCII, at most 16 characters for port and value names, and they say what the control does. Never name or describe a pedal it resembles.
- License: GPL-2.0-or-later, `Copyright (c) 2026 Alejandro Bustos`; the text is in `LICENSE`. Every source file, header, Makefile and `.ttl` file starts with `SPDX-License-Identifier: GPL-2.0-or-later`; the plugin declares `doap:license <https://spdx.org/licenses/GPL-2.0-or-later>`. Octant is our own code: nothing in it is vendored.
- Line endings are LF. Files go to Linux and the Pi.
- Use Peepo's name, never "Pedal PI", in new text.
- Never commit or push; the user does.

## Testing

- `make test` (Linux; under WSL on the PC): `ttl_check`, the FFT on its own (`test_fft`), the voices (`test_voices`: pitch, level, purity, delay, chords) and the plugin sliced as Ingen runs it (`test_lv2`).
- Measurements against the design's targets are recorded in `docs/plan.md`, not just passed or failed.
- `make render` builds `build/render`, which runs a WAV through the plugin, for listening on the PC.
- Peepo's development engine runs under WSL (Debian) with Ingen in `/opt/peepo`; Peepo's `tools/ingen-spike/env.sh` starts it.
- On the Pi: play single notes on every string, chords and bends, and measure xruns at 128 frames, before calling a version done.
