#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "VoiceConfig.h"

class VoiceClient;

// Captura microfone WASAPI e entrega frames fixos de 10 ms / 48 kHz / mono / PCM16 para o VoiceClient.
// O envio em frame fixo reduz jitter, evita truncamento UDP e melhora a mixagem no servidor.
class VoiceCapture
{
public:
    VoiceCapture();
    ~VoiceCapture();

    bool Start(VoiceClient* client, const VoiceConfig& config);
    void Stop();

private:
    static constexpr UINT32 kTargetFrameBytes = 960; // 10 ms, 48 kHz, mono, PCM16.
    static constexpr UINT32 kMaxBufferedBytes = kTargetFrameBytes * 8;
    static constexpr int kVadRmsThreshold = 220;
    static constexpr DWORD kSilenceHoldMs = 180;

    void CaptureLoop();
    void AppendConvertedAudio(const BYTE* data, UINT32 dataSize, WAVEFORMATEX* waveFormat);
    void FlushReadyFrames();
    bool IsProbablyVoice(const BYTE* pcm16, UINT32 size) const;

private:
    VoiceClient* _client;
    VoiceConfig _config;
    std::thread _thread;
    std::atomic<bool> _running;

    std::vector<BYTE> _frameBuffer;
    DWORD _lastVoiceMs;
};
