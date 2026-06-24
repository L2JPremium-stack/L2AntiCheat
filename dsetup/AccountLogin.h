#pragma once

#include <windows.h>
#include <string>

bool AccountLogin_Initialize();
void AccountLogin_Shutdown();

bool AccountLogin_Request(const std::wstring& login, const std::wstring& password);
bool AccountLogin_CanOpenPanel();
bool AccountLogin_IsGameSessionActive();
const wchar_t* AccountLogin_GetPanelBlockReason();
void AccountLogin_SetGameSessionActive(bool active);
