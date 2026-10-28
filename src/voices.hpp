// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
#pragma once
#include "fft.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

// The four voices, Swell, Tone and the output (docs/design.md): a phase
// vocoder with Laroche and Dolson's pitch shifting and identity phase locking.
// Each hop, the newest samples are windowed and transformed; the spectrum's
// peaks are found; each peak's exact frequency comes from its phase turn
// since the last hop; each peak, with the bins around it, is moved to r times
// its frequency for each voice, its phases turned so that it keeps turning at
// r times its frequency. All the voices go into one spectrum, so one FFT in
// and one out per hop, whatever the number of voices.
//
// Per sample, so a host may run a period in slices: the hop's work falls on
// the sample that completes it. Memory is allocated in the constructor only.
namespace octant {

struct Settings {
    float dry = 1, down_2 = 0, down_1 = 0.6f, up_1 = 0, up_2 = 0;
    float tone = 16000;  // Hz
    float swell = 0;     // s
};

enum Voice { down_2 = 0, down_1 = 1, up_1 = 2, up_2 = 3, voices = 4 };
constexpr float ratio[voices] = {0.25f, 0.5f, 2.0f, 4.0f};

// State below this is flushed to zero (float: well above denormals, -300 dB).
constexpr float flush = 1e-15f;
inline float flushed(float v) { return std::fabs(v) < flush ? 0.0f : v; }

class Voices {
public:
    // The analysis window: about 85 ms, a power of 2 (4096 at 44.1 and 48 kHz;
    // the FFT is twice that).
    static int analysis(double rate) {
        int n = 512;
        while (n < 0.085 * rate / std::sqrt(2.0)) n *= 2;
        return n;
    }
    // The synthesis window, and so the voices' delay: about 16 ms, a whole
    // number of hops of a quarter of it (768 samples at 48 kHz).
    static int synthesis(double rate) { return 4 * std::max(16, int(std::lround(0.016 * rate / 4))); }

    explicit Voices(double rate)
        : rate_(float(rate)), n_(analysis(rate)), size_(pad * n_), out_n_(synthesis(rate)), hop_(out_n_ / 4), bins_(size_ / 2 + 1), fft_(size_), window_(n_),
          synthesis_(n_, 0.0f), history_(n_), frame_(size_), pending_(out_n_), ready_(hop_), re_(bins_), im_(bins_), last_re_(bins_), last_im_(bins_), power_(bins_), out_re_(bins_),
          out_im_(bins_), peaks_(bins_), last_peaks_(bins_), lo_(bins_), hi_(bins_), swell_(bins_), last_swell_(bins_) {
        for (int v = 0; v < voices; ++v) {
            phase_[v].assign(bins_, 0.0f);
            last_phase_[v].assign(bins_, 0.0f);
            shift_[v].assign(bins_, 0);
            last_shift_[v].assign(bins_, 0);
        }
        // Asymmetric windows for a short delay (as in Mauler and Martin's
        // low-delay STFT): the analysis window rises over n - out/2 samples and
        // falls over the last out/2, so the newest samples weigh most; the
        // synthesis window covers only the last `out` samples, and the two
        // multiply to a Hann window of length `out`.
        const double pi = 3.14159265358979323846;
        const auto hann = [&](double i, double length) { return 0.5 - 0.5 * std::cos(2 * pi * i / length); };
        const int rise = n_ - out_n_ / 2;
        double sum = 0;
        for (int i = 0; i < n_; ++i) {
            window_[i] = float(i < rise ? std::sqrt(hann(i, 2.0 * rise)) : std::sqrt(hann(i - rise + out_n_ / 2, out_n_)));
            sum += window_[i];
        }
        double overlap = 0;
        for (int i = 0; i < out_n_; ++i) {
            const double a = window_[n_ - out_n_ + i];
            synthesis_[n_ - out_n_ + i] = a > 1e-9 ? float(hann(i, out_n_) / a) : 0.0f;
            if (i % hop_ == 0) overlap += hann(i, out_n_);
        }
        gain_ = float(1 / overlap);
        // The window's own phase at an offset from a bin, for -2 to 2 bins,
        // with the frame turned as frame() turns it: a partial between two bins
        // shows in each with its phase plus this.
        const int centre = n_ - out_n_ / 2;
        for (int t = 0; t <= 2 * shape_steps; ++t) {
            const double offset = 2.0 * (t - shape_steps) / shape_steps;
            double sr = 0, si = 0;
            for (int i = 0; i < n_; ++i) {
                const double a = -2 * pi * offset * double(i - centre) / size_;
                sr += window_[i] * std::cos(a);
                si += window_[i] * std::sin(a);
            }
            shape_[t] = float(std::atan2(si, sr));
        }
        // A sine of amplitude A peaks at A/2 times the window's sum: below
        // 1e-5 (-100 dBFS) nothing is shifted.
        silence_ = float(std::pow(1e-5 * sum / 2, 2));
        low_ = std::max(reach, int(40.0 * size_ / rate));
        high_ = std::min(bins_ - 1 - reach, int(6000.0 * size_ / rate));
        smooth_ = float(1 - std::exp(-1 / (0.01 * rate)));
        reset();
    }

