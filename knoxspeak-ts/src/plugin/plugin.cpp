#include "plugin/plugin.hpp"

#include "core/info_text.hpp"

#include <cstdint>
#include <format>
#include <plugin_definitions.h>
#include <ranges>
#include <teamspeak/public_errors.h>
#include <teamspeak/public_rare_definitions.h>
#include <utility>
#include <vector>

namespace knoxspeak {
namespace {

constexpr const char* kLogChannel = "knoxspeak";
constexpr auto kInfoRefreshInterval = std::chrono::seconds(1);

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

} // namespace

Plugin::Plugin(const TS3Functions& ts3) : ts3_(ts3) {
    ui_ = std::make_unique<UiDispatcher>(); // we're inside ts3plugin_init: the GUI thread
    if (!ui_->ok()) {
        log("could not create the UI dispatcher window; status panel and mesh messages are disabled", LogLevel_ERROR);
    }
    ipc_ = std::make_unique<IpcServer>(
        proto::kDefaultPort, IpcServer::Callbacks{
                                 .onConnected = [this] { ui_->post([this] { onGameConnected(); }); },
                                 .onLine = [this](std::string_view l) { onGameLine(l); },
                                 .onDisconnected = [this] { ui_->post([this] { onGameDisconnected(true); }); },
                                 .onLog = [this](std::string_view m) { log(m); },
                             });
    ipc_->start();
}

Plugin::~Plugin() {
    ipc_->stop(); // joins the ipc thread: nothing posts after this
    const auto leave = mesh::encodeLeave();
    for (uint64 schid : connectedServers()) {
        sendCommandNow(schid, leave, std::nullopt);
    }
    ui_.reset(); // drops any queued work, which would otherwise outlive `this`
}

void Plugin::setPluginId(std::string id) {
    {
        std::scoped_lock lock(stateMutex_);
        if (id.empty() || id == pluginId_) {
            return;
        }
        pluginId_ = std::move(id);
    }
    // enabled mid session
    // request state and re-announce
    for (uint64 schid : connectedServers()) {
        sendCommand(schid, mesh::encodeQuery());
    }
    broadcastStateEverywhere();
}

// ---------------------------------------------------------------------------
// ipc thread
// ---------------------------------------------------------------------------

void Plugin::onGameConnected() {
    locked([&] { gameConnected_ = true; });
    strict_ = true; // the game is running (even at the main menu): mute everyone it hasn't placed
    log("game connected");
    refreshSelfInfo();
}

void Plugin::onGameDisconnected(bool socketClosed) {
    bool hadUser = false;
    {
        std::scoped_lock lock(stateMutex_);
        assembler_.reset();
        lastFrame_.reset();
        hadUser = gameUsername_.has_value();
        gameUsername_.reset();
        serverLacksMod_ = false;
        if (socketClosed) {
            gameConnected_ = false; // on BYE the game is still open, at the main menu
        }
        ownCarry_.clear();          // peers drop our walkies when they see IDLE
        snapshot_.publish(nullptr); // pass-through
    }
    if (socketClosed) {
        strict_ = false; // the game is gone: plain TeamSpeak again (at the main menu, everyone stays muted)
    }
    handleSetSilenced(false); // never leave the mic deactivated after the game is gone
    log(socketClosed ? "game disconnected" : "game left the multiplayer session");
    if (hadUser) {
        broadcastStateEverywhere(); // -> IDLE
    }
    refreshSelfInfo();
}

void Plugin::onGameLine(std::string_view line) {
    // ipc thread: anything that needs TS goes through ui_.
    auto msg = proto::parseGameMessage(line);
    if (!msg) {
        return;
    }
    const bool handled =
        std::visit(overloaded{
                       [this](const proto::Hello& h) {
                           ui_->post([this, h] { handleHello(h); });
                           return true;
                       },
                       [this](const proto::Ping& p) {
                           ipc_->send(proto::encodePong(p.token));
                           return true;
                       },
                       [this](const proto::Bye&) {
                           ui_->post([this] { onGameDisconnected(false); });
                           return true;
                       },
                       [this](const proto::SetMicMuted& m) {
                           ui_->post([this, muted = m.muted] { handleSetMicMuted(muted); });
                           return true;
                       },
                       [this](const proto::SetSilenced& s) {
                           ui_->post([this, silenced = s.silenced] { handleSetSilenced(silenced); });
                           return true;
                       },
                       [this](const proto::Config& cfg) {
                           if (cfg.key == "radiofilter") {
                               radioFilter_ = cfg.value;
                           } else if (cfg.key == "radiostatic") {
                               radioStatic_ = cfg.value;
                           }
                           return true;
                       },
                       [this](const proto::ServerLacksMod& nomod) {
                           if (locked([&] { return std::exchange(serverLacksMod_, nomod.lacking) != nomod.lacking; })) {
                               if (nomod.lacking) {
                                   log("this server doesn't run knoxspeak (not in its mod list): positional voice is "
                                       "off",
                                       LogLevel_WARNING);
                               }
                               refreshSelfInfo(); // posts to the UI thread; no TS calls here
                           }
                           return true;
                       },
                       [this](const proto::Carry& carry) {
                           ui_->post([this, list = carry.list] { handleCarry(list); });
                           return true;
                       },
                       [](const auto&) { return false; }, // FRAME / SPK / END: the assembler below
                   },
                   *msg);
    if (handled) {
        return;
    }

    bool committed = false;
    {
        std::scoped_lock lock(stateMutex_);
        if (auto frame = assembler_.feed(*msg)) {
            lastFrame_ = std::move(*frame);
            lastFrameAt_ = Clock::now();
            rebuildSnapshotLocked();
            committed = true;
        }
    }
    if (committed) {
        refreshSelfInfoThrottled(); // picks up "positional voice active" once frames flow
    }
}

void Plugin::handleHello(const proto::Hello& hello) {
    if (hello.version != proto::kVersion) {
        const auto msg = std::format("protocol mismatch: game={} plugin={}", hello.version, proto::kVersion);
        ipc_->send(proto::encodeError(msg));
        log(msg, LogLevel_WARNING);
        return;
    }
    auto username = mesh::validUsername(hello.username);
    if (!username) {
        const auto msg = std::format("rejecting invalid username '{}'", hello.username);
        ipc_->send(proto::encodeError(msg));
        log(msg, LogLevel_WARNING);
        return;
    }
    log(std::format("HELLO from mod {} as '{}'", hello.modVersion, *username));
    ipc_->send(proto::encodeHello(KNOXSPEAK_VERSION));
    ipc_->send(proto::encodeTsStatus(!connectedServers().empty()));
    sendMicState();
    sendAllPeerCarry();
    strict_ = true;
    {
        std::scoped_lock lock(stateMutex_);
        serverLacksMod_ = false; // HELLO means our Lua runs, so the server has the mod
        if (gameUsername_ == username) {
            return; // reconnect with the same identity: nothing to re-announce
        }
        gameUsername_ = std::move(username);
    }
    broadcastStateEverywhere();
    refreshSelfInfo();
}

void Plugin::setSelfFlagEverywhere(std::size_t flag, bool on) {
    for (uint64 schid : connectedServers()) {
        if (ts3_.setClientSelfVariableAsInt(schid, flag, on ? 1 : 0) == ERROR_ok) {
            ts3_.flushClientSelfUpdates(schid, nullptr);
        }
    }
}

void Plugin::handleSetMicMuted(bool muted) {
    setSelfFlagEverywhere(CLIENT_INPUT_MUTED, muted);
    sendMicState(); // onClientSelfVariableUpdate confirms again once TS applies it
}

void Plugin::handleSetSilenced(bool silenced) {
    if (silenced == silencedByUs_) {
        return;
    }
    silencedByUs_ = silenced;
    setSelfFlagEverywhere(CLIENT_INPUT_DEACTIVATED, silenced);
    log(silenced ? "dead or invisible: microphone deactivated" : "microphone reactivated");
}

void Plugin::handleCarry(const std::string& list) {
    if (!locked([&] { return std::exchange(ownCarry_, list) != list; })) {
        return;
    }
    const auto cmd = mesh::encodeCarry(list);
    for (uint64 schid : connectedServers()) {
        sendCommand(schid, cmd);
    }
}

void Plugin::forwardPeerCarry(std::uint64_t key) {
    auto user = registry_.username(key);
    if (!user) {
        return; // we'll forward once they ANNOUNCE
    }
    const auto list = locked([&] {
        auto it = peerCarry_.find(key);
        return it == peerCarry_.end() ? std::string{} : it->second;
    });
    ipc_->send(proto::encodePeerCarry(*user, list));
}

void Plugin::sendAllPeerCarry() {
    const auto keys = locked([&] { return peerCarry_ | std::views::keys | std::ranges::to<std::vector>(); });
    for (auto k : keys) {
        forwardPeerCarry(k);
    }
}

bool Plugin::micMutedAnywhere() const {
    for (uint64 schid : connectedServers()) {
        int muted = 0;
        if (ts3_.getClientSelfVariableAsInt(schid, CLIENT_INPUT_MUTED, &muted) == ERROR_ok && muted) {
            return true;
        }
    }
    return false;
}

void Plugin::sendMicState() {
    ipc_->send(proto::encodeMic(micMutedAnywhere()));
}

void Plugin::onClientSelfVariableUpdate(uint64 /*schid*/, int flag) {
    if (flag == CLIENT_INPUT_MUTED) {
        ui_->post([this] { sendMicState(); }); // keeps the game's toggle in sync if muted from TS itself
    }
}

// ---------------------------------------------------------------------------
// TeamSpeak event thread
// ---------------------------------------------------------------------------

void Plugin::onConnectStatusChange(uint64 schid, int newStatus) {
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        registry_.removeServer(schid);
        sendCommand(schid, mesh::encodeQuery());
        sendCommand(schid, stateCommand());
        if (auto carry = ownCarryCommand()) {
            sendCommand(schid, *carry);
        }
        refreshSelfInfo();
        ui_->post([this] { sendMicState(); });
    } else if (newStatus == STATUS_DISCONNECTED) {
        registry_.removeServer(schid);
        rebuildSnapshot();
    }
    ipc_->send(proto::encodeTsStatus(!connectedServers().empty()));
}

