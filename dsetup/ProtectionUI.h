#pragma once

#include <string>

void ShowProtectionMessage(const std::wstring& text);
void ShowProtectionMessageTimed(const std::wstring& text, int seconds);

void ShowProtectionAlertTimed(
    const std::wstring& title,
    const std::wstring& message,
    const std::wstring& detail,
    int milliseconds);

void ShowProtectionAlertBlocking(
    const std::wstring& title,
    const std::wstring& message,
    const std::wstring& detail,
    int milliseconds);