    // Samples of delay from In to the voices.
    int latency() const { return out_n_; }
    int fft_size() const { return n_; }

    void reset() {
        std::fill(history_.begin(), history_.end(), 0.0f);
        std::fill(pending_.begin(), pending_.end(), 0.0f);
        std::fill(ready_.begin(), ready_.end(), 0.0f);
        std::fill(last_re_.begin(), last_re_.end(), 0.0f);
        std::fill(last_im_.begin(), last_im_.end(), 0.0f);
        fill_ = 0;
        count_ = last_count_ = 0;
        ic1_ = ic2_ = 0;
        fresh_ = true;
        swell_for_ = tone_for_ = -1;
    }

    // One sample in, one out.
    float step(float x, const Settings& raw) {
        const Settings s = clamped(raw);
        if (!std::isfinite(x)) x = 0;
        follow(s);
        const float wet = ready_[fill_];
        history_[n_ - hop_ + fill_] = x;
        if (++fill_ == hop_) {
            fill_ = 0;
            frame();
        }
        const float out = dry_ * x + tone(wet);
        if (!std::isfinite(out)) {
            reset();
            return dry_ * x;
        }
        return out;
    }

    // Controls brought within their ranges; non-finite ones take the default.
    static Settings clamped(const Settings& s) {
        const Settings d;
        const auto within = [](float v, float fallback, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; };
        Settings c;
        c.dry = within(s.dry, d.dry, 0, 1);
        c.down_2 = within(s.down_2, d.down_2, 0, 1);
        c.down_1 = within(s.down_1, d.down_1, 0, 1);
        c.up_1 = within(s.up_1, d.up_1, 0, 1);
        c.up_2 = within(s.up_2, d.up_2, 0, 1);
        c.tone = within(s.tone, d.tone, 200, 16000);
        c.swell = within(s.swell, d.swell, 0, 2);
        return c;
    }

    // For the tests: the peaks found in the last hop.
    int peaks() const { return count_; }

private:
    // The window's phase at `offset` bins (clamped to 2), interpolated.
    float shape(float offset) const {
        const float t = (std::clamp(offset, -2.0f, 2.0f) + 2.0f) * shape_steps / 2;
        const int i = std::min(int(t), 2 * shape_steps - 1);
        return shape_[i] + (t - i) * (shape_[i + 1] - shape_[i]);
    }

    static float wrap(float p) {
        const float two_pi = 6.28318530718f;
        return p - two_pi * std::floor((p + 3.14159265359f) / two_pi);
    }

    // Levels smoothed over about 10 ms; Tone in octaves; Swell's rise per hop.
    void follow(const Settings& s) {
        const float target[voices] = {s.down_2, s.down_1, s.up_1, s.up_2};
        if (s.tone != tone_for_) {
            tone_for_ = s.tone;
            octave_for_ = std::log2(s.tone);
        }
        if (fresh_) {
            for (int k = 0; k < voices; ++k) level_[k] = target[k];
            dry_ = s.dry;
            octave_ = octave_for_;
            set_tone();
            fresh_ = false;
        }
        for (int k = 0; k < voices; ++k) level_[k] += smooth_ * (target[k] - level_[k]);
        dry_ += smooth_ * (s.dry - dry_);
        if (octave_ != octave_for_) {
            octave_ += smooth_ * (octave_for_ - octave_);
            if (std::fabs(octave_for_ - octave_) < 1e-4f) octave_ = octave_for_;
            set_tone();
        }
        if (s.swell != swell_for_) {
            swell_for_ = s.swell;
            // From -60 dB to full level in the Swell time, at a steady rate in dB.
            rise_ = s.swell > 0 ? std::pow(1000.0f, hop_ / (s.swell * rate_)) : 0;
        }
    }

