#include "VoiceResampler.h"
#include <cmath>
#include <ks.h>
#include <ksmedia.h>

static bool IsFloatFormat(const WAVEFORMATEX* fmt)
{
    if (!fmt)
        return false;

    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return true;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE* ext = (const WAVEFORMATEXTENSIBLE*)fmt;
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }

    return false;
}

static bool IsPcmFormat(const WAVEFORMATEX* fmt)
{
    if (!fmt)
        return false;

    if (fmt->wFormatTag == WAVE_FORMAT_PCM)
        return true;

    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const WAVEFORMATEXTENSIBLE* ext = (const WAVEFORMATEXTENSIBLE*)fmt;
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
    }

    return false;
}

static float ReadInputSampleAsFloat(const BYTE* data, UINT32 sampleIndex, const WAVEFORMATEX* fmt)
{
    if (IsFloatFormat(fmt) && fmt->wBitsPerSample == 32)
    {
        const float* samples = (const float*)data;
        return samples[sampleIndex];
    }

    if (IsPcmFormat(fmt) && fmt->wBitsPerSample == 16)
    {
        const short* samples = (const short*)data;
        return samples[sampleIndex] / 32768.0f;
    }

    if (IsPcmFormat(fmt) && fmt->wBitsPerSample == 32)
    {
        const int* samples = (const int*)data;
        return samples[sampleIndex] / 2147483648.0f;
    }

    return 0.0f;
}

static short FloatToInt16(float v)
{
    if (v > 1.0f)
        v = 1.0f;

    if (v < -1.0f)
        v = -1.0f;

    return (short)(v * 32767.0f);
}

static void WriteOutputSampleFromFloat(BYTE* data, UINT32 sampleIndex, const WAVEFORMATEX* fmt, float value)
{
    if (value > 1.0f)
        value = 1.0f;

    if (value < -1.0f)
        value = -1.0f;

    if (IsFloatFormat(fmt) && fmt->wBitsPerSample == 32)
    {
        float* samples = (float*)data;
        samples[sampleIndex] = value;
        return;
    }

    if (IsPcmFormat(fmt) && fmt->wBitsPerSample == 16)
    {
        short* samples = (short*)data;
        samples[sampleIndex] = FloatToInt16(value);
        return;
    }

    if (IsPcmFormat(fmt) && fmt->wBitsPerSample == 32)
    {
        int* samples = (int*)data;
        samples[sampleIndex] = (int)(value * 2147483647.0f);
        return;
    }
}

void VoiceResampler::ConvertTo16kMono16(
    const BYTE* input,
    UINT32 inputBytes,
    const WAVEFORMATEX* inputFormat,
    std::vector<BYTE>& output
)
{
    output.clear();

    if (!input || !inputFormat || inputBytes == 0)
        return;

    UINT32 inputChannels = inputFormat->nChannels;
    UINT32 inputRate = inputFormat->nSamplesPerSec;
    UINT32 blockAlign = inputFormat->nBlockAlign;

    if (inputChannels == 0 || inputRate == 0 || blockAlign == 0)
        return;

    UINT32 inputFrames = inputBytes / blockAlign;

    if (inputFrames == 0)
        return;

    const UINT32 outputRate = 48000;
    UINT32 outputFrames = (UINT32)((double)inputFrames * outputRate / inputRate);

    if (outputFrames == 0)
        return;

    output.resize(outputFrames * sizeof(short));

    short* outSamples = (short*)output.data();

    for (UINT32 i = 0; i < outputFrames; i++)
    {
        double srcPos = (double)i * inputRate / outputRate;
        UINT32 srcFrame = (UINT32)srcPos;

        if (srcFrame >= inputFrames)
            srcFrame = inputFrames - 1;

        float mixed = 0.0f;

        for (UINT32 ch = 0; ch < inputChannels; ch++)
        {
            UINT32 sampleIndex = srcFrame * inputChannels + ch;
            mixed += ReadInputSampleAsFloat(input, sampleIndex, inputFormat);
        }

        mixed /= inputChannels;
        outSamples[i] = FloatToInt16(mixed);
    }
}

void VoiceResampler::Convert16kMono16ToFormat(
    const BYTE* input,
    UINT32 inputBytes,
    const WAVEFORMATEX* outputFormat,
    std::vector<BYTE>& output
)
{
    output.clear();

    if (!input || inputBytes == 0 || !outputFormat)
        return;

    if (inputBytes < sizeof(short))
        return;

    const UINT32 inputRate = 48000;
    const UINT32 inputChannels = 1;
    const UINT32 inputBlockAlign = sizeof(short);

    UINT32 inputFrames = inputBytes / inputBlockAlign;

    if (inputFrames == 0)
        return;

    UINT32 outputRate = outputFormat->nSamplesPerSec;
    UINT32 outputChannels = outputFormat->nChannels;
    UINT32 outputBlockAlign = outputFormat->nBlockAlign;

    if (outputRate == 0 || outputChannels == 0 || outputBlockAlign == 0)
        return;

    UINT32 outputFrames = (UINT32)((double)inputFrames * outputRate / inputRate);

    if (outputFrames == 0)
        return;

    output.resize(outputFrames * outputBlockAlign);
    ZeroMemory(output.data(), output.size());

    const short* inSamples = (const short*)input;

    for (UINT32 frame = 0; frame < outputFrames; frame++)
    {
        double srcPos = (double)frame * inputRate / outputRate;
        UINT32 srcFrame = (UINT32)srcPos;

        if (srcFrame >= inputFrames)
            srcFrame = inputFrames - 1;

        float sample = inSamples[srcFrame] / 32768.0f;

        for (UINT32 ch = 0; ch < outputChannels; ch++)
        {
            UINT32 sampleIndex = frame * outputChannels + ch;
            WriteOutputSampleFromFloat(output.data(), sampleIndex, outputFormat, sample);
        }
    }
}