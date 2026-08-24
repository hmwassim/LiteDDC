#pragma once

#include <windows.h>

#include <functional>

// Tray icon + context menu. Starting-point copy of LiteZones' TrayService
// (same structure and callback wiring pattern); extended per milestone:
// M2 adds NOTIFYICON_VERSION_4 event semantics plus the WH_MOUSE_LL wheel
// fallback scoped to the icon rectangle (M0 checkpoint result: V4 delivers
// clicks/context-menu but NEVER WM_MOUSEWHEEL on this build - architecture
// doc section 5 fallback required).
class TrayService
{
public:
    // Shared between TrayService and App's WndProc. With NOTIFYICON_VERSION_4
    // the callback event arrives in HIWORD(wParam) of kTrayCallbackMessage,
    // and kTrayScrollMessage carries the signed wheel delta in wParam.
    static constexpr UINT kTrayCallbackMessage = WM_APP + 1;
    static constexpr UINT kTrayScrollMessage = WM_APP + 2;
    static constexpr UINT kTrayIconId = 1;
    static constexpr UINT_PTR kRectRefreshTimerId = 2;

    /// M7 idle-audit compliance: the rect refresh is the process's only
    /// steady-state timer and must stay slower than "a few seconds". Moves
    /// that matter immediately (explorer restart, resolution change) are
    /// handled event-driven via TaskbarCreated / WM_DISPLAYCHANGE instead.
    static constexpr UINT kRectRefreshIntervalMs = 5000;

    TrayService() = default;
    ~TrayService();

    TrayService(const TrayService&) = delete;
    TrayService& operator=(const TrayService&) = delete;

    // The caller owns the HICON (App loads both themed glyphs once with
    // LR_SHARED and hands in whichever matches the current taskbar theme).
    bool AddIcon(HWND hwnd, HICON icon);
    void RemoveIcon();
    /// M14 theming: swap the tray glyph live (WM_SETTINGCHANGE /
    /// "ImmersiveColorSet"). NIM_MODIFY with NIF_ICON only.
    void SetIcon(HWND hwnd, HICON icon);
    void UpdateTip(HWND hwnd, const wchar_t* tip);
    void ShowBalloon(HWND hwnd, const wchar_t* title, const wchar_t* message);

    // Builds and shows the tray context menu. Returns the selected action via callbacks.
    void ShowMenu(HWND hwnd, bool autostartEnabled);

    /// Installs the WH_MOUSE_LL wheel-scoping hook (M2). UI thread only,
    /// after AddIcon. Pairs with RemoveWheelHook().
    ///
    /// Why a low-level hook at all: NOTIFYICON_VERSION_4 does not deliver
    /// WM_MOUSEWHEEL over tray icons on current Windows builds (M0 spike 2),
    /// so wheel input is observed system-wide and scoped to the icon rect.
    bool InstallWheelHook(HWND hwnd);

    /// Removes the wheel hook. Safe to call repeatedly or without a prior
    /// InstallWheelHook.
    void RemoveWheelHook();

    /// Re-queries the icon rectangle used for wheel scoping. MUST run on a
    /// periodic UI-thread timer (App drives it via kRectRefreshTimerId):
    /// taskbar icons move (neighbors appear/disappear, overflow flyout -
    /// observed twice within one M0 session), and querying inside the hook
    /// itself would stall the mouse and risk the hook being silently removed.
    void RefreshWheelRect(HWND hwnd);

    void SetOnSettings(std::function<void()> cb) { m_onSettings = std::move(cb); }
    void SetOnToggleAutostart(std::function<void()> cb) { m_onToggleAutostart = std::move(cb); }
    void SetOnExit(std::function<void()> cb) { m_onExit = std::move(cb); }

    // NOTE: there is deliberately no SetOnScroll callback here. The hook
    // (WheelHookProc) posts kTrayScrollMessage directly to the owning
    // window, and App::HandleMessage does the notch accumulation - adding
    // a callback here would just be a second, unused path to the same
    // event. Route new scroll consumers through kTrayScrollMessage.

private:

    std::function<void()> m_onSettings;
    std::function<void()> m_onToggleAutostart;
    std::function<void()> m_onExit;

    HWND m_iconHwnd = nullptr;
};
