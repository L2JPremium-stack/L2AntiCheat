#include "AccountLogin.h"

#include <windows.h>

class UNetworkHandler {};

typedef int(__fastcall* RequestAuthLoginFn)(UNetworkHandler*, int, const wchar_t*, const wchar_t*, int);

static constexpr uintptr_t UNETWORK_OFFSET = 0x81F538; // Interlude

static UNetworkHandler** g_UNetwork = NULL;
static RequestAuthLoginFn g_RequestAuthLogin = NULL;
static volatile LONG g_GameSessionActive = 0;

bool AccountLogin_Initialize()
{
    HMODULE engine = GetModuleHandleW(L"engine.dll");
    if (!engine)
        return false;

    g_UNetwork = reinterpret_cast<UNetworkHandler**>(
        reinterpret_cast<uintptr_t>(engine) + UNETWORK_OFFSET
    );

    g_RequestAuthLogin = reinterpret_cast<RequestAuthLoginFn>(
        GetProcAddress(engine, "?RequestAuthLogin@UNetworkHandler@@UAEHPAG0H@Z")
    );

    InterlockedExchange(&g_GameSessionActive, 0);
    return g_UNetwork != NULL && g_RequestAuthLogin != NULL;
}

void AccountLogin_Shutdown()
{
    g_UNetwork = NULL;
    g_RequestAuthLogin = NULL;
    InterlockedExchange(&g_GameSessionActive, 0);
}

void AccountLogin_SetGameSessionActive(bool active)
{
    InterlockedExchange(&g_GameSessionActive, active ? 1 : 0);
}

bool AccountLogin_IsGameSessionActive()
{
    return InterlockedCompareExchange(&g_GameSessionActive, 0, 0) != 0;
}

bool AccountLogin_CanOpenPanel()
{
    if (AccountLogin_IsGameSessionActive())
        return false;

    return g_UNetwork != NULL && *g_UNetwork != NULL && g_RequestAuthLogin != NULL;
}

const wchar_t* AccountLogin_GetPanelBlockReason()
{
    if (AccountLogin_IsGameSessionActive())
        return L"Painel dispon\u00edvel apenas antes de entrar com personagem.";

    return L"Cliente ainda n\u00e3o est\u00e1 pronto para login.";
}

bool AccountLogin_Request(const std::wstring& login, const std::wstring& password)
{
    if (login.empty() || password.empty())
        return false;

    if (AccountLogin_IsGameSessionActive())
        return false;

    if (!g_UNetwork || !*g_UNetwork || !g_RequestAuthLogin)
        return false;

    g_RequestAuthLogin(*g_UNetwork, 0, login.c_str(), password.c_str(), 0);
    return true;
}
