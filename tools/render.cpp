// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
// Runs a WAV through the built plugin, to listen on the PC.
//   render IN.wav OUT.wav [symbol=value ...] [--plugin build/octant.lv2/octant.so]
// IN: PCM (16, 24 or 32 bit) or 32-bit float, any rate; several channels are
// mixed to one. OUT: mono, 32-bit float, at IN's rate. Symbols: dry, down_2,
// down_1, up_1, up_2, tone, swell; the rest keep their defaults.
#include "../src/ports.hpp"
#include <lv2/core/lv2.h>
#include <dlfcn.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

using octant::Port;
namespace {
void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
uint32_t u32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t u16(const unsigned char* p) { return uint16_t(p[0] | p[1] << 8); }

std::vector<float> read_wav(const std::string& path, uint32_t& rate) {
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "cannot open " + path);
    const std::vector<unsigned char> b((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    require(b.size() >= 12 && !std::memcmp(b.data(), "RIFF", 4) && !std::memcmp(b.data() + 8, "WAVE", 4), path + ": not a WAV file");
    uint16_t format = 0, channels = 0, bits = 0;
    const unsigned char* data = nullptr;
    size_t bytes = 0;
    for (size_t at = 12; at + 8 <= b.size();) {
        const uint32_t size = u32(b.data() + at + 4);
        const unsigned char* body = b.data() + at + 8;
        if (at + 8 + size > b.size()) break;
        if (!std::memcmp(b.data() + at, "fmt ", 4) && size >= 16) {
            format = u16(body);
            channels = u16(body + 2);
            rate = u32(body + 4);
            bits = u16(body + 14);
            if (format == 0xFFFE && size >= 26) format = u16(body + 24);  // extensible: the sub-format
        } else if (!std::memcmp(b.data() + at, "data", 4)) {
            data = body;
            bytes = size;
        }
        at += 8 + size + (size & 1);
    }
    require(data && channels > 0 && rate > 0, path + ": no format or data");
    require((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32), path + ": PCM 16, 24 or 32 bit, or float 32 bit");
    const size_t step = bits / 8, frames = bytes / (step * channels);
    std::vector<float> x(frames, 0);
    for (size_t f = 0; f < frames; ++f)
        for (size_t c = 0; c < channels; ++c) {
            const unsigned char* p = data + (f * channels + c) * step;
            float v;
            if (format == 3) std::memcpy(&v, p, 4);
            else if (bits == 16) v = int16_t(u16(p)) / 32768.0f;
            else if (bits == 24) v = float(int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) / 2147483648.0);
            else v = float(int32_t(u32(p)) / 2147483648.0);
            x[f] += v / channels;
        }
    return x;
}

void write_wav(const std::string& path, const std::vector<float>& y, uint32_t rate) {
    std::ofstream file(path, std::ios::binary);
    require(bool(file), "cannot write " + path);
    const auto put32 = [&](uint32_t v) { const char c[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)}; file.write(c, 4); };
    const auto put16 = [&](uint16_t v) { const char c[2] = {char(v), char(v >> 8)}; file.write(c, 2); };
    const uint32_t bytes = uint32_t(y.size() * 4);
    file.write("RIFF", 4); put32(36 + bytes); file.write("WAVEfmt ", 8);
    put32(16); put16(3); put16(1); put32(rate); put32(rate * 4); put16(4); put16(32);
    file.write("data", 4); put32(bytes);
    file.write(reinterpret_cast<const char*>(y.data()), bytes);
    require(bool(file), "cannot write " + path);
}
}

int main(int argc, char** argv) {
    try {
        std::string plugin = "build/octant.lv2/octant.so";
        std::vector<std::string> files;
        const char* symbols[] = {"dry", "down_2", "down_1", "up_1", "up_2", "tone", "swell"};
        float values[] = {1, 0, 0.6f, 0, 0, 16000, 0};
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--plugin" && i + 1 < argc) { plugin = argv[++i]; continue; }
            const size_t eq = a.find('=');
            if (eq == std::string::npos) { files.push_back(a); continue; }
            bool known = false;
            for (int c = 0; c < 7; ++c)
                if (a.compare(0, eq, symbols[c]) == 0 && eq == std::strlen(symbols[c])) { values[c] = std::strtof(a.c_str() + eq + 1, nullptr); known = true; }
            require(known, "unknown control " + a.substr(0, eq) + " (dry, down_2, down_1, up_1, up_2, tone, swell)");
        }
        require(files.size() == 2, "usage: render IN.wav OUT.wav [symbol=value ...] [--plugin PATH]");
        uint32_t rate = 0;
        std::vector<float> x = read_wav(files[0], rate);
        void* library = dlopen(plugin.c_str(), RTLD_NOW | RTLD_LOCAL);
        require(library, dlerror() ? dlerror() : "dlopen");
        const auto get = reinterpret_cast<const LV2_Descriptor* (*)(uint32_t)>(dlsym(library, "lv2_descriptor"));
        require(get && get(0), "no descriptor in " + plugin);
        const LV2_Descriptor* d = get(0);
        LV2_Handle h = d->instantiate(d, rate, ".", nullptr);
        require(h, "instantiate at " + std::to_string(rate) + " Hz");
        for (uint32_t c = 0; c < 7; ++c) d->connect_port(h, uint32_t(Port::dry) + c, &values[c]);
        d->activate(h);
        std::vector<float> y(x.size());
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t at = 0; at < x.size(); at += 128) {
            const uint32_t n = uint32_t(std::min<size_t>(128, x.size() - at));
            d->connect_port(h, uint32_t(Port::in), x.data() + at);
            d->connect_port(h, uint32_t(Port::out), y.data() + at);
            d->run(h, n);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (d->deactivate) d->deactivate(h);
        d->cleanup(h);
        dlclose(library);
        write_wav(files[1], y, rate);
        float peak = 0;
        for (float v : y) peak = std::max(peak, std::fabs(v));
        std::printf("%s: %.1f s at %u Hz, peak %.2f (%.1f dBFS), %.2f %% of a core\n", files[1].c_str(), double(x.size()) / rate, rate, peak,
                    20 * std::log10(std::max(peak, 1e-9f)), 100 * seconds * rate / std::max<size_t>(x.size(), 1));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "render: %s\n", e.what());
        return 1;
    }
}
