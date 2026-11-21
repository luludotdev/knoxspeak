#include "core/mesh.hpp"
#include "core/protocol.hpp"

#include <charconv>
#include <string>

namespace knoxspeak::mesh {
namespace {

std::string header(std::string_view verb) {
    std::string out(kTag);
    out += '\t';
    out += std::to_string(kVersion);
    out += '\t';
    out += verb;
    return out;
}

bool nextField(std::string_view& rest, std::string_view& out) {
    if (rest.data() == nullptr) {
        return false;
    }
    const auto tab = rest.find('\t');
    out = rest.substr(0, tab);
    rest = tab == std::string_view::npos ? std::string_view{} : rest.substr(tab + 1);
    return true;
}

} // namespace

std::optional<std::string> validUsername(std::string_view username) {
    const std::string_view trimmed = proto::trimAscii(username);
    // checks and returns valid usernames the same way zomboid does
    std::size_t units = 0;
    for (unsigned char c : trimmed) {
        if ((c & 0xC0) != 0x80) {
            units += c >= 0xF0 ? 2 : 1;
        }
    }
    if (units < kMinUsername || units > kMaxUsername || trimmed.size() > kMaxUsername * 4 ||
        trimmed.find_first_of(std::string_view("\t\n\r\0", 4)) != std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(trimmed);
}

std::optional<Message> parse(std::string_view command) {
    std::string_view rest = command, tag, version, verb;
    if (!nextField(rest, tag) || tag != kTag || !nextField(rest, version) || !nextField(rest, verb)) {
        return std::nullopt;
    }
    int v = 0;
    auto [ptr, ec] = std::from_chars(version.data(), version.data() + version.size(), v);
    if (ec != std::errc{} || ptr != version.data() + version.size() || v != kVersion) {
        return std::nullopt;
    }
    if (verb == "ANNOUNCE") {
        std::string_view name;
        if (!nextField(rest, name)) {
            return std::nullopt;
        }
        if (auto valid = validUsername(name)) {
            return Announce{std::move(*valid)};
        }
        return std::nullopt;
    }
    if (verb == "QUERY") {
        return Query{};
    }
    if (verb == "IDLE") {
        return Idle{};
    }
    if (verb == "LEAVE") {
        return Leave{};
    }
    if (verb == "CARRY") {
        std::string_view list;
        nextField(rest, list); // may be absent or empty
        if (list.size() > 512) {
            return std::nullopt;
        }
        return CarryRadios{std::string(list)};
    }
    return std::nullopt;
}

std::string encodeAnnounce(std::string_view username) {
    return header("ANNOUNCE") + '\t' + proto::sanitizeField(username);
}

std::string encodeQuery() {
    return header("QUERY");
}
std::string encodeIdle() {
    return header("IDLE");
}
std::string encodeCarry(std::string_view list) {
    return header("CARRY") + '\t' + proto::sanitizeField(list);
}
std::string encodeLeave() {
    return header("LEAVE");
}

} // namespace knoxspeak::mesh
