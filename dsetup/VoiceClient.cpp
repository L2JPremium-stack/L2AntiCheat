#include "VoiceClient.h"
#include "VoiceLog.h"
#include "VoiceOverlay.h"

#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <mutex>

extern char g_FinalPayload[512];

namespace
{
    struct KnownSpeaker
    {
        std::string name;
        DWORD lastSeen;
    };

    static const size_t kMaxKnownSpeakers = 256;
    static const DWORD kKnownSpeakerTtlMs = 10 * 60 * 1000;

    static std::map<int, KnownSpeaker> g_knownSpeakerNames;
    static std::map<int, bool> g_playerMuteCache;
    static std::mutex g_cacheMutex;

    static bool g_channelSent = false;
    static VoiceChannelType g_lastChannelSent = VoiceChannelType::Global;

    static bool IsClientForeground()
    {
        HWND foreground = GetForegroundWindow();
        if (!foreground)
            return false;

        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(foreground, &foregroundPid);
        return foregroundPid == GetCurrentProcessId();
    }

    static void ResetNetworkCache()
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_channelSent = false;
        g_lastChannelSent = VoiceChannelType::Global;
        g_knownSpeakerNames.clear();
        g_playerMuteCache.clear();
    }

    static void PruneKnownSpeakers(DWORD now)
    {
        for (std::map<int, KnownSpeaker>::iterator it =
            g_knownSpeakerNames.begin();
            it != g_knownSpeakerNames.end();)
        {
            if (now - it->second.lastSeen > kKnownSpeakerTtlMs)
                it = g_knownSpeakerNames.erase(it);
            else
                ++it;
        }

        while (g_knownSpeakerNames.size() >= kMaxKnownSpeakers)
        {
            std::map<int, KnownSpeaker>::iterator oldest =
                g_knownSpeakerNames.begin();

            for (std::map<int, KnownSpeaker>::iterator it =
                g_knownSpeakerNames.begin();
                it != g_knownSpeakerNames.end();
                ++it)
            {
                if (it->second.lastSeen < oldest->second.lastSeen)
                    oldest = it;
            }

            g_knownSpeakerNames.erase(oldest);
        }
    }

    static void RememberSpeakerName(int objectId, const char* name)
    {
        if (objectId <= 0)
            return;

        std::lock_guard<std::mutex> lock(g_cacheMutex);
        const DWORD now = GetTickCount();
        PruneKnownSpeakers(now);

        if (name != NULL && name[0] != '\0' && strcmp(name, "Player") != 0)
            g_knownSpeakerNames[objectId] = { name, now };
        else if (g_knownSpeakerNames.find(objectId) == g_knownSpeakerNames.end())
            g_knownSpeakerNames[objectId] = { "Player", now };
        else
            g_knownSpeakerNames[objectId].lastSeen = now;
    }

    static std::string GetSpeakerName(int objectId)
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);

        std::map<int, KnownSpeaker>::iterator it =
            g_knownSpeakerNames.find(objectId);
        if (it != g_knownSpeakerNames.end() && !it->second.name.empty())
        {
            it->second.lastSeen = GetTickCount();
            return it->second.name;
        }

        return "Player";
    }

    static bool IsSamePlayerMuteState(int objectId, bool muted)
    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);

        std::map<int, bool>::const_iterator it = g_playerMuteCache.find(objectId);
        if (it != g_playerMuteCache.end() && it->second == muted)
            return true;

        g_playerMuteCache[objectId] = muted;
        return false;
    }
}

VoiceClient::VoiceClient()
{
    _socket = INVALID_SOCKET;
    ZeroMemory(&_serverAddr, sizeof(_serverAddr));
    _authenticated = false;
    _started = false;
    _running = false;
    _talking = false;
    _txSequence = 0;
    _lastTalkRefresh = 0;
}

VoiceClient::~VoiceClient()
{
    Stop();
}

