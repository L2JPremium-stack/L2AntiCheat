#include "VoiceConfig.h"

#include <windows.h>
#include <shlobj.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#pragma comment(lib, "shell32.lib")

static int ReadIntAuto(const char* section, const char* key, const char* def, const char* fileName)
{
    char buffer[64];
    GetPrivateProfileStringA(section, key, def, buffer, sizeof(buffer), fileName);
    return (int)strtol(buffer, nullptr, 0);
}

static bool ReadBoolAuto(const char* section, const char* key, const char* def, const char* fileName)
{
    char buffer[32];
    GetPrivateProfileStringA(section, key, def, buffer, sizeof(buffer), fileName);

    return _stricmp(buffer, "true") == 0 ||
        _stricmp(buffer, "1") == 0 ||
        _stricmp(buffer, "yes") == 0 ||
        _stricmp(buffer, "on") == 0;
}

static VoiceChannelType ReadChannel(const char* fileName)
{
    char buffer[32];
    GetPrivateProfileStringA("Voice", "Channel", "GLOBAL", buffer, sizeof(buffer), fileName);

    if (_stricmp(buffer, "PARTY") == 0)
        return VoiceChannelType::Party;

    return VoiceChannelType::Global;
}

static VoiceTalkMode ReadTalkMode(const char* fileName)
{
    char buffer[32];
    GetPrivateProfileStringA("Voice", "TalkMode", "PTT", buffer, sizeof(buffer), fileName);

    if (_stricmp(buffer, "OPEN") == 0 ||
        _stricmp(buffer, "VOICE") == 0 ||
        _stricmp(buffer, "FREE") == 0)
    {
        return VoiceTalkMode::VoiceActivation;
    }

    return VoiceTalkMode::PushToTalk;
}

static void ClampVoiceConfig(VoiceConfig& config)
{
    if (config.ServerPort <= 0 || config.ServerPort > 65535)
        config.ServerPort = 9010;

    if (config.PushToTalkKey <= 0)
        config.PushToTalkKey = 113;

    if (config.OpenPanelKey <= 0)
        config.OpenPanelKey = 81;

    config.SampleRate = 48000;
    config.Channels = 1;
    config.BitsPerSample = 16;

    if (config.MasterVolume < 0) config.MasterVolume = 0;
    if (config.MasterVolume > 100) config.MasterVolume = 100;
    if (config.VoiceVolume < 0) config.VoiceVolume = 0;
    if (config.VoiceVolume > 100) config.VoiceVolume = 100;
}

std::string VoiceConfigLoader::GetConfigPath()
{
    char appData[MAX_PATH];

    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
    {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);

        char* lastSlash = strrchr(path, '\\');
        if (lastSlash)
            *(lastSlash + 1) = '\0';

        strcat_s(path, MAX_PATH, "voice.ini");
        return path;
    }

    char dir[MAX_PATH];
    sprintf_s(dir, sizeof(dir), "%s\\LineageII", appData);
    CreateDirectoryA(dir, nullptr);

    char file[MAX_PATH];
    sprintf_s(file, sizeof(file), "%s\\voice.ini", dir);
    return file;
}

VoiceConfig VoiceConfigLoader::LoadDefault()
{
    std::string path = GetConfigPath();
    return Load(path.c_str());
}

VoiceConfig VoiceConfigLoader::Load(const char* fileName)
{
    VoiceConfig config;
    char buffer[256];

    config.Enabled = ReadBoolAuto("Voice", "Enabled", "1", fileName);

    GetPrivateProfileStringA("Voice", "ServerIp", "127.0.0.1", buffer, sizeof(buffer), fileName);
    config.ServerIp = buffer;

    config.ServerPort = ReadIntAuto("Voice", "ServerPort", "9010", fileName);
    config.PushToTalkKey = ReadIntAuto("Voice", "PushToTalkKey", "113", fileName);
    config.TalkMode = ReadTalkMode(fileName);
    config.Channel = ReadChannel(fileName);

    config.SampleRate = ReadIntAuto("Voice", "SampleRate", "48000", fileName);
    config.Channels = ReadIntAuto("Voice", "Channels", "1", fileName);
    config.BitsPerSample = ReadIntAuto("Voice", "BitsPerSample", "16", fileName);

    config.MasterVolume = ReadIntAuto("Voice", "MasterVolume", "100", fileName);
    config.VoiceVolume = ReadIntAuto("Voice", "VoiceVolume", "100", fileName);
    config.Muted = ReadBoolAuto("Voice", "Muted", "0", fileName);

    config.OverlayPos.X = ReadIntAuto("Overlay", "X", "902", fileName);
    config.OverlayPos.Y = ReadIntAuto("Overlay", "Y", "174", fileName);

    config.AddAccountPos.X = ReadIntAuto("AddAccount", "X", "411", fileName);
    config.AddAccountPos.Y = ReadIntAuto("AddAccount", "Y", "207", fileName);

    config.VoicePanelPos.X = ReadIntAuto("VoicePanel", "X", "64", fileName);
    config.VoicePanelPos.Y = ReadIntAuto("VoicePanel", "Y", "137", fileName);

    config.VoiceSpeakersPos.X = ReadIntAuto("VoiceSpeakers", "X", "902", fileName);
    config.VoiceSpeakersPos.Y = ReadIntAuto("VoiceSpeakers", "Y", "390", fileName);

    config.OpenPanelKey = ReadIntAuto("VoiceKeys", "OpenPanel", "81", fileName);

    config.PanelVisible = ReadBoolAuto("VoiceUI", "PanelVisible", "1", fileName);
    config.SpeakersVisible = ReadBoolAuto("VoiceUI", "SpeakersVisible", "1", fileName);
    config.IntroShown = ReadBoolAuto("VoiceUI", "IntroShown", "0", fileName);

    int legacyPtt = ReadIntAuto("VoiceKeys", "PushToTalk", "0", fileName);
    if (legacyPtt > 0)
        config.PushToTalkKey = legacyPtt;

    ClampVoiceConfig(config);
    Save(fileName, config);

    return config;
}

