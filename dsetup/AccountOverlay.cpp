#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "AccountOverlay.h"
#include "AccountVault.h"
#include "AccountLogin.h"
#include "resource.h"

#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "Shell32.lib")

static HMODULE g_Module = NULL;
static HWND g_Window = NULL;
static HWND g_AddWindow = NULL;
static HWND g_AddLoginEdit = NULL;
static HWND g_AddPassEdit = NULL;

static HANDLE g_OverlayThread = NULL;
static DWORD g_OverlayThreadId = 0;
static HANDLE g_OverlayReadyEvent = NULL;
static bool g_VisibleRequested = false;
static bool g_InsertWasDown = false;

static const UINT_PTR ACCOUNT_INPUT_TIMER_ID = 1;
static const UINT WM_ACCOUNT_SHOW = WM_APP + 6101;
static const UINT WM_ACCOUNT_HIDE = WM_APP + 6102;
static const UINT WM_ACCOUNT_TOGGLE = WM_APP + 6103;
static const UINT WM_ACCOUNT_SHUTDOWN = WM_APP + 6104;

static const int PANEL_W = 360;
static const int PANEL_H = 492;
static const int ADD_W = 360;
static const int ADD_H = 250;
static const int ACCOUNT_ROW_H = 41;
static const int ACCOUNT_ROW_PITCH = 47;

enum PanelButton
{
    BTN_NONE = 0,
    BTN_CLOSE,
    BTN_LOGIN,
    BTN_ADD,
    BTN_FAVORITE,
    BTN_DELETE
};

enum AddButton
{
    ADD_BTN_NONE = 0,
    ADD_BTN_SAVE,
    ADD_BTN_SAVE_LOGIN,
    ADD_BTN_CANCEL
};

struct ButtonRect
{
    int id;
    RECT rc;
};

static std::vector<SavedAccount> g_Accounts;
static int g_SelectedIndex = -1;
static int g_ScrollOffset = 0;
static int g_HoverButton = BTN_NONE;
static int g_PressedButton = BTN_NONE;
static int g_HoverAccountIndex = -1;
static ULONGLONG g_LastAccountClickAt = 0;
static int g_LastClickedAccount = -1;

static int g_AddHoverButton = ADD_BTN_NONE;
static int g_AddPressedButton = ADD_BTN_NONE;
static std::wstring g_StatusText = L"Painel pronto.";
static std::wstring g_AddStatusText;

static ButtonRect g_PanelButtons[5] = {};
static ButtonRect g_AddButtons[3] = {};
static RECT g_CloseRect = {};
static RECT g_SummaryRect = {};
static RECT g_ListRect = {};
static RECT g_StatusRect = {};
static RECT g_AddLoginRect = {};
static RECT g_AddPassRect = {};

static HBRUSH g_BgBrush = NULL;
static HBRUSH g_PanelBrush = NULL;
static HBRUSH g_EditBrush = NULL;
static HFONT g_TitleFont = NULL;
static HFONT g_SubtitleFont = NULL;
static HFONT g_StatusFont = NULL;
static HFONT g_LabelFont = NULL;
static HFONT g_TextFont = NULL;
static HFONT g_ButtonFont = NULL;
static HICON g_AppIcon = NULL;
static WNDPROC g_OriginalEditProc = NULL;

static const COLORREF C_BG = RGB(15, 14, 19);
static const COLORREF C_ACCENT = RGB(255, 84, 26);
static const COLORREF C_ACCENT_HOVER = RGB(255, 111, 47);
static const COLORREF C_ACCENT_SOFT = RGB(255, 132, 77);
static const COLORREF C_PANEL = RGB(24, 22, 28);
static const COLORREF C_PANEL_2 = RGB(31, 28, 35);
static const COLORREF C_CARD_HOVER = RGB(39, 35, 43);
static const COLORREF C_BORDER = RGB(67, 57, 51);
static const COLORREF C_BORDER_HOT = RGB(121, 78, 45);
static const COLORREF C_TEXT = RGB(246, 242, 238);
static const COLORREF C_MUTED = RGB(180, 169, 163);
static const COLORREF C_DARK_TEXT = RGB(31, 24, 20);
static const COLORREF C_DANGER = RGB(238, 86, 92);
static const COLORREF C_GREEN = RGB(68, 214, 121);

static std::wstring GetOverlayConfigPath();
static POINT GetDefaultOverlayPosition();
static POINT LoadOverlayPosition();
static void SaveOverlayPosition(HWND hwnd);
static void PositionWindowSaved(HWND hwnd);
static POINT LoadAddWindowPosition();
static void SaveAddWindowPosition(HWND hwnd);
static void PositionAddWindowSaved(HWND hwnd);
static void ReloadAccounts();
static void LoginSelected();
static void HideAccountPanelNow();
static void ShowAccountPanelNow();

static HFONT CreateUiFont(int height, int weight)
{
    return CreateFontW(
        height,
        0,
        0,
        0,
        weight,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS,
        L"Segoe UI");
}

