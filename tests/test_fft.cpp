// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
// The real FFT on its own: against a direct DFT, the round trip, and its cost.
#include "../src/fft.hpp"
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }

void against_dft(int n) {
    octant::RealFft fft(n);
    std::vector<float> x(n), re(n / 2 + 1), im(n / 2 + 1), back(n);
    uint32_t seed = 12345;
    for (float& v : x) { seed = seed * 1664525u + 1013904223u; v = float(int32_t(seed)) / 2147483648.0f; }
    fft.forward(x.data(), re.data(), im.data());
    double worst = 0, scale = 0;
    for (int k = 0; k <= n / 2; ++k) {
        std::complex<double> s = 0;
        for (int i = 0; i < n; ++i) s += double(x[i]) * std::polar(1.0, -2 * 3.14159265358979323846 * double(k) * i / n);
        worst = std::max(worst, std::abs(s - std::complex<double>(re[k], im[k])));
        scale = std::max(scale, std::abs(s));
    }
    require(worst < 1e-5 * scale * std::log2(n), "n " + std::to_string(n) + ": matches a direct DFT, error " + std::to_string(worst / scale));
    fft.inverse(re.data(), im.data(), back.data());
    double err = 0;
    for (int i = 0; i < n; ++i) err = std::max(err, std::fabs(double(back[i]) - x[i]));
    require(err < 1e-5, "n " + std::to_string(n) + ": inverse(forward(x)) is x, error " + std::to_string(err));
}
}

int main() {
    try {
        for (const int n : {16, 32, 64, 256, 1024}) against_dft(n);
        // Round trip at the sizes the voices use.
        for (const int n : {2048, 4096, 8192}) {
            octant::RealFft fft(n);
            std::vector<float> x(n), re(n / 2 + 1), im(n / 2 + 1), back(n);
            for (int i = 0; i < n; ++i) x[i] = float(std::sin(0.1 * i) + 0.3 * std::cos(1.7 * i));
            fft.forward(x.data(), re.data(), im.data());
            fft.inverse(re.data(), im.data(), back.data());
            double err = 0;
            for (int i = 0; i < n; ++i) err = std::max(err, std::fabs(double(back[i]) - x[i]));
            require(err < 1e-5, "n " + std::to_string(n) + ": round trip, error " + std::to_string(err));
        }
        octant::RealFft fft(4096);
        std::vector<float> x(4096, 0.5f), re(2049), im(2049);
        const auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 2000; ++k) { fft.forward(x.data(), re.data(), im.data()); fft.inverse(re.data(), im.data(), x.data()); }
        const double us = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 2000 * 1e6;
        std::printf("  n 4096: forward and inverse %.1f us on this PC\n", us);
        std::puts("PASS test_fft: against a direct DFT (16 to 1024) and round trips (to 8192)");
    } catch (const std::exception& e) {
        std::printf("FAIL test_fft: %s\n", e.what());
        return 1;
    }
}