bool VoiceConfigLoader::SaveDefault(const VoiceConfig& config)
{
    std::string path = GetConfigPath();
    return Save(path.c_str(), config);
}

bool VoiceConfigLoader::Save(const char* fileName, const VoiceConfig& input)
{
    VoiceConfig config = input;
    ClampVoiceConfig(config);

    int openPanelModifiers = ReadIntAuto("VoiceKeys", "OpenPanelModifiers", "0", fileName);
    int voicePanelWidth = ReadIntAuto("VoicePanel", "Width", "306", fileName);
    int voicePanelHeight = ReadIntAuto("VoicePanel", "Height", "326", fileName);

    if (voicePanelWidth < 296) voicePanelWidth = 296;
    if (voicePanelHeight < 312) voicePanelHeight = 312;

    FILE* fp = nullptr;
    if (fopen_s(&fp, fileName, "w") != 0 || fp == nullptr)
        return false;

    fprintf(fp, "[Voice]\n");
    fprintf(fp, "Enabled=%d\n", config.Enabled ? 1 : 0);
    fprintf(fp, "ServerIp=%s\n", config.ServerIp.c_str());
    fprintf(fp, "ServerPort=%d\n", config.ServerPort);
    fprintf(fp, "PushToTalkKey=%d\n", config.PushToTalkKey);
    fprintf(fp, "TalkMode=%s\n", config.TalkMode == VoiceTalkMode::VoiceActivation ? "OPEN" : "PTT");
    fprintf(fp, "Channel=%s\n", config.Channel == VoiceChannelType::Global ? "GLOBAL" : "PARTY");
    fprintf(fp, "SampleRate=48000\n");
    fprintf(fp, "Channels=1\n");
    fprintf(fp, "BitsPerSample=16\n");
    fprintf(fp, "MasterVolume=%d\n", config.MasterVolume);
    fprintf(fp, "VoiceVolume=%d\n", config.VoiceVolume);
    fprintf(fp, "Muted=%d\n\n", config.Muted ? 1 : 0);

    fprintf(fp, "[Overlay]\n");
    fprintf(fp, "X=%d\n", config.OverlayPos.X);
    fprintf(fp, "Y=%d\n\n", config.OverlayPos.Y);

    fprintf(fp, "[AddAccount]\n");
    fprintf(fp, "X=%d\n", config.AddAccountPos.X);
    fprintf(fp, "Y=%d\n\n", config.AddAccountPos.Y);

    fprintf(fp, "[VoicePanel]\n");
    fprintf(fp, "X=%d\n", config.VoicePanelPos.X);
    fprintf(fp, "Y=%d\n", config.VoicePanelPos.Y);
    fprintf(fp, "Width=%d\n", voicePanelWidth);
    fprintf(fp, "Height=%d\n\n", voicePanelHeight);

    fprintf(fp, "[VoiceSpeakers]\n");
    fprintf(fp, "X=%d\n", config.VoiceSpeakersPos.X);
    fprintf(fp, "Y=%d\n\n", config.VoiceSpeakersPos.Y);

    fprintf(fp, "[VoiceKeys]\n");
    fprintf(fp, "OpenPanel=%d\n", config.OpenPanelKey);
    fprintf(fp, "OpenPanelModifiers=%d\n\n", openPanelModifiers);

    fprintf(fp, "[VoiceUI]\n");
    fprintf(fp, "PanelVisible=%d\n", config.PanelVisible ? 1 : 0);
    fprintf(fp, "SpeakersVisible=%d\n", config.SpeakersVisible ? 1 : 0);
    fprintf(fp, "IntroShown=%d\n", config.IntroShown ? 1 : 0);

    fclose(fp);
    return true;
}