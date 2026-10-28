// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Alejandro Bustos
#pragma once
#include <cmath>
#include <cstring>
#include <vector>

// An FFT for real signals of n samples (a power of 2, at least 16): a complex
// FFT of n/2 points on the even and odd samples, then one pass to separate
// them. Real and imaginary parts are kept in separate arrays, and the
// butterflies of the later stages run four at a time with GCC's vector types
// (SSE on a PC, NEON on the Pi), whatever the optimisation flags.
//
// Memory is allocated in the constructor only.
namespace octant {

class RealFft {
public:
    explicit RealFft(int n) : n_(n), m_(n / 2), order_(m_), wr_(m_), wi_(m_), sr_(n / 2 + 1), si_(n / 2 + 1), re_(m_), im_(m_) {
        const double pi = 3.14159265358979323846;
        int bits = 0;
        while ((1 << bits) < m_) ++bits;
        for (int i = 0; i < m_; ++i) {
            int r = 0;
            for (int b = 0; b < bits; ++b) r |= ((i >> b) & 1) << (bits - 1 - b);
            order_[i] = r;
        }
        // Each stage's twiddles in a row of their own: stage `half` uses
        // wr_[half + j], j < half (e^(-2 pi i j / (2 half))).
        for (int half = 1; half < m_; half *= 2)
            for (int j = 0; j < half; ++j) {
                wr_[half + j] = float(std::cos(-pi * j / half));
                wi_[half + j] = float(std::sin(-pi * j / half));
            }
        // The separating pass: e^(-2 pi i k / n), k <= n/2.
        for (int k = 0; k <= m_; ++k) {
            sr_[k] = float(std::cos(-2 * pi * k / n));
            si_[k] = float(std::sin(-2 * pi * k / n));
        }
    }

    int size() const { return n_; }

    // x: n samples. Out: bins 0 to n/2 (n/2 + 1 of them), unscaled.
    void forward(const float* x, float* out_re, float* out_im) {
        for (int i = 0; i < m_; ++i) {
            re_[order_[i]] = x[2 * i];
            im_[order_[i]] = x[2 * i + 1];
        }
        transform();
        // Z[k] holds E[k] + i O[k]; X[k] = E[k] + w^k O[k].
        for (int k = 0; k <= m_; ++k) {
            const int a = k % m_, b = (m_ - k) % m_;
            const float zr = re_[a], zi = im_[a], cr = re_[b], ci = -im_[b];  // Z[k], conj(Z[m - k])
            const float er = 0.5f * (zr + cr), ei = 0.5f * (zi + ci);
            // O = (Z - conj(Z')) / 2i
            const float dr = 0.5f * (zr - cr), di = 0.5f * (zi - ci);
            const float orr = di, oi = -dr;
            out_re[k] = er + sr_[k] * orr - si_[k] * oi;
            out_im[k] = ei + sr_[k] * oi + si_[k] * orr;
        }
    }

    // Bins 0 to n/2 in; n samples out, scaled so that inverse(forward(x)) is x.
    void inverse(const float* in_re, const float* in_im, float* x) {
        for (int k = 0; k < m_; ++k) {
            const float xr = in_re[k], xi = in_im[k], cr = in_re[m_ - k], ci = -in_im[m_ - k];  // X[k], conj(X[m - k])
            const float er = 0.5f * (xr + cr), ei = 0.5f * (xi + ci);
            // O = (X - conj(X')) / 2 * conj(w^k)
            const float dr = 0.5f * (xr - cr), di = 0.5f * (xi - ci);
            const float orr = dr * sr_[k] + di * si_[k], oi = di * sr_[k] - dr * si_[k];
            // Z = E + i O, conjugated: the forward transform then inverts.
            re_[order_[k]] = er - oi;
            im_[order_[k]] = -(ei + orr);
        }
        transform();
        const float scale = 1.0f / m_;
        for (int i = 0; i < m_; ++i) {
            x[2 * i] = re_[i] * scale;
            x[2 * i + 1] = -im_[i] * scale;
        }
    }

private:
    typedef float v4 __attribute__((vector_size(16), aligned(4), may_alias));

    // In place on re_, im_ (already in bit-reversed order): a radix-2 FFT.
    void transform() {
        float* re = re_.data();
        float* im = im_.data();
        for (int half = 1; half < m_; half *= 2) {
            const float* wr = wr_.data() + half;
            const float* wi = wi_.data() + half;
            for (int start = 0; start < m_; start += 2 * half) {
                float* ar = re + start;
                float* ai = im + start;
                float* br = ar + half;
                float* bi = ai + half;
                if (half >= 4) {
                    for (int j = 0; j < half; j += 4) {
                        v4 xr, xi, yr, yi, cr, ci;
                        std::memcpy(&xr, ar + j, 16);
                        std::memcpy(&xi, ai + j, 16);
                        std::memcpy(&yr, br + j, 16);
                        std::memcpy(&yi, bi + j, 16);
                        std::memcpy(&cr, wr + j, 16);
                        std::memcpy(&ci, wi + j, 16);
                        const v4 tr = yr * cr - yi * ci, ti = yr * ci + yi * cr;
                        const v4 ur = xr + tr, ui = xi + ti, lr = xr - tr, li = xi - ti;
                        std::memcpy(ar + j, &ur, 16);
                        std::memcpy(ai + j, &ui, 16);
                        std::memcpy(br + j, &lr, 16);
                        std::memcpy(bi + j, &li, 16);
                    }
                } else {
                    for (int j = 0; j < half; ++j) {
                        const float tr = br[j] * wr[j] - bi[j] * wi[j], ti = br[j] * wi[j] + bi[j] * wr[j];
                        br[j] = ar[j] - tr;
                        bi[j] = ai[j] - ti;
                        ar[j] += tr;
                        ai[j] += ti;
                    }
                }
            }
        }
    }

    int n_, m_;
    std::vector<int> order_;
    std::vector<float> wr_, wi_, sr_, si_, re_, im_;
};

}
