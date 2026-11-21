#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace knoxspeak::info {

struct SelfState {
    bool gameConnected = false;          // the PZ client is attached to this plugin
    std::optional<std::string> username; // set once the player is in a multiplayer game (recv HELLO)
    bool voiceLive = false;              // frames are arriving; positional voice active
    bool serverLacksMod = false;         // in multiplayer, server doesn't run knoxspeak (recv NOMOD)
    std::string_view version;
};

enum class PeerKind { Unknown, Idle, Playing };

struct PeerState {
    PeerKind kind = PeerKind::Unknown;
    std::string username;
};

[[nodiscard]] std::string selfText(const SelfState& s);
[[nodiscard]] std::string peerText(const PeerState& p);
[[nodiscard]] std::string serverText(int playing, int withPlugin);

} // namespace knoxspeak::info
