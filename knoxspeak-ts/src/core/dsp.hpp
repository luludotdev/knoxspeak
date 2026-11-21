#pragma once
// i dont understand half this shit thank god for audio engineers

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <utility>

namespace knoxspeak::dsp {

inline constexpr float kSampleRate = 48000.0f; // TS3 playback voice data is 48 kHz
inline constexpr float kClearCutoffHz = 18000.0f;
inline constexpr float kMuffledCutoffHz = 350.0f;

// log interpolation from a (t = 0) to b (t = 1).
[[nodiscard]] inline float logLerp(float a, float b, float t) {
    return a * std::pow(b / a, t);
}

// map muffle ∈ [0,1] to a low-pass cutoff with log interpolation (perceptually even steps)
[[nodiscard]] inline float muffleToCutoffHz(float muffle) {
    return logLerp(kClearCutoffHz, kMuffledCutoffHz, std::clamp(muffle, 0.0f, 1.0f));
}

// one-pole low-pass coefficient for the given cutoff
[[nodiscard]] inline float onePoleAlpha(float cutoffHz, float sampleRate = kSampleRate) {
    return 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * cutoffHz / sampleRate);
}

// constant power pan law
// pan ∈ [-1,1] -> {left, right}
[[nodiscard]] inline std::pair<float, float> panGains(float pan) {
    const float theta = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * (std::numbers::pi_v<float> / 4.0f);
    return {std::cos(theta), std::sin(theta)};
}

inline constexpr unsigned int kSpeakerFrontLeft = 0x1;
inline constexpr unsigned int kSpeakerFrontRight = 0x2;
inline constexpr unsigned int kSpeakerHeadphonesLeft = 0x10000000;
inline constexpr unsigned int kSpeakerHeadphonesRight = 0x20000000;

[[nodiscard]] inline std::pair<int, int> findStereoPair(const unsigned int* speakers, int channels) {
    if (!speakers || channels < 2) {
        return {-1, -1};
    }
    const std::span<const unsigned int> sp(speakers, static_cast<std::size_t>(channels));
    auto find = [&](unsigned int flag) {
        const auto it = std::ranges::find(sp, flag);
        return it == sp.end() ? -1 : static_cast<int>(it - sp.begin());
    };
    for (auto [l, r] : {std::pair{kSpeakerHeadphonesLeft, kSpeakerHeadphonesRight},
                        std::pair{kSpeakerFrontLeft, kSpeakerFrontRight}}) {
        const int li = find(l), ri = find(r);
        if (li >= 0 && ri >= 0) {
            return {li, ri};
        }
    }
    return {-1, -1};
}

[[nodiscard]] inline std::int16_t toPcm16(float v) {
    return static_cast<std::int16_t>(std::lrint(std::clamp(v, -32768.0f, 32767.0f)));
}

inline void flushDenormals(std::span<float> state) {
    for (float& v : state) {
        if (std::abs(v) < 1e-15f) {
            v = 0.0f;
        }
    }
}

[[nodiscard]] inline float glideFactor(int frames, float tauSeconds) {
    return 1.0f - std::exp(-static_cast<float>(frames) / (kSampleRate * tauSeconds));
}

class VoiceProcessor {
  public:
    static constexpr float kMuffleGlideSeconds = 0.3f;

    // interleaved samples
    // effects applied to all channels
    void process(std::int16_t* samples, int frames, int channels, float targetGain, float targetMuffle) {
        if (frames <= 0 || channels <= 0) {
            return;
        }
        if (!primed_) {
            gain_ = targetGain;
            muffle_ = targetMuffle;
            primed_ = true;
        }

        const float gainStep = (targetGain - gain_) / static_cast<float>(frames);
        const float nextMuffle = muffle_ + (targetMuffle - muffle_) * glideFactor(frames, kMuffleGlideSeconds);
        const float a0 = onePoleAlpha(muffleToCutoffHz(muffle_));
        const float a1 = onePoleAlpha(muffleToCutoffHz(nextMuffle));
        const float alphaStep = (a1 - a0) / static_cast<float>(frames);
        const int ch = std::min(channels, kMaxChannels);

        float g = gain_;
        float alpha = a0;
        for (int i = 0; i < frames; ++i) {
            g += gainStep;
            alpha += alphaStep;
            for (int c = 0; c < channels; ++c) {
                auto& s = samples[i * channels + c];
                float x = static_cast<float>(s);
                if (c < ch) {
                    z1_[c] += alpha * (x - z1_[c]);
                    z2_[c] += alpha * (z1_[c] - z2_[c]);
                    x = z2_[c];
                }
                s = toPcm16(x * g);
            }
        }
        gain_ = targetGain;
        muffle_ = nextMuffle;
        flushDenormals(z1_);
        flushDenormals(z2_);
    }