    void frame() {
        // Zero phase at the centre of the part resynthesised: the frame is
        // turned so that sample is at index 0, and every phase is measured
        // (and set) there. Moving a peak by whole bins then errs by at most
        // half a (padded) bin over half the synthesis window.
        const int centre = n_ - out_n_ / 2;
        std::fill(frame_.begin(), frame_.end(), 0.0f);
        for (int i = 0; i < n_; ++i) frame_[(i - centre + size_) % size_] = history_[i] * window_[i];
        std::copy(history_.begin() + hop_, history_.end(), history_.begin());
        fft_.forward(frame_.data(), re_.data(), im_.data());
        float top = 0;
        for (int k = 0; k < bins_; ++k) {
            power_[k] = re_[k] * re_[k] + im_[k] * im_[k];
            top = std::max(top, power_[k]);
        }
        // Peaks: local maxima over two unpadded bins either side, within 70 dB of the
        // hop's loudest and above -100 dBFS, from 40 Hz to 6 kHz.
        const float floor = std::max(top * 1e-7f, silence_);
        int count = 0;
        for (int k = low_; k <= high_; ++k) {
            const float p = power_[k];
            bool peak = p > floor;
            for (int d = 1; peak && d <= reach; ++d) peak = p > power_[k - d] && p >= power_[k + d];
            if (peak) peaks_[count++] = k;
        }
        // Regions: each peak's, from the lowest bin between it and the peak
        // below to the lowest between it and the peak above.
        for (int p = 0; p < count; ++p) {
            lo_[p] = p == 0 ? std::max(1, peaks_[p] - 3 * reach) : hi_[p - 1] + 1;
            int hi = peaks_[p];
            if (p + 1 < count) {
                for (int k = peaks_[p]; k < peaks_[p + 1]; ++k)
                    if (power_[k] < power_[hi]) hi = k;
            } else {
                hi = std::min(bins_ - 2, peaks_[p] + 3 * reach);
            }
            hi_[p] = hi;
        }
        std::fill(out_re_.begin(), out_re_.end(), 0.0f);
        std::fill(out_im_.begin(), out_im_.end(), 0.0f);
        const float expected = 6.28318530718f * hop_ / size_;  // a bin's turn over a hop
        const float start = rise_ > 0 ? 1e-3f : 1.0f;
        int j = 0;
        for (int p = 0; p < count; ++p) {
            const int k = peaks_[p];
            // The peak's frequency, in bins, from its phase turn over a hop.
            const float turn = std::atan2(im_[k] * last_re_[k] - re_[k] * last_im_[k], re_[k] * last_re_[k] + im_[k] * last_im_[k]);
            const float f = k + wrap(turn - expected * k) / expected;
            // The partial's own phase: the bin's, less the window's at the offset.
            const float phase = std::atan2(im_[k], re_[k]) - shape(k - f);
            // The last hop's peak nearest this one (both lists rise), if it is
            // within two unpadded bins: the same partial, carried on.
            while (j + 1 < last_count_ && std::abs(last_peaks_[j + 1] - k) <= std::abs(last_peaks_[j] - k)) ++j;
            const bool carried = last_count_ > 0 && std::abs(last_peaks_[j] - k) <= reach;
            const int before = carried ? last_peaks_[j] : 0;
            // Swell: a new partial starts at -60 dB, and a partial carried on
            // rises from where it was, no faster than Swell allows.
            const float g = carried ? (rise_ > 0 ? std::min(1.0f, last_swell_[before] * rise_) : 1.0f) : start;
            swell_[k] = g;
            for (int v = 0; v < voices; ++v) {
                const float r = ratio[v];
                // The output phase: the carried partial's, turned on by r times
                // its frequency over a hop; a new one starts from r times its own.
                const float out = carried ? wrap(last_phase_[v][before] + r * f * expected) : wrap(r * phase);
                phase_[v][k] = out;
                const float target = r * f;
                const float level = level_[v] * g;
                // Up voices skip what would land above 0.4 of the rate.
                if (level <= 0 || target > 0.4f * size_ || target < 0.5f) continue;
                // The shift in whole bins: from the partial's frequency, not its
                // bin, and a carried partial keeps its last one while that is
                // within 0.75 of a bin, so it does not flip from hop to hop.
                const float want = target - f;
                const int last = carried ? last_shift_[v][before] : 0;
                const int shift = carried && std::fabs(want - last) < 0.75f ? last : int(std::lround(want));
                shift_[v][k] = shift;
                const float cr = level * std::cos(out - phase), ci = level * std::sin(out - phase);
                const int from = std::max(lo_[p], 1 - shift), to = std::min(hi_[p], bins_ - 2 - shift);
                for (int b = from; b <= to; ++b) {
                    out_re_[b + shift] += re_[b] * cr - im_[b] * ci;
                    out_im_[b + shift] += re_[b] * ci + im_[b] * cr;
                }
            }
        }
        std::swap(re_, last_re_);
        std::swap(im_, last_im_);
        std::swap(peaks_, last_peaks_);
        std::swap(swell_, last_swell_);
        for (int v = 0; v < voices; ++v) {
            std::swap(phase_[v], last_phase_[v]);
            std::swap(shift_[v], last_shift_[v]);
        }
        last_count_ = count_ = count;
        // Back to time, turned back, and overlap-added over the last `out`
        // samples; the first hop of the sum is final.
        fft_.inverse(out_re_.data(), out_im_.data(), frame_.data());
        for (int i = 0; i < out_n_; ++i) {
            const int at = n_ - out_n_ + i;
            pending_[i] = flushed(pending_[i] + frame_[(at - centre + size_) % size_] * synthesis_[at] * gain_);
        }
        std::copy(pending_.begin(), pending_.begin() + hop_, ready_.begin());
        std::copy(pending_.begin() + hop_, pending_.end(), pending_.begin());
        std::fill(pending_.end() - hop_, pending_.end(), 0.0f);
    }

