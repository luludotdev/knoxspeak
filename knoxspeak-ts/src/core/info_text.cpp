#include "core/info_text.hpp"

#include <format>

namespace knoxspeak::info {
namespace {

constexpr std::string_view kGood = "#27ae60";
constexpr std::string_view kWarn = "#e67e22";
constexpr std::string_view kOff = "#95a5a6";

std::string dot(std::string_view color) {
    return std::format("[color={}]●[/color] ", color);
}

// strip bbcode from usernames
std::string plain(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out += (c == '[' || c == ']') ? '_' : c;
    }
    return out;
}

} // namespace

std::string selfText(const SelfState& s) {
    std::string line;
    if (!s.gameConnected) {
        line = dot(kOff) + "Project Zomboid not connected";
    } else if (!s.username && s.serverLacksMod) {
        line = dot(kWarn) + "In a multiplayer game, but the server doesn't have knoxspeak enabled"
                            "\nServer admin: add knoxspeak and ZombieBuddy to Mods=";
    } else if (!s.username) {
        line = dot(kWarn) + "Project Zomboid open, not in a multiplayer game: everyone muted";
    } else if (s.voiceLive) {
        line = dot(kGood) + std::format("In game as [b]{}[/b]: positional voice active", plain(*s.username));
    } else {
        line = dot(kWarn) + std::format("In game as [b]{}[/b]: everyone muted until the game sends updates (loading?)",
                                        plain(*s.username));
    }
    return line + std::format("\nknoxspeak {}", s.version);
}

std::string peerText(const PeerState& p) {
    switch (p.kind) {
    case PeerKind::Unknown:
        return dot(kOff) + "knoxspeak not detected";
    case PeerKind::Idle:
        return dot(kWarn) + "knoxspeak installed, not in game";
    case PeerKind::Playing:
        break;
    }
    return dot(kGood) + std::format("In game as [b]{}[/b]", plain(p.username));
}

std::string serverText(int playing, int withPlugin) {
    return std::format("{} in game, {} with knoxspeak", playing, withPlugin);
}

} // namespace knoxspeak::info
