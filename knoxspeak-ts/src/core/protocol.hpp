#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace knoxspeak::proto {

inline constexpr int kVersion = 1;
inline constexpr std::uint16_t kDefaultPort = 47123;
inline constexpr std::size_t kMaxLineBytes = 4096;

// ---- game -> plugin ----
struct Hello {
    int version = 0;
    std::string modVersion;
    std::string username;
};
struct FrameBegin {
    std::uint32_t seq = 0;
};
struct VoiceParams {
    float gain = 0.0f;    // [0,1]
    float muffle = 0.0f;  // [0,1], 0 = clear
    float pan = 0.0f;     // [-1,1], left..right
    bool radio = false;   // heard through a radio: apply the radio effect instead of muffle
    float quality = 1.0f; // radio signal [0,1]; static grows as it drops (cutoff is decided by the game)
    float pickupMuffle =
        0.0f; // radio only: how muffled the sender is at their radio's mic, applied before the radio effect
};
struct Speaker {
    std::string username;
    VoiceParams params;
};
struct FrameEnd {
    std::uint32_t seq = 0;
};
struct Ping {
    std::string token;
};
struct Bye {};
struct SetMicMuted {
    bool muted = false;
};
struct SetSilenced {
    bool silenced = false;
};
struct Config {
    std::string key; // "radiofilter" | "radiostatic"
    bool value = false;
};
struct Carry {
    std::string list;
};
struct ServerLacksMod {
    bool lacking = false;
};

using GameMessage = std::variant<Hello, FrameBegin, Speaker, FrameEnd, Ping, Bye, SetMicMuted, SetSilenced,
                                 Config, Carry, ServerLacksMod>;

[[nodiscard]] std::optional<GameMessage> parseGameMessage(std::string_view line);

// ---- plugin -> game ----
[[nodiscard]] std::string encodeHello(std::string_view pluginVersion);
[[nodiscard]] std::string encodeTsStatus(bool connected);
[[nodiscard]] std::string encodeTalk(std::string_view username, bool talking);
[[nodiscard]] std::string encodeMic(bool muted);
// another players carried walkies (relayed from the mesh): PEERCARRY <username> <list>; empty list = none.
[[nodiscard]] std::string encodePeerCarry(std::string_view username, std::string_view list);
[[nodiscard]] std::string encodePong(std::string_view token);
[[nodiscard]] std::string encodeError(std::string_view message);

// strip leading/trailing ascii whitespace
[[nodiscard]] std::string_view trimAscii(std::string_view s);
// normalize usernames
[[nodiscard]] std::string normalizeName(std::string_view name);
// sanitize whitespace in protocol messages
[[nodiscard]] std::string sanitizeField(std::string_view value);

using FrameMap = std::unordered_map<std::string, VoiceParams>;

// assemble a complete frame from start/speak/end messages
class FrameAssembler {
  public:
    std::optional<FrameMap> feed(const GameMessage& msg);
    void reset();

  private:
    std::optional<std::uint32_t> openSeq_;
    FrameMap pending_;
};

class LineSplitter {
  public:
    bool append(std::string_view bytes, const std::function<void(std::string_view)>& onLine);
    void reset() { buffer_.clear(); }

  private:
    std::string buffer_;
};

} // namespace knoxspeak::proto