  private:
    static constexpr int kMaxChannels = 8;
    bool primed_ = false;
    float gain_ = 1.0f;
    float muffle_ = 0.0f;
    float z1_[kMaxChannels]{};
    float z2_[kMaxChannels]{};
};

class RadioProcessor {
  public:
    static constexpr float kQualityGlideSeconds = 0.3f;

    void process(std::int16_t* samples, int frames, int channels, float targetGain, float quality, bool filter,
                 bool staticNoise) {
        if (frames <= 0 || channels <= 0) {
            return;
        }
        const float targetQ = std::clamp(quality, 0.0f, 1.0f);
        if (!primed_) {
            gain_ = targetGain;
            q_ = targetQ;
            primed_ = true;
        }
        const float gainStep = (targetGain - gain_) / static_cast<float>(frames);
        q_ += (targetQ - q_) * glideFactor(frames, kQualityGlideSeconds);
        const float weak = 1.0f - q_;

        // interpolation between the near and edge band edges
        const float hpA = onePoleAlpha(logLerp(kHighPassNearHz, kHighPassEdgeHz, weak));
        const float lpA = onePoleAlpha(logLerp(kLowPassNearHz, kLowPassEdgeHz, weak));
        const float noiseHpA = onePoleAlpha(kNoiseHighPassHz);
        const float noiseLpA = onePoleAlpha(kNoiseLowPassHz);
        const float voiceLevel = 1.0f - (1.0f - kVoiceAtEdge) * weak;
        // weak = fraction of the range covered (the game's quality is linear in distance).
        const float noiseAmp = kNoiseEdge * std::clamp((weak - kStaticStart) / (1.0f - kStaticStart), 0.0f, 1.0f);
        const float driveNorm = 1.0f / std::tanh(kDrive);
        const int ch = std::min(channels, kMaxChannels);

        float g = gain_;
        for (int i = 0; i < frames; ++i) {
            g += gainStep;
            for (int c = 0; c < channels; ++c) {
                auto& s = samples[i * channels + c];
                float x = static_cast<float>(s) / 32768.0f;
                if (filter && c < ch) {
                    // high-pass
                    hp1_[c] += hpA * (x - hp1_[c]);
                    x -= hp1_[c];
                    hp2_[c] += hpA * (x - hp2_[c]);
                    x -= hp2_[c];
                    lp1_[c] += lpA * (x - lp1_[c]);
                    lp2_[c] += lpA * (lp1_[c] - lp2_[c]);
                    x = std::tanh(lp2_[c] * kDrive) * driveNorm * kMakeup;
                }
                x *= voiceLevel; // fading with range always applies
                if (staticNoise && c < ch) {
                    float n = nextNoise();
                    nhp_[c] += noiseHpA * (n - nhp_[c]);
                    n -= nhp_[c];
                    nlp_[c] += noiseLpA * (n - nlp_[c]);
                    x += noiseAmp * nlp_[c];
                }
                s = toPcm16(x * g * 32768.0f);
            }
        }
        gain_ = targetGain;
        for (auto* state : {hp1_, hp2_, lp1_, lp2_, nhp_, nlp_}) {
            flushDenormals(std::span(state, kMaxChannels));
        }
    }

