#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <queue>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

#include "VoiceConfig.h"

class VoicePlayback
{
private:
    VoiceConfig _config;

    std::atomic<bool> _running;
    std::thread _thread;

    std::mutex _queueLock;
    std::map<int, std::queue<std::vector<BYTE>>> _speakerQueues;

    // Com muitas pessoas falando ao mesmo tempo, chegam varios pacotes PCM no mesmo periodo.
    // Se tocar 1 por vez, a fila cresce e a voz fica robotizada/atrasada.
    // Por isso a fila e mantida curta e o playback mistura pequenos lotes.
    const size_t JITTER_START_PACKETS = 2;
    const size_t JITTER_MAX_PACKETS_PER_SPEAKER = 8;
    const size_t MIX_MAX_SPEAKERS = 8;

private:
    void PlaybackLoop();
    size_t QueueSize();
    std::vector<BYTE> PopMixedPacket16k();
    static std::vector<BYTE> MixPcm16MonoPackets(const std::vector<std::vector<BYTE>>& packets);

public:
    VoicePlayback();
    ~VoicePlayback();

    bool Start(const VoiceConfig& config);
    void Stop();

    void PushAudio(const BYTE* data, UINT32 size);
    void PushAudioFrom(int speakerId, const BYTE* data, UINT32 size);
};