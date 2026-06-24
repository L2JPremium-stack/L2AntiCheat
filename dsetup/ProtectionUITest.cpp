#include "ClientInstanceManager.h"
#include "ProtectionUI.h"

#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int)
{
    if (commandLine && wcsstr(commandLine, L"--slot"))
    {
        if (!ClientInstanceManager::Acquire())
            return 2;

        Sleep(4000);
        ClientInstanceManager::Release();
        return 0;
    }

    ShowProtectionAlertBlocking(
        L"Limite de clientes atingido",
        L"Ja existe um cliente ativo neste computador.",
        L"Feche o cliente atual antes de iniciar outro.",
        5000);
    return 0;
}