void Plugin::onClientMove(uint64 schid, anyID clientId, uint64 newChannelId) {
    if (newChannelId != 0) {
        return; // joins/channel changes don't affect mesh mappings (announcements are server-wide)
    }
    registry_.remove(clientKey(schid, clientId)); // left the server
    rebuildSnapshot();
    ui_->post([this, schid] { ts3_.requestInfoUpdate(schid, PLUGIN_SERVER, 0); });
}

void Plugin::onPluginCommand(uint64 schid, std::string_view command, anyID invokerId) {
    auto msg = mesh::parse(command);
    if (!msg) {
        return; // not ours, or another protocol version
    }
    if (selfId(schid) == invokerId) {
        return; // our own broadcast echoed back
    }
    const auto key = clientKey(schid, invokerId);

    if (const auto* announce = std::get_if<mesh::Announce>(&*msg)) {
        const auto clashes = registry_.setPlaying(key, announce->username);
        const auto nick = clientNickname(schid, invokerId).value_or("?");
        log(std::format("TS client '{}' (#{}) is PZ user '{}'", nick, invokerId, announce->username));
        if (!clashes.empty()) {
            // possible impersonation or stale entry
            // im not gonna fix it right now lol
            log(std::format("{} other TS client(s) also claim PZ user '{}'", clashes.size(), announce->username),
                LogLevel_WARNING);
        }
        forwardPeerCarry(key); // walkies may have arrived before their name
    } else if (std::holds_alternative<mesh::Idle>(*msg) || std::holds_alternative<mesh::Leave>(*msg)) {
        locked([&] { peerCarry_.erase(key); });
        forwardPeerCarry(key); // empty list: the game drops their walkies (needs the name, so before remove)
        if (std::holds_alternative<mesh::Idle>(*msg)) {
            registry_.setIdle(key);
        } else {
            registry_.remove(key);
        }
    } else if (const auto* carry = std::get_if<mesh::CarryRadios>(&*msg)) {
        locked([&] { peerCarry_[key] = carry->list; });
        forwardPeerCarry(key);
        return; // mapping unchanged
    } else if (std::holds_alternative<mesh::Query>(*msg)) {
        sendCommand(schid, stateCommand(), invokerId);
        if (auto carryCmd = ownCarryCommand()) {
            sendCommand(schid, *carryCmd, invokerId);
        }
        return; // our state didn't change
    }
    rebuildSnapshot();
    refreshInfo(schid, invokerId);
}

