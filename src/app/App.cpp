#include "App.h"
#include "../data/Settings.h"
#include "../utils/AutostartUtils.h"
#include "../utils/resource.h"
#include <dbt.h>
#include <shellapi.h>
#include <winreg.h>

App::App(HINSTANCE hInstance) :
    m_hInstance(hInstance)
{
}

App::~App()
{
    // Do NOT call ExitProcess() here: this destructor also runs during normal
    // stack unwind when Init() returns false, and forcing an exit there would
    // discard main()'s exit code.
    if (m_hwnd)
    {
        m_tray.RemoveIcon();
        m_hwnd = nullptr;
    }
    // Idempotent; joins the worker while m_monitors' handles are still alive
    // (member destruction order guarantees this even without the call).
    m_coordinator.Stop();
    // Persist anything the dialog left dirty (e.g. slider drags since last
    // hide); cheap no-op when everything was already written through.
    Settings::instance().SaveIfDirty();
}

bool App::IsLightTheme() const
{
    // Standard proxy for taskbar color: 1 = light theme (dark glyph needed
    // is FALSE -> black sun). Key missing (pre-1809, or reset) = Windows
    // default = light theme.
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
    {
        return value != 0;
    }
    return true;
}

HICON App::CurrentThemeIcon() const
{
    // Dark taskbar -> white glyph; light taskbar -> black glyph. Fall back
    // to the classic app icon if a glyph failed to load.
    const HICON themed = IsLightTheme() ? m_sunBlack : m_sunWhite;
    return themed ? themed : LoadIconW(m_hInstance, MAKEINTRESOURCE(IDI_APP));
}

bool App::Init()
{
    m_autostartEnabled = Autostart::IsEnabled();

    // M5: load persisted settings and push them into the coordinator's
    // runtime state BEFORE the worker starts. Safe either way (all setters
    // are mutex-guarded); AdjustAll no-ops until the worker is ready.
    Settings::instance().Load();
    const SettingsData& saved = Settings::instance().data();
    m_coordinator.SetParamEnabled(Param::Brightness, saved.brightnessEnabled);
    m_coordinator.SetParamEnabled(Param::Contrast, saved.contrastEnabled);
    m_coordinator.SetParamEnabled(Param::Saturation, saved.saturationEnabled);
    // Volume is slider-only (M14 human follow-up): its enable flag stays
    // false at the constructor default, like the R/G/B gains.
    // R/G/B gains are never scroll-driven (slider-only rows, M12): their
    // m_enabled flags stay at the constructor default (false) on purpose.
    m_coordinator.SetScrollStep(saved.scrollStep);
    m_coordinator.SetScope(saved.scope);

    // M13: bring up the NVIDIA driver interface (dynamic load + init; the
    // M13 probe measured this as instant, so it is safe on the UI thread
    // here - before the mouse hook exists). Absent hardware simply leaves
    // Available() false and the dialog grays the vibrance row.
    m_vibrance.TryLoad();

    if (!CreateHiddenWindow())
    {
        return false;
    }

    m_tray.SetOnSettings([this] { OpenSettings(); });
    m_tray.SetOnToggleAutostart([this] { ToggleAutostart(); });
    m_tray.SetOnExit([this] { Exit(); });

    // M14: load both themed glyphs once (small size = what the tray draws).
    // LR_SHARED means the system owns them - no DestroyIcon, process-lifetime.
    const int iconSize = GetSystemMetrics(SM_CXSMICON);
    const int iconSizeY = GetSystemMetrics(SM_CYSMICON);
    m_sunBlack = static_cast<HICON>(LoadImageW(m_hInstance, MAKEINTRESOURCE(IDI_APP_SUN_LIGHT),
                                               IMAGE_ICON, iconSize, iconSizeY, LR_SHARED));
    m_sunWhite = static_cast<HICON>(LoadImageW(m_hInstance, MAKEINTRESOURCE(IDI_APP_SUN_DARK),
                                               IMAGE_ICON, iconSize, iconSizeY, LR_SHARED));

    if (!m_tray.AddIcon(m_hwnd, CurrentThemeIcon()))
    {
        return false;
    }

    // BUGFIX (was: whole-system input lag on launch): this used to call
    // m_monitors.Refresh() directly here, followed by m_coordinator.Start()
    // synchronously reading a baseline value per channel - AFTER
    // InstallWheelHook() below had already armed a process-wide WH_MOUSE_LL
    // hook. DDC/CI I/O is slow (a capability-string round trip per monitor,
    // then one VCP read per channel, each easily 50-300ms), and a low-level
    // hook's owning thread must keep pumping messages or Windows stalls
    // mouse/keyboard input for the ENTIRE SYSTEM until that thread responds
    // again - not just this app's window. That is exactly what caused the
    // reported freeze. AdjustmentCoordinator::Start() now does all of that
    // I/O on its own worker thread and returns immediately; see its header
    // comment. Do not call MonitorManager::Refresh() from this thread again.
    m_coordinator.SetOnReady([this](bool anyChannels) {
        // Worker thread: marshal to the UI thread. Fires after the first
        // enumeration AND every M6 re-enumeration (wParam = anyChannels).
        PostMessageW(m_hwnd, kMonitorsChangedMessage, anyChannels ? 1 : 0, 0);
    });
    m_coordinator.Start(&m_monitors);

    // WH_MOUSE_LL fallback for wheel input (M0 checkpoint 2: NOTIFYICON_
    // VERSION_4 never delivers WM_MOUSEWHEEL over tray icons on this build).
    // Installed last, immediately before Run()'s message loop starts, to
    // keep the window between "hook is live" and "this thread is pumping
    // messages" as small as possible. The icon rect is re-queried on a
    // UI-thread timer because taskbar icons move; doing the query inside
    // the hook itself would stall the mouse.
    if (!m_tray.InstallWheelHook(m_hwnd))
    {
        return false;
    }
    // M7 audit: this is the ONLY steady-state timer in the process. 5 s (not
    // 1 s) keeps idle work inside the "no polling faster than a few seconds"
    // rule; the cases that actually move an icon mid-interval - explorer
    // restart (TaskbarCreated also RE-ADDS the wiped icon), resolution
    // change - get INSTANT rect refreshes via the TaskbarCreated broadcast
    // and WM_DISPLAYCHANGE instead of a faster poll. Worst case for
    // un-broadcast moves (e.g. overflow promotion): a few seconds of wheel
    // events passing through before self-healing.
    m_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    SetTimer(m_hwnd, TrayService::kRectRefreshTimerId, TrayService::kRectRefreshIntervalMs, nullptr);

    return true;
}