static void ApplyFont(HWND hwnd, HFONT font)
{
    if (hwnd && font)
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

static void FillSolidRect(HDC hdc, const RECT& rect, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

static void FillRoundedRect(
    HDC hdc,
    const RECT& rect,
    COLORREF fill,
    COLORREF border,
    int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    RoundRect(
        hdc,
        rect.left,
        rect.top,
        rect.right,
        rect.bottom,
        radius,
        radius);

    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void DrawUiText(
    HDC hdc,
    const std::wstring& text,
    RECT rect,
    HFONT font,
    COLORREF color,
    UINT format)
{
    HFONT oldFont = NULL;
    if (font)
        oldFont = reinterpret_cast<HFONT>(SelectObject(hdc, font));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text.empty() ? L"" : text.c_str(), -1, &rect, format);

    if (font)
        SelectObject(hdc, oldFont);
}

static void DrawShield(HDC hdc, int x, int y)
{
    POINT shield[] =
    {
        { x + 17, y },
        { x + 34, y + 6 },
        { x + 32, y + 25 },
        { x + 17, y + 37 },
        { x + 2, y + 25 },
        { x, y + 6 }
    };

    HBRUSH shieldBrush = CreateSolidBrush(C_ACCENT);
    HPEN shieldPen = CreatePen(PS_SOLID, 1, C_ACCENT_SOFT);
    HGDIOBJ oldBrush = SelectObject(hdc, shieldBrush);
    HGDIOBJ oldPen = SelectObject(hdc, shieldPen);
    Polygon(hdc, shield, ARRAYSIZE(shield));
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(shieldPen);
    DeleteObject(shieldBrush);

    HPEN checkPen = CreatePen(PS_SOLID, 3, RGB(255, 255, 255));
    oldPen = SelectObject(hdc, checkPen);
    MoveToEx(hdc, x + 9, y + 20, NULL);
    LineTo(hdc, x + 15, y + 26);
    LineTo(hdc, x + 26, y + 14);
    SelectObject(hdc, oldPen);
    DeleteObject(checkPen);
}

static void DrawButtonIcon(HDC hdc, PanelButton id, RECT rc, COLORREF color)
{
    const int cx = rc.left + 17;
    const int cy = (rc.top + rc.bottom) / 2;

    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, brush);

    if (id == BTN_LOGIN)
    {
        POINT play[] =
        {
            { cx - 4, cy - 7 },
            { cx - 4, cy + 7 },
            { cx + 8, cy }
        };
        Polygon(hdc, play, ARRAYSIZE(play));
    }
    else if (id == BTN_ADD)
    {
        MoveToEx(hdc, cx - 7, cy, NULL);
        LineTo(hdc, cx + 7, cy);
        MoveToEx(hdc, cx, cy - 7, NULL);
        LineTo(hdc, cx, cy + 7);
    }
    else if (id == BTN_FAVORITE)
    {
        POINT star[] =
        {
            { cx, cy - 9 },
            { cx + 3, cy - 3 },
            { cx + 10, cy - 2 },
            { cx + 5, cy + 3 },
            { cx + 6, cy + 10 },
            { cx, cy + 6 },
            { cx - 6, cy + 10 },
            { cx - 5, cy + 3 },
            { cx - 10, cy - 2 },
            { cx - 3, cy - 3 }
        };
        Polygon(hdc, star, ARRAYSIZE(star));
    }
    else if (id == BTN_DELETE)
    {
        Rectangle(hdc, cx - 6, cy - 5, cx + 7, cy + 8);
        MoveToEx(hdc, cx - 8, cy - 8, NULL);
        LineTo(hdc, cx + 9, cy - 8);
        MoveToEx(hdc, cx - 3, cy - 10, NULL);
        LineTo(hdc, cx + 4, cy - 10);
    }

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

static bool PtIn(const RECT& rc, int x, int y)
{
    POINT pt = { x, y };
    return PtInRect(&rc, pt) != FALSE;
}

static std::wstring Trim(const std::wstring& value)
{
    size_t begin = 0;
    while (begin < value.size() && iswspace(value[begin]))
        begin++;

    size_t end = value.size();
    while (end > begin && iswspace(value[end - 1]))
        end--;

    return value.substr(begin, end - begin);
}

static std::wstring GetWindowTextString(HWND hwnd)
{
    if (!hwnd)
        return {};

    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0)
        return {};

    std::wstring value(len + 1, L'\0');
    GetWindowTextW(hwnd, &value[0], len + 1);
    value.resize(len);
    return value;
}

static HWND FindLineageWindow()
{
    DWORD pid = GetCurrentProcessId();

    struct EnumData
    {
        DWORD pid;
        HWND hwnd;
    } data = { pid, NULL };

    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
    {
        EnumData* data = reinterpret_cast<EnumData*>(lParam);

        DWORD wndPid = 0;
        GetWindowThreadProcessId(hwnd, &wndPid);

        if (wndPid != data->pid)
            return TRUE;

        if (!IsWindowVisible(hwnd))
            return TRUE;

        wchar_t className[256] = {};
        GetClassNameW(hwnd, className, ARRAYSIZE(className));

        if (_wcsicmp(className, L"L2UnrealWWindowsViewportWindow") == 0)
        {
            data->hwnd = hwnd;
            return FALSE;
        }

        return TRUE;
    }, reinterpret_cast<LPARAM>(&data));

    return data.hwnd;
}

static bool IsSameWindowOrChild(HWND root, HWND target)
{
    if (!root || !target)
        return false;

    if (root == target)
        return true;

    HWND parent = target;
    while (parent)
    {
        parent = GetParent(parent);
        if (parent == root)
            return true;
    }

    return false;
}

static bool IsLineageForeground()
{
    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return false;

    if (IsSameWindowOrChild(g_Window, foreground))
        return true;

    if (g_AddWindow && IsSameWindowOrChild(g_AddWindow, foreground))
        return true;

    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);

    return foregroundPid == GetCurrentProcessId();
}

static void SetStatus(const wchar_t* text)
{
    g_StatusText = text ? text : L"";
    if (g_Window)
        InvalidateRect(g_Window, &g_StatusRect, FALSE);
}

static void InvalidatePanel()
{
    if (g_Window)
        InvalidateRect(g_Window, NULL, FALSE);
}

static int VisibleAccountRows()
{
    const int height = std::max(0, static_cast<int>(g_ListRect.bottom - g_ListRect.top - 16));
    return std::max(1, height / ACCOUNT_ROW_PITCH);
}

static void ClampSelectionAndScroll()
{
    const int count = static_cast<int>(g_Accounts.size());

    if (count <= 0)
    {
        g_SelectedIndex = -1;
        g_ScrollOffset = 0;
        return;
    }

    if (g_SelectedIndex < 0)
        g_SelectedIndex = 0;
    if (g_SelectedIndex >= count)
        g_SelectedIndex = count - 1;

    const int visibleRows = VisibleAccountRows();
    if (g_SelectedIndex < g_ScrollOffset)
        g_ScrollOffset = g_SelectedIndex;
    if (g_SelectedIndex >= g_ScrollOffset + visibleRows)
        g_ScrollOffset = g_SelectedIndex - visibleRows + 1;

    const int maxScroll = std::max(0, count - visibleRows);
    if (g_ScrollOffset > maxScroll)
        g_ScrollOffset = maxScroll;
    if (g_ScrollOffset < 0)
        g_ScrollOffset = 0;
}

static void SelectAccountByLogin(const std::wstring& login)
{
    g_SelectedIndex = -1;

    for (size_t i = 0; i < g_Accounts.size(); ++i)
    {
        if (_wcsicmp(g_Accounts[i].login.c_str(), login.c_str()) == 0)
        {
            g_SelectedIndex = static_cast<int>(i);
            break;
        }
    }

    ClampSelectionAndScroll();
}

static SavedAccount* GetSelectedAccount()
{
    if (g_SelectedIndex < 0 ||
        g_SelectedIndex >= static_cast<int>(g_Accounts.size()))
    {
        return NULL;
    }

    return &g_Accounts[g_SelectedIndex];
}

static void ReloadAccounts()
{
    const std::wstring selectedLogin =
        GetSelectedAccount() ? GetSelectedAccount()->login : L"";

    g_Accounts.clear();
    AccountVault_LoadAccounts(g_Accounts);

    if (!selectedLogin.empty())
        SelectAccountByLogin(selectedLogin);
    else
        ClampSelectionAndScroll();

    if (g_Accounts.empty())
        SetStatus(L"Nenhuma conta salva. Adicione sua primeira conta.");
    else
        SetStatus(L"Cofre pronto. Selecione uma conta para entrar.");

    InvalidatePanel();
}