void Plugin::onTalkStatusChange(uint64 schid, int status, anyID clientId) {
    // TALK carries the zomboid username so the game can set the correct indicator
    const bool self = selfId(schid) == clientId;
    std::optional<std::string> who = self ? currentUsername() : registry_.username(clientKey(schid, clientId));
    if (who) {
        ipc_->send(proto::encodeTalk(*who, status == STATUS_TALKING));
    }
}

// ---------------------------------------------------------------------------
// info panel (TS ui thread)
// ---------------------------------------------------------------------------

std::optional<std::string> Plugin::infoData(uint64 schid, uint64 id, int itemType) {
    bool live = false;
    info::SelfState self{.version = KNOXSPEAK_VERSION};
    {
        std::scoped_lock lock(stateMutex_);
        live = voiceLiveLocked();
        self.gameConnected = gameConnected_;
        self.username = gameUsername_;
        self.voiceLive = live;
        self.serverLacksMod = serverLacksMod_;
    }

    if (itemType == PLUGIN_SERVER) {
        int playing = self.username ? 1 : 0;
        int withPlugin = 1; // us
        for (const auto& e : registry_.entries()) {
            if (serverOf(e.key) == schid) {
                ++withPlugin;
                playing += e.username ? 1 : 0;
            }
        }
        return info::serverText(playing, withPlugin);
    }
    if (itemType != PLUGIN_CLIENT) {
        return std::nullopt;
    }

    const auto clientId = static_cast<anyID>(id);
    if (selfId(schid) == clientId) {
        return info::selfText(self);
    }

    info::PeerState peer;
    if (auto e = registry_.find(clientKey(schid, clientId))) {
        peer.kind = e->username ? info::PeerKind::Playing : info::PeerKind::Idle;
        peer.username = e->username.value_or("");
    }
    return info::peerText(peer);
}

