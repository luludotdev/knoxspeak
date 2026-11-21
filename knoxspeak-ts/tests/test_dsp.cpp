#include "core/dsp.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace knoxspeak::dsp;

namespace {
std::vector<std::int16_t> sine(float hz, int frames, float amp = 10000.0f) {
    std::vector<std::int16_t> v(frames);
    for (int i = 0; i < frames; ++i) {
        v[i] = static_cast<std::int16_t>(amp * std::sin(2.0f * std::numbers::pi_v<float> * hz * i / kSampleRate));
    }
    return v;
}

double rms(const std::vector<std::int16_t>& v, std::size_t skip = 0) {
    double acc = 0;
    for (std::size_t i = skip; i < v.size(); ++i) {
        acc += static_cast<double>(v[i]) * v[i];
    }
    return std::sqrt(acc / static_cast<double>(v.size() - skip));
}
} // namespace

TEST_CASE("muffle maps to a monotonic cutoff") {
    CHECK(muffleToCutoffHz(0.0f) == Catch::Approx(kClearCutoffHz));
    CHECK(muffleToCutoffHz(1.0f) == Catch::Approx(kMuffledCutoffHz));
    CHECK(muffleToCutoffHz(0.5f) < muffleToCutoffHz(0.25f));
}

TEST_CASE("constant-power pan") {
    auto [l, r] = panGains(0.0f);
    CHECK(l == Catch::Approx(r));
    CHECK(l * l + r * r == Catch::Approx(1.0f));
    auto [hl, hr] = panGains(-1.0f);
    CHECK(hl == Catch::Approx(1.0f));
    CHECK(hr == Catch::Approx(0.0f).margin(1e-6));
}

TEST_CASE("zero gain silences") {
    auto buf = sine(440, 960);
    VoiceProcessor vp;
    vp.process(buf.data(), 960, 1, 0.0f, 0.0f);
    CHECK(rms(buf) == Catch::Approx(0.0));
}

TEST_CASE("clear voice passes nearly unchanged") {
    auto ref = sine(1000, 4800);
    auto buf = ref;
    VoiceProcessor vp;
    vp.process(buf.data(), 4800, 1, 1.0f, 0.0f);
    CHECK(rms(buf, 480) == Catch::Approx(rms(ref, 480)).epsilon(0.05));
}

TEST_CASE("muffle attenuates highs more than lows") {
    VoiceProcessor lowVp, highVp;
    auto low = sine(200, 4800);
    auto high = sine(4000, 4800);
    const double lowRef = rms(low, 480), highRef = rms(high, 480);
    lowVp.process(low.data(), 4800, 1, 1.0f, 0.9f);
    highVp.process(high.data(), 4800, 1, 1.0f, 0.9f);
    CHECK(rms(low, 480) / lowRef > 0.7);
    CHECK(rms(high, 480) / highRef < 0.2);
}

TEST_CASE("stereo pair: headphones mode") {
    const unsigned int headphones[] = {0x10000000, 0x20000000};
    CHECK(findStereoPair(headphones, 2) == std::pair{0, 1});
}

TEST_CASE("stereo pair: speakers, surround, and unusable layouts") {
    const unsigned int stereo[] = {0x1, 0x2};
    CHECK(findStereoPair(stereo, 2) == std::pair{0, 1});
    const unsigned int surround[] = {0x1, 0x2, 0x4, 0x8, 0x10, 0x20}; // 5.1: FL FR C LFE BL BR
    CHECK(findStereoPair(surround, 6) == std::pair{0, 1});
    const unsigned int swapped[] = {0x20000000, 0x10000000};
    CHECK(findStereoPair(swapped, 2) == std::pair{1, 0});
    const unsigned int mono[] = {0x40000000};
    CHECK(findStereoPair(mono, 1) == std::pair{-1, -1});
    CHECK(findStereoPair(nullptr, 2) == std::pair{-1, -1});
}
