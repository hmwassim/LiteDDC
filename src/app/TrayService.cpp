#include "TrayService.h"
#include "../utils/resource.h"
#include <shellapi.h>

namespace
{
    HHOOK g_wheelHook = nullptr;
    HWND g_hookHwnd = nullptr;
    RECT g_iconRect{};
    bool g_iconRectValid = false;

    constexpr UINT kMenuSettings = 40001;
    constexpr UINT kMenuToggleAutostart = 40002;
    constexpr UINT kMenuExit = 40006;

    // File-scope hook state is deliberate: WH_MOUSE_LL callbacks carry no
    // context pointer, and this app is single-instance (main.cpp mutex).

    void QueryIconRect(HWND hwnd)
    {
        NOTIFYICONIDENTIFIER ident{};
        ident.cbSize = sizeof(ident);
        ident.hWnd = hwnd;
        ident.uID = TrayService::kTrayIconId;
        RECT rect{};
        if (SUCCEEDED(Shell_NotifyIconGetRect(&ident, &rect)))
        {
            g_iconRect = rect;
            g_iconRectValid = true;
        }
        else
        {
            // Icon temporarily invisible/unqueryable (e.g. overflow flyout
            // closed, explorer restarting): stop consuming wheel events until
            // the next successful refresh rather than hit-testing a stale rect.
            g_iconRectValid = false;
        }
    }
}

LRESULT CALLBACK WheelHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && wParam == WM_MOUSEWHEEL)
    {
        const auto info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
        const POINT pt{info->pt.x, info->pt.y};
        if (g_iconRectValid && PtInRect(&g_iconRect, pt) != FALSE)
        {
            // Forward the wheel EXACTLY as WM_MOUSEWHEEL carries it: signed
            // delta in the HIGH word of wParam (GET_WHEEL_DELTA_WPARAM reads
            // it back on the receiving side).
            PostMessageW(g_hookHwnd, TrayService::kTrayScrollMessage,
                         static_cast<WPARAM>(info->mouseData & 0xFFFF0000u), 0);
            return 1; // consumed: whatever is under the cursor must not scroll
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

TrayService::~TrayService()
{
    RemoveWheelHook();
    RemoveIcon();
}

bool TrayService::AddIcon(HWND hwnd, HICON icon)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = kTrayCallbackMessage;
    nid.hIcon = icon;
    wcscpy_s(nid.szTip, L"LiteDDC");
    if (!Shell_NotifyIconW(NIM_ADD, &nid))
    {
        return false;
    }

    // NOTIFYICON_VERSION_4 switches the callback to (wParam HIWORD = event,
    // lParam = POINTS) semantics and enables WM_CONTEXTMENU/NIN_SELECT.
    nid.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIconW(NIM_SETVERSION, &nid))
    {
        // Partial-failure cleanup: NIM_ADD succeeded but the version switch
        // did not. Delete the icon we just created instead of leaving it
        // orphaned with legacy callback semantics - m_iconHwnd is still
        // null here, so RemoveIcon() could not reach it.
        Shell_NotifyIconW(NIM_DELETE, &nid);
        return false;
    }
    m_iconHwnd = hwnd;
    return true;
}

void TrayService::RemoveIcon()
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = m_iconHwnd;
    nid.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    m_iconHwnd = nullptr;
}

void TrayService::SetIcon(HWND hwnd, HICON icon)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON;
    nid.hIcon = icon;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayService::UpdateTip(HWND hwnd, const wchar_t* tip)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_TIP;
    wcscpy_s(nid.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayService::ShowBalloon(HWND hwnd, const wchar_t* title, const wchar_t* message)
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, title);
    wcscpy_s(nid.szInfo, message);
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayService::ShowMenu(HWND hwnd, bool autostartEnabled){
    POINT pt{};
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    if (!menu)
    {
        return;
    }

    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings...");
    AppendMenuW(menu, MF_STRING | (autostartEnabled ? MF_CHECKED : 0), kMenuToggleAutostart, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    SetForegroundWindow(hwnd);
    const UINT selected = static_cast<UINT>(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr));
    DestroyMenu(menu);

    switch (selected)
    {
    case kMenuSettings:
        if (m_onSettings) m_onSettings();
        break;
    case kMenuToggleAutostart:
        if (m_onToggleAutostart) m_onToggleAutostart();
        break;
    case kMenuExit:
        if (m_onExit) m_onExit();
        break;
    default:
        break;
    }
}

bool TrayService::InstallWheelHook(HWND hwnd)
{
    if (g_wheelHook)
    {
        return true;
    }

    QueryIconRect(hwnd);
    g_hookHwnd = hwnd;
    g_wheelHook = SetWindowsHookExW(WH_MOUSE_LL, &WheelHookProc, nullptr, 0);
    if (!g_wheelHook)
    {
        g_hookHwnd = nullptr;
        return false;
    }
    return true;
}

void TrayService::RemoveWheelHook()
{
    if (g_wheelHook)
    {
        UnhookWindowsHookEx(g_wheelHook);
        g_wheelHook = nullptr;
    }
    g_hookHwnd = nullptr;
    g_iconRectValid = false;
}

void TrayService::RefreshWheelRect(HWND hwnd)
{
    QueryIconRect(hwnd);
}
