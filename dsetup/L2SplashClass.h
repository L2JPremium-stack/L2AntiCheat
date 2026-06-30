#pragma once

#include <windows.h>
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

extern HMODULE g_ModuleHandle;
extern HWND g_hSplash;

void ShowSplash();
void HideSplash();

LRESULT CALLBACK SplashProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);