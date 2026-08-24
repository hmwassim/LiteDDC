#pragma once

#include "AdjustmentCoordinator.h"
#include "TrayService.h"
#include "VibranceService.h"
#include "../data/SettingsDialog.h"
#include "../ddc/MonitorManager.h"

#include <windows.h>

class App
{
public:
    explicit App(HINSTANCE hInstance);
    ~App();

    bool Init();
    int Run();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Posted (from AdjustmentCoordinator's worker thread, via PostMessageW -
    // safe cross-thread) once first enumeration finds zero adjustable
    // channels, so the UI thread (which owns all Shell_NotifyIcon calls)
    // can show a balloon instead of leaving the user wondering why
    // scrolling does nothing.
    /// Posted (wParam = anyChannels ? 1 : 0) when a coordinator enumeration
    /// cycle completes - the first one after launch AND every M6
    /// re-enumeration (hot-plug, sleep/wake). Handler balloons on zero
    /// channels and refreshes the settings dialog if it exists.
    static constexpr UINT kMonitorsChangedMessage = WM_APP + 3;

    bool CreateHiddenWindow();
    void OpenSettings();
    void ToggleAutostart();
    void Exit();

    // M14 tray-icon theming: pick the sun glyph matching the taskbar theme
    // and swap it live when Windows announces a theme change.
    bool IsLightTheme() const;
    HICON CurrentThemeIcon() const;

    HINSTANCE m_hInstance = nullptr;
    HWND m_hwnd = nullptr;
    bool m_autostartEnabled = false;
    UINT m_taskbarCreatedMsg = 0;
    int m_scrollRemainder = 0;
    UINT m_lastTrayAction = 0;
    DWORD m_lastTrayActionTick = 0;
    // M14 themed glyphs, loaded once with LR_SHARED (process-lifetime,
    // never destroyed). Black glyph for light taskbars, white for dark.
    HICON m_sunBlack = nullptr;
    HICON m_sunWhite = nullptr;
    // Declaration order is load-bearing: members die in reverse order, and
    // m_coordinator's destructor joins a worker thread that borrows monitor
    // handles owned by m_monitors. m_settings borrows both, so it dies
    // first (its window must outlive nothing).
    MonitorManager m_monitors;
    AdjustmentCoordinator m_coordinator;
    TrayService m_tray;
    // M13: GPU vibrance backend. Dies before m_settings borrows nothing
    // from it (the dialog only stores the pointer and dies first anyway).
    VibranceService m_vibrance;
    SettingsDialog m_settings;
};
