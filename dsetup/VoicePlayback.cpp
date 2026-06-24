#include "VoicePlayback.h"
#include "VoiceLog.h"
#include "VoiceResampler.h"

#include <cstring>
#include <cmath>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

#define SAFE_RELEASE(x) if ((x) != nullptr) { (x)->Release(); (x) = nullptr; }

VoicePlayback::VoicePlayback()
{
    _running = false;
}

VoicePlayback::~VoicePlayback()
{
    Stop();
}

bool VoicePlayback::Start(const VoiceConfig& config)
{
    if (_running)
        return false;

    _config = config;
    _running = true;

    _thread = std::thread(&VoicePlayback::PlaybackLoop, this);
    return true;
}

void VoicePlayback::Stop()
{
    _running = false;

    if (_thread.joinable())
        _thread.join();

    std::lock_guard<std::mutex> guard(_queueLock);
    _speakerQueues.clear();
}

size_t VoicePlayback::QueueSize()
{
    std::lock_guard<std::mutex> guard(_queueLock);

    size_t total = 0;

    for (const auto& it : _speakerQueues)
        total += it.second.size();

    return total;
}

void VoicePlayback::PushAudio(const BYTE* data, UINT32 size)
{
    // Compatibilidade com servidor antigo, sem objectId no pacote.
    PushAudioFrom(0, data, size);
}

void VoicePlayback::PushAudioFrom(int speakerId, const BYTE* data, UINT32 size)
{
    if (!_running || data == nullptr || size == 0)
        return;

    std::vector<BYTE> packet(data, data + size);

    std::lock_guard<std::mutex> guard(_queueLock);

    std::queue<std::vector<BYTE>>& q = _speakerQueues[speakerId];

    while (q.size() >= JITTER_MAX_PACKETS_PER_SPEAKER)
        q.pop();

    q.push(packet);
}

std::vector<BYTE> VoicePlayback::PopMixedPacket16k()
{
    std::vector<std::vector<BYTE>> batch;

    {
        std::lock_guard<std::mutex> guard(_queueLock);

        for (auto it = _speakerQueues.begin(); it != _speakerQueues.end();)
        {
            std::queue<std::vector<BYTE>>& q = it->second;

            // Se um jogador acumulou muitos pacotes, descarte os antigos.
            // Audio atrasado e pior que perder alguns frames.
            while (q.size() > JITTER_MAX_PACKETS_PER_SPEAKER)
                q.pop();

            if (!q.empty() && batch.size() < MIX_MAX_SPEAKERS)
            {
                batch.push_back(q.front());
                q.pop();
            }

            if (q.empty())
                it = _speakerQueues.erase(it);
            else
                ++it;
        }
    }

    return MixPcm16MonoPackets(batch);
}

std::vector<BYTE> VoicePlayback::MixPcm16MonoPackets(const std::vector<std::vector<BYTE>>& packets)
{
    if (packets.empty())
        return std::vector<BYTE>();

    if (packets.size() == 1)
        return packets[0];

    size_t maxSize = 0;

    for (const auto& p : packets)
    {
        if (p.size() > maxSize)
            maxSize = p.size();
    }

    if ((maxSize & 1) != 0)
        maxSize--;

    if (maxSize == 0)
        return std::vector<BYTE>();

    std::vector<BYTE> mixed(maxSize, 0);

    // Quanto mais falantes, menor o ganho para evitar clipping.
    double gain = 0.85 / sqrt((double)packets.size());

    if (gain > 0.85)
        gain = 0.85;

    for (size_t i = 0; i + 1 < maxSize; i += 2)
    {
        int sum = 0;

        for (const auto& p : packets)
        {
            if (i + 1 >= p.size())
                continue;

            int lo = p[i] & 0xFF;
            int hi = (int)((signed char)p[i + 1]);
            short sample = (short)((hi << 8) | lo);

            sum += (int)sample;
        }

        int value = (int)(sum * gain);

        // Soft limiter simples.
        if (value > 28000)
            value = 28000 + (value - 28000) / 5;
        else if (value < -28000)
            value = -28000 + (value + 28000) / 5;

        if (value > 32767)
            value = 32767;
        else if (value < -32768)
            value = -32768;

        mixed[i] = (BYTE)(value & 0xFF);
        mixed[i + 1] = (BYTE)((value >> 8) & 0xFF);
    }

    return mixed;
}

void VoicePlayback::PlaybackLoop()
{
    HRESULT hr;

    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioRenderClient* renderClient = nullptr;
    WAVEFORMATEX* renderFormat = nullptr;

    UINT32 bufferFrameCount = 0;
    bool jitterReady = false;

    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator
    );

    if (FAILED(hr))
        goto cleanup;

    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);

    if (FAILED(hr))
        goto cleanup;

    hr = device->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        (void**)&audioClient
    );

    if (FAILED(hr))
        goto cleanup;

    hr = audioClient->GetMixFormat(&renderFormat);

    if (FAILED(hr))
        goto cleanup;

    hr = audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        0,
        10000000,
        0,
        renderFormat,
        nullptr
    );

    if (FAILED(hr))
        goto cleanup;

    hr = audioClient->GetBufferSize(&bufferFrameCount);

    if (FAILED(hr))
        goto cleanup;

    hr = audioClient->GetService(
        __uuidof(IAudioRenderClient),
        (void**)&renderClient
    );

    if (FAILED(hr))
        goto cleanup;

    hr = audioClient->Start();

    if (FAILED(hr))
        goto cleanup;

    while (_running)
    {
        if (!jitterReady)
        {
            if (QueueSize() < JITTER_START_PACKETS)
            {
                Sleep(3);
                continue;
            }

            jitterReady = true;
        }

        std::vector<BYTE> packet16k = PopMixedPacket16k();

        if (packet16k.empty())
        {
            jitterReady = false;
            Sleep(3);
            continue;
        }

        std::vector<BYTE> renderPacket;

        VoiceResampler::Convert16kMono16ToFormat(
            packet16k.data(),
            (UINT32)packet16k.size(),
            renderFormat,
            renderPacket
        );

        if (renderPacket.empty())
            continue;

        UINT32 frames = (UINT32)(renderPacket.size() / renderFormat->nBlockAlign);

        if (frames == 0)
            continue;

        UINT32 offsetFrames = 0;

        while (frames > 0 && _running)
        {
            UINT32 padding = 0;
            audioClient->GetCurrentPadding(&padding);

            UINT32 availableFrames = bufferFrameCount - padding;

            if (availableFrames == 0)
            {
                Sleep(1);
                continue;
            }

            UINT32 framesToWrite = frames;

            if (framesToWrite > availableFrames)
                framesToWrite = availableFrames;

            BYTE* renderData = nullptr;

            hr = renderClient->GetBuffer(framesToWrite, &renderData);

            if (SUCCEEDED(hr))
            {
                memcpy(
                    renderData,
                    renderPacket.data() + (offsetFrames * renderFormat->nBlockAlign),
                    framesToWrite * renderFormat->nBlockAlign
                );

                renderClient->ReleaseBuffer(framesToWrite, 0);
            }

            frames -= framesToWrite;
            offsetFrames += framesToWrite;
        }
    }

    if (audioClient)
        audioClient->Stop();

cleanup:

    if (renderFormat)
        CoTaskMemFree(renderFormat);

    SAFE_RELEASE(renderClient);
    SAFE_RELEASE(audioClient);
    SAFE_RELEASE(device);
    SAFE_RELEASE(enumerator);

    CoUninitialize();
}