bool VoiceClient::Start(const VoiceConfig& config)
{
    if (_started || _running)
        return true;

    _config = config;
    ResetNetworkCache();

    if (!_config.Enabled)
        return false;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return false;

    _socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (_socket == INVALID_SOCKET)
    {
        WSACleanup();
        return false;
    }

    _serverAddr.sin_family = AF_INET;
    _serverAddr.sin_port = htons((u_short)_config.ServerPort);

    if (inet_pton(AF_INET, _config.ServerIp.c_str(), &_serverAddr.sin_addr) <= 0)
    {
        Stop();
        return false;
    }

    _started = true;
    _authenticated = false;
    _talking = false;

    SendPing();

    _running = true;

    _authThread = std::thread(&VoiceClient::AuthLoop, this);
    _pttThread = std::thread(&VoiceClient::PushToTalkLoop, this);

    if (!_config.Muted)
        _capture.Start(this, _config);

    _playback.Start(_config);
    _receiveThread = std::thread(&VoiceClient::ReceiveLoop, this);

    return true;
}

void VoiceClient::SendConfiguredChannelOnce()
{
    if (!_started || !_running || !_authenticated)
        return;

    VoiceChannelType current = _config.Channel;

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);

        if (g_channelSent && g_lastChannelSent == current)
            return;

        g_channelSent = true;
        g_lastChannelSent = current;
    }

    SendMessageToServer(current == VoiceChannelType::Global ?
        "VOICE_CHANNEL:GLOBAL" : "VOICE_CHANNEL:PARTY");
}

void VoiceClient::SetTalkingState(bool talking, bool forceRefresh)
{
    if (!_started || !_running || !_authenticated)
        return;

    DWORD now = GetTickCount();

    if (talking)
    {
        bool wasTalking = _talking.exchange(true);

        if (!wasTalking || forceRefresh || now - _lastTalkRefresh.load() >= kTalkRefreshMs)
        {
            SendMessageToServer("VOICE_TALK_START");
            _lastTalkRefresh = now;
        }
    }
    else
    {
        bool wasTalking = _talking.exchange(false);

        if (wasTalking || forceRefresh)
            SendMessageToServer("VOICE_TALK_STOP");
    }
}

bool VoiceClient::IsReadyForVoice() const
{
    return _started && _running && _authenticated && !_config.Muted && _socket != INVALID_SOCKET;
}

void VoiceClient::AuthLoop()
{
    bool talkResentAfterAuth = false;

    while (_running)
    {
        if (!_authenticated)
        {
            AuthenticateHwid(g_FinalPayload);
            talkResentAfterAuth = false;

            {
                std::lock_guard<std::mutex> lock(g_cacheMutex);
                g_channelSent = false;
            }

            Sleep(kAuthFirstRetryMs);
            continue;
        }

        SendConfiguredChannelOnce();

        if (!talkResentAfterAuth && !_config.Muted && _config.TalkMode == VoiceTalkMode::VoiceActivation)
        {
            _talking = false;
            SetTalkingState(true, true);
            talkResentAfterAuth = true;
        }

        Sleep(1000);
    }
}

void VoiceClient::ReceiveLoop()
{
    char buffer[4096];

    while (_running)
    {
        sockaddr_in fromAddr;
        int fromLen = sizeof(fromAddr);
        ZeroMemory(&fromAddr, sizeof(fromAddr));

        int received = recvfrom(
            _socket,
            buffer,
            sizeof(buffer),
            0,
            (sockaddr*)&fromAddr,
            &fromLen
        );

        if (!_running)
            break;

        if (received <= 0)
        {
            int err = WSAGetLastError();

            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK)
                continue;

            Sleep(5);
            continue;
        }

        HandleIncomingPacket((const BYTE*)buffer, received);
    }
}

void VoiceClient::HandleIncomingPacket(const BYTE* data, int size)
{
    if (data == NULL || size <= 0)
        return;

    char text[1024];

    if (size < (int)sizeof(text))
    {
        memcpy(text, data, size);
        text[size] = '\0';

        if (strncmp(text, "VOICE_AUTH_OK:", 14) == 0)
        {
            bool wasAuthenticated = _authenticated.exchange(true);

            if (!wasAuthenticated)
            {
                {
                    std::lock_guard<std::mutex> lock(g_cacheMutex);
                    g_channelSent = false;
                }

                SendConfiguredChannelOnce();

                if (!_config.Muted && _config.TalkMode == VoiceTalkMode::VoiceActivation)
                {
                    _talking = false;
                    SetTalkingState(true, true);
                }
            }

            return;
        }

        if (strcmp(text, "VOICE_AUTH_WAIT") == 0)
            return;

        if (strcmp(text, "VOICE_AUTH_FAIL") == 0 || strcmp(text, "VOICE_NOT_AUTH") == 0)
        {
            _authenticated = false;
            _talking = false;

            std::lock_guard<std::mutex> lock(g_cacheMutex);
            g_channelSent = false;
            return;
        }

        if (strncmp(text, "VOICE_CHANNEL_OK:", 17) == 0)
            return;

        if (strncmp(text, "VOICE_USER_TALK_START:", 22) == 0)
        {
            const char* payload = text + 22;
            int objectId = atoi(payload);

            const char* name = strchr(payload, ':');
            name = (name && *(name + 1)) ? name + 1 : "Player";

            RememberSpeakerName(objectId, name);
            VoiceOverlay_OnTalkStart(objectId, name);
            return;
        }

        if (strncmp(text, "VOICE_USER_TALK_STOP:", 21) == 0)
        {
            int objectId = atoi(text + 21);
            VoiceOverlay_OnTalkStop(objectId);
            return;
        }
    }

    const char fromHeader[] = "VOICE_PCM_FROM:";
    const int fromHeaderSize = sizeof(fromHeader) - 1;

    if (size > fromHeaderSize && memcmp(data, fromHeader, fromHeaderSize) == 0)
    {
        int pos = fromHeaderSize;
        int objectId = 0;

        while (pos < size && data[pos] >= '0' && data[pos] <= '9')
        {
            objectId = (objectId * 10) + (data[pos] - '0');
            pos++;
        }

        if (objectId <= 0)
            return;

        if (pos < size && data[pos] == ':')
        {
            pos++;

            const BYTE* audioData = data + pos;
            UINT32 audioSize = (UINT32)(size - pos);

            if (audioSize > 0)
            {
                std::string speakerName = GetSpeakerName(objectId);
                VoiceOverlay_OnTalkStart(objectId, speakerName.c_str());
                _playback.PushAudioFrom(objectId, audioData, audioSize);
            }

            return;
        }
    }

    const char header[] = "VOICE_PCM:";
    const int headerSize = sizeof(header) - 1;

    if (size <= headerSize)
        return;

    if (memcmp(data, header, headerSize) != 0)
        return;

    const BYTE* audioData = data + headerSize;
    UINT32 audioSize = (UINT32)(size - headerSize);

    if (audioSize > 0)
        _playback.PushAudio(audioData, audioSize);
}

bool VoiceClient::SendMessageToServer(const char* message)
{
    if (!_started || _socket == INVALID_SOCKET || message == NULL)
        return false;

    std::lock_guard<std::mutex> lock(_sendMutex);

    int sent = sendto(
        _socket,
        message,
        (int)strlen(message),
        0,
        (sockaddr*)&_serverAddr,
        sizeof(_serverAddr)
    );

    return sent > 0;
}

bool VoiceClient::SendBytesToServer(const BYTE* data, int size)
{
    if (!_started || _socket == INVALID_SOCKET || data == NULL || size <= 0)
        return false;

    std::lock_guard<std::mutex> lock(_sendMutex);

    int sent = sendto(
        _socket,
        (const char*)data,
        size,
        0,
        (sockaddr*)&_serverAddr,
        sizeof(_serverAddr)
    );

    return sent > 0;
}

bool VoiceClient::SendPing()
{
    if (!SendMessageToServer("VOICE_PING"))
        return false;

    char buffer[1024];
    sockaddr_in fromAddr;
    int fromLen = sizeof(fromAddr);
    ZeroMemory(&fromAddr, sizeof(fromAddr));

    timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    int received = recvfrom(
        _socket,
        buffer,
        sizeof(buffer) - 1,
        0,
        (sockaddr*)&fromAddr,
        &fromLen
    );

    timeval loopTimeout;
    loopTimeout.tv_sec = 0;
    loopTimeout.tv_usec = 100000;
    setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&loopTimeout, sizeof(loopTimeout));

    if (received <= 0)
        return false;

    buffer[received] = '\0';
    return std::string(buffer) == "VOICE_OK";
}

bool VoiceClient::AuthenticateHwid(const char* hwid)
{
    if (hwid == NULL || hwid[0] == '\0')
        return false;

    char message[1024];
    sprintf_s(message, sizeof(message), "VOICE_AUTH_HWID:%s", hwid);
    return SendMessageToServer(message);
}

bool VoiceClient::Authenticate(int objectId)
{
    if (objectId <= 0)
        return false;

    char message[64];
    sprintf_s(message, sizeof(message), "VOICE_AUTH:%d", objectId);
    return SendMessageToServer(message);
}

bool VoiceClient::SetChannelParty()
{
    if (_config.Channel == VoiceChannelType::Party)
        return true;

    _config.Channel = VoiceChannelType::Party;

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_channelSent = false;
    }

    SendConfiguredChannelOnce();
    return true;
}

bool VoiceClient::SetChannelGlobal()
{
    if (_config.Channel == VoiceChannelType::Global)
        return true;

    _config.Channel = VoiceChannelType::Global;

    {
        std::lock_guard<std::mutex> lock(g_cacheMutex);
        g_channelSent = false;
    }

    SendConfiguredChannelOnce();
    return true;
}

void VoiceClient::SetTalkMode(VoiceTalkMode mode)
{
    if (_config.TalkMode == mode)
        return;

    _config.TalkMode = mode;

    if (mode == VoiceTalkMode::PushToTalk)
        SetTalkingState(false, true);
    else if (!_config.Muted)
        SetTalkingState(true, true);
}

void VoiceClient::SetPushToTalkKey(int key)
{
    if (key <= 0 || _config.PushToTalkKey == key)
        return;

    _config.PushToTalkKey = key;
}

bool VoiceClient::SetPlayerMuted(int objectId, bool muted)
{
    if (objectId <= 0)
        return false;

    if (IsSamePlayerMuteState(objectId, muted))
        return true;

    char message[64];
    sprintf_s(message, sizeof(message), muted ? "VOICE_MUTE_PLAYER:%d" : "VOICE_UNMUTE_PLAYER:%d", objectId);
    return SendMessageToServer(message);
}

void VoiceClient::PushToTalkLoop()
{
    while (_running)
    {
        if (!_authenticated || _config.Muted)
        {
            if (_talking)
            {
                if (_authenticated)
                    SetTalkingState(false, true);
                else
                    _talking = false;
            }

            Sleep(kPttPollMs);
            continue;
        }

        if (_config.TalkMode == VoiceTalkMode::VoiceActivation)
        {
            SetTalkingState(true, false);
            Sleep(80);
            continue;
        }

        bool keyPressed =
            IsClientForeground() &&
            (GetAsyncKeyState(_config.PushToTalkKey) & 0x8000) != 0;

        if (keyPressed)
            SetTalkingState(true, false);
        else
            SetTalkingState(false, false);

        Sleep(kPttPollMs);
    }

    if (_talking)
        SetTalkingState(false, true);
}

void VoiceClient::Stop()
{
    if (!_started && !_running)
        return;

    _running = false;

    if (_socket != INVALID_SOCKET)
    {
        closesocket(_socket);
        _socket = INVALID_SOCKET;
    }

    _capture.Stop();

    if (_receiveThread.joinable())
        _receiveThread.join();

    if (_pttThread.joinable())
        _pttThread.join();

    if (_authThread.joinable())
        _authThread.join();

    _playback.Stop();

    if (_started)
        WSACleanup();

    _started = false;
    _authenticated = false;
    _talking = false;

    ResetNetworkCache();
}

bool VoiceClient::SetMuted(bool muted)
{
    if (_config.Muted == muted)
        return true;

    _config.Muted = muted;

    if (muted)
    {
        if (_authenticated)
            SetTalkingState(false, true);
        else
            _talking = false;

        _capture.Stop();
        return SendMessageToServer("VOICE_MUTE");
    }

    bool ok = SendMessageToServer("VOICE_UNMUTE");

    if (_started && _running && _config.Enabled)
        _capture.Start(this, _config);

    if (_config.TalkMode == VoiceTalkMode::VoiceActivation)
        SetTalkingState(true, true);

    return ok;
}

bool VoiceClient::SendPcmPacket(const BYTE* data, UINT32 size)
{
    if (data == NULL || size == 0)
        return false;

    if (!IsReadyForVoice())
        return false;

    if (!_talking)
        return false;

    SetTalkingState(true, false);

    const char header[] = "VOICE_PCM:";
    const int headerSize = sizeof(header) - 1;

    if (size > kMaxUdpAudioBytes)
        size = kMaxUdpAudioBytes;

    BYTE packet[1500];
    memcpy(packet, header, headerSize);
    memcpy(packet + headerSize, data, size);

    int totalSize = headerSize + (int)size;
    return SendBytesToServer(packet, totalSize);
}