void Plugin::refreshInfo(uint64 schid, anyID clientId) {
    ui_->post([this, schid, clientId] {
        ts3_.requestInfoUpdate(schid, PLUGIN_CLIENT, clientId);
        ts3_.requestInfoUpdate(schid, PLUGIN_SERVER, 0);
    });
}

void Plugin::refreshSelfInfo() {
    ui_->post([this] {
        for (uint64 schid : connectedServers()) {
            if (auto me = selfId(schid)) {
                ts3_.requestInfoUpdate(schid, PLUGIN_CLIENT, *me);
                ts3_.requestInfoUpdate(schid, PLUGIN_SERVER, 0);
            }
        }
    });
}

void Plugin::refreshSelfInfoThrottled() {
    {
        std::scoped_lock lock(stateMutex_);
        const auto now = Clock::now();
        if (now - lastInfoRefresh_ < kInfoRefreshInterval) {
            return;
        }
        lastInfoRefresh_ = now;
    }
    refreshSelfInfo();
}

// ---------------------------------------------------------------------------
// audio thread
// ---------------------------------------------------------------------------

void Plugin::onPlaybackVoiceData(uint64 schid, anyID clientId, short* samples, int sampleCount, int channels) {
    const auto snap = snapshot_.load();
    const auto key = clientKey(schid, clientId);
    auto* pcm = reinterpret_cast<std::int16_t*>(samples);
    if (!snap || Clock::now() - snap->committedAt > kSnapshotTtl) {
        if (strict_.load(std::memory_order_relaxed)) {
            dsp_.get(key).voice.process(pcm, sampleCount, channels, 0.0f, 0.0f);
        }
        return; // no game attached, dont run postprocessing
    }
    const auto it = snap->clients.find(key);
    const proto::VoiceParams* p = it != snap->clients.end() ? &it->second : nullptr;
    auto& d = dsp_.get(key);
    if (p && p->radio) {
        d.pickup.process(pcm, sampleCount, channels, 1.0f, p->pickupMuffle);
        d.radio.process(pcm, sampleCount, channels, p->gain, p->quality, radioFilter_.load(), radioStatic_.load());
        d.voice.process(pcm, sampleCount, channels, 1.0f, p->muffle);
        return;
    }
    d.voice.process(pcm, sampleCount, channels, p ? p->gain : 0.0f, p ? p->muffle : 0.0f); // unknown => muted
}

void Plugin::onPostProcessVoiceData(uint64 schid, anyID clientId, short* samples, int sampleCount, int channels,
                                    const unsigned int* channelSpeakerArray, unsigned int* channelFillMask) {
    const auto snap = snapshot_.load();
    if (!snap || Clock::now() - snap->committedAt > kSnapshotTtl) {
        return;
    }
    const auto key = clientKey(schid, clientId);
    const auto it = snap->clients.find(key);
    if (it == snap->clients.end()) {
        return; // already silenced in onPlaybackVoiceData
    }
    // TS labels stereo as FRONT_LEFT/RIGHT or, in headphones mode, HEADPHONES_LEFT/RIGHT
    const auto [left, right] = dsp::findStereoPair(channelSpeakerArray, channels);
    if (left < 0 || right < 0 || sampleCount <= 0) {
        return; // mono output device: nothing to pan
    }

    // downmix whatever TS filled to mono, then re-pan with constant power
    const auto [panStart, panEnd] = dsp_.get(key).pan.next(it->second.pan);
    *channelFillMask = dsp::panToStereo(reinterpret_cast<std::int16_t*>(samples), sampleCount, channels,
                                        *channelFillMask, left, right, panStart, panEnd);
}

