#include "VoiceState.h"
#include "VoiceConfig.h"

CRITICAL_SECTION VoiceState::_lock;
bool VoiceState::_initialized = false;
VoiceConfig VoiceState::_config;
std::vector<VoiceSpeaker> VoiceState::_speakers;
std::vector<int> VoiceState::_mutedPlayers;

void VoiceState::SaveState()
{
    VoiceConfigLoader::SaveDefault(_config);
}

void VoiceState::Initialize()
{
    if (_initialized)
        return;

    InitializeCriticalSection(&_lock);
    _config = VoiceConfigLoader::LoadDefault();
    _initialized = true;
}

void VoiceState::Shutdown()
{
    if (!_initialized)
        return;

    EnterCriticalSection(&_lock);
    SaveState();
    LeaveCriticalSection(&_lock);

    DeleteCriticalSection(&_lock);
    _initialized = false;
}

VoiceConfig VoiceState::GetConfig()
{
    EnterCriticalSection(&_lock);
    VoiceConfig copy = _config;
    LeaveCriticalSection(&_lock);
    return copy;
}

void VoiceState::SetConfig(const VoiceConfig& config)
{
    EnterCriticalSection(&_lock);

    bool changed =
        _config.Enabled != config.Enabled ||
        _config.ServerIp != config.ServerIp ||
        _config.ServerPort != config.ServerPort ||
        _config.Channel != config.Channel ||
        _config.TalkMode != config.TalkMode ||
        _config.PushToTalkKey != config.PushToTalkKey ||
        _config.MasterVolume != config.MasterVolume ||
        _config.VoiceVolume != config.VoiceVolume ||
        _config.Muted != config.Muted ||
        _config.OpenPanelKey != config.OpenPanelKey ||
        _config.VoicePanelPos.X != config.VoicePanelPos.X ||
        _config.VoicePanelPos.Y != config.VoicePanelPos.Y ||
        _config.VoiceSpeakersPos.X != config.VoiceSpeakersPos.X ||
        _config.VoiceSpeakersPos.Y != config.VoiceSpeakersPos.Y ||
        _config.PanelVisible != config.PanelVisible ||
        _config.SpeakersVisible != config.SpeakersVisible ||
        _config.IntroShown != config.IntroShown;

    if (changed)
    {
        _config = config;
        SaveState();
    }

    LeaveCriticalSection(&_lock);
}

void VoiceState::SetChannel(VoiceChannelType channel)
{
    EnterCriticalSection(&_lock);
    if (_config.Channel != channel)
    {
        _config.Channel = channel;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

VoiceChannelType VoiceState::GetChannel()
{
    EnterCriticalSection(&_lock);
    VoiceChannelType channel = _config.Channel;
    LeaveCriticalSection(&_lock);
    return channel;
}

void VoiceState::SetTalkMode(VoiceTalkMode mode)
{
    EnterCriticalSection(&_lock);
    if (_config.TalkMode != mode)
    {
        _config.TalkMode = mode;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

VoiceTalkMode VoiceState::GetTalkMode()
{
    EnterCriticalSection(&_lock);
    VoiceTalkMode mode = _config.TalkMode;
    LeaveCriticalSection(&_lock);
    return mode;
}

void VoiceState::SetPushToTalkKey(int key)
{
    EnterCriticalSection(&_lock);
    if (key > 0 && _config.PushToTalkKey != key)
    {
        _config.PushToTalkKey = key;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

int VoiceState::GetPushToTalkKey()
{
    EnterCriticalSection(&_lock);
    int key = _config.PushToTalkKey;
    LeaveCriticalSection(&_lock);
    return key;
}

void VoiceState::SetOverlayKey(int key)
{
    EnterCriticalSection(&_lock);
    if (key > 0 && _config.OpenPanelKey != key)
    {
        _config.OpenPanelKey = key;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

int VoiceState::GetOverlayKey()
{
    EnterCriticalSection(&_lock);
    int key = _config.OpenPanelKey;
    LeaveCriticalSection(&_lock);
    return key;
}

VoiceWindowPos VoiceState::GetSettingsPos()
{
    EnterCriticalSection(&_lock);
    VoiceWindowPos pos = { _config.VoicePanelPos.X, _config.VoicePanelPos.Y };
    LeaveCriticalSection(&_lock);
    return pos;
}

VoiceWindowPos VoiceState::GetSpeakersPos()
{
    EnterCriticalSection(&_lock);
    VoiceWindowPos pos = { _config.VoiceSpeakersPos.X, _config.VoiceSpeakersPos.Y };
    LeaveCriticalSection(&_lock);
    return pos;
}

void VoiceState::SetSettingsPos(int x, int y)
{
    EnterCriticalSection(&_lock);
    if (_config.VoicePanelPos.X != x || _config.VoicePanelPos.Y != y)
    {
        _config.VoicePanelPos.X = x;
        _config.VoicePanelPos.Y = y;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

void VoiceState::SetSpeakersPos(int x, int y)
{
    EnterCriticalSection(&_lock);
    if (_config.VoiceSpeakersPos.X != x || _config.VoiceSpeakersPos.Y != y)
    {
        _config.VoiceSpeakersPos.X = x;
        _config.VoiceSpeakersPos.Y = y;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}


void VoiceState::SetPanelVisible(bool visible)
{
    EnterCriticalSection(&_lock);
    if (_config.PanelVisible != visible)
    {
        _config.PanelVisible = visible;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

bool VoiceState::IsPanelVisible()
{
    EnterCriticalSection(&_lock);
    bool visible = _config.PanelVisible;
    LeaveCriticalSection(&_lock);
    return visible;
}

void VoiceState::SetSpeakersVisible(bool visible)
{
    EnterCriticalSection(&_lock);
    if (_config.SpeakersVisible != visible)
    {
        _config.SpeakersVisible = visible;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

bool VoiceState::IsSpeakersVisible()
{
    EnterCriticalSection(&_lock);
    bool visible = _config.SpeakersVisible;
    LeaveCriticalSection(&_lock);
    return visible;
}

void VoiceState::SetIntroShown(bool shown)
{
    EnterCriticalSection(&_lock);
    if (_config.IntroShown != shown)
    {
        _config.IntroShown = shown;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

bool VoiceState::IsIntroShown()
{
    EnterCriticalSection(&_lock);
    bool shown = _config.IntroShown;
    LeaveCriticalSection(&_lock);
    return shown;
}

void VoiceState::SetMuted(bool muted)
{
    EnterCriticalSection(&_lock);
    if (_config.Muted != muted)
    {
        _config.Muted = muted;
        SaveState();
    }
    LeaveCriticalSection(&_lock);
}

bool VoiceState::IsMuted()
{
    EnterCriticalSection(&_lock);
    bool muted = _config.Muted;
    LeaveCriticalSection(&_lock);
    return muted;
}

void VoiceState::AddSpeaker(int objectId, const char* name)
{
    EnterCriticalSection(&_lock);

    for (auto& speaker : _speakers)
    {
        if (speaker.objectId == objectId)
        {
            if (name && name[0])
                speaker.name = name;

            speaker.lastUpdate = GetTickCount();
            LeaveCriticalSection(&_lock);
            return;
        }
    }

    VoiceSpeaker speaker;
    speaker.objectId = objectId;
    speaker.name = name ? name : "Player";
    speaker.lastUpdate = GetTickCount();

    _speakers.push_back(speaker);

    LeaveCriticalSection(&_lock);
}

void VoiceState::RemoveSpeaker(int objectId)
{
    EnterCriticalSection(&_lock);

    for (auto it = _speakers.begin(); it != _speakers.end();)
    {
        if (it->objectId == objectId)
            it = _speakers.erase(it);
        else
            ++it;
    }

    LeaveCriticalSection(&_lock);
}

std::vector<VoiceSpeaker> VoiceState::GetSpeakers()
{
    EnterCriticalSection(&_lock);
    std::vector<VoiceSpeaker> copy = _speakers;
    LeaveCriticalSection(&_lock);
    return copy;
}

void VoiceState::CleanupSpeakers()
{
    EnterCriticalSection(&_lock);

    DWORD now = GetTickCount();

    for (auto it = _speakers.begin(); it != _speakers.end();)
    {
        if (now - it->lastUpdate > 3000)
            it = _speakers.erase(it);
        else
            ++it;
    }

    LeaveCriticalSection(&_lock);
}

void VoiceState::SetPlayerMuted(int objectId, bool muted)
{
    EnterCriticalSection(&_lock);

    for (auto it = _mutedPlayers.begin(); it != _mutedPlayers.end(); ++it)
    {
        if (*it == objectId)
        {
            if (!muted)
                _mutedPlayers.erase(it);

            LeaveCriticalSection(&_lock);
            return;
        }
    }

    if (muted)
        _mutedPlayers.push_back(objectId);

    LeaveCriticalSection(&_lock);
}

bool VoiceState::IsPlayerMuted(int objectId)
{
    EnterCriticalSection(&_lock);

    bool muted = false;
    for (int id : _mutedPlayers)
    {
        if (id == objectId)
        {
            muted = true;
            break;
        }
    }

    LeaveCriticalSection(&_lock);
    return muted;
}

std::vector<int> VoiceState::GetMutedPlayers()
{
    EnterCriticalSection(&_lock);
    std::vector<int> copy = _mutedPlayers;
    LeaveCriticalSection(&_lock);
    return copy;
}
