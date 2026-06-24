#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// Inicializa a thread de overlay e cria as janelas Win32.
// Chame uma vez quando o módulo de voz estiver pronto.
void VoiceOverlay_Initialize(HINSTANCE hInstance);

// Finaliza a thread, salva posições e libera janelas/timers/hotkeys.
void VoiceOverlay_Shutdown();

// Controle externo do painel principal.
void VoiceOverlay_Show();
void VoiceOverlay_Hide();
void VoiceOverlay_Toggle();

// Eventos de fala. A janela de falantes aparece somente enquanto houver jogadores falando.
void VoiceOverlay_OnTalkStart(int objectId, const char* name);
void VoiceOverlay_OnTalkStop(int objectId);
