#pragma once
// plugin <-> plugin messages to communicate teamspeak<->zomboid user info

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace knoxspeak::mesh {

inline constexpr std::string_view kTag = "KNOXSPEAK";
inline constexpr int kVersion = 1;

// zomboid account name limits
inline constexpr std::size_t kMinUsername = 2;
inline constexpr std::size_t kMaxUsername = 32;

struct Announce {
    std::string username; // "my TS client is this PZ user (game attached)"
};
struct Idle {};  // "I have knoxspeak, but no game is attached"
struct Leave {}; // "my plugin is shutting down"
struct Query {}; // "everyone, tell me your state" (replies: Announce or Idle, sent to the asker only)
struct CarryRadios {
    std::string list; // my carried walkies "freq:headphones:volume;..." (empty = none)
};

using Message = std::variant<Announce, Idle, Leave, Query, CarryRadios>;

[[nodiscard]] std::optional<Message> parse(std::string_view command);

[[nodiscard]] std::string encodeAnnounce(std::string_view username);
[[nodiscard]] std::string encodeIdle();
[[nodiscard]] std::string encodeLeave();
[[nodiscard]] std::string encodeQuery();
[[nodiscard]] std::string encodeCarry(std::string_view list);

// trimmed username, nullopt if invalid
[[nodiscard]] std::optional<std::string> validUsername(std::string_view username);

} // namespace knoxspeak::mesh