  private:
    static constexpr int kMaxChannels = 8;
    static constexpr float kHighPassNearHz = 400.0f;
    static constexpr float kHighPassEdgeHz = 800.0f;
    static constexpr float kLowPassNearHz = 2800.0f;
    static constexpr float kLowPassEdgeHz = 1600.0f;
    static constexpr float kDrive = 2.0f;
    static constexpr float kMakeup = 0.6f; // tanh(2x)/tanh(2) ~2x speech level (band pass helps return dampen)
    static constexpr float kVoiceAtEdge = 0.3f; // voice level at edge of range (relative to clear signal)

    // band-limited static noise
    static constexpr float kStaticStart = 0.3f; // range % static starts at
    static constexpr float kNoiseEdge = 0.010f;
    static constexpr float kNoiseHighPassHz = 300.0f;
    static constexpr float kNoiseLowPassHz = 2500.0f;

    float nextNoise() { // xorshift32 -> [-1, 1)
        noise_ ^= noise_ << 13;
        noise_ ^= noise_ >> 17;
        noise_ ^= noise_ << 5;
        return static_cast<float>(noise_) / 2147483648.0f - 1.0f;
    }

    bool primed_ = false;
    float gain_ = 1.0f;
    float q_ = 1.0f;
    std::uint32_t noise_ = 0x9E3779B9u;
    float hp1_[kMaxChannels]{};
    float hp2_[kMaxChannels]{};
    float lp1_[kMaxChannels]{};
    float lp2_[kMaxChannels]{};
    float nhp_[kMaxChannels]{};
    float nlp_[kMaxChannels]{};
};

// smoothly moves the pan position between buffers
class PanSmoother {
  public:
    // returns {startPan, endPan} for this buffer
    std::pair<float, float> next(float target) {
        const float start = current_.value_or(target);
        current_ = target;
        return {start, target};
    }

  private:
    std::optional<float> current_;
};

[[nodiscard]] inline unsigned int panToStereo(std::int16_t* samples, int frames, int channels, unsigned int filledMask,
                                              int left, int right, float panStart, float panEnd) {
    const unsigned int channelMask = channels >= 32 ? ~0u : (1u << channels) - 1u;
    const unsigned int filled = filledMask & channelMask;
    const int filledCount = std::popcount(filled);
    if (frames <= 0 || filledCount == 0) {
        return filledMask;
    }
    const float norm = std::numbers::sqrt2_v<float> / static_cast<float>(filledCount);
    const auto [l0, r0] = panGains(panStart);
    const auto [l1, r1] = panGains(panEnd);
    const float stepL = (l1 - l0) * norm / static_cast<float>(frames);
    const float stepR = (r1 - r0) * norm / static_cast<float>(frames);
    float gl = l0 * norm;
    float gr = r0 * norm;
    for (int i = 0; i < frames; ++i) {
        gl += stepL; // reaches the end gains on the last frame
        gr += stepR;
        std::int16_t* frame = samples + static_cast<std::ptrdiff_t>(i) * channels;
        float mono = 0.0f;
        for (int c = 0; c < channels; ++c) {
            if ((filled >> c) & 1u) {
                mono += static_cast<float>(frame[c]);
            }
            frame[c] = 0;
        }
        frame[left] = toPcm16(mono * gl);
        frame[right] = toPcm16(mono * gr);
    }
    return (1u << left) | (1u << right);
}

// fixed capacity per-key state for audio thread
// keeps allocations slow and lookups quick
template <class T, std::size_t N> class StatePool {
  public:
    T& get(std::uint64_t key) {
        const std::uint64_t now = ++tick_;
        for (std::size_t i = 0; i < count_; ++i) {
            if (keys_[i] == key) {
                lastUse_[i] = now;
                return values_[i];
            }
        }
        std::size_t slot = count_;
        if (count_ < N) {
            ++count_;
        } else {
            slot = static_cast<std::size_t>(std::ranges::min_element(lastUse_) - lastUse_.begin());
            values_[slot] = T{};
        }
        keys_[slot] = key;
        lastUse_[slot] = now;
        return values_[slot];
    }

  private:
    std::size_t count_ = 0;
    std::uint64_t tick_ = 0;
    std::array<std::uint64_t, N> keys_{};
    std::array<std::uint64_t, N> lastUse_{};
    std::array<T, N> values_{};
};

} // namespace knoxspeak::dsp
