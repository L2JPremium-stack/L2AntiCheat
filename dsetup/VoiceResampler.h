#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <audioclient.h>
#include <vector>

class VoiceResampler
{
public:
    static void ConvertTo16kMono16(
        const BYTE* input,
        UINT32 inputBytes,
        const WAVEFORMATEX* inputFormat,
        std::vector<BYTE>& output
    );

    static void Convert16kMono16ToFormat(
        const BYTE* input,
        UINT32 inputBytes,
        const WAVEFORMATEX* outputFormat,
        std::vector<BYTE>& output
    );
};