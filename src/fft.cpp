#include "fft.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sp {

bool is_power_of_two(std::size_t n) { return n != 0 && (n & (n - 1)) == 0; }

void fft_inplace(std::vector<cplx>& a) {
    const std::size_t n = a.size();
    if (!is_power_of_two(n)) throw std::invalid_argument("FFT size must be a power of two");

    // bit-reversal permutation so the butterflies can run in place
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * sp::kPi / static_cast<double>(len);
        const cplx w_len(std::cos(angle), std::sin(angle));
        const std::size_t half = len / 2;
        for (std::size_t start = 0; start < n; start += len) {
            cplx w(1.0, 0.0);
            for (std::size_t k = 0; k < half; ++k) {
                const cplx u = a[start + k];
                const cplx v = a[start + k + half] * w;
                a[start + k] = u + v;
                a[start + k + half] = u - v;
                w *= w_len;
            }
        }
    }
}

std::vector<cplx> naive_dft(const std::vector<cplx>& in) {
    const std::size_t n = in.size();
    std::vector<cplx> out(n);
    for (std::size_t k = 0; k < n; ++k) {
        cplx sum(0.0, 0.0);
        for (std::size_t t = 0; t < n; ++t) {
            const double angle = -2.0 * sp::kPi * static_cast<double>(k * t) / static_cast<double>(n);
            sum += in[t] * cplx(std::cos(angle), std::sin(angle));
        }
        out[k] = sum;
    }
    return out;
}

SpectrumAnalyzer::SpectrumAnalyzer(std::size_t n, double sample_rate_hz)
    : n_(n), sample_rate_(sample_rate_hz), window_(n), buf_(n), mag_(n / 2 + 1) {
    if (!is_power_of_two(n)) throw std::invalid_argument("window size must be a power of two");
    if (sample_rate_hz <= 0) throw std::invalid_argument("sample rate must be positive");
    // Hann window: cuts spectral leakage when a tone falls between bins
    for (std::size_t i = 0; i < n; ++i)
        window_[i] = 0.5 * (1.0 - std::cos(2.0 * sp::kPi * static_cast<double>(i) / static_cast<double>(n - 1)));
}

const std::vector<double>& SpectrumAnalyzer::magnitudes(const std::vector<float>& samples) {
    if (samples.size() != n_) throw std::invalid_argument("sample count does not match analyzer size");
    for (std::size_t i = 0; i < n_; ++i) buf_[i] = cplx(samples[i] * window_[i], 0.0);
    fft_inplace(buf_);
    const double scale = 4.0 / static_cast<double>(n_);  // single-sided, corrected for the Hann gain of 0.5
    for (std::size_t k = 0; k <= n_ / 2; ++k) mag_[k] = std::abs(buf_[k]) * scale;
    mag_[0] *= 0.5;
    return mag_;
}

std::vector<Peak> SpectrumAnalyzer::top_peaks(const std::vector<float>& samples, std::size_t k, double min_relative) {
    const auto& m = magnitudes(samples);
    const double max_mag = *std::max_element(m.begin() + 1, m.end());

    std::vector<Peak> peaks;
    for (std::size_t i = 1; i + 1 < m.size(); ++i) {
        if (m[i] <= m[i - 1] || m[i] < m[i + 1] || m[i] < min_relative * max_mag) continue;
        // fit a parabola through the bin and its neighbours to place the peak between bins
        const double a = m[i - 1], b = m[i], c = m[i + 1];
        const double denom = a - 2 * b + c;
        const double offset = denom != 0.0 ? 0.5 * (a - c) / denom : 0.0;
        peaks.push_back({(static_cast<double>(i) + offset) * bin_width_hz(), b - 0.25 * (a - c) * offset});
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak& x, const Peak& y) { return x.magnitude > y.magnitude; });
    if (peaks.size() > k) peaks.resize(k);
    return peaks;
}

}  // namespace sp
