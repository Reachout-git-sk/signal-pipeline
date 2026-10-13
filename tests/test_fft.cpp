#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "fft.h"

using sp::cplx;

namespace {

std::vector<float> tones(std::size_t n, double fs, std::initializer_list<std::pair<double, double>> freq_amp,
                         double noise = 0.0, unsigned seed = 1) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, noise > 0 ? noise : 1.0);
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        double v = 0;
        for (auto [f, a] : freq_amp) v += a * std::sin(2 * sp::kPi * f * t);
        x[i] = static_cast<float>(v + (noise > 0 ? gauss(rng) : 0.0));
    }
    return x;
}

}  // namespace

TEST(Fft, RejectsNonPowerOfTwo) {
    std::vector<cplx> v(1000);
    EXPECT_THROW(sp::fft_inplace(v), std::invalid_argument);
    EXPECT_THROW(sp::SpectrumAnalyzer(1000, 1000.0), std::invalid_argument);
}

TEST(Fft, ImpulseIsFlat) {
    std::vector<cplx> v(64, cplx(0, 0));
    v[0] = 1.0;
    sp::fft_inplace(v);
    for (const auto& c : v) EXPECT_NEAR(std::abs(c), 1.0, 1e-12);
}

TEST(Fft, DcGoesToBinZero) {
    std::vector<cplx> v(128, cplx(2.0, 0));
    sp::fft_inplace(v);
    EXPECT_NEAR(v[0].real(), 256.0, 1e-9);
    for (std::size_t k = 1; k < v.size(); ++k) EXPECT_NEAR(std::abs(v[k]), 0.0, 1e-9);
}

TEST(Fft, MatchesNaiveDftOnRandomInput) {
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> u(-1, 1);
    for (std::size_t n : {2u, 8u, 64u, 256u}) {
        std::vector<cplx> x(n);
        for (auto& c : x) c = cplx(u(rng), u(rng));
        auto expected = sp::naive_dft(x);
        sp::fft_inplace(x);
        for (std::size_t k = 0; k < n; ++k) {
            EXPECT_NEAR(x[k].real(), expected[k].real(), 1e-9) << "n=" << n << " k=" << k;
            EXPECT_NEAR(x[k].imag(), expected[k].imag(), 1e-9) << "n=" << n << " k=" << k;
        }
    }
}

TEST(Fft, ParsevalHolds) {
    std::mt19937 rng(7);
    std::normal_distribution<double> g;
    std::vector<cplx> x(1024);
    double time_energy = 0;
    for (auto& c : x) {
        c = cplx(g(rng), 0);
        time_energy += std::norm(c);
    }
    sp::fft_inplace(x);
    double freq_energy = 0;
    for (auto& c : x) freq_energy += std::norm(c);
    EXPECT_NEAR(freq_energy / 1024.0, time_energy, 1e-6 * time_energy);
}

TEST(Spectrum, Pure50HzSinePeaksAt50Hz) {
    const double fs = 1000;
    sp::SpectrumAnalyzer a(1024, fs);
    auto peaks = a.top_peaks(tones(1024, fs, {{50, 1.0}}), 3);
    ASSERT_FALSE(peaks.empty());
    EXPECT_NEAR(peaks[0].frequency_hz, 50.0, 0.5 * a.bin_width_hz());
    EXPECT_NEAR(peaks[0].magnitude, 1.0, 0.1);
    EXPECT_EQ(peaks.size(), 1u);  // no spurious peaks above 5% of the main one
}

TEST(Spectrum, FiftyPlus120HzGivesBothPeaksWithNoise) {
    const double fs = 1000;
    sp::SpectrumAnalyzer a(1024, fs);
    auto peaks = a.top_peaks(tones(1024, fs, {{50, 1.0}, {120, 0.5}}, 0.2), 2);
    ASSERT_EQ(peaks.size(), 2u);
    EXPECT_NEAR(peaks[0].frequency_hz, 50.0, 0.5 * a.bin_width_hz());
    EXPECT_NEAR(peaks[1].frequency_hz, 120.0, 0.5 * a.bin_width_hz());
    EXPECT_GT(peaks[0].magnitude, peaks[1].magnitude);
    EXPECT_NEAR(peaks[1].magnitude / peaks[0].magnitude, 0.5, 0.1);
}

TEST(Spectrum, InterpolationLandsBetweenBins) {
    const double fs = 1000;
    sp::SpectrumAnalyzer a(1024, fs);
    // 0.977 Hz bins: 123.3 Hz falls between bins 126 and 127
    auto peaks = a.top_peaks(tones(1024, fs, {{123.3, 1.0}}), 1);
    ASSERT_EQ(peaks.size(), 1u);
    EXPECT_NEAR(peaks[0].frequency_hz, 123.3, 0.1);
}

TEST(Spectrum, WrongSampleCountThrows) {
    sp::SpectrumAnalyzer a(256, 1000);
    EXPECT_THROW(a.magnitudes(std::vector<float>(255)), std::invalid_argument);
}