// ---------------------------------------------------------------------------
// mesh utils
// ---------------------------------------------------------------------------

void Plugin::sendCommand(uint64 schid, std::string command, std::optional<anyID> target) {
    ui_->post([this, schid, command = std::move(command), target] { sendCommandNow(schid, command, target); });
}

void Plugin::sendCommandNow(uint64 schid, const std::string& command, std::optional<anyID> target) {
    const auto id = locked([&] { return pluginId_; });
    if (id.empty()) {
        return; // not registered yet; setPluginId() catches up
    }
    // broadcasts are server-wide
    // players in different TS channels still find each other
    const anyID targets[] = {target.value_or(0), 0};
    ts3_.sendPluginCommand(schid, id.c_str(), command.c_str(),
                           target ? PluginCommandTarget_CLIENT : PluginCommandTarget_SERVER, target ? targets : nullptr,
                           nullptr);
}

std::string Plugin::stateCommand() {
    auto me = currentUsername();
    return me ? mesh::encodeAnnounce(*me) : mesh::encodeIdle();
}

std::optional<std::string> Plugin::ownCarryCommand() {
    const auto carry = locked([&] { return ownCarry_; });
    return carry.empty() ? std::nullopt : std::optional(mesh::encodeCarry(carry));
}

void Plugin::broadcastStateEverywhere() {
    const auto cmd = stateCommand();
    for (uint64 schid : connectedServers()) {
        sendCommand(schid, cmd);
    }
}

// ---------------------------------------------------------------------------
// utils
// ---------------------------------------------------------------------------

void Plugin::rebuildSnapshot() {
    locked([&] { rebuildSnapshotLocked(); });
}

void Plugin::rebuildSnapshotLocked() {
    if (!lastFrame_) {
        snapshot_.publish(nullptr);
        return;
    }
    auto snap = std::make_shared<VoiceSnapshot>();
    snap->committedAt = lastFrameAt_;
    for (const auto& entry : registry_.entries()) {
        if (!entry.normalized) {
            continue; // IDLE: no game, nothing to map
        }
        if (auto it = lastFrame_->find(*entry.normalized); it != lastFrame_->end()) {
            snap->clients.emplace(entry.key, it->second);
        }
    }
    snapshot_.publish(std::move(snap));
}

bool Plugin::voiceLiveLocked() const {
    return lastFrame_.has_value() && Clock::now() - lastFrameAt_ <= kSnapshotTtl;
}

std::optional<std::string> Plugin::currentUsername() {
    return locked([&] { return gameUsername_; });
}

std::vector<uint64> Plugin::connectedServers() const {
    std::vector<uint64> out;
    uint64* handlers = nullptr;
    if (ts3_.getServerConnectionHandlerList(&handlers) != ERROR_ok || !handlers) {
        return out;
    }
    for (uint64* h = handlers; *h != 0; ++h) {
        int status = 0;
        if (ts3_.getConnectionStatus(*h, &status) == ERROR_ok && status == STATUS_CONNECTION_ESTABLISHED) {
            out.push_back(*h);
        }
    }
    ts3_.freeMemory(handlers);
    return out;
}

std::optional<anyID> Plugin::selfId(uint64 schid) const {
    anyID id = 0;
    if (ts3_.getClientID(schid, &id) != ERROR_ok || id == 0) {
        return std::nullopt;
    }
    return id;
}

std::optional<std::string> Plugin::clientNickname(uint64 schid, anyID clientId) const {
    char* name = nullptr;
    if (ts3_.getClientVariableAsString(schid, clientId, CLIENT_NICKNAME, &name) != ERROR_ok || !name) {
        return std::nullopt;
    }
    std::string out(name);
    ts3_.freeMemory(name);
    return out;
}

void Plugin::log(std::string_view msg, LogLevel level) const {
    const std::string s(msg);
    ts3_.logMessage(s.c_str(), level, kLogChannel, 0);
}

} // namespace knoxspeak
