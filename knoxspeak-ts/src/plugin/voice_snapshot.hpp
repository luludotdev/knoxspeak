#pragma once

#include "core/protocol.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace knoxspeak {

using Clock = std::chrono::steady_clock;

[[nodiscard]] inline std::uint64_t clientKey(std::uint64_t schid, std::uint16_t clientId) {
    return (schid << 16) | clientId;
}
[[nodiscard]] inline std::uint64_t serverOf(std::uint64_t key) {
    return key >> 16;
}
[[nodiscard]] inline std::uint16_t clientOf(std::uint64_t key) {
    return static_cast<std::uint16_t>(key & 0xFFFF);
}

struct VoiceSnapshot {
    Clock::time_point committedAt{};
    std::unordered_map<std::uint64_t, proto::VoiceParams> clients;
};

// stale snapshot lifetime
inline constexpr auto kSnapshotTtl = std::chrono::seconds(2);

// audio thread must never free a snapshot (frees a whole map)
class SnapshotSlot {
  public:
    void publish(std::shared_ptr<const VoiceSnapshot> s) {
        auto old = slot_.exchange(std::move(s), std::memory_order_acq_rel);
        std::scoped_lock lock(retiredMutex_);
        if (old) {
            retired_.push_back(std::move(old));
        }
        std::erase_if(retired_, [](const auto& p) { return p.use_count() == 1; });
    }
    [[nodiscard]] std::shared_ptr<const VoiceSnapshot> load() const { return slot_.load(std::memory_order_acquire); }

  private:
    std::atomic<std::shared_ptr<const VoiceSnapshot>> slot_;
    std::mutex retiredMutex_; // publishers only; never taken by the audio thread
    std::vector<std::shared_ptr<const VoiceSnapshot>> retired_;
};

} // namespace knoxspeak
