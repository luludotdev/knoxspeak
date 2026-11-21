#include "core/protocol.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace knoxspeak::proto {
namespace {

constexpr std::size_t kMaxFields = 8;

struct Fields {
    std::array<std::string_view, kMaxFields> v{};
    std::size_t count = 0;
};

Fields split(std::string_view line) {
    Fields f;
    std::size_t start = 0;
    while (f.count < kMaxFields) {
        const auto tab = line.find('\t', start);
        f.v[f.count++] = line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start);
        if (tab == std::string_view::npos) {
            break;
        }
        start = tab + 1;
    }
    return f;
}

template <typename T> std::optional<T> parseNumber(std::string_view s) {
    T value{};
    const auto* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(s.data(), end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<float> parseClamped(std::string_view s, float lo, float hi) {
    auto v = parseNumber<float>(s);
    if (!v || *v != *v) { // reject NaN
        return std::nullopt;
    }
    return std::clamp(*v, lo, hi);
}

template <typename T> std::optional<GameMessage> seqMsg(std::string_view s) {
    if (auto seq = parseNumber<std::uint32_t>(s)) {
        return T{*seq};
    }
    return std::nullopt;
}

std::optional<bool> strictBool(std::string_view s) {
    if (s == "0" || s == "1") {
        return s == "1";
    }
    return std::nullopt;
}

std::string line(std::initializer_list<std::string_view> fields) {
    std::string out;
    bool first = true;
    for (auto f : fields) {
        if (!first) {
            out.push_back('\t');
        }
        first = false;
        out += sanitizeField(f);
    }
    out.push_back('\n');
    return out;
}

} // namespace

std::optional<GameMessage> parseGameMessage(std::string_view raw) {
    if (!raw.empty() && raw.back() == '\r') {
        raw.remove_suffix(1);
    }
    if (raw.empty()) {
        return std::nullopt;
    }
    const auto f = split(raw);
    const auto cmd = f.v[0];

    if (cmd == "SPK" && f.count >= 5) {
        auto gain = parseClamped(f.v[2], 0.0f, 1.0f);
        auto muffle = parseClamped(f.v[3], 0.0f, 1.0f);
        auto pan = parseClamped(f.v[4], -1.0f, 1.0f);
        if (!gain || !muffle || !pan || f.v[1].empty()) {
            return std::nullopt;
        }
        Speaker s{std::string(f.v[1]), {*gain, *muffle, *pan}};
        if (f.count >= 7) { // optional: radio quality
            s.params.radio = f.v[5] == "1";
            s.params.quality = parseClamped(f.v[6], 0.0f, 1.0f).value_or(1.0f);
        }
        if (f.count >= 8) { // optional: muffle at the sender's radio mic
            s.params.pickupMuffle = parseClamped(f.v[7], 0.0f, 1.0f).value_or(0.0f);
        }
        return s;
    }
    if (cmd == "CFG" && f.count >= 3 && !f.v[1].empty()) {
        return Config{std::string(f.v[1]), f.v[2] == "1"};
    }
    if (cmd == "CARRY") {
        return Carry{f.count >= 2 ? std::string(f.v[1]) : std::string{}};
    }
    if (cmd == "FRAME" && f.count >= 2) {
        return seqMsg<FrameBegin>(f.v[1]);
    }
    if (cmd == "END" && f.count >= 2) {
        return seqMsg<FrameEnd>(f.v[1]);
    }
    if (cmd == "HELLO" && f.count >= 4) {
        auto version = parseNumber<int>(f.v[1]);
        if (!version) {
            return std::nullopt;
        }
        return Hello{*version, std::string(f.v[2]), std::string(f.v[3])};
    }
    if (cmd == "PING") {
        return Ping{f.count >= 2 ? std::string(f.v[1]) : std::string{}};
    }
    if (cmd == "BYE") {
        return Bye{};
    }
    if (cmd == "NOMOD" && f.count >= 2) {
        if (auto b = strictBool(f.v[1])) {
            return ServerLacksMod{*b};
        }
        return std::nullopt;
    }
    if (cmd == "SILENCE" && f.count >= 2) {
        return SetSilenced{f.v[1] == "1"};
    }
    if (cmd == "MUTE" && f.count >= 2) {
        if (auto b = strictBool(f.v[1])) {
            return SetMicMuted{*b};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

std::string encodeHello(std::string_view pluginVersion) {
    return line({"HELLO", std::to_string(kVersion), pluginVersion});
}
std::string encodeTsStatus(bool connected) {
    return line({"TS", connected ? "1" : "0"});
}
std::string encodeTalk(std::string_view username, bool talking) {
    return line({"TALK", username, talking ? "1" : "0"});
}
std::string encodeMic(bool muted) {
    return line({"MIC", muted ? "1" : "0"});
}
std::string encodePeerCarry(std::string_view username, std::string_view list) {
    return line({"PEERCARRY", username, list});
}
std::string encodePong(std::string_view token) {
    return line({"PONG", token});
}
std::string encodeError(std::string_view message) {
    return line({"ERR", message});
}

std::string_view trimAscii(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n\f\v";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string_view::npos) {
        return {};
    }
    return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

std::string normalizeName(std::string_view name) {
    std::string out(trimAscii(name));
    std::ranges::transform(out, out.begin(),
                           [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); });
    return out;
}

std::string sanitizeField(std::string_view value) {
    std::string out(value);
    std::ranges::replace_if(out, [](char c) { return c == '\t' || c == '\n' || c == '\r'; }, ' ');
    return out;
}

std::optional<FrameMap> FrameAssembler::feed(const GameMessage& msg) {
    if (const auto* begin = std::get_if<FrameBegin>(&msg)) {
        openSeq_ = begin->seq;
        pending_.clear();
        return std::nullopt;
    }
    if (const auto* spk = std::get_if<Speaker>(&msg)) {
        if (openSeq_) {
            pending_[normalizeName(spk->username)] = spk->params;
        }
        return std::nullopt;
    }
    if (const auto* end = std::get_if<FrameEnd>(&msg)) {
        const bool ok = openSeq_ && *openSeq_ == end->seq;
        openSeq_.reset();
        if (!ok) {
            pending_.clear();
            return std::nullopt;
        }
        return std::exchange(pending_, {});
    }
    return std::nullopt;
}

void FrameAssembler::reset() {
    openSeq_.reset();
    pending_.clear();
}

bool LineSplitter::append(std::string_view bytes, const std::function<void(std::string_view)>& onLine) {
    buffer_.append(bytes);
    std::size_t start = 0;
    for (;;) {
        const auto nl = buffer_.find('\n', start);
        if (nl == std::string::npos) {
            break;
        }
        onLine(std::string_view(buffer_).substr(start, nl - start));
        start = nl + 1;
    }
    buffer_.erase(0, start);
    return buffer_.size() <= kMaxLineBytes;
}

} // namespace knoxspeak::proto