static void LoginSelected()
{
    SavedAccount* account = GetSelectedAccount();
    if (!account)
    {
        SetStatus(L"Selecione uma conta primeiro.");
        return;
    }

    if (!AccountLogin_CanOpenPanel())
    {
        SetStatus(AccountLogin_GetPanelBlockReason());
        return;
    }

    if (AccountLogin_Request(account->login, account->password))
    {
        SetStatus(L"Login enviado.");
        HideAccountPanelNow();
    }
    else
    {
        SetStatus(L"Cliente ainda n\u00e3o est\u00e1 pronto para login.");
    }
}

static void DeleteSelected()
{
    SavedAccount* account = GetSelectedAccount();
    if (!account)
    {
        SetStatus(L"Selecione uma conta para excluir.");
        return;
    }

    const std::wstring login = account->login;
    if (AccountVault_DeleteAccount(login))
    {
        ReloadAccounts();
        SetStatus(L"Conta exclu\u00edda.");
    }
    else
    {
        SetStatus(L"N\u00e3o foi poss\u00edvel excluir a conta.");
    }
}

static void ToggleFavoriteSelected()
{
    SavedAccount* account = GetSelectedAccount();
    if (!account)
    {
        SetStatus(L"Selecione uma conta para favoritar.");
        return;
    }

    const std::wstring login = account->login;
    const bool newValue = !account->favorite;

    if (AccountVault_SetFavorite(login, newValue))
    {
        ReloadAccounts();
        SelectAccountByLogin(login);
        SetStatus(newValue ? L"Conta marcada como favorita." : L"Favorito removido.");
    }
}

static void LayoutPanel()
{
    g_CloseRect = { 318, 16, 344, 42 };
    g_SummaryRect = { 16, 70, 344, 124 };
    g_ListRect = { 16, 136, 344, 346 };
    g_StatusRect = { 16, 452, 344, 476 };

    g_PanelButtons[0] = { BTN_CLOSE, g_CloseRect };
    g_PanelButtons[1] = { BTN_LOGIN, { 16, 360, 170, 394 } };
    g_PanelButtons[2] = { BTN_ADD, { 190, 360, 344, 394 } };
    g_PanelButtons[3] = { BTN_FAVORITE, { 16, 406, 170, 440 } };
    g_PanelButtons[4] = { BTN_DELETE, { 190, 406, 344, 440 } };
}

static int HitPanelButton(int x, int y)
{
    LayoutPanel();

    for (const ButtonRect& button : g_PanelButtons)
    {
        if (button.id != BTN_NONE && PtIn(button.rc, x, y))
            return button.id;
    }

    return BTN_NONE;
}

static int HitAccountIndex(int x, int y)
{
    LayoutPanel();

    if (!PtIn(g_ListRect, x, y))
        return -1;

    const int visibleRows = VisibleAccountRows();
    for (int row = 0; row < visibleRows; ++row)
    {
        const int index = g_ScrollOffset + row;
        if (index >= static_cast<int>(g_Accounts.size()))
            break;

        RECT rowRect =
        {
            g_ListRect.left + 10,
            g_ListRect.top + 10 + row * ACCOUNT_ROW_PITCH,
            g_ListRect.right - 10,
            g_ListRect.top + 10 + row * ACCOUNT_ROW_PITCH + ACCOUNT_ROW_H
        };

        if (PtIn(rowRect, x, y))
            return index;
    }

    return -1;
}

static void DrawCloseButton(HDC hdc)
{
    const bool hot = g_HoverButton == BTN_CLOSE;
    const bool pressed = g_PressedButton == BTN_CLOSE;
    const COLORREF fill = pressed ? RGB(48, 40, 43) :
        (hot ? RGB(42, 36, 42) : RGB(28, 26, 33));
    const COLORREF border = hot ? RGB(99, 77, 72) : RGB(58, 53, 64);

    FillRoundedRect(hdc, g_CloseRect, fill, border, 8);

    HPEN pen = CreatePen(PS_SOLID, 2, hot ? C_TEXT : RGB(210, 202, 197));
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, g_CloseRect.left + 8, g_CloseRect.top + 8, NULL);
    LineTo(hdc, g_CloseRect.right - 8, g_CloseRect.bottom - 8);
    MoveToEx(hdc, g_CloseRect.right - 8, g_CloseRect.top + 8, NULL);
    LineTo(hdc, g_CloseRect.left + 8, g_CloseRect.bottom - 8);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