int App::Run()
{
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        // Standard modeless-dialog pattern: let the settings dialog consume
        // its navigation keys (Tab/arrows/Esc); everything else flows on.
        if (!m_settings.IsDialogInput(&msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

bool App::CreateHiddenWindow()
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::WndProc;
    wc.hInstance = m_hInstance;
    wc.hIcon = LoadIconW(m_hInstance, MAKEINTRESOURCE(IDI_APP));
    wc.lpszClassName = L"LiteDDCWindow";
    if (RegisterClassExW(&wc) == 0)
    {
        return false;
    }

    m_hwnd = CreateWindowExW(0, L"LiteDDCWindow", L"LiteDDC", 0, 0, 0, 0, 0, nullptr, nullptr, m_hInstance, this);
    return m_hwnd != nullptr;
}

void App::OpenSettings()
{
    // M4: real dialog now. Create-on-first-use, restore+foreground on later
    // calls (single instance); closing it only hides it. M13 adds the
    // vibrance backend pointer.
    m_settings.Open(m_hInstance, &m_coordinator, &m_monitors, &m_vibrance);
}

void App::ToggleAutostart()
{
    m_autostartEnabled = !m_autostartEnabled;
    Autostart::SetEnabled(m_autostartEnabled);
}

void App::Exit()
{
    KillTimer(m_hwnd, TrayService::kRectRefreshTimerId);
    m_tray.RemoveWheelHook();
    m_tray.RemoveIcon();
    PostQuitMessage(0);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        const auto createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(createStruct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self)
    {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    // Observed on this machine (M2 checkpoint, 2026-08-23): despite
    // NOTIFYICON_VERSION_4 being accepted by NIM_SETVERSION, callbacks
    // arrive LEGACY-packed - event in LOWORD(lParam), icon id in
    // HIWORD(lParam), wParam=0. Other Windows builds deliver true V4
    // packing (HIWORD(wParam)=event). Accept both packings; identical
    // actions inside 400 ms are debounced because the shell sends BOTH a
    // button-up and its NIN_SELECT/WM_CONTEXTMENU counterpart.
    case TrayService::kTrayCallbackMessage:
    {
        const UINT v4Event = HIWORD(wParam);
        const UINT legacyEvent = LOWORD(lParam);
        const UINT legacyId = HIWORD(lParam);

        UINT evt = v4Event;
        if (v4Event == 0 || legacyId == TrayService::kTrayIconId)
        {
            evt = legacyEvent;
        }

        UINT action = 0;
        switch (evt)
        {
        case NIN_SELECT:
        case WM_LBUTTONUP:
            action = 1;
            break;
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            action = 2;
            break;
        default:
            break;
        }

        const DWORD now = GetTickCount();
        if (action != 0 && (action != m_lastTrayAction || now - m_lastTrayActionTick > 400))
        {
            m_lastTrayAction = action;
            m_lastTrayActionTick = now;
            if (action == 1)
            {
                OpenSettings();
            }
            else
            {
                m_tray.ShowMenu(hwnd, m_autostartEnabled);
            }
        }
        return 0;
    }

    case TrayService::kTrayScrollMessage:
    {
        // Deltas arrive per hook event; only whole notches (WHEEL_DELTA)
        // become steps. The remainder is kept so fast scrolling never
        // loses input - last-value-wins coalescing is M3's job.
        const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        m_scrollRemainder += delta;
        int steps = 0;
        while (m_scrollRemainder >= WHEEL_DELTA)
        {
            ++steps;
            m_scrollRemainder -= WHEEL_DELTA;
        }
        while (m_scrollRemainder <= -WHEEL_DELTA)
        {
            --steps;
            m_scrollRemainder += WHEEL_DELTA;
        }
        if (steps != 0)
        {
            // UI thread only touches integer targets; the DDC writes happen
            // later on the coordinator's worker thread. AdjustAll also
            // updated the cached values on THIS thread, so an open settings
            // dialog can reflect them immediately (M11 live slider sync).
            m_coordinator.AdjustAll(steps);
            m_settings.OnValuesChanged();
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == TrayService::kRectRefreshTimerId)
        {
            m_tray.RefreshWheelRect(hwnd);
            return 0;
        }
        break;

    case kMonitorsChangedMessage:
        // M6: fires after every enumeration cycle. wParam = anyChannels.
        if (!wParam)
        {
            m_tray.ShowBalloon(hwnd, L"LiteDDC",
                               L"No controllable monitor was detected. Brightness, "
                               L"contrast, and saturation adjustments will have no "
                               L"effect until a DDC/CI-capable monitor is connected.");
        }
        if (m_settings.Exists())
        {
            m_settings.OnMonitorsChanged();
        }
        return 0;

    case WM_SETTINGCHANGE:
        // M14 theming: Windows fires this with lParam = "ImmersiveColorSet"
        // whenever the system theme flips (Settings > Colors). lParam is a
        // string only when non-null. Standard handling still runs.
        if (lParam != 0 && lstrcmpiW(reinterpret_cast<LPCWSTR>(lParam), L"ImmersiveColorSet") == 0)
        {
            m_tray.SetIcon(hwnd, CurrentThemeIcon());
        }
        break;

    case WM_DISPLAYCHANGE:
    case WM_POWERBROADCAST:
        // M6: display topology or power state changed (resolution switch,
        // sleep/wake). DDC handles can go stale across either - re-enumerate
        // on the worker thread. Power-resume is the sneaky one: monitors
        // often lose their settings cache over suspend even when the
        // topology is unchanged. Both still get standard system handling.
        // A display change also moves/recreates tray icon rects.
        m_tray.RefreshWheelRect(hwnd);
        m_coordinator.RequestReenumerate();
        return msg == WM_POWERBROADCAST ? TRUE : DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_DEVICECHANGE:
        // Only real devnode changes (monitor plug/unplug reports here too);
        // every media/USB-stick notification must NOT trigger a DDC burst.
        if (wParam == DBT_DEVNODES_CHANGED)
        {
            m_coordinator.RequestReenumerate();
            return TRUE;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_CLOSE:
        Exit();
        return 0;

    default:
        // "TaskbarCreated" is a RegisterWindowMessage id, so it can only be
        // caught here. Explorer restarting (or the taskbar re-creating for
        // any reason) DESTROYS every tray icon - without a re-add the app
        // keeps running but is invisible until manually relaunched. Remove
        // first so NIM_ADD can't fail against a shell that still remembers
        // our id, then refresh the wheel rect immediately instead of waiting
        // out the 5 s tick.
        if (m_taskbarCreatedMsg != 0 && msg == m_taskbarCreatedMsg)
        {
            m_tray.RemoveIcon();
            m_tray.AddIcon(hwnd, CurrentThemeIcon());
            m_tray.RefreshWheelRect(hwnd);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
