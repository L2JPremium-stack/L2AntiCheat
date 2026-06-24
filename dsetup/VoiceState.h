#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "VoiceConfig.h"

struct VoiceSpeaker
{
    int objectId;
    std::string name;
    DWORD lastUpdate;
};

struct VoiceWindowPos
{
    int x;
    int y;
};

class VoiceState
{
public:
    static void Initialize();
    static void Shutdown();

    static VoiceConfig GetConfig();
    static void SetConfig(const VoiceConfig& config);

    static void SetChannel(VoiceChannelType channel);
    static VoiceChannelType GetChannel();

    static void SetTalkMode(VoiceTalkMode mode);
    static VoiceTalkMode GetTalkMode();

    static void SetPushToTalkKey(int key);
    static int GetPushToTalkKey();

    static void SetOverlayKey(int key);
    static int GetOverlayKey();

    static VoiceWindowPos GetSettingsPos();
    static VoiceWindowPos GetSpeakersPos();
    static void SetSettingsPos(int x, int y);
    static void SetSpeakersPos(int x, int y);

    static void SetPanelVisible(bool visible);
    static bool IsPanelVisible();
    static void SetSpeakersVisible(bool visible);
    static bool IsSpeakersVisible();
    static void SetIntroShown(bool shown);
    static bool IsIntroShown();

    static void SetMuted(bool muted);
    static bool IsMuted();

    static void AddSpeaker(int objectId, const char* name);
    static void RemoveSpeaker(int objectId);
    static std::vector<VoiceSpeaker> GetSpeakers();
    static void CleanupSpeakers();

    static void SetPlayerMuted(int objectId, bool muted);
    static bool IsPlayerMuted(int objectId);
    static std::vector<int> GetMutedPlayers();

private:
    static void SaveState();

    static CRITICAL_SECTION _lock;
    static bool _initialized;
    static VoiceConfig _config;
    static std::vector<VoiceSpeaker> _speakers;
    static std::vector<int> _mutedPlayers;
};
