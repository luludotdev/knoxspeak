#include "plugin/plugin.hpp"

#include <cstring>
#include <memory>
#include <plugin_definitions.h>
#include <string>

#define KNOXSPEAK_EXPORT extern "C" __declspec(dllexport)

namespace {
constexpr int kPluginApiVersion = 26;
TS3Functions g_ts3{};
std::string g_pluginId; // TS calls registerPluginID before init
std::unique_ptr<knoxspeak::Plugin> g_plugin;
} // namespace

// ---- required ----
KNOXSPEAK_EXPORT const char* ts3plugin_name() {
    return "knoxspeak";
}
KNOXSPEAK_EXPORT const char* ts3plugin_version() {
    return KNOXSPEAK_VERSION;
}
KNOXSPEAK_EXPORT int ts3plugin_apiVersion() {
    return kPluginApiVersion;
}
KNOXSPEAK_EXPORT const char* ts3plugin_author() {
    return KNOXSPEAK_AUTHOR;
}
KNOXSPEAK_EXPORT const char* ts3plugin_description() {
    return KNOXSPEAK_DESCRIPTION;
}
KNOXSPEAK_EXPORT void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) {
    g_ts3 = funcs;
}

KNOXSPEAK_EXPORT int ts3plugin_init() {
    g_plugin = std::make_unique<knoxspeak::Plugin>(g_ts3);
    g_plugin->setPluginId(g_pluginId);
    return 0; // 0 = success, 1 = failure, -2 = failure without "failed to load" warning
}

KNOXSPEAK_EXPORT void ts3plugin_shutdown() {
    g_plugin.reset();
}

// ---- optional ----
KNOXSPEAK_EXPORT void ts3plugin_registerPluginID(const char* id) {
    g_pluginId = id ? id : "";
    if (g_plugin) {
        g_plugin->setPluginId(g_pluginId);
    }
}
KNOXSPEAK_EXPORT int ts3plugin_offersConfigure() {
    return PLUGIN_OFFERS_NO_CONFIGURE;
}
KNOXSPEAK_EXPORT int ts3plugin_requestAutoload() {
    return 0;
}
KNOXSPEAK_EXPORT void ts3plugin_freeMemory(void* data) {
    free(data);
}

// ---- info panel
KNOXSPEAK_EXPORT const char* ts3plugin_infoTitle() {
    return "knoxspeak status";
}

KNOXSPEAK_EXPORT void ts3plugin_infoData(uint64 schid, uint64 id, enum PluginItemType type, char** data) {
    *data = nullptr; // nullptr = no row
    if (!g_plugin) {
        return;
    }
    if (auto text = g_plugin->infoData(schid, id, type)) {
        *data = static_cast<char*>(malloc(text->size() + 1));
        if (*data) {
            memcpy(*data, text->c_str(), text->size() + 1);
        }
    }
}

// ---- events ----
KNOXSPEAK_EXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 schid, int newStatus, unsigned int /*errorNumber*/) {
    if (g_plugin)
        g_plugin->onConnectStatusChange(schid, newStatus);
}

KNOXSPEAK_EXPORT void ts3plugin_onClientMoveEvent(uint64 schid, anyID clientID, uint64 /*oldChannelID*/,
                                                  uint64 newChannelID, int /*visibility*/,
                                                  const char* /*moveMessage*/) {
    if (g_plugin)
        g_plugin->onClientMove(schid, clientID, newChannelID);
}

KNOXSPEAK_EXPORT void ts3plugin_onClientMoveTimeoutEvent(uint64 schid, anyID clientID, uint64 /*oldChannelID*/,
                                                         uint64 newChannelID, int /*visibility*/,
                                                         const char* /*timeoutMessage*/) {
    if (g_plugin)
        g_plugin->onClientMove(schid, clientID, newChannelID);
}

KNOXSPEAK_EXPORT void ts3plugin_onClientMoveMovedEvent(uint64 schid, anyID clientID, uint64 /*oldChannelID*/,
                                                       uint64 newChannelID, int /*visibility*/, anyID /*moverID*/,
                                                       const char* /*moverName*/, const char* /*moverUniqueIdentifier*/,
                                                       const char* /*moveMessage*/) {
    if (g_plugin)
        g_plugin->onClientMove(schid, clientID, newChannelID);
}

KNOXSPEAK_EXPORT void ts3plugin_onClientKickFromServerEvent(uint64 schid, anyID clientID, uint64 /*oldChannelID*/,
                                                            uint64 /*newChannelID*/, int /*visibility*/,
                                                            anyID /*kickerID*/, const char* /*kickerName*/,
                                                            const char* /*kickerUniqueIdentifier*/,
                                                            const char* /*kickMessage*/) {
    if (g_plugin)
        g_plugin->onClientMove(schid, clientID, 0);
}

KNOXSPEAK_EXPORT void ts3plugin_onPluginCommandEvent(uint64 schid, const char* /*pluginName*/,
                                                     const char* pluginCommand, anyID invokerClientID,
                                                     const char* /*invokerName*/,
                                                     const char* /*invokerUniqueIdentity*/) {
    if (g_plugin && pluginCommand)
        g_plugin->onPluginCommand(schid, pluginCommand, invokerClientID);
}

KNOXSPEAK_EXPORT void ts3plugin_onClientSelfVariableUpdateEvent(uint64 schid, int flag, const char* /*oldValue*/,
                                                                const char* /*newValue*/) {
    if (g_plugin)
        g_plugin->onClientSelfVariableUpdate(schid, flag);
}

KNOXSPEAK_EXPORT void ts3plugin_onTalkStatusChangeEvent(uint64 schid, int status, int /*isReceivedWhisper*/,
                                                        anyID clientID) {
    if (g_plugin)
        g_plugin->onTalkStatusChange(schid, status, clientID);
}

// ---- audio ----
KNOXSPEAK_EXPORT void ts3plugin_onEditPlaybackVoiceDataEvent(uint64 schid, anyID clientID, short* samples,
                                                             int sampleCount, int channels) {
    if (g_plugin)
        g_plugin->onPlaybackVoiceData(schid, clientID, samples, sampleCount, channels);
}

KNOXSPEAK_EXPORT void ts3plugin_onEditPostProcessVoiceDataEvent(uint64 schid, anyID clientID, short* samples,
                                                                int sampleCount, int channels,
                                                                const unsigned int* channelSpeakerArray,
                                                                unsigned int* channelFillMask) {
    if (g_plugin) {
        g_plugin->onPostProcessVoiceData(schid, clientID, samples, sampleCount, channels, channelSpeakerArray,
                                         channelFillMask);
    }
}
