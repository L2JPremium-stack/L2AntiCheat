#pragma once

#include <string>

enum class VoiceChannelType
{
    Global = 0,
    Party = 1
};

enum class VoiceTalkMode
{
    PushToTalk = 0,
    VoiceActivation = 1
};

struct VoiceWindowPosConfig
{
    int X = 0;
    int Y = 0;
};

struct VoiceConfig
{
    bool Enabled = false;
    std::string ServerIp = "127.0.0.1";
    int ServerPort = 9010;

    // Principal: a tecla de falar continua dentro da seção [Voice].
    int PushToTalkKey = 113; // F2 padrão do seu exemplo.
    VoiceTalkMode TalkMode = VoiceTalkMode::PushToTalk;
    VoiceChannelType Channel = VoiceChannelType::Global;

    int SampleRate = 48000;
    int Channels = 1;
    int BitsPerSample = 16;

    int MasterVolume = 100;
    int VoiceVolume = 100;
    bool Muted = false;

    // Tudo abaixo também fica no mesmo voice.ini.
    VoiceWindowPosConfig OverlayPos = { 902, 174 };
    VoiceWindowPosConfig AddAccountPos = { 411, 207 };
    VoiceWindowPosConfig VoicePanelPos = { 64, 137 };
    VoiceWindowPosConfig VoiceSpeakersPos = { 902, 390 };

    // [VoiceKeys]
    int OpenPanelKey = 81; // Q padrão do seu exemplo.

    // [VoiceUI]
    bool PanelVisible = true;
    bool SpeakersVisible = true;
    bool IntroShown = false;
};

class VoiceConfigLoader
{
public:
    static std::string GetConfigPath();
    static VoiceConfig Load(const char* fileName);
    static VoiceConfig LoadDefault();
    static bool Save(const char* fileName, const VoiceConfig& config);
    static bool SaveDefault(const VoiceConfig& config);
};
