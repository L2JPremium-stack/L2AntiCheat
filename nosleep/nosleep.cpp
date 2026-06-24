typedef void* HMODULE;
typedef unsigned long DWORD;
typedef void* LPVOID;
typedef int BOOL;

// Fire.dll imports this symbol only to force nosleep.dll to load.
// This compatibility build intentionally does not patch Core.dll!appSleep.
extern "C" BOOL __stdcall DllEntryPoint(
    HMODULE,
    DWORD,
    LPVOID)
{
    return 1;
}

extern "C" BOOL __stdcall DllMainEntry(
    HMODULE,
    DWORD,
    LPVOID)
{
    return 1;
}