    // Tone: a 2-pole low-pass (state variable, Butterworth), open at 16 kHz:
    // from 8 kHz up it fades to the voices unfiltered.
    void set_tone() {
        const float f = std::min(std::exp2(octave_), 0.45f * rate_);
        const float g = std::tan(3.14159265f * f / rate_), k = 1.41421356f;
        a1_ = 1 / (1 + g * (g + k));
        a2_ = g * a1_;
        a3_ = g * a2_;
        open_ = std::clamp(octave_ - 13.0f, 0.0f, 1.0f);  // log2(8000) = 12.97
        if (octave_ >= 13.9657843f) open_ = 1;           // log2(16000)
    }
    float tone(float x) {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_ = flushed(2 * v1 - ic1_);
        ic2_ = flushed(2 * v2 - ic2_);
        return v2 + open_ * (x - v2);
    }

    float rate_;
    // The FFT is twice the analysis window (zero-padded), so bins are half as
    // far apart and moving a peak by whole bins errs by at most a quarter of
    // an unpadded bin. `reach`: two unpadded bins.
    static constexpr int pad = 2, reach = 2 * pad;
    int n_, size_, out_n_, hop_, bins_;
    RealFft fft_;
    std::vector<float> window_, synthesis_, history_, frame_, pending_, ready_;
    std::vector<float> re_, im_, last_re_, last_im_, power_, out_re_, out_im_;
    std::vector<int> peaks_, last_peaks_, lo_, hi_, shift_[voices], last_shift_[voices];
    std::vector<float> swell_, last_swell_, phase_[voices], last_phase_[voices];
    static constexpr int shape_steps = 128;
    float shape_[2 * shape_steps + 1]{};
    float gain_ = 1, silence_ = 0;
    int low_ = 2, high_ = 2, fill_ = 0, count_ = 0, last_count_ = 0;
    float level_[voices]{}, dry_ = 1, smooth_ = 0;
    float octave_ = 14, octave_for_ = 14, tone_for_ = -1, a1_ = 0, a2_ = 0, a3_ = 0, open_ = 1, ic1_ = 0, ic2_ = 0;
    float rise_ = 0, swell_for_ = -1;
    bool fresh_ = true;
};

}
