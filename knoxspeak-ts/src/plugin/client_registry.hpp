#pragma once

#include "core/protocol.hpp"

#include "plugin/voice_snapshot.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

namespace knoxspeak {

class ClientRegistry {
  public:
    struct Entry {
        std::uint64_t key;                     // clientKey(schid, clientId)
        std::optional<std::string> username;   // as announced; nullopt = plugin present but no game
        std::optional<std::string> normalized; // proto::normalizeName(username): frame lookup key
    };

    // ANNOUNCE
    // returns the keys of *other* clients that already claim the same username
    std::vector<std::uint64_t> setPlaying(std::uint64_t key, std::string_view username) {
        std::scoped_lock lock(mutex_);
        const auto norm = proto::normalizeName(username);
        std::vector<std::uint64_t> clashes;
        for (const auto& [k, other] : entries_) {
            if (k != key && other.normalized == norm) {
                clashes.push_back(k);
            }
        }
        entries_[key] = Entry{key, std::string(username), norm};
        return clashes;
    }

    // IDLE
    void setIdle(std::uint64_t key) {
        std::scoped_lock lock(mutex_);
        entries_[key] = Entry{key, std::nullopt, std::nullopt};
    }

    // LEAVE, or the client left the server
    void remove(std::uint64_t key) {
        std::scoped_lock lock(mutex_);
        entries_.erase(key);
    }

    void removeServer(std::uint64_t schid) {
        std::scoped_lock lock(mutex_);
        std::erase_if(entries_, [schid](const auto& kv) { return serverOf(kv.first) == schid; });
    }

    // nullopt = never heard from this client's plugin
    [[nodiscard]] std::optional<Entry> find(std::uint64_t key) const {
        std::scoped_lock lock(mutex_);
        auto it = entries_.find(key);
        return it == entries_.end() ? std::nullopt : std::optional<Entry>(it->second);
    }

    [[nodiscard]] std::optional<std::string> username(std::uint64_t key) const {
        auto e = find(key);
        return e ? e->username : std::nullopt;
    }

    [[nodiscard]] std::vector<Entry> entries() const {
        std::scoped_lock lock(mutex_);
        return entries_ | std::views::values | std::ranges::to<std::vector>();
    }

  private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, Entry> entries_;
};

} // namespace knoxspeak
