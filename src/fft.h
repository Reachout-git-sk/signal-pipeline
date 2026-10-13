#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace sp {

using cplx = std::complex<double>;

inline constexpr double kPi = 3.14159265358979323846;

bool is_power_of_two(std::size_t n);

// In-place iterative radix-2 Cooley-Tukey FFT. Size must be a power of two.
// O(n log n): log2(n) butterfly stages, each touching every element once.
void fft_inplace(std::vector<cplx>& data);

// Reference O(n^2) DFT, used by the tests to check fft_inplace.
std::vector<cplx> naive_dft(const std::vector<cplx>& in);

struct Peak {
    double frequency_hz;
    double magnitude;
};

// Single-sided magnitude spectrum of a real signal (Hann windowed), and the
// strongest local maxima in it. Frequencies are refined with parabolic
// interpolation across the neighbouring bins.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer(std::size_t n, double sample_rate_hz);

    std::size_t size() const { return n_; }
    double bin_width_hz() const { return sample_rate_ / static_cast<double>(n_); }

    // Returns n/2 + 1 magnitudes. `samples` must have exactly size() entries.
    const std::vector<double>& magnitudes(const std::vector<float>& samples);

    std::vector<Peak> top_peaks(const std::vector<float>& samples, std::size_t k, double min_relative = 0.05);

private:
    std::size_t n_;
    double sample_rate_;
    std::vector<double> window_;
    std::vector<cplx> buf_;
    std::vector<double> mag_;
};

}  // namespace sp
