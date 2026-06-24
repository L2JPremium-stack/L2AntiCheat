#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "VoiceCapture.h"
#include "VoiceClient.h"
#include "VoiceResampler.h"
#include "VoiceLog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

#define SAFE_RELEASE(x) if ((x) != nullptr) { (x)->Release(); (x) = nullptr; }

VoiceCapture::VoiceCapture()
{
    _client = nullptr;
    _running = false;
    _lastVoiceMs = 0;
}

VoiceCapture::~VoiceCapture()
{
    Stop();
}

bool VoiceCapture::Start(VoiceClient* client, const VoiceConfig& config)
{
    if (_running)
        return true;

    _client = client;
    _config = config;
    _frameBuffer.clear();
    _lastVoiceMs = 0;
    _running = true;
    _thread = std::thread(&VoiceCapture::CaptureLoop, this);
    return true;
}

void VoiceCapture::Stop()
{
    _running = false;
    if (_thread.joinable())
        _thread.join();
    _frameBuffer.clear();
}

bool VoiceCapture::IsProbablyVoice(const BYTE* pcm16, UINT32 size) const
{
    if (!pcm16 || size < sizeof(short))
        return false;

    const short* samples = (const short*)pcm16;
    UINT32 count = size / sizeof(short);
    double sum = 0.0;

    for (UINT32 i = 0; i < count; ++i)
        sum += (double)samples[i] * (double)samples[i];

    double rms = sqrt(sum / (double)count);
    return rms >= kVadRmsThreshold;
}

void VoiceCapture::AppendConvertedAudio(const BYTE* data, UINT32 dataSize, WAVEFORMATEX* waveFormat)
{
    if (!_running || !data || dataSize == 0 || !waveFormat)
        return;

    std::vector<BYTE> converted;
    VoiceResampler::ConvertTo16kMono16(data, dataSize, waveFormat, converted); // nome legado, hoje converte para 48k mono PCM16.

    if (converted.empty())
        return;

    if (_frameBuffer.size() + converted.size() > kMaxBufferedBytes)
    {
        size_t keep = std::min<size_t>(_frameBuffer.size(), kTargetFrameBytes);
        if (keep > 0)
        {
            std::vector<BYTE> tail(_frameBuffer.end() - keep, _frameBuffer.end());
            _frameBuffer.swap(tail);
        }
        else
        {
            _frameBuffer.clear();
        }
    }

    _frameBuffer.insert(_frameBuffer.end(), converted.begin(), converted.end());
    FlushReadyFrames();
}

void VoiceCapture::FlushReadyFrames()
{
    while (_frameBuffer.size() >= kTargetFrameBytes && _running)
    {
        BYTE frame[kTargetFrameBytes];
        memcpy(frame, _frameBuffer.data(), kTargetFrameBytes);
        _frameBuffer.erase(_frameBuffer.begin(), _frameBuffer.begin() + kTargetFrameBytes);

        DWORD now = GetTickCount();
        bool voice = IsProbablyVoice(frame, kTargetFrameBytes);
        if (voice)
            _lastVoiceMs = now;

        // Mantem alguns frames depois da voz para nao cortar fim de palavra.
        if (voice || (_lastVoiceMs != 0 && now - _lastVoiceMs <= kSilenceHoldMs))
        {
            if (_client)
                _client->SendPcmPacket(frame, kTargetFrameBytes);
        }
    }
}

void VoiceCapture::CaptureLoop()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool didCoInit = SUCCEEDED(hr);

    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        VoiceLog("[VoiceCapture] CoInitializeEx failed.");
        _running = false;
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;
    WAVEFORMATEX* waveFormat = nullptr;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr))
    {

        goto cleanup;
    }

    hr = enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &device);
    if (FAILED(hr))
    {

        goto cleanup;
    }

    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
    if (FAILED(hr))
    {

        goto cleanup;
    }

    IAudioClient2* audioClient2 = nullptr;
    hr = audioClient->QueryInterface(__uuidof(IAudioClient2), (void**)&audioClient2);
    if (SUCCEEDED(hr) && audioClient2)
    {
        AudioClientProperties props = {};
        props.cbSize = sizeof(AudioClientProperties);
        props.bIsOffload = FALSE;
        props.eCategory = AudioCategory_Communications;
        props.Options = AUDCLNT_STREAMOPTIONS_NONE;
        audioClient2->SetClientProperties(&props);
        SAFE_RELEASE(audioClient2);
    }

    hr = audioClient->GetMixFormat(&waveFormat);
    if (FAILED(hr))
    {
      
        goto cleanup;
    }

    hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, waveFormat, nullptr);
    if (FAILED(hr))
    {
      
        goto cleanup;
    }

    hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
    if (FAILED(hr))
    {
      
        goto cleanup;
    }

    hr = audioClient->Start();
    if (FAILED(hr))
    {
      
        goto cleanup;
    }

    while (_running)
    {
        UINT32 packetLength = 0;
        hr = captureClient->GetNextPacketSize(&packetLength);
        if (FAILED(hr))
            break;

        while (packetLength != 0 && _running)
        {
            BYTE* data = nullptr;
            UINT32 framesAvailable = 0;
            DWORD flags = 0;

            hr = captureClient->GetBuffer(&data, &framesAvailable, &flags, nullptr, nullptr);
            if (FAILED(hr))
                break;

            UINT32 dataSize = framesAvailable * waveFormat->nBlockAlign;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data && dataSize > 0)
                AppendConvertedAudio(data, dataSize, waveFormat);

            captureClient->ReleaseBuffer(framesAvailable);
            hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr))
                break;
        }

        Sleep(2);
    }

    if (audioClient)
        audioClient->Stop();

cleanup:
    if (waveFormat)
        CoTaskMemFree(waveFormat);
    SAFE_RELEASE(captureClient);
    SAFE_RELEASE(audioClient);
    SAFE_RELEASE(device);
    SAFE_RELEASE(enumerator);
    if (didCoInit)
        CoUninitialize();
}
