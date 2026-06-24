#pragma once
#include <windows.h>
#include <string>

struct FileCheckResult
{
    bool allOk;
    bool fileChanged;
    bool fileMissing;

    std::wstring fileName;
    std::wstring fullPath;

    std::string hash;
    std::string expectedHash;

    DWORD errorCode;
};

bool InitializeFileProtection();
FileCheckResult VerifyProtectedFiles();
bool GetFileHash(const wchar_t* file, std::string& outHash);