// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
// The voices against the design's targets: pitch, level and purity of steady
// sines and of single notes with harmonics, two chords, a slow sweep, the
// delay, Swell, Tone, Dry, and extreme controls and input. The figures are
// printed for docs/plan.md. Nothing in step() may allocate: operator new is
// counted.
#include "../src/voices.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace { long allocations = 0; }
void* operator new(std::size_t size) {
    ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace octant;
namespace {
constexpr double pi = 3.14159265358979323846;
const char* const name[voices] = {"2 oct down", "1 oct down", "1 oct up", "2 oct up"};
void require(bool ok, const std::string& what) { if (!ok) throw std::runtime_error(what); }
double db(double v) { return 20 * std::log10(v); }
std::string hz(double f) { char s[32]; std::snprintf(s, sizeof s, "%.1f Hz", f); return s; }

// Settings with one voice alone at level 1 and no dry.
Settings alone(int k) {
    Settings s;
    s.dry = 0;
    s.down_1 = 0;
    float* level[voices] = {&s.down_2, &s.down_1, &s.up_1, &s.up_2};
    *level[k] = 1;
    return s;
}

// Least squares fit of sines at `freqs` to y. Returns the energy of the fit
// and sets the residual's; the amplitude and phase of each sine if asked.
double fit(const std::vector<float>& y, double rate, const std::vector<double>& freqs, double& residual, std::vector<std::complex<double>>* sines = nullptr) {
    const size_t m = 2 * freqs.size();
    std::vector<double> a(m * m, 0), b(m, 0), basis(m);
    std::vector<std::complex<double>> turn(freqs.size()), at(freqs.size(), 1);
    for (size_t k = 0; k < freqs.size(); ++k) turn[k] = std::polar(1.0, 2 * pi * freqs[k] / rate);
    double energy = 0;
    for (size_t n = 0; n < y.size(); ++n) {
        for (size_t k = 0; k < freqs.size(); ++k) {
            if (n % 4096 == 0) at[k] = std::polar(1.0, 2 * pi * freqs[k] * double(n) / rate);
            basis[2 * k] = at[k].real();
            basis[2 * k + 1] = at[k].imag();
            at[k] *= turn[k];
        }
        for (size_t r = 0; r < m; ++r) {
            b[r] += basis[r] * y[n];
            for (size_t c = r; c < m; ++c) a[r * m + c] += basis[r] * basis[c];
        }
        energy += double(y[n]) * y[n];
    }
    for (size_t r = 0; r < m; ++r) for (size_t c = 0; c < r; ++c) a[r * m + c] = a[c * m + r];
    std::vector<double> x(b), lu(a);
    for (size_t p = 0; p < m; ++p) {  // Gaussian elimination, partial pivoting
        size_t best = p;
        for (size_t r = p + 1; r < m; ++r) if (std::fabs(lu[r * m + p]) > std::fabs(lu[best * m + p])) best = r;
        for (size_t c = 0; c < m; ++c) std::swap(lu[p * m + c], lu[best * m + c]);
        std::swap(x[p], x[best]);
        for (size_t r = p + 1; r < m; ++r) {
            const double f = lu[r * m + p] / lu[p * m + p];
            for (size_t c = p; c < m; ++c) lu[r * m + c] -= f * lu[p * m + c];
            x[r] -= f * x[p];
        }
    }
    for (size_t p = m; p-- > 0;) {
        for (size_t c = p + 1; c < m; ++c) x[p] -= lu[p * m + c] * x[c];
        x[p] /= lu[p * m + p];
    }
    double fitted = 0;
    for (size_t r = 0; r < m; ++r) fitted += x[r] * b[r];
    residual = std::max(energy - fitted, 1e-30);
    if (sines) {
        sines->clear();
        for (size_t k = 0; k < freqs.size(); ++k) sines->push_back({x[2 * k], -x[2 * k + 1]});
    }
    return fitted;
}

// Runs `frames` samples of `input` and keeps Out from `from` on. step() may
// not allocate.
template <class Input>
std::vector<float> run(Voices& v, const Settings& s, Input input, int frames, int from) {
    std::vector<float> y;
    y.reserve(size_t(frames - from));
    for (int n = 0; n < frames; ++n) {
        const long before = allocations;
        const float o = v.step(input(n), s);
        require(allocations == before, "step allocates");
        if (n >= from) y.push_back(o);
    }
    return y;
}

// Purity of a voice: energy at the expected partials against the rest.
double purity(const std::vector<float>& y, double rate, const std::vector<double>& freqs) {
    double residual;
    const double fitted = fit(y, rate, freqs, residual);
    return 10 * std::log10(fitted / residual);
}

struct Steady { double cents, level, purity; };
// A sine of amplitude 0.5 at f, each voice alone: its pitch (from the turn of
// its phase between the two halves of a second), level and purity.
Steady steady(double rate, double f, int k) {
    Voices v(rate);
    const int settle = int(rate / 2), length = int(rate);
    const auto y = run(v, alone(k), [&](int n) { return float(0.5 * std::sin(2 * pi * f * n / rate)); }, settle + length, settle);
    const double g = f * ratio[k];
    double residual, r;
    std::vector<std::complex<double>> whole, first, second;
    const double fitted = fit(y, rate, {g}, residual, &whole);
    fit(std::vector<float>(y.begin(), y.begin() + length / 2), rate, {g}, r, &first);
    fit(std::vector<float>(y.begin() + length / 2, y.end()), rate, {g}, r, &second);
    const double turn = std::arg(second[0] * std::conj(first[0] * std::polar(1.0, 2 * pi * g * (length / 2) / rate)));
    const double offset = turn / (2 * pi * (length / 2) / rate);
    return {1200 * std::log2(1 + offset / g), db(std::abs(whole[0]) / 0.5), 10 * std::log10(fitted / residual)};
}

void sines(double rate, bool print) {
    const std::vector<double> freqs = {70, 82.41, 98, 110, 146.8, 196, 246.9, 330, 440, 587.3, 784, 987.8, 1300, 2000, 3000, 4000};
    double worst[3][voices] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {1e9, 1e9, 1e9, 1e9}};
    if (print) std::printf("  %9s  %-30s  %-30s  %-26s\n", "sine", "pitch, cents", "level, dB", "purity, dB");
    for (const double f : freqs) {
        char line[3][100] = {"", "", ""};
        for (int k = 0; k < voices; ++k) {
            if (f * ratio[k] > 0.4 * rate) continue;
            const Steady s = steady(rate, f, k);
            const bool pitch = f <= (k < 2 ? 2000 : 1300);
            if (pitch) worst[0][k] = std::max(worst[0][k], std::fabs(s.cents));
            worst[1][k] = std::max(worst[1][k], std::fabs(s.level));
            worst[2][k] = std::min(worst[2][k], s.purity);
            const std::string at = hz(f) + ", " + name[k] + " (" + std::to_string(int(rate)) + " Hz)";
            // 2 oct down from below 100 Hz (17 to 25 Hz) runs into 0 Hz: 1.5 dB there.
            const double tolerance = k == down_2 && f < 100 ? 1.5 : 1.0;
            if (pitch) require(std::fabs(s.cents) <= 1, at + ": pitch within 1 cent, " + std::to_string(s.cents));
            require(std::fabs(s.level) <= tolerance, at + ": level within " + std::to_string(tolerance) + " dB, " + std::to_string(s.level));
            require(s.purity >= 30, at + ": purity at least 30 dB, " + std::to_string(s.purity));
            std::snprintf(line[0] + std::strlen(line[0]), 20, "%+7.2f", s.cents);
            std::snprintf(line[1] + std::strlen(line[1]), 20, "%+7.2f", s.level);
            std::snprintf(line[2] + std::strlen(line[2]), 20, "%6.0f", s.purity);
        }
        if (print) std::printf("  %9s  %-30s  %-30s  %-26s\n", hz(f).c_str(), line[0], line[1], line[2]);
    }
    std::printf("  %g Hz, worst per voice (2 down, 1 down, 1 up, 2 up): pitch %.2f %.2f %.2f %.2f cents; level %.2f %.2f %.2f %.2f dB; purity %.0f %.0f %.0f %.0f dB\n",
                rate, worst[0][0], worst[0][1], worst[0][2], worst[0][3], worst[1][0], worst[1][1], worst[1][2], worst[1][3], worst[2][0], worst[2][1], worst[2][2],
                worst[2][3]);
}

// The expected partials of notes (each with `harmonics`, amplitude 1/h) in voice k.
std::vector<double> expected(const std::vector<double>& notes, int harmonics, int k, double rate) {
    std::vector<double> e;
    for (const double f : notes)
        for (int h = 1; h <= harmonics; ++h) {
            const double p = h * f, g = p * ratio[k];
            if (p > 6000 || g > 0.4 * rate) continue;
            bool near = false;
            for (const double x : e) near = near || std::fabs(x - g) < 0.3;
            if (!near) e.push_back(g);
        }
    return e;
}

// Steady notes and chords, eight harmonics each: purity of every voice.
void notes(double rate, double (&chord)[2][voices]) {
    const struct { const char* what; std::vector<double> notes; int harmonics; } cases[] = {
        {"82.4 Hz, 8 harmonics", {82.41}, 8},
        {"110 Hz, 8 harmonics", {110}, 8},
        {"196 Hz, 8 harmonics", {196}, 8},
        {"329.6 Hz, 8 harmonics", {329.63}, 8},
        {"open E major", {82.41, 123.47, 164.81, 207.65, 246.94, 329.63}, 8},
        {"barre A", {110, 164.81, 220, 277.18, 329.63, 440}, 8},
    };
    std::printf("  purity in dB, each voice alone (2 oct down, 1 oct down, 1 oct up, 2 oct up)\n");
    int c = 0;
    for (const auto& t : cases) {
        double p[voices];
        for (int k = 0; k < voices; ++k) {
            Voices v(rate);
            const int settle = int(rate / 2);
            const auto y = run(v, alone(k), [&](int n) {
                double x = 0;
                for (const double f : t.notes) for (int h = 1; h <= t.harmonics; ++h) x += (t.notes.size() > 1 ? 0.08 : 0.3) / h * std::sin(2 * pi * h * f * n / rate + h * f);
                return float(x);
            }, settle + int(rate), settle);
            p[k] = purity(y, rate, expected(t.notes, t.harmonics, k, rate));
        }
        std::printf("  %-22s %6.1f %6.1f %6.1f %6.1f\n", t.what, p[0], p[1], p[2], p[3]);
        if (t.notes.size() > 1) for (int k = 0; k < voices; ++k) chord[c][k] = p[k];
        if (t.notes.size() > 1) ++c;
    }
}

// A sine swept slowly (exponentially, 10 s) from 70 Hz to 2 kHz: no voice may
// step further from one sample to the next than a sine at its pitch and the
// input's amplitude could, by more than half again.
void sweep(double rate) {
    const double from = 70, to = 2000, seconds = 10, rise = std::log(to / from) / (seconds * rate);
    std::printf("  sweep 70 Hz to 2 kHz, biggest step against a sine's own:");
    for (int k = 0; k < voices; ++k) {
        Voices v(rate);
        const int hold = int(rate / 2), frames = hold + int(seconds * rate);
        double phase = 0, worst = 0, last = 0;
        for (int n = 0; n < frames; ++n) {
            const double f = from * std::exp(rise * std::max(0, n - hold));
            phase += 2 * pi * f / rate;
            const double y = v.step(float(0.5 * std::sin(phase)), alone(k));
            const double g = std::min(f * ratio[k], 0.4 * rate);
            if (n > hold + v.latency()) worst = std::max(worst, std::fabs(y - last) / (0.5 * 2 * pi * g / rate));
            last = y;
        }
        std::printf("  %s %.2f", name[k], worst);
        require(worst <= 1.5, std::string(name[k]) + ": no step in the sweep larger than its sine could make, by half again");
    }
    std::printf("\n");
}

// The voices' envelope: RMS over 5 ms.
std::vector<double> envelope(const std::vector<float>& y, double rate) {
    const int w = int(0.005 * rate);
    std::vector<double> e(y.size(), 0);
    double sum = 0;
    for (size_t n = 0; n < y.size(); ++n) {
        sum += double(y[n]) * y[n];
        if (n >= size_t(w)) sum -= double(y[n - w]) * y[n - w];
        e[n] = std::sqrt(std::max(sum, 0.0) / w);
    }
    return e;
}

// Plucked tones (six harmonics, a sharp start, a 1 s decay): the delay from
// the start to the voice's envelope reaching half its level 0.3 s later (the
// decay taken out).
void delays(double rate, int latency) {
    std::printf("  delay to half level, ms (2 oct down, 1 oct down, 1 oct up, 2 oct up); the synthesis window is %.1f ms\n", 1000.0 * latency / rate);
    for (const double f : {82.41, 110.0, 164.8, 330.0, 1000.0}) {
        std::printf("  %9s plucked", hz(f).c_str());
        for (int k = 0; k < voices; ++k) {
            Voices v(rate);
            const int start = int(0.2 * rate), frames = start + int(0.6 * rate);
            const auto y = run(v, alone(k), [&](int n) {
                if (n < start) return 0.0f;
                const double t = double(n - start) / rate;
                double x = 0;
                for (int h = 1; h <= 6; ++h) x += 0.3 / h * std::sin(2 * pi * h * f * t);
                return float(x * std::exp(-t));
            }, frames, 0);
            const auto e = envelope(y, rate);
            const auto at = [&](int n) { return e[n] * std::exp(std::max(0.0, double(n - start - latency)) / rate); };
            const double settled = at(start + int(0.3 * rate));
            int n = start;
            while (n < frames && at(n) < 0.5 * settled) ++n;
            const double ms = 1000.0 * (n - start) / rate;
            std::printf("  %5.1f", ms);
            require(ms <= 1000.0 * latency / rate + 6, hz(f) + ", " + name[k] + ": delay within the synthesis window and 6 ms");
        }
        std::printf("\n");
    }
}

// Amplitude of a sine at g over a window of y.
double amplitude(const std::vector<float>& y, double rate, double g, size_t at, size_t length) {
    double r;
    std::vector<std::complex<double>> s;
    fit(std::vector<float>(y.begin() + at, y.begin() + at + length), rate, {g}, r, &s);
    return std::abs(s[0]);
}

// Swell: a note's voice reaches full level in the Swell time, not sooner; a
// second note swells on its own, and the first does not dip.
void swell(double rate) {
    const float time = 0.5f;
    Settings s = alone(down_1);
    s.swell = time;
    Voices v(rate);
    const double f1 = 110, f2 = 330;
    const int second = int(1.5 * rate), frames = int(3 * rate), window = int(0.02 * rate);
    const auto y = run(v, s, [&](int n) {
        double x = 0.5 * std::sin(2 * pi * f1 * n / rate);
        if (n >= second) x += 0.5 * std::sin(2 * pi * f2 * n / rate);
        return float(x);
    }, frames, 0);
    const double full = amplitude(y, rate, f1 / 2, second - window, window);
    require(std::fabs(db(full / 0.5)) < 1.5, "the first note at full level before the second");
    size_t reach = 0;
    while (amplitude(y, rate, f1 / 2, reach, window) < 0.9 * full) reach += window / 4;
    const double t = double(reach + window) / rate;
    std::printf("  Swell 0.5 s: a note at half of full scale reaches 90 %% of its level in %.2f s", t);
    require(t > 0.6 * time && t <= 1.1 * time, "a note reaches its level in the Swell time, not sooner");
    double dip = 1e9;
    for (size_t at = second; at + window < size_t(frames); at += window) dip = std::min(dip, amplitude(y, rate, f1 / 2, at, window));
    const double early = amplitude(y, rate, f2 / 2, second + int(0.1 * time * rate), window);
    const double late = amplitude(y, rate, f2 / 2, frames - window, window);
    std::printf("; the first note's least after the second comes %.1f dB; the second at 0.1 of the Swell time %.1f dB, at the end %.1f dB\n", db(dip / full),
                db(early / 0.5), db(late / 0.5));
    require(dip > 0.8 * full, "the first note does not swell again when the second comes");
    require(early < 0.25 * 0.5, "the second note swells on its own");
    require(late > 0.8 * 0.5, "the second note reaches its level");
}

// Tone: the response of 1 oct up at its cutoff and around it, at 200 Hz,
// 1 kHz and 16 kHz (open), against the same with Tone open.
void tone(double rate) {
    std::printf("  Tone, response of 1 oct up, dB:");
    for (const double cutoff : {200.0, 1000.0, 16000.0}) {
        const std::vector<double> points = cutoff == 16000 ? std::vector<double>{150, 1000, 8000} : std::vector<double>{std::max(cutoff / 2, 150.0), cutoff, 4 * cutoff};
        for (const double at : points) {
            Settings s = alone(up_1), open = s;
            s.tone = float(cutoff);
            const int settle = int(rate / 2);
            const auto input = [&](int n) { return float(0.5 * std::sin(2 * pi * at / 2 * n / rate)); };
            Voices a(rate), b(rate);
            const auto y = run(a, s, input, settle + int(rate / 2), settle);
            const auto z = run(b, open, input, settle + int(rate / 2), settle);
            double r;
            std::vector<std::complex<double>> p, q;
            fit(y, rate, {at}, r, &p);
            fit(z, rate, {at}, r, &q);
            const double response = db(std::abs(p[0]) / std::abs(q[0]));
            std::printf("  %g at %g: %+.1f", cutoff, at, response);
            const double butterworth = cutoff == 16000 ? 0 : -10 * std::log10(1 + std::pow(at / cutoff, 4));
            require(std::fabs(response - butterworth) < 0.5, "Tone at " + std::to_string(int(cutoff)) + " Hz: a 2-pole low-pass, open at 16 kHz");
        }
    }
    std::printf("\n");
}

// Dry: the input, exactly and with no delay; a level moved at once does not
// click.
void dry(double rate) {
    Settings s;
    s.down_1 = 0;
    Voices v(rate);
    uint32_t seed = 7;
    for (int n = 0; n < int(rate); ++n) {
        seed = seed * 1664525u + 1013904223u;
        const float x = float(int32_t(seed)) / 2147483648.0f;
        require(v.step(x, s) == x, "Dry 1, voices 0: Out is In, sample for sample");
    }
    Voices w(rate);
    s.dry = 0;
    double last = 0, worst = 0;
    for (int n = 0; n < int(rate); ++n) {
        if (n == int(rate / 2)) s.down_1 = 1;
        const double o = w.step(float(0.5 * std::sin(2 * pi * 200 * n / rate)), s);
        if (n > 0) worst = std::max(worst, std::fabs(o - last));
        last = o;
    }
    require(worst <= 0.5 * 2 * pi * 100 / rate * 1.5, "a level moved at once does not click");
}

// Controls out of range or not finite, and an input that is not.
void extremes(double rate) {
    const float inf = INFINITY, nan = NAN;
    const Settings wild[] = {{nan, inf, -inf, 1e9f, -1e9f, nan, inf}, {-5, 5, 5, 5, 5, 1e9f, -1}, {inf, nan, nan, nan, nan, -inf, nan}};
    for (const Settings& s : wild) {
        Voices a(rate), b(rate);
        const Settings c = Voices::clamped(s);
        for (const float v : {c.dry, c.down_2, c.down_1, c.up_1, c.up_2}) require(v >= 0 && v <= 1, "levels within 0 to 1");
        require(c.tone >= 200 && c.tone <= 16000 && c.swell >= 0 && c.swell <= 2, "Tone and Swell within range");
        for (int n = 0; n < int(rate / 4); ++n) {
            const float x = float(0.5 * std::sin(2 * pi * 196 * n / rate));
            const float o = a.step(x, s);
            require(std::isfinite(o) && o == b.step(x, c), "extreme controls act as their clamped values");
        }
    }
    Voices v(rate);
    const auto y = run(v, alone(down_1), [&](int n) { return n >= 1000 && n < 1010 ? (n % 2 ? nan : inf) : float(0.5 * std::sin(2 * pi * 196 * n / rate)); },
                       int(rate), int(rate / 2));
    double r;
    std::vector<std::complex<double>> a;
    fit(y, rate, {98}, r, &a);
    require(std::fabs(db(std::abs(a[0]) / 0.5)) < 1.5, "a non-finite input does not leave the voices stuck");
}

// Seconds of computing per second of audio: a strummed chord at 48 kHz, every
// voice on; and the worst single hop.
void cost() {
    const double rate = 48000;
    Settings s;
    s.down_2 = s.up_1 = s.up_2 = 0.5f;
    s.tone = 4000;
    std::vector<float> x(size_t(rate) * 4);
    for (size_t n = 0; n < x.size(); ++n)
        for (const double f : {82.41, 123.47, 164.81, 207.65, 246.94, 329.63})
            for (int h = 1; h <= 6; ++h) x[n] += float(0.05 / h * std::sin(2 * pi * h * f * double(n) / rate) * std::exp(-double(n % 48000) / 20000));
    double best = 1e9, hop = 0;
    float sink = 0;
    for (int k = 0; k < 3; ++k) {
        Voices v(rate);
        const auto t0 = std::chrono::steady_clock::now();
        for (const float sample : x) sink += v.step(sample, s);
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
        for (int n = 0; n < 2000; ++n) {
            const auto a = std::chrono::steady_clock::now();
            sink += v.step(x[size_t(n)], s);
            hop = std::max(hop, std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count());
        }
    }
    std::printf("  CPU on this PC: %.2f %% of one core at 48 kHz (%.1f ms per second of audio); the worst sample (a hop's work) %.0f us%s\n", 100 * best / 4,
                1000 * best / 4, hop * 1e6, sink == 1234.5f ? " " : "");
}
}

int main() {
    try {
        Voices probe(48000);
        std::printf("  48 kHz: analysis %d samples, synthesis (delay) %d samples\n", probe.fft_size(), probe.latency());
        sines(48000, true);
        for (const double rate : {24000.0, 44100.0, 96000.0}) sines(rate, false);
        double chord[2][voices];
        notes(48000, chord);
        // The baseline recorded in docs/plan.md (28 October 2026): kept from
        // getting worse by more than 1 dB.
        const double baseline[2][voices] = {{9.4, 7.7, 4.8, 3.5}, {11.2, 10.3, 7.1, 3.4}};
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < voices; ++k)
                require(chord[c][k] >= baseline[c][k] - 1, std::string(c ? "barre A" : "open E major") + ", " + name[k] + ": chord purity no worse than its baseline");
        sweep(48000);
        delays(48000, probe.latency());
        swell(48000);
        tone(48000);
        dry(48000);
        extremes(48000);
        cost();
        std::puts("PASS test_voices: pitch, level, purity, notes, chords, sweep, delay, Swell, Tone, Dry and extremes");
    } catch (const std::exception& e) {
        std::printf("FAIL test_voices: %s\n", e.what());
        return 1;
    }
}
