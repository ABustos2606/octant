// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
// The built plugin, run as Ingen runs it: a period split into slices at
// random points and at each control change, the audio pointers moved to each
// slice. Out must match the voices stepped directly, sample for sample, each
// control change acting at its own frame. Then the same unsliced, as other
// hosts run it, and with In and Out on one buffer.
//   test_lv2 build/octant.lv2/octant.so
#include "../src/ports.hpp"
#include "../src/voices.hpp"
#include <lv2/core/lv2.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using octant::Port;
namespace {
void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
constexpr uint32_t block = 128, controls = 7;
constexpr double pi = 3.14159265358979323846, rate = 48000;
// dry, down_2, down_1, up_1, up_2, tone, swell
using Controls = std::array<float, controls>;
octant::Settings settings(const Controls& c) { return {c[0], c[1], c[2], c[3], c[4], c[5], c[6]}; }
struct Change { uint64_t frame; uint32_t control; float value; };

struct Host {
    Host(const LV2_Descriptor* d, bool slicing, bool in_place) : descriptor(d), slicing(slicing), in_place(in_place) {
        handle = d->instantiate(d, rate, ".", features);
        require(handle, "instantiate");
        d->activate(handle);
    }
    ~Host() { if (descriptor->deactivate) descriptor->deactivate(handle); descriptor->cleanup(handle); }
    // One period of In. Sliced, a control change starts a slice at its frame,
    // as in Ingen; unsliced, it waits for the next period. Returns the frames
    // at which the changes were applied.
    std::vector<std::pair<uint64_t, Change>> period(const float* in, const std::vector<Change>& changes) {
        std::vector<std::pair<uint64_t, Change>> applied;
        const auto at = [&](const Change& c) { return slicing ? uint32_t(c.frame - frames) : 0u; };
        const auto inside = [&](const Change& c) { return c.frame >= frames && c.frame < frames + block; };
        std::vector<uint32_t> cuts;
        for (const auto& c : changes) if (inside(c)) cuts.push_back(at(c));
        float* out = this->out.data() + frames;
        if (in_place) std::copy(in, in + block, out);
        for (uint32_t offset = 0; offset < block;) {
            for (const auto& c : changes)
                if (inside(c) && at(c) == offset) {
                    values[c.control] = c.value;
                    applied.push_back({frames + offset, c});
                }
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            uint32_t count = slicing ? std::min(1 + rng % 40, block - offset) : block;
            for (uint32_t cut : cuts) if (cut > offset && cut < offset + count) count = cut - offset;
            descriptor->connect_port(handle, uint32_t(Port::in), const_cast<float*>(in_place ? out + offset : in + offset));
            descriptor->connect_port(handle, uint32_t(Port::out), out + offset);
            for (uint32_t c = 0; c < controls; ++c) descriptor->connect_port(handle, uint32_t(Port::dry) + c, &values[c]);
            descriptor->run(handle, count);
            offset += count;
        }
        frames += block;
        return applied;
    }
    const LV2_Descriptor* descriptor;
    bool slicing, in_place;
    const LV2_Feature* features[1]{nullptr};
    LV2_Handle handle;
    Controls values{};
    std::vector<float> out = std::vector<float>(1 << 16, NAN);
    uint64_t frames = 0;
    uint32_t rng = 0x9E3779B9;
};

// Plucked notes and a chord, with a few frames that are not finite.
std::vector<float> input(uint64_t length) {
    std::vector<float> x(length, 0);
    const double notes[][2] = {{0, 82.41}, {3000, 196}, {7000, 329.63}, {7000, 246.94}, {7000, 207.65}, {12000, 110}};
    for (const auto& note : notes)
        for (uint64_t n = uint64_t(note[0]); n < length; ++n) {
            const double t = double(n - uint64_t(note[0])) / rate;
            for (int h = 1; h <= 4; ++h) x[n] += float(0.15 / h * std::sin(2 * pi * h * note[1] * t) * std::exp(-t / 0.5));
        }
    x[9000] = NAN;
    x[9001] = INFINITY;
    return x;
}

// Runs the plugin through the input and the control changes, and steps the
// voices directly with what the plugin saw, when it saw it.
void play(const LV2_Descriptor* d, bool slicing, bool in_place, const Controls& initial, const std::vector<Change>& changes) {
    const std::string how = std::string(slicing ? "sliced as Ingen runs it" : "unsliced") + (in_place ? ", in place" : "");
    const uint64_t length = 120 * block;
    const std::vector<float> x = input(length);
    Host host(d, slicing, in_place);
    host.values = initial;
    std::vector<std::pair<uint64_t, Change>> applied;
    for (uint64_t start = 0; start < length; start += block) {
        const auto a = host.period(x.data() + start, changes);
        applied.insert(applied.end(), a.begin(), a.end());
    }
    octant::Voices voices(rate);
    Controls values = initial;
    size_t next = 0;
    double energy = 0;
    for (uint64_t f = 0; f < length; ++f) {
        while (next < applied.size() && applied[next].first == f) { values[applied[next].second.control] = applied[next].second.value; ++next; }
        const float want = voices.step(x[f], settings(values));
        const float got = host.out[f];
        require(std::isfinite(got), "Out finite at frame " + std::to_string(f) + ", " + how);
        require(got == want, "Out at frame " + std::to_string(f) + " matches the voices, " + how + ": got " + std::to_string(got) + ", want " + std::to_string(want));
        energy += double(got) * got;
    }
    require(next == applied.size(), "every change applied");
    require(energy > 1, "Out is not silent, " + how);
}
}

int main(int argc, char** argv) {
    try {
        require(argc > 1, "usage: test_lv2 PLUGIN.so");
        void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        require(library, dlerror() ? dlerror() : "dlopen");
        using Get = const LV2_Descriptor* (*)(uint32_t);
        const auto get = reinterpret_cast<Get>(dlsym(library, "lv2_descriptor"));
        require(get && get(0) && !get(1), "one descriptor");
        require(std::string(get(0)->URI) == "https://github.com/ABustos2606/octant#octant", "URI");
        require(get(0)->instantiate(get(0), 0, ".", nullptr) == nullptr, "no instance at a sample rate of 0");
        const Controls defaults{1, 0, 0.6f, 0, 0, 16000, 0};
        const Controls all{0.5f, 0.7f, 0.6f, 0.5f, 0.4f, 3000, 0.1f};
        // Every control moved inside periods, some at the same frame, and
        // some out of range or not finite.
        const std::vector<Change> changes = {{77, 1, 1}, {500, 3, 1}, {500, 4, 0.5f}, {1300, 5, 400}, {2001, 6, 0.5f}, {3333, 0, 0},
                                             {4100, 2, 1}, {5000, 5, 16000}, {6000, 6, 0}, {7100, 1, NAN}, {8000, 5, -5}, {9500, 3, 7},
                                             {11000, 0, INFINITY}, {13000, 2, 0}};
        for (const bool slicing : {true, false}) {
            for (const bool in_place : {false, true}) {
                play(get(0), slicing, in_place, defaults, {});
                play(get(0), slicing, in_place, all, changes);
            }
        }
        std::puts("PASS test_lv2: sliced as Ingen runs it, unsliced and in place, Out matches the voices; control changes act at their own frames");
        dlclose(library);
    } catch (const std::exception& e) {
        std::printf("FAIL test_lv2: %s\n", e.what());
        return 1;
    }
}
