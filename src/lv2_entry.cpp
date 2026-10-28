// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
#include "ports.hpp"
#include "voices.hpp"
#include <lv2/core/lv2.h>
#include <cstdint>
#include <new>

namespace octant {
namespace {
constexpr const char* uri = "https://github.com/ABustos2606/octant#octant";
constexpr uint32_t first_control = static_cast<uint32_t>(Port::dry), controls = static_cast<uint32_t>(Port::count) - first_control;
// A control not connected keeps its default.
float control(const float* port, float fallback) { return port ? *port : fallback; }

struct Plugin {
    explicit Plugin(double rate) : voices(rate) {}
    Voices voices;
    const float* in = nullptr;
    float* out = nullptr;
    const float* settings[controls]{};
};

// The bands are built here, for the host's sample rate.
LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const*) {
    if (!(rate >= 8000 && rate <= 384000)) return nullptr;
    return new (std::nothrow) Plugin(rate);
}
void connect_port(LV2_Handle handle, uint32_t port, void* data) {
    auto& self = *static_cast<Plugin*>(handle);
    if (port == static_cast<uint32_t>(Port::in)) self.in = static_cast<const float*>(data);
    else if (port == static_cast<uint32_t>(Port::out)) self.out = static_cast<float*>(data);
    else if (port >= first_control && port < first_control + controls) self.settings[port - first_control] = static_cast<const float*>(data);
}
void activate(LV2_Handle handle) { static_cast<Plugin*>(handle)->voices.reset(); }
// Per sample, so a slice simply carries on where the last one ended
// (docs/design.md: Ingen runs a block in slices). Voices clamps the controls.
void run(LV2_Handle handle, uint32_t frames) {
    auto& self = *static_cast<Plugin*>(handle);
    const Settings d;
    Settings s;
    s.dry = control(self.settings[0], d.dry);
    s.down_2 = control(self.settings[1], d.down_2);
    s.down_1 = control(self.settings[2], d.down_1);
    s.up_1 = control(self.settings[3], d.up_1);
    s.up_2 = control(self.settings[4], d.up_2);
    s.tone = control(self.settings[5], d.tone);
    s.swell = control(self.settings[6], d.swell);
    for (uint32_t i = 0; i < frames; ++i) {
        const float y = self.voices.step(self.in ? self.in[i] : 0.0f, s);
        if (self.out) self.out[i] = y;
    }
}
void cleanup(LV2_Handle handle) { delete static_cast<Plugin*>(handle); }
const LV2_Descriptor descriptor = {uri, instantiate, connect_port, activate, run, nullptr, cleanup, nullptr};
static_assert(controls == 7, "seven controls after In and Out");
static_assert(static_cast<uint32_t>(Port::swell) == first_control + 6, "Swell is the last control");
}
}

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    return index == 0 ? &octant::descriptor : nullptr;
}