static void DrawActionButton(
    HDC hdc,
    PanelButton id,
    RECT rect,
    const wchar_t* label,
    bool enabled)
{
    const bool hot = enabled && g_HoverButton == id;
    const bool pressed = enabled && g_PressedButton == id;

    COLORREF fill = enabled ? C_PANEL_2 : RGB(29, 27, 32);
    COLORREF border = enabled ? C_BORDER : RGB(48, 45, 51);
    COLORREF text = enabled ? C_TEXT : RGB(115, 109, 111);

    if (id == BTN_LOGIN && enabled)
    {
        fill = hot ? C_ACCENT_HOVER : C_ACCENT;
        border = hot ? RGB(255, 163, 112) : RGB(255, 116, 57);
        text = RGB(255, 255, 255);
    }
    else if (hot)
    {
        fill = C_CARD_HOVER;
        border = C_BORDER_HOT;
    }

    if (pressed)
        fill = id == BTN_LOGIN ? RGB(211, 62, 21) : RGB(34, 30, 37);

    FillRoundedRect(hdc, rect, fill, border, 10);

    RECT iconRect = rect;
    iconRect.right = iconRect.left + 38;
    DrawButtonIcon(hdc, id, iconRect, id == BTN_LOGIN && enabled ? RGB(255, 255, 255) : text);

    RECT textRect = rect;
    textRect.left += 36;
    textRect.right -= 10;
    DrawUiText(
        hdc,
        label,
        textRect,
        g_ButtonFont,
        text,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void DrawAccountRow(HDC hdc, int index, RECT row)
{
    const SavedAccount& account = g_Accounts[index];
    const bool selected = index == g_SelectedIndex;
    const bool hot = index == g_HoverAccountIndex;

    COLORREF fill = selected ? RGB(43, 33, 29) : C_PANEL_2;
    COLORREF border = selected ? C_ACCENT_SOFT : C_BORDER;

    if (hot && !selected)
    {
        fill = C_CARD_HOVER;
        border = C_BORDER_HOT;
    }

    FillRoundedRect(hdc, row, fill, border, 8);

    RECT dot = { row.left + 12, row.top + 15, row.left + 22, row.top + 25 };
    FillRoundedRect(
        hdc,
        dot,
        account.favorite ? C_ACCENT : C_GREEN,
        account.favorite ? C_ACCENT : C_GREEN,
        10);

    RECT nameRect =
    {
        row.left + 34,
        row.top + 5,
        row.right - 58,
        row.top + 24
    };

    const std::wstring display =
        account.name.empty() ? account.login : account.name;

    DrawUiText(
        hdc,
        display,
        nameRect,
        g_ButtonFont,
        C_TEXT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT loginRect =
    {
        row.left + 34,
        row.top + 23,
        row.right - 14,
        row.bottom - 4
    };

    DrawUiText(
        hdc,
        account.login,
        loginRect,
        g_LabelFont,
        C_MUTED,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (account.favorite)
    {
        RECT favRect = { row.right - 50, row.top + 10, row.right - 12, row.top + 29 };
        FillRoundedRect(hdc, favRect, RGB(52, 37, 25), RGB(119, 78, 42), 8);
        DrawUiText(
            hdc,
            L"FAV",
            favRect,
            g_LabelFont,
            C_ACCENT_SOFT,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static void DrawAccountList(HDC hdc)
{
    FillRoundedRect(hdc, g_ListRect, C_PANEL, C_BORDER, 12);

    if (g_Accounts.empty())
    {
        RECT title = { g_ListRect.left + 20, g_ListRect.top + 62, g_ListRect.right - 20, g_ListRect.top + 88 };
        RECT detail = { g_ListRect.left + 20, g_ListRect.top + 90, g_ListRect.right - 20, g_ListRect.top + 120 };

        DrawUiText(
            hdc,
            L"Nenhuma conta salva",
            title,
            g_StatusFont,
            C_TEXT,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        DrawUiText(
            hdc,
            L"Use Adicionar para criar seu cofre.",
            detail,
            g_TextFont,
            C_MUTED,
            DT_CENTER | DT_TOP | DT_WORDBREAK);
        return;
    }

    const int visibleRows = VisibleAccountRows();
    for (int row = 0; row < visibleRows; ++row)
    {
        const int index = g_ScrollOffset + row;
        if (index >= static_cast<int>(g_Accounts.size()))
            break;

        RECT rowRect =
        {
            g_ListRect.left + 10,
            g_ListRect.top + 10 + row * ACCOUNT_ROW_PITCH,
            g_ListRect.right - 10,
            g_ListRect.top + 10 + row * ACCOUNT_ROW_PITCH + ACCOUNT_ROW_H
        };

        DrawAccountRow(hdc, index, rowRect);
    }

    if (static_cast<int>(g_Accounts.size()) > visibleRows)
    {
        const int trackTop = g_ListRect.top + 12;
        const int trackBottom = g_ListRect.bottom - 12;
        const int trackH = trackBottom - trackTop;
        const int total = static_cast<int>(g_Accounts.size());
        const int thumbH = std::max(24, (trackH * visibleRows) / total);
        const int maxScroll = std::max(1, total - visibleRows);
        const int thumbTop = trackTop +
            ((trackH - thumbH) * g_ScrollOffset) / maxScroll;

        RECT track = { g_ListRect.right - 8, trackTop, g_ListRect.right - 4, trackBottom };
        RECT thumb = { track.left, thumbTop, track.right, thumbTop + thumbH };
        FillRoundedRect(hdc, track, RGB(40, 36, 42), RGB(40, 36, 42), 4);
        FillRoundedRect(hdc, thumb, C_ACCENT_SOFT, C_ACCENT_SOFT, 4);
    }
}

static void PaintAccountPanel(HWND hwnd)
{
    PAINTSTRUCT paint = {};
    HDC windowDc = BeginPaint(hwnd, &paint);

    RECT client = {};
    GetClientRect(hwnd, &client);
    LayoutPanel();

    HDC bufferDc = CreateCompatibleDC(windowDc);
    HBITMAP bufferBitmap = CreateCompatibleBitmap(
        windowDc,
        client.right,
        client.bottom);
    HGDIOBJ oldBitmap = SelectObject(bufferDc, bufferBitmap);

    FillSolidRect(bufferDc, client, C_BG);
    RECT accent = { 0, 0, client.right, 4 };
    FillSolidRect(bufferDc, accent, C_ACCENT);

    HPEN border = CreatePen(PS_SOLID, 1, RGB(136, 61, 18));
    HGDIOBJ oldPen = SelectObject(bufferDc, border);
    HGDIOBJ oldBrush = SelectObject(bufferDc, GetStockObject(NULL_BRUSH));
    RoundRect(bufferDc, 0, 0, client.right - 1, client.bottom - 1, 18, 18);
    SelectObject(bufferDc, oldBrush);
    SelectObject(bufferDc, oldPen);
    DeleteObject(border);

    DrawShield(bufferDc, 18, 18);

    RECT title = { 64, 14, 300, 38 };
    DrawUiText(
        bufferDc,
        L"CONTAS",
        title,
        g_TitleFont,
        RGB(255, 255, 255),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT subtitle = { 65, 38, 300, 56 };
    DrawUiText(
        bufferDc,
        L"PAINEL DO JOGO",
        subtitle,
        g_SubtitleFont,
        C_ACCENT_SOFT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawCloseButton(bufferDc);

    FillRoundedRect(bufferDc, g_SummaryRect, C_PANEL, C_BORDER, 12);

    const int favoriteCount = static_cast<int>(std::count_if(
        g_Accounts.begin(),
        g_Accounts.end(),
        [](const SavedAccount& account) { return account.favorite; }));

    RECT summaryTitle = { g_SummaryRect.left + 14, g_SummaryRect.top + 8, g_SummaryRect.right - 14, g_SummaryRect.top + 29 };
    DrawUiText(
        bufferDc,
        L"Cofre de contas",
        summaryTitle,
        g_StatusFont,
        C_TEXT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    wchar_t summary[96] = {};
    swprintf_s(
        summary,
        ARRAYSIZE(summary),
        L"%d conta%s salva%s  |  %d favorita%s",
        static_cast<int>(g_Accounts.size()),
        g_Accounts.size() == 1 ? L"" : L"s",
        g_Accounts.size() == 1 ? L"" : L"s",
        favoriteCount,
        favoriteCount == 1 ? L"" : L"s");

    RECT summaryDetail = { g_SummaryRect.left + 14, g_SummaryRect.top + 30, g_SummaryRect.right - 14, g_SummaryRect.bottom - 8 };
    DrawUiText(
        bufferDc,
        summary,
        summaryDetail,
        g_TextFont,
        C_MUTED,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    DrawAccountList(bufferDc);

    DrawActionButton(
        bufferDc,
        BTN_LOGIN,
        g_PanelButtons[1].rc,
        L"Entrar",
        GetSelectedAccount() != NULL && AccountLogin_CanOpenPanel());
    DrawActionButton(
        bufferDc,
        BTN_ADD,
        g_PanelButtons[2].rc,
        L"Adicionar",
        AccountLogin_CanOpenPanel());
    DrawActionButton(
        bufferDc,
        BTN_FAVORITE,
        g_PanelButtons[3].rc,
        L"Favorito",
        GetSelectedAccount() != NULL);
    DrawActionButton(
        bufferDc,
        BTN_DELETE,
        g_PanelButtons[4].rc,
        L"Excluir",
        GetSelectedAccount() != NULL);

    FillRoundedRect(bufferDc, g_StatusRect, RGB(22, 20, 25), RGB(54, 48, 54), 9);
    DrawUiText(
        bufferDc,
        g_StatusText,
        { g_StatusRect.left + 10, g_StatusRect.top, g_StatusRect.right - 10, g_StatusRect.bottom },
        g_LabelFont,
        C_MUTED,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(
        windowDc,
        0,
        0,
        client.right,
        client.bottom,
        bufferDc,
        0,
        0,
        SRCCOPY);

    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bufferBitmap);
    DeleteDC(bufferDc);
    EndPaint(hwnd, &paint);
}

static void LayoutAddDialog()
{
    g_AddLoginRect = { 18, 74, 342, 104 };
    g_AddPassRect = { 18, 124, 342, 154 };
    g_AddButtons[0] = { ADD_BTN_SAVE, { 18, 168, 114, 202 } };
    g_AddButtons[1] = { ADD_BTN_SAVE_LOGIN, { 124, 168, 236, 202 } };
    g_AddButtons[2] = { ADD_BTN_CANCEL, { 246, 168, 342, 202 } };
}

static int HitAddButton(int x, int y)
{
    LayoutAddDialog();

    for (const ButtonRect& button : g_AddButtons)
    {
        if (button.id != ADD_BTN_NONE && PtIn(button.rc, x, y))
            return button.id;
    }

    return ADD_BTN_NONE;
}

static void DrawPlainButton(
    HDC hdc,
    int id,
    RECT rect,
    const wchar_t* label,
    bool accent)
{
    const bool hot = g_AddHoverButton == id;
    const bool pressed = g_AddPressedButton == id;

    COLORREF fill = accent ? C_ACCENT : C_PANEL_2;
    COLORREF border = accent ? RGB(255, 116, 57) : C_BORDER;
    COLORREF text = accent ? RGB(255, 255, 255) : C_TEXT;

    if (hot)
    {
        fill = accent ? C_ACCENT_HOVER : C_CARD_HOVER;
        border = accent ? RGB(255, 163, 112) : C_BORDER_HOT;
    }

    if (pressed)
        fill = accent ? RGB(211, 62, 21) : RGB(34, 30, 37);

    FillRoundedRect(hdc, rect, fill, border, 10);
    DrawUiText(
        hdc,
        label,
        rect,
        g_ButtonFont,
        text,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

static void PaintAddDialog(HWND hwnd)
{
    PAINTSTRUCT paint = {};
    HDC windowDc = BeginPaint(hwnd, &paint);

    RECT client = {};
    GetClientRect(hwnd, &client);
    LayoutAddDialog();

    HDC bufferDc = CreateCompatibleDC(windowDc);
    HBITMAP bufferBitmap = CreateCompatibleBitmap(
        windowDc,
        client.right,
        client.bottom);
    HGDIOBJ oldBitmap = SelectObject(bufferDc, bufferBitmap);

    FillSolidRect(bufferDc, client, C_BG);
    RECT accent = { 0, 0, client.right, 4 };
    FillSolidRect(bufferDc, accent, C_ACCENT);

    HPEN border = CreatePen(PS_SOLID, 1, RGB(136, 61, 18));
    HGDIOBJ oldPen = SelectObject(bufferDc, border);
    HGDIOBJ oldBrush = SelectObject(bufferDc, GetStockObject(NULL_BRUSH));
    RoundRect(bufferDc, 0, 0, client.right - 1, client.bottom - 1, 18, 18);
    SelectObject(bufferDc, oldBrush);
    SelectObject(bufferDc, oldPen);
    DeleteObject(border);

    RECT title = { 18, 16, client.right - 18, 42 };
    DrawUiText(
        bufferDc,
        L"Nova conta",
        title,
        g_TitleFont,
        C_TEXT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT detail = { 18, 43, client.right - 18, 62 };
    DrawUiText(
        bufferDc,
        L"Salve o login no cofre local.",
        detail,
        g_TextFont,
        C_MUTED,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT loginLabel = { 20, 59, 160, 74 };
    DrawUiText(bufferDc, L"Login", loginLabel, g_LabelFont, C_ACCENT_SOFT, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    FillRoundedRect(bufferDc, g_AddLoginRect, C_PANEL_2, C_BORDER, 8);

    RECT passLabel = { 20, 109, 160, 124 };
    DrawUiText(bufferDc, L"Senha", passLabel, g_LabelFont, C_ACCENT_SOFT, DT_LEFT | DT_BOTTOM | DT_SINGLELINE);
    FillRoundedRect(bufferDc, g_AddPassRect, C_PANEL_2, C_BORDER, 8);

    DrawPlainButton(bufferDc, ADD_BTN_SAVE, g_AddButtons[0].rc, L"Salvar", false);
    DrawPlainButton(bufferDc, ADD_BTN_SAVE_LOGIN, g_AddButtons[1].rc, L"Salvar + entrar", true);
    DrawPlainButton(bufferDc, ADD_BTN_CANCEL, g_AddButtons[2].rc, L"Cancelar", false);

    RECT statusRect = { 18, 212, 342, 236 };
    DrawUiText(
        bufferDc,
        g_AddStatusText,
        statusRect,
        g_LabelFont,
        g_AddStatusText.empty() ? C_MUTED : C_ACCENT_SOFT,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(
        windowDc,
        0,
        0,
        client.right,
        client.bottom,
        bufferDc,
        0,
        0,
        SRCCOPY);

    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bufferBitmap);
    DeleteDC(bufferDc);
    EndPaint(hwnd, &paint);
}

static HWND CreateEditControl(HWND parent, bool password, RECT rect)
{
    DWORD style = WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP;
    if (password)
        style |= ES_PASSWORD;

    HWND hwnd = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        style,
        rect.left + 8,
        rect.top + 6,
        rect.right - rect.left - 16,
        rect.bottom - rect.top - 10,
        parent,
        NULL,
        g_Module,
        NULL);

    ApplyFont(hwnd, g_TextFont);
    return hwnd;
}

static void PerformAddSave(bool loginAfter)
{
    const std::wstring login = Trim(GetWindowTextString(g_AddLoginEdit));
    const std::wstring pass = GetWindowTextString(g_AddPassEdit);

    if (login.empty() || pass.empty())
    {
        g_AddStatusText = L"Informe login e senha.";
        if (g_AddWindow)
            InvalidateRect(g_AddWindow, NULL, FALSE);
        return;
    }

    SavedAccount account;
    account.name = login;
    account.login = login;
    account.password = pass;
    account.favorite = false;

    if (!AccountVault_SaveAccount(account))
    {
        g_AddStatusText = L"N\u00e3o foi poss\u00edvel salvar a conta.";
        if (g_AddWindow)
            InvalidateRect(g_AddWindow, NULL, FALSE);
        return;
    }

    ReloadAccounts();
    SelectAccountByLogin(account.login);
    SetStatus(L"Conta salva.");

    if (loginAfter)
    {
        if (AccountLogin_Request(account.login, account.password))
        {
            SetStatus(L"Login enviado.");
            HideAccountPanelNow();
        }
        else
        {
            SetStatus(AccountLogin_GetPanelBlockReason());
        }
    }

    if (g_AddWindow)
        DestroyWindow(g_AddWindow);
}

static LRESULT CALLBACK AddEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN)
    {
        if (wParam == VK_ESCAPE)
        {
            if (g_AddWindow)
                DestroyWindow(g_AddWindow);
            return 0;
        }

        if (wParam == VK_RETURN)
        {
            PerformAddSave(false);
            return 0;
        }

        if (wParam == VK_TAB)
        {
            SetFocus(hwnd == g_AddLoginEdit ? g_AddPassEdit : g_AddLoginEdit);
            return 0;
        }
    }

    return CallWindowProcW(g_OriginalEditProc, hwnd, msg, wParam, lParam);
}

static void HideAccountWindowsForBackground()
{
    if (g_AddWindow)
        DestroyWindow(g_AddWindow);

    if (g_Window && IsWindowVisible(g_Window))
        ShowWindow(g_Window, SW_HIDE);
}

static void HideAccountWindowsForUnavailable()
{
    g_VisibleRequested = false;
    HideAccountWindowsForBackground();
}

static void PollAccountOverlayInput()
{
    const bool insertDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;

    if (!IsLineageForeground())
    {
        HideAccountWindowsForBackground();
        g_InsertWasDown = insertDown;
        return;
    }

    if (!AccountLogin_CanOpenPanel())
    {
        HideAccountWindowsForUnavailable();
        g_InsertWasDown = insertDown;
        return;
    }

    if (insertDown && !g_InsertWasDown)
    {
        if (g_VisibleRequested)
            HideAccountPanelNow();
        else
            ShowAccountPanelNow();
    }
    else if (g_VisibleRequested && g_Window && !IsWindowVisible(g_Window))
    {
        ShowAccountPanelNow();
    }

    g_InsertWasDown = insertDown;
}

static LRESULT CALLBACK AddAccountProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_AddWindow = hwnd;
        g_AddStatusText.clear();
        g_AddHoverButton = ADD_BTN_NONE;
        g_AddPressedButton = ADD_BTN_NONE;

        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_AppIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_AppIcon));

        HRGN region = CreateRoundRectRgn(0, 0, ADD_W + 1, ADD_H + 1, 18, 18);
        SetWindowRgn(hwnd, region, TRUE);

        LayoutAddDialog();
        g_AddLoginEdit = CreateEditControl(hwnd, false, g_AddLoginRect);
        g_AddPassEdit = CreateEditControl(hwnd, true, g_AddPassRect);

        if (g_AddLoginEdit)
        {
            g_OriginalEditProc = reinterpret_cast<WNDPROC>(
                SetWindowLongPtrW(g_AddLoginEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AddEditProc)));
        }

        if (g_AddPassEdit)
        {
            SetWindowLongPtrW(g_AddPassEdit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(AddEditProc));
        }

        PositionAddWindowSaved(hwnd);
        SetFocus(g_AddLoginEdit);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintAddDialog(hwnd);
        return 0;

    case WM_MOVE:
        if (IsWindowVisible(hwnd))
            SaveAddWindowPosition(hwnd);
        return 0;

    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (hit != HTCLIENT)
            return hit;

        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &point);
        return point.y <= 58 ? HTCAPTION : HTCLIENT;
    }

    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int hot = HitAddButton(x, y);

        if (hot != g_AddHoverButton)
        {
            g_AddHoverButton = hot;
            InvalidateRect(hwnd, NULL, FALSE);
        }

        TRACKMOUSEEVENT tracking = {};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = hwnd;
        TrackMouseEvent(&tracking);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_AddHoverButton != ADD_BTN_NONE)
        {
            g_AddHoverButton = ADD_BTN_NONE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        const int button = HitAddButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (button != ADD_BTN_NONE)
        {
            g_AddPressedButton = button;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        const int button = HitAddButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        const int pressed = g_AddPressedButton;
        g_AddPressedButton = ADD_BTN_NONE;
        ReleaseCapture();
        InvalidateRect(hwnd, NULL, FALSE);

        if (button == pressed)
        {
            if (button == ADD_BTN_SAVE)
                PerformAddSave(false);
            else if (button == ADD_BTN_SAVE_LOGIN)
                PerformAddSave(true);
            else if (button == ADD_BTN_CANCEL)
                DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_SETCURSOR:
    {
        POINT point = {};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);
        SetCursor(LoadCursor(NULL, HitAddButton(point.x, point.y) ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, C_TEXT);
        SetBkColor(hdc, C_PANEL_2);
        return reinterpret_cast<LRESULT>(g_EditBrush);
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == VK_RETURN)
        {
            PerformAddSave(false);
            return 0;
        }
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_AddWindow = NULL;
        g_AddLoginEdit = NULL;
        g_AddPassEdit = NULL;
        g_AddHoverButton = ADD_BTN_NONE;
        g_AddPressedButton = ADD_BTN_NONE;
        g_AddStatusText.clear();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowAddAccountDialog(HWND parent)
{
    if (!AccountLogin_CanOpenPanel())
    {
        SetStatus(AccountLogin_GetPanelBlockReason());
        return;
    }

    if (g_AddWindow)
    {
        SetForegroundWindow(g_AddWindow);
        return;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = AddAccountProc;
    wc.hInstance = g_Module;
    wc.lpszClassName = L"L2AccountVaultAddWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_BgBrush;
    wc.hIcon = g_AppIcon;

    RegisterClassW(&wc);

    HWND wnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        wc.lpszClassName,
        L"Nova conta",
        WS_POPUP,
        200,
        200,
        ADD_W,
        ADD_H,
        parent,
        NULL,
        g_Module,
        NULL);

    if (!wnd)
        return;

    SetLayeredWindowAttributes(wnd, 0, 247, LWA_ALPHA);
    ShowWindow(wnd, SW_SHOW);
    UpdateWindow(wnd);
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_AppIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_AppIcon));

        HRGN region = CreateRoundRectRgn(0, 0, PANEL_W + 1, PANEL_H + 1, 18, 18);
        SetWindowRgn(hwnd, region, TRUE);

        LayoutPanel();
        ReloadAccounts();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        PaintAccountPanel(hwnd);
        return 0;

    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
        if (hit != HTCLIENT)
            return hit;

        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &point);

        if (PtIn(g_CloseRect, point.x, point.y))
            return HTCLIENT;

        return point.y <= 60 ? HTCAPTION : HTCLIENT;
    }

    case WM_MOVE:
        if (IsWindowVisible(hwnd))
            SaveOverlayPosition(hwnd);
        return 0;

    case WM_MOUSEMOVE:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int hotButton = HitPanelButton(x, y);
        const int hotAccount = HitAccountIndex(x, y);

        if (hotButton != g_HoverButton || hotAccount != g_HoverAccountIndex)
        {
            g_HoverButton = hotButton;
            g_HoverAccountIndex = hotAccount;
            InvalidatePanel();
        }

        TRACKMOUSEEVENT tracking = {};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = hwnd;
        TrackMouseEvent(&tracking);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_HoverButton != BTN_NONE || g_HoverAccountIndex != -1)
        {
            g_HoverButton = BTN_NONE;
            g_HoverAccountIndex = -1;
            InvalidatePanel();
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        const int x = GET_X_LPARAM(lParam);
        const int y = GET_Y_LPARAM(lParam);
        const int button = HitPanelButton(x, y);

        if (button != BTN_NONE)
        {
            g_PressedButton = button;
            SetCapture(hwnd);
            InvalidatePanel();
            return 0;
        }

        const int accountIndex = HitAccountIndex(x, y);
        if (accountIndex >= 0)
        {
            const ULONGLONG now = GetTickCount64();
            const bool doubleClick =
                accountIndex == g_LastClickedAccount &&
                now - g_LastAccountClickAt <= 350;

            g_SelectedIndex = accountIndex;
            ClampSelectionAndScroll();
            InvalidatePanel();

            g_LastClickedAccount = accountIndex;
            g_LastAccountClickAt = now;

            if (doubleClick)
                LoginSelected();
        }

        return 0;
    }

    case WM_LBUTTONUP:
    {
        const int button = HitPanelButton(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        const int pressed = g_PressedButton;
        g_PressedButton = BTN_NONE;
        ReleaseCapture();
        InvalidatePanel();

        if (button != pressed)
            return 0;

        if (button == BTN_CLOSE)
            HideAccountPanelNow();
        else if (button == BTN_LOGIN)
            LoginSelected();
        else if (button == BTN_ADD)
            ShowAddAccountDialog(hwnd);
        else if (button == BTN_FAVORITE)
            ToggleFavoriteSelected();
        else if (button == BTN_DELETE)
            DeleteSelected();

        return 0;
    }

    case WM_MOUSEWHEEL:
    {
        if (g_Accounts.empty())
            return 0;

        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        const int visibleRows = VisibleAccountRows();
        const int maxScroll = std::max(0, static_cast<int>(g_Accounts.size()) - visibleRows);

        if (delta < 0)
            g_ScrollOffset = std::min(maxScroll, g_ScrollOffset + 1);
        else if (delta > 0)
            g_ScrollOffset = std::max(0, g_ScrollOffset - 1);

        InvalidatePanel();
        return 0;
    }

    case WM_SETCURSOR:
    {
        POINT point = {};
        GetCursorPos(&point);
        ScreenToClient(hwnd, &point);

        const bool hand =
            HitPanelButton(point.x, point.y) != BTN_NONE ||
            HitAccountIndex(point.x, point.y) >= 0;

        SetCursor(LoadCursor(NULL, hand ? IDC_HAND : IDC_ARROW));
        return TRUE;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            HideAccountPanelNow();
            return 0;
        }

        if (wParam == VK_RETURN)
        {
            LoginSelected();
            return 0;
        }

        if (wParam == VK_UP && !g_Accounts.empty())
        {
            g_SelectedIndex = std::max(0, g_SelectedIndex - 1);
            ClampSelectionAndScroll();
            InvalidatePanel();
            return 0;
        }

        if (wParam == VK_DOWN && !g_Accounts.empty())
        {
            g_SelectedIndex = std::min(static_cast<int>(g_Accounts.size()) - 1, g_SelectedIndex + 1);
            ClampSelectionAndScroll();
            InvalidatePanel();
            return 0;
        }
        return 0;

    case WM_TIMER:
        if (wParam == ACCOUNT_INPUT_TIMER_ID)
        {
            PollAccountOverlayInput();
            return 0;
        }
        return 0;

    case WM_ACCOUNT_SHOW:
        ShowAccountPanelNow();
        return 0;

    case WM_ACCOUNT_HIDE:
        HideAccountPanelNow();
        return 0;

    case WM_ACCOUNT_TOGGLE:
        if (g_VisibleRequested)
            HideAccountPanelNow();
        else
            ShowAccountPanelNow();
        return 0;

    case WM_ACCOUNT_SHUTDOWN:
        if (g_AddWindow)
            DestroyWindow(g_AddWindow);
        KillTimer(hwnd, ACCOUNT_INPUT_TIMER_ID);
        DestroyWindow(hwnd);
        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        HideAccountPanelNow();
        return 0;

    case WM_DESTROY:
        g_Window = NULL;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI OverlayThread(LPVOID)
{
    g_BgBrush = CreateSolidBrush(C_BG);
    g_PanelBrush = CreateSolidBrush(C_PANEL);
    g_EditBrush = CreateSolidBrush(C_PANEL_2);

    g_TitleFont = CreateUiFont(22, FW_BOLD);
    g_SubtitleFont = CreateUiFont(11, FW_SEMIBOLD);
    g_StatusFont = CreateUiFont(16, FW_BOLD);
    g_LabelFont = CreateUiFont(10, FW_SEMIBOLD);
    g_TextFont = CreateUiFont(14, FW_NORMAL);
    g_ButtonFont = CreateUiFont(12, FW_BOLD);

    g_AppIcon = reinterpret_cast<HICON>(LoadImageW(
        g_Module,
        MAKEINTRESOURCEW(IDI_TRAYICON),
        IMAGE_ICON,
        16,
        16,
        LR_DEFAULTCOLOR));

    if (!g_AppIcon)
        g_AppIcon = LoadIconW(NULL, IDI_APPLICATION);

    WNDCLASSW wc = {};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = g_Module;
    wc.lpszClassName = L"L2AccountVaultPanelWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_BgBrush;
    wc.hIcon = g_AppIcon;

    RegisterClassW(&wc);

    g_Window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        wc.lpszClassName,
        L"Painel de contas",
        WS_POPUP,
        100,
        100,
        PANEL_W,
        PANEL_H,
        NULL,
        NULL,
        g_Module,
        NULL);

    if (g_Window)
    {
        SetLayeredWindowAttributes(g_Window, 0, 247, LWA_ALPHA);
        g_InsertWasDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
        SetTimer(g_Window, ACCOUNT_INPUT_TIMER_ID, 100, NULL);
        ShowWindow(g_Window, SW_HIDE);
    }

    if (g_OverlayReadyEvent)
        SetEvent(g_OverlayReadyEvent);

    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}

bool AccountOverlay_Initialize(HMODULE module)
{
    g_Module = module;

    if (g_OverlayThread)
        return true;

    g_OverlayReadyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_OverlayReadyEvent)
        return false;

    g_OverlayThread = CreateThread(NULL, 0, OverlayThread, NULL, 0, &g_OverlayThreadId);
    if (!g_OverlayThread)
    {
        CloseHandle(g_OverlayReadyEvent);
        g_OverlayReadyEvent = NULL;
        return false;
    }

    WaitForSingleObject(g_OverlayReadyEvent, 3000);

    CloseHandle(g_OverlayReadyEvent);
    g_OverlayReadyEvent = NULL;

    return g_Window != NULL;
}

void AccountOverlay_Shutdown()
{
    if (g_Window)
    {
        PostMessageW(g_Window, WM_ACCOUNT_SHUTDOWN, 0, 0);
    }
    else if (g_OverlayThreadId)
    {
        PostThreadMessageW(g_OverlayThreadId, WM_QUIT, 0, 0);
    }

    if (g_OverlayThread)
    {
        WaitForSingleObject(g_OverlayThread, 1500);
        CloseHandle(g_OverlayThread);
        g_OverlayThread = NULL;
    }

    if (g_BgBrush)
    {
        DeleteObject(g_BgBrush);
        g_BgBrush = NULL;
    }

    if (g_PanelBrush)
    {
        DeleteObject(g_PanelBrush);
        g_PanelBrush = NULL;
    }

    if (g_EditBrush)
    {
        DeleteObject(g_EditBrush);
        g_EditBrush = NULL;
    }

    HFONT* fonts[] =
    {
        &g_TitleFont,
        &g_SubtitleFont,
        &g_StatusFont,
        &g_LabelFont,
        &g_TextFont,
        &g_ButtonFont
    };

    for (HFONT* font : fonts)
    {
        if (*font)
        {
            DeleteObject(*font);
            *font = NULL;
        }
    }

    if (g_AppIcon)
    {
        DestroyIcon(g_AppIcon);
        g_AppIcon = NULL;
    }

    g_OverlayThreadId = 0;
    g_VisibleRequested = false;
    g_InsertWasDown = false;
    g_SelectedIndex = -1;
    g_ScrollOffset = 0;
    g_HoverButton = BTN_NONE;
    g_PressedButton = BTN_NONE;
}

static void ShowAccountPanelNow()
{
    if (!g_Window)
        return;

    g_VisibleRequested = true;

    if (!IsLineageForeground())
    {
        HideAccountWindowsForBackground();
        return;
    }

    if (!AccountLogin_CanOpenPanel())
    {
        HideAccountWindowsForUnavailable();
        return;
    }

    ReloadAccounts();
    PositionWindowSaved(g_Window);
    ShowWindow(g_Window, SW_SHOWNOACTIVATE);
    InvalidatePanel();
}

static void HideAccountPanelNow()
{
    if (!g_Window)
        return;

    g_VisibleRequested = false;

    if (g_AddWindow)
        DestroyWindow(g_AddWindow);

    ShowWindow(g_Window, SW_HIDE);
}

void AccountOverlay_Show()
{
    if (!g_Window)
        return;

    PostMessageW(g_Window, WM_ACCOUNT_SHOW, 0, 0);
}

void AccountOverlay_Hide()
{
    if (!g_Window)
        return;

    PostMessageW(g_Window, WM_ACCOUNT_HIDE, 0, 0);
}

void AccountOverlay_Toggle()
{
    if (!g_Window)
        return;

    PostMessageW(g_Window, WM_ACCOUNT_TOGGLE, 0, 0);
}

static std::wstring GetOverlayConfigPath()
{
    wchar_t path[MAX_PATH] = {};

    if (FAILED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path)))
        return L".\\voice.ini";

    std::wstring dir = std::wstring(path) + L"\\LineageII";
    CreateDirectoryW(dir.c_str(), NULL);

    return dir + L"\\voice.ini";
}

static POINT GetDefaultOverlayPosition()
{
    RECT rcGame = {};
    HWND game = FindLineageWindow();

    if (game && GetWindowRect(game, &rcGame))
        return { rcGame.left + 145, rcGame.top + 135 };

    return { 125, 200 };
}

static POINT LoadOverlayPosition()
{
    const std::wstring path = GetOverlayConfigPath();

    POINT def = GetDefaultOverlayPosition();

    POINT pos = {};
    pos.x = GetPrivateProfileIntW(L"Overlay", L"X", def.x, path.c_str());
    pos.y = GetPrivateProfileIntW(L"Overlay", L"Y", def.y, path.c_str());

    return pos;
}

static void SaveOverlayPosition(HWND hwnd)
{
    RECT rc = {};
    if (!GetWindowRect(hwnd, &rc))
        return;

    const std::wstring path = GetOverlayConfigPath();

    wchar_t buffer[32] = {};

    wsprintfW(buffer, L"%d", rc.left);
    WritePrivateProfileStringW(L"Overlay", L"X", buffer, path.c_str());

    wsprintfW(buffer, L"%d", rc.top);
    WritePrivateProfileStringW(L"Overlay", L"Y", buffer, path.c_str());
}

static void PositionWindowSaved(HWND hwnd)
{
    POINT pos = LoadOverlayPosition();

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        pos.x,
        pos.y,
        0,
        0,
        SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static POINT LoadAddWindowPosition()
{
    const std::wstring path = GetOverlayConfigPath();

    POINT def = LoadOverlayPosition();
    def.x += 30;
    def.y += 30;

    POINT pos = {};
    pos.x = GetPrivateProfileIntW(L"AddAccount", L"X", def.x, path.c_str());
    pos.y = GetPrivateProfileIntW(L"AddAccount", L"Y", def.y, path.c_str());

    return pos;
}

static void SaveAddWindowPosition(HWND hwnd)
{
    RECT rc = {};
    if (!GetWindowRect(hwnd, &rc))
        return;

    const std::wstring path = GetOverlayConfigPath();

    wchar_t buffer[32] = {};

    wsprintfW(buffer, L"%d", rc.left);
    WritePrivateProfileStringW(L"AddAccount", L"X", buffer, path.c_str());

    wsprintfW(buffer, L"%d", rc.top);
    WritePrivateProfileStringW(L"AddAccount", L"Y", buffer, path.c_str());
}

static void PositionAddWindowSaved(HWND hwnd)
{
    POINT pos = LoadAddWindowPosition();

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        pos.x,
        pos.y,
        0,
        0,
        SWP_NOSIZE | SWP_SHOWWINDOW);
}
