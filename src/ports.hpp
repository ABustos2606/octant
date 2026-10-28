// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
#pragma once
#include <cstdint>

// Port indices of Octant. Permanent once a patch is saved with them: new ports
// go at the end. tests/ttl_check.py compares them with the Turtle.
namespace octant {
enum class Port : uint32_t {
    in = 0,      // audio_in
    out = 1,     // audio_out
    dry = 2,     // control_in
    down_2 = 3,  // control_in
    down_1 = 4,  // control_in
    up_1 = 5,    // control_in
    up_2 = 6,    // control_in
    tone = 7,    // control_in
    swell = 8,   // control_in
    count = 9
};
}
