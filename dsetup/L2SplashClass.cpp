#include "L2SplashClass.h"
#include "resource.h"

#include <objidl.h>

using namespace Gdiplus;

HWND g_hSplash = nullptr;

static ULONG_PTR g_GdiToken = 0;
static bool g_GdiStarted = false;

static void StartGdiPlus()
{
    if (g_GdiStarted)
        return;

    GdiplusStartupInput input;
    if (GdiplusStartup(&g_GdiToken, &input, nullptr) == Ok)
        g_GdiStarted = true;
}

static void StopGdiPlus()
{
    if (!g_GdiStarted)
        return;

    GdiplusShutdown(g_GdiToken);
    g_GdiToken = 0;
    g_GdiStarted = false;
}
static Bitmap* LoadPngFromResource(HINSTANCE instance, int resourceId)
{
    HRSRC resource = FindResourceW(
        instance,
        MAKEINTRESOURCEW(resourceId),
        RT_RCDATA
    );

    if (!resource)
        return nullptr;

    DWORD size = SizeofResource(instance, resource);
    if (size == 0)
        return nullptr;

    HGLOBAL loadedResource = LoadResource(instance, resource);
    if (!loadedResource)
        return nullptr;

    void* resourceData = LockResource(loadedResource);
    if (!resourceData)
        return nullptr;

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory)
        return nullptr;

    void* memoryData = GlobalLock(memory);
    if (!memoryData)
    {
        GlobalFree(memory);
        return nullptr;
    }

    CopyMemory(memoryData, resourceData, size);
    GlobalUnlock(memory);

    IStream* stream = nullptr;
    HRESULT hr = CreateStreamOnHGlobal(memory, TRUE, &stream);

    if (FAILED(hr) || !stream)
    {
        GlobalFree(memory);
        return nullptr;
    }

    Bitmap* loadedBitmap = Bitmap::FromStream(stream, FALSE);

    if (!loadedBitmap || loadedBitmap->GetLastStatus() != Ok)
    {
        delete loadedBitmap;
        stream->Release();
        return nullptr;
    }

    UINT imageWidth = loadedBitmap->GetWidth();
    UINT imageHeight = loadedBitmap->GetHeight();

    Bitmap* finalBitmap = new Bitmap(
        imageWidth,
        imageHeight,
        PixelFormat32bppPARGB
    );

    if (!finalBitmap || finalBitmap->GetLastStatus() != Ok)
    {
        delete finalBitmap;
        delete loadedBitmap;
        stream->Release();
        return nullptr;
    }

    Graphics graphics(finalBitmap);
    graphics.SetCompositingMode(CompositingModeSourceCopy);
    graphics.SetCompositingQuality(CompositingQualityHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    graphics.Clear(Color(0, 0, 0, 0));
    graphics.DrawImage(
        loadedBitmap,
        0,
        0,
        imageWidth,
        imageHeight
    );

    delete loadedBitmap;
    stream->Release();

    return finalBitmap;
}

static bool DrawLayeredPng(HWND hwnd, Image* image, int width, int height)
{
    if (!hwnd || !image)
        return false;

    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
        return false;

    HDC memoryDc = CreateCompatibleDC(screenDc);
    if (!memoryDc)
    {
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;

    HBITMAP dib = CreateDIBSection(
        screenDc,
        &bmi,
        DIB_RGB_COLORS,
        &bits,
        nullptr,
        0
    );

    if (!dib)
    {
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memoryDc, dib);

    Graphics graphics(memoryDc);
    graphics.SetCompositingMode(CompositingModeSourceCopy);
    graphics.SetCompositingQuality(CompositingQualityHighQuality);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetSmoothingMode(SmoothingModeHighQuality);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);

    graphics.Clear(Color(0, 0, 0, 0));
    graphics.DrawImage(image, 0, 0, width, height);

    RECT rect{};
    GetWindowRect(hwnd, &rect);

    POINT screenPos{};
    screenPos.x = rect.left;
    screenPos.y = rect.top;

    POINT sourcePos{};
    sourcePos.x = 0;
    sourcePos.y = 0;

    SIZE windowSize{};
    windowSize.cx = width;
    windowSize.cy = height;

    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    BOOL ok = UpdateLayeredWindow(
        hwnd,
        screenDc,
        &screenPos,
        &windowSize,
        memoryDc,
        &sourcePos,
        0,
        &blend,
        ULW_ALPHA
    );

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(dib);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    return ok == TRUE;
}

void ShowSplash()
{
    StartGdiPlus();

    const int width = 600;
    const int height = 415;

    static bool classRegistered = false;

    if (!classRegistered)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = SplashProc;
        wc.hInstance = (HINSTANCE)g_ModuleHandle;
        wc.lpszClassName = L"L2SplashClass";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;

        classRegistered = true;
    }

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    g_hSplash = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        L"L2SplashClass",
        L"",
        WS_POPUP,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        (HINSTANCE)g_ModuleHandle,
        nullptr
    );

    if (!g_hSplash)
        return;

    Bitmap* image = LoadPngFromResource(
        (HINSTANCE)g_ModuleHandle,
        IDB_SPLASHLOAD
    );

    if (!image)
    {
        DestroyWindow(g_hSplash);
        g_hSplash = nullptr;
        return;
    }

    DrawLayeredPng(g_hSplash, image, width, height);

    delete image;

    SetWindowPos(
        g_hSplash,
        HWND_TOPMOST,
        x,
        y,
        width,
        height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW
    );

    ShowWindow(g_hSplash, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hSplash);
}
void HideSplash()
{
    if (g_hSplash)
    {
        ShowWindow(g_hSplash, SW_HIDE);
        DestroyWindow(g_hSplash);
        g_hSplash = nullptr;
    }

    StopGdiPlus();
}

LRESULT CALLBACK SplashProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_ERASEBKGND:
            return 1;

      case WM_NCHITTEST:
    return HTCAPTION;

        case WM_DESTROY:
            if (hwnd == g_hSplash)
                g_hSplash = nullptr;
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}