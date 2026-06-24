#pragma once

#include <windows.h>
#include <string>

class ClientInstanceManager
{
public:
    static bool Acquire();
    static void Release();

    static bool IsOwner();
    static LONG GetCurrentCount();
    static LONG GetMaxClients();

private:
    static void Cleanup();
    static std::wstring GetSlotMutexName(LONG slot);

private:
    static HANDLE _slotMutex;
    static LONG _slotIndex;
    static bool _hasSlot;
    static bool _isOwner;

private:
    static constexpr LONG MAX_CLIENTS = 1; // ALTERA AQUI
};
