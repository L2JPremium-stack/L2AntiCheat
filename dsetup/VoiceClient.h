#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "VoiceCapture.h"
#include "VoicePlayback.h"
#include "VoiceConfig.h"

#pragma comment(lib, "ws2_32.lib")

// Rebuild focado em jogo:
// - autenticação com backoff para evitar flood no servidor;
// - envio de voz em frames fixos de 10 ms / 48 kHz / mono / PCM16;
// - refresh de estado de fala bem maior, sem spam;
// - parser seguro para pacotes de texto e áudio;
// - socket UDP com timeout estável.
class VoiceClient
{
public:
    VoiceClient();
    ~VoiceClient();

    bool Start(const VoiceConfig& config);
    void Stop();

    bool SetMuted(bool muted);
    bool SetPlayerMuted(int objectId, bool muted);
    bool SetChannelParty();
    bool SetChannelGlobal();
    void SetTalkMode(VoiceTalkMode mode);
    void SetPushToTalkKey(int key);

    bool SendPcmPacket(const BYTE* data, UINT32 size);
    bool Authenticate(int objectId);
    bool AuthenticateHwid(const char* hwid);

private:
    static constexpr DWORD kRecvTimeoutMs = 250;
    static constexpr DWORD kAuthFirstRetryMs = 10000;
    static constexpr DWORD kAuthMaxRetryMs = 60000;
    static constexpr DWORD kTalkRefreshMs = 15000;
    static constexpr DWORD kPttPollMs = 25;
    static constexpr UINT32 kPcmFrameBytes10ms48kMono16 = 960;
    static constexpr UINT32 kMaxUdpAudioBytes = 960;

    bool ConfigureSocket();
    bool SendPing();
    bool SendMessageToServer(const char* message);
    bool SendBytesToServer(const BYTE* data, int size);
    bool IsReadyForVoice() const;

    void AuthLoop();
    void ReceiveLoop();
    void PushToTalkLoop();
    void HandleIncomingPacket(const BYTE* data, int size);
    void SendConfiguredChannelOnce();
    void SetTalkingState(bool talking, bool forceRefresh = false);

private:
    SOCKET _socket;
    sockaddr_in _serverAddr;

    VoiceConfig _config;
    VoiceCapture _capture;
    VoicePlayback _playback;

    std::thread _authThread;
    std::thread _receiveThread;
    std::thread _pttThread;

    std::atomic<bool> _authenticated;
    std::atomic<bool> _started;
    std::atomic<bool> _running;
    std::atomic<bool> _talking;

    std::atomic<uint32_t> _txSequence;
    std::atomic<DWORD> _lastTalkRefresh;

    mutable std::mutex _sendMutex;
    mutable std::mutex _configMutex;
};
