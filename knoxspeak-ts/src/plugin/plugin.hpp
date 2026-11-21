#pragma once

#include "core/dsp.hpp"
#include "core/mesh.hpp"
#include "core/protocol.hpp"

#include "plugin/client_registry.hpp"
#include "plugin/ipc_server.hpp"
#include "plugin/ui_dispatcher.hpp"
#include "plugin/voice_snapshot.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <teamspeak/public_definitions.h>
#include <ts3_functions.h>
#include <unordered_map>
#include <vector>

namespace knoxspeak {

class Plugin {
  public:
    explicit Plugin(const TS3Functions& ts3);
    ~Plugin();

    // TS hands us the ID that sendPluginCommand needs
    void setPluginId(std::string id);

    // --- TeamSpeak events (TS client thread) ---
    void onConnectStatusChange(uint64 schid, int newStatus);
    void onClientMove(uint64 schid, anyID clientId, uint64 newChannelId);
    void onTalkStatusChange(uint64 schid, int status, anyID clientId);
    void onPluginCommand(uint64 schid, std::string_view command, anyID invokerId);
    void onClientSelfVariableUpdate(uint64 schid, int flag); // our own mic mute toggled (by us or in TS)

    // --- Info panel (TS UI thread) ---
    [[nodiscard]] std::optional<std::string> infoData(uint64 schid, uint64 id, int itemType);

    // --- Audio (TS audio thread) ---
    void onPlaybackVoiceData(uint64 schid, anyID clientId, short* samples, int sampleCount, int channels);
    void onPostProcessVoiceData(uint64 schid, anyID clientId, short* samples, int sampleCount, int channels,
                                const unsigned int* channelSpeakerArray, unsigned int* channelFillMask);

  private:
    // IPC thread
    void onGameConnected();
    void onGameLine(std::string_view line);
    // socketClosed=false: BYE (left the multiplayer game, still at the main menu)
    // true: game quit or crashed
    void onGameDisconnected(bool socketClosed);
    void handleHello(const proto::Hello& hello);
    void handleSetMicMuted(bool muted);              // GUI thread
    void handleSetSilenced(bool silenced);           // GUI thread: dead/invisible -> CLIENT_INPUT_DEACTIVATED
    void setSelfFlagEverywhere(std::size_t flag,
                               bool on);       // GUI thread: a self variable on every connected server
    void handleCarry(const std::string& list); // GUI thread: our walkies -> mesh
    void forwardPeerCarry(std::uint64_t key);  // their walkies -> our game (if we know their username)
    void sendAllPeerCarry();
    void sendMicState(); // GUI thread: MIC <0|1> to the game
    [[nodiscard]] bool micMutedAnywhere() const;

    // plugin <-> plugisn mesh
    void sendCommand(uint64 schid, std::string command, std::optional<anyID> target = std::nullopt);
    void sendCommandNow(uint64 schid, const std::string& command,
                        std::optional<anyID> target);           // GUI thread only
    [[nodiscard]] std::string stateCommand();                   // ANNOUNCE <me> or IDLE
    [[nodiscard]] std::optional<std::string> ownCarryCommand(); // CARRY <our walkies>, if we carry any
    void broadcastStateEverywhere();


    void refreshInfo(uint64 schid, anyID clientId);
    void refreshSelfInfo();
    void refreshSelfInfoThrottled();

    // runs f() with stateMutex_ held and returns its result
    // never call TeamSpeak inside f
    template <typename F> auto locked(F&& f) {
        std::scoped_lock lock(stateMutex_);
        return f();
    }

    void rebuildSnapshot();                     // takes stateMutex_
    void rebuildSnapshotLocked();               // caller holds stateMutex_
    [[nodiscard]] bool voiceLiveLocked() const; // caller holds stateMutex_
    [[nodiscard]] std::optional<std::string> currentUsername();
    [[nodiscard]] std::vector<uint64> connectedServers() const;
    [[nodiscard]] std::optional<anyID> selfId(uint64 schid) const;
    [[nodiscard]] std::optional<std::string> clientNickname(uint64 schid, anyID clientId) const;

    void log(std::string_view msg, LogLevel level = LogLevel_INFO) const;

    TS3Functions ts3_;

    ClientRegistry registry_;
    SnapshotSlot snapshot_;

    // guarded by stateMutex_
    std::mutex stateMutex_;
    std::string pluginId_;
    proto::FrameAssembler assembler_;
    std::optional<proto::FrameMap> lastFrame_;
    Clock::time_point lastFrameAt_{};
    std::optional<std::string> gameUsername_;
    bool gameConnected_ = false;              // socket attached (may be at the main menu, before HELLO)
    bool serverLacksMod_ = false;             // NOMOD 1: in a world whose server doesn't run knoxspeak
    std::string ownCarry_;                                      // our carried walkies (CARRY from the game)
    bool silencedByUs_ = false;                                 // we deactivated the mic (GUI thread only)
    std::unordered_map<std::uint64_t, std::string> peerCarry_;  // their carried walkies (mesh CARRY)

    std::atomic<bool> radioFilter_{true};
    std::atomic<bool> radioStatic_{true};
    Clock::time_point lastInfoRefresh_{};

    // strict mode
    // while game is running and connected, nobody can be heard
    std::atomic<bool> strict_{false};

    // audio thread DSP state
    struct ClientDsp {
        dsp::VoiceProcessor voice;
        dsp::VoiceProcessor pickup;
        dsp::RadioProcessor radio;
        dsp::PanSmoother pan;
    };
    static constexpr std::size_t kDspSlots = 128;
    dsp::StatePool<ClientDsp, kDspSlots> dsp_;

    std::unique_ptr<UiDispatcher> ui_; // created first (on the GUI thread), destroyed last
    std::unique_ptr<IpcServer> ipc_;
};

} // namespace knoxspeak
